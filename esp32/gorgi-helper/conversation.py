"""One conversation with Gorgi: listen -> understand -> think -> speak, with
short follow-ups, pausing the music while you talk."""

import asyncio
import logging
import time

import lang
import todolist
import weather
from brain import BrainError

log = logging.getLogger("gorgi.talk")

FIRST_WAIT_SECONDS = 7.0  # how long to wait for you to start talking after a tap


class Conversation:
    def __init__(self, send, media, brain, recorder, transcriber, speaker, followup_seconds=5.0, timekeeper=None,
                 extra_context=None, todos=None):
        self.send = send  # async fn(dict): message to Gorgi
        self.media = media
        self.timekeeper = timekeeper  # timers and reminders (see timekeeper.py)
        self.todos = todos  # the to-do list (see todolist.py)
        self.extra_context = extra_context  # fn() -> more lines for the AI (Claude Code, calls)
        self.brain = brain
        self.recorder = recorder
        self.transcriber = transcriber
        self.speaker = speaker
        self.followup_seconds = followup_seconds
        self.dictate = False  # True: just write down what's said (To-do screen; no AI, no voice)
        self.dictate_mode = "todo"  # "todo": a new task (with its day); "text": words for the keyboard's text box

    async def run(self):
        if self.dictate:
            await self.run_dictation()
            return
        await self.run_chat()

    async def _ears_problem(self):
        """None when Whisper is ready (loads it if needed), else what to tell the user."""
        if self.transcriber.ready:
            return None
        if self.transcriber.loading:
            return lang.t("Give me a minute, I'm still learning to hear. Try again soon!",
                          "רגע, אני עוד מתכוננת לשמוע. נסה שוב עוד רגע!")
        await asyncio.to_thread(self.transcriber.load)
        if not self.transcriber.ready:
            return lang.t("My ears aren't working. Please check the helper's log on your PC.",
                          "האוזניים שלי לא עובדות. תבדוק את הלוג של התוכנה במחשב.")
        return None

    async def run_dictation(self):
        """Voice typing for the To-do screen: listen once, send the words back (as a task with its day)."""
        paused_music = False
        problem = None
        try:
            info = await self.media.get_info()
            if info["playing"]:
                await self.media.set_playing(False)
                paused_music = True
            problem = await self._ears_problem()
            if problem:
                return
            await self.send({"type": "dictate_state", "state": "listening"})
            audio = await self.recorder.record(wait_for_speech=FIRST_WAIT_SECONDS, max_seconds=30.0, end_silence=1.5)
            if audio is None:
                problem = lang.t("I didn't hear anything", "לא שמעתי כלום")
                return
            await self.send({"type": "dictate_state", "state": "thinking"})
            text = await asyncio.to_thread(self.transcriber.transcribe, audio)
            log.info("dictation: %r", text)
            if text and self.dictate_mode == "todo":
                task = todolist.parse_spoken(text)
                log.info("new task: %s", task)
                msg = todolist.to_message(task, "dictation")
                msg["mode"] = "todo"
                await self.send(msg)
            elif text:
                await self.send({"type": "dictation", "text": text, "mode": "text"})
            else:
                problem = lang.t("Sorry, I didn't catch that", "סליחה, לא הבנתי")
        finally:
            try:
                msg = {"type": "dictate_state", "state": "idle"}
                if problem:
                    msg["problem"] = problem
                await self.send(msg)
            except Exception:
                pass
            if paused_music:
                await self.media.set_playing(True)

    async def run_chat(self):
        """Runs until the chat ends. Cancel the task to stop it early."""
        paused_music = False
        resume_music = True
        info = {"title": "", "artist": "", "playing": False}
        try:
            info = await self.media.get_info()
            if info["playing"]:
                await self.media.set_playing(False)
                paused_music = True

            problem = await self._ears_problem()
            if problem:
                await self._say("thinking" if self.transcriber.loading else "sad", problem)
                return

            followup = False
            while True:
                await self.send({"type": "voice", "state": "listening", "followup": followup})
                wait = self.followup_seconds if followup else FIRST_WAIT_SECONDS
                if followup:
                    # Stricter while waiting for a follow-up: a fan, a car or the end of
                    # Gorgi's own voice shouldn't count as someone talking.
                    audio = await self.recorder.record(wait_for_speech=wait, max_seconds=8.0,
                                                       min_speech=0.6, sensitivity=4.0)
                else:
                    audio = await self.recorder.record(wait_for_speech=wait)
                if audio is None:
                    if not followup:
                        await self._say("confused", lang.t("I didn't hear anything. Is the microphone on?",
                                                           "לא שמעתי כלום. המיקרופון פועל?"))
                    return

                await self.send({"type": "voice", "state": "thinking"})
                text = await asyncio.to_thread(self.transcriber.transcribe, audio)
                log.info("heard: %r", text)
                if not text:
                    if not followup:  # after a follow-up wait it was just noise: end quietly
                        await self._say("confused", lang.t("Sorry, I didn't catch that.", "סליחה, לא הבנתי."))
                    return
                await self.send({"type": "heard", "text": text})

                log.info("asking the AI...")
                started = time.monotonic()
                try:
                    mood, spoken, actions = await self.brain.ask(text, self._context(info, paused_music))
                except BrainError as e:
                    log.info("AI problem: %s", e.spoken)
                    await self._say(e.mood, e.spoken)
                    return
                log.info("AI answered in %.1f s", time.monotonic() - started)

                keep_talking, resume_music, problem = await self._do_actions(actions, resume_music)
                if problem:  # e.g. too many timers: say that instead of "done!"
                    mood, spoken = "confused", problem
                await self._say(mood, spoken)
                if not keep_talking:
                    return
                followup = True
                await asyncio.sleep(0.7)  # let the room (and Gorgi's own voice) go quiet before listening again
        finally:
            try:
                await self.send({"type": "voice", "state": "idle"})
            except Exception:
                pass
            if paused_music and resume_music:
                await self.media.set_playing(True)

    async def _say(self, mood, text):
        """Speaks. The text goes to Gorgi when the sound starts, with the time of each word,
        so the subtitle appears word by word with the voice."""
        loop = asyncio.get_running_loop()
        sent = False

        async def on_start(timings):
            nonlocal sent
            sent = True
            await self.send({"type": "say", "text": text, "mood": mood, "words": timings})

        def on_level(level):
            loop.create_task(self.send({"type": "level", "value": round(level, 2)}))

        log.info("speaking (%s): %s", mood, text)
        try:
            await self.speaker.say(text, on_level=on_level, on_start=on_start)
            log.info("done speaking")
        except asyncio.CancelledError:
            raise
        except Exception as e:  # no internet for the voice: still show the text for a while
            log.warning("speaking failed: %s", e)
            if not sent:
                await self.send({"type": "say", "text": text, "mood": mood})
            await asyncio.sleep(min(8.0, 1.0 + len(text) * 0.06))

    async def _do_actions(self, actions, resume_music):
        """Carries out music and time actions. Returns (keep_talking, resume_music, problem)."""
        keep_talking = True
        problem = None
        for action, value in actions:
            log.info("action: %s %s", action, value if value is not None else "")
            if action == "todo":
                if self.todos is None:
                    continue
                messages, trouble = self.todos.handle(value)
                for message in messages:
                    await self.send(message)
                problem = problem or trouble
            elif action in ("timer", "focus", "remind", "reminder", "cancel"):
                if self.timekeeper is None:
                    continue
                messages, trouble = self.timekeeper.handle(action, value)
                for message in messages:
                    await self.send(message)
                problem = problem or trouble
            elif action == "pause":
                resume_music = False  # you want quiet: don't restart it afterwards
            elif action == "play":
                resume_music = True
                keep_talking = False  # music on: stop listening so the mic doesn't hear it
            elif action in ("next", "prev"):
                await self.media.command(action)
                resume_music = True
                keep_talking = False
            elif action == "volume" and value is not None:
                self.media.set_volume(value)
            elif action in ("volume_up", "volume_down"):
                current = self.media.get_volume() or 50
                self.media.set_volume(current + (15 if action == "volume_up" else -15))
        return keep_talking, resume_music, problem

    def _context(self, info, paused_music):
        lines = [f"Right now it is {time.strftime('%A %d %B %Y, %H:%M')}."]
        if info.get("title"):
            state = "playing (paused while you talk)" if paused_music else "paused"
            lines.append(f"Music on the PC: \"{info['title']}\" by {info.get('artist') or 'unknown artist'}, {state}.")
        else:
            lines.append("No music is playing on the PC.")
        volume = self.media.get_volume()
        if volume is not None:
            lines.append(f"PC volume: {volume}%.")
        forecast = weather.summary()
        if forecast:
            lines.append(forecast)
        if self.timekeeper is not None:
            lines.append(self.timekeeper.prompt_block())
        if self.todos is not None:
            lines.append(self.todos.prompt_block())
        if self.extra_context is not None:
            try:
                extra = self.extra_context()
                if extra:
                    lines.append(extra)
            except Exception as e:
                log.warning("extra context failed: %s", e)
        return "\n".join(lines)
