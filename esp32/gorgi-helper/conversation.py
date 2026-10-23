"""One conversation with Gorgi: listen -> understand -> think -> speak, with
short follow-ups, pausing the music while you talk."""

import asyncio
import logging
import time

import weather
from brain import BrainError

log = logging.getLogger("gorgi.talk")

FIRST_WAIT_SECONDS = 7.0  # how long to wait for you to start talking after a tap


class Conversation:
    def __init__(self, send, media, brain, recorder, transcriber, speaker, followup_seconds=5.0):
        self.send = send  # async fn(dict): message to Gorgi
        self.media = media
        self.brain = brain
        self.recorder = recorder
        self.transcriber = transcriber
        self.speaker = speaker
        self.followup_seconds = followup_seconds

    async def run(self):
        """Runs until the chat ends. Cancel the task to stop it early."""
        paused_music = False
        resume_music = True
        info = {"title": "", "artist": "", "playing": False}
        try:
            info = await self.media.get_info()
            if info["playing"]:
                await self.media.set_playing(False)
                paused_music = True

            if not self.transcriber.ready:
                if self.transcriber.loading:
                    await self._say("thinking", "Give me a minute, I'm still learning to hear. Try again soon!")
                    return
                await asyncio.to_thread(self.transcriber.load)
                if not self.transcriber.ready:
                    await self._say("sad", "My ears aren't working. Please check the helper's log on your PC.")
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
                        await self._say("confused", "I didn't hear anything. Is the microphone on?")
                    return

                await self.send({"type": "voice", "state": "thinking"})
                text = await asyncio.to_thread(self.transcriber.transcribe, audio)
                log.info("heard: %r", text)
                if not text:
                    if not followup:  # after a follow-up wait it was just noise: end quietly
                        await self._say("confused", "Sorry, I didn't catch that.")
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

                keep_talking, resume_music = await self._do_actions(actions, resume_music)
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
        await self.send({"type": "say", "text": text, "mood": mood})
        loop = asyncio.get_running_loop()

        def on_level(level):
            loop.create_task(self.send({"type": "level", "value": round(level, 2)}))

        log.info("speaking (%s): %s", mood, text)
        try:
            await self.speaker.say(text, on_level=on_level)
            log.info("done speaking")
        except asyncio.CancelledError:
            raise
        except Exception as e:  # no internet for the voice: still show the text for a while
            log.warning("speaking failed: %s", e)
            await asyncio.sleep(min(8.0, 1.0 + len(text) * 0.06))

    async def _do_actions(self, actions, resume_music):
        """Carries out music actions. Returns (keep_talking, resume_music)."""
        keep_talking = True
        for action, value in actions:
            log.info("action: %s %s", action, value if value is not None else "")
            if action == "pause":
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
        return keep_talking, resume_music

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
        return "\n".join(lines)
