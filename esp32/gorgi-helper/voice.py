"""Gorgi's ears and voice on the PC.

- Recorder: listens to the PC microphone and stops when you stop talking.
- Transcriber: turns speech into text with Whisper, on your PC (free, private).
  Ears picks the English or the Hebrew one (ivrit.ai's Hebrew Whisper), see lang.py.
- Speaker: says Gorgi's answers with a free Microsoft Edge voice and reports
  how loud each moment is, so Gorgi's mouth can follow the voice.
"""

import asyncio
import logging
import re
import os
import time

import numpy as np

import lang

log = logging.getLogger("gorgi.voice")

SAMPLE_RATE = 16000  # what Whisper wants
FRAME_SECONDS = 0.03


class Recorder:
    def __init__(self, device=None):
        self.device = device

    async def record(self, wait_for_speech=7.0, max_seconds=15.0, end_silence=0.9, cancel=None,
                     min_speech=0.25, sensitivity=3.0):
        """Records one utterance. Returns float32 audio at 16 kHz, or None if nobody spoke.

        min_speech: seconds of sound needed to count as speech (shorter = a click or a cough).
        sensitivity: how much louder than the background speech must be (higher = stricter)."""
        import sounddevice as sd

        loop = asyncio.get_running_loop()
        queue = asyncio.Queue()
        frame = int(SAMPLE_RATE * FRAME_SECONDS)

        def callback(indata, _frames, _time, status):
            if status:
                log.debug("mic status: %s", status)
            loop.call_soon_threadsafe(queue.put_nowait, indata[:, 0].copy())

        chunks = []
        noise = None
        speaking = False
        speech_seconds = 0.0
        silence = 0.0
        start = time.monotonic()

        with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="float32", blocksize=frame,
                            device=self.device, callback=callback):
            while True:
                if cancel is not None and cancel.is_set():
                    return None
                try:
                    block = await asyncio.wait_for(queue.get(), 1.0)
                except asyncio.TimeoutError:
                    continue
                rms = float(np.sqrt(np.mean(block * block)) + 1e-9)
                elapsed = time.monotonic() - start

                # Learn the background noise during the first moments
                if noise is None:
                    noise = rms
                elif not speaking:
                    noise = 0.95 * noise + 0.05 * rms
                threshold = max(noise * sensitivity, 0.012)

                if speaking:
                    chunks.append(block)
                    if rms < threshold:
                        silence += FRAME_SECONDS
                    else:
                        silence = 0.0
                        speech_seconds += FRAME_SECONDS
                    if silence >= end_silence or elapsed > max_seconds:
                        break
                else:
                    chunks.append(block)
                    chunks = chunks[-10:]  # keep a little audio from just before you started
                    if rms > threshold and elapsed > 0.15:
                        speaking = True
                        speech_seconds = FRAME_SECONDS
                    elif elapsed > wait_for_speech:
                        return None

        if speech_seconds < min_speech:  # just a click or a cough
            return None
        return np.concatenate(chunks)


def bundled_model_dir(model_name):
    """The Whisper model folder shipped next to GorgiHelper.exe, if there is one."""
    import sys
    from pathlib import Path

    app_dir = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).parent
    folder = app_dir / "models" / model_name
    return folder if (folder / "model.bin").exists() else None


HEBREW_MODEL = "ivrit-ai/whisper-large-v3-turbo-ct2"  # ivrit.ai's Hebrew Whisper (Apache 2.0), ~1.6 GB

# What Whisper sometimes "hears" in silence
NOT_SPEECH = {"", "you", "thank you", "thanks for watching", "bye",
              "תודה", "תודה רבה", "תודה שצפיתם", "תודה רבה שצפיתם", "תודה על הצפייה", "כתוביות", "שלום"}


class Transcriber:
    def __init__(self, model_name, models_dir, language="en"):
        self.model_name = model_name
        self.models_dir = models_dir
        self.language = language
        self._model = None
        self.loading = False
        self.error = None

    def load(self):
        """Loads (and the first time downloads) the Whisper model. Slow: run in a thread."""
        if self._model is not None:
            return
        self.loading = True
        try:
            from faster_whisper import WhisperModel

            model = self.model_name
            bundled = bundled_model_dir(self.model_name)
            if bundled:
                model = str(bundled)  # shipped with the app: no download needed
                log.info("loading bundled Whisper model from %s", bundled)
            else:
                log.info("loading Whisper model %s (downloads once, %s)...", self.model_name,
                         "~1.6 GB" if self.language == "he" else "~150 MB")
            # All the PC's cores (the default is 4): the big Hebrew model is ~3x slower than the English one
            threads = min(os.cpu_count() or 4, 12)
            self._model = WhisperModel(model, device="cpu", compute_type="int8", cpu_threads=threads,
                                       download_root=str(self.models_dir))
            log.info("Whisper model ready")
            self.error = None
        except Exception as e:
            import traceback

            log.exception("could not load Whisper")
            self.error = f"{e!r}\n{traceback.format_exc()}"
        finally:
            self.loading = False

    @property
    def ready(self):
        return self._model is not None

    def transcribe(self, audio):
        """Slow: run in a thread."""
        self.load()
        if self._model is None:
            return ""
        started = time.monotonic()
        segments, _info = self._model.transcribe(audio, language=self.language, beam_size=1, vad_filter=False,
                                                 condition_on_previous_text=False, without_timestamps=True)
        text = " ".join(s.text.strip() for s in segments).strip()
        log.info("transcribed %.1f s of speech in %.1f s (%s)", len(audio) / SAMPLE_RATE, time.monotonic() - started,
                 self.language)
        if text.lower().strip(" .!?,") in NOT_SPEECH:
            return ""
        return text


class Ears:
    """The English and the Hebrew Whisper; works like a Transcriber for the language chosen now.
    The Hebrew one loads (and the first time downloads) only when Hebrew is used."""

    def __init__(self, english, hebrew):
        self.english = english
        self.hebrew = hebrew

    def current(self):
        return self.hebrew if lang.hebrew() else self.english

    @property
    def ready(self):
        return self.current().ready

    @property
    def loading(self):
        return self.current().loading

    @property
    def error(self):
        return self.current().error

    def load(self):
        self.current().load()

    def transcribe(self, audio):
        return self.current().transcribe(audio)


class Speaker:
    OUTPUT_RATE = 24000

    def __init__(self, voice, rate="+0%", pitch="+0Hz", voice_he="he-IL-HilaNeural"):
        self.voice = voice
        self.voice_he = voice_he  # used when the text has Hebrew letters
        self.rate = rate
        self.pitch = pitch

    async def synthesize(self, text, words=None):
        """Text -> float32 audio at 24 kHz, using a free Edge voice (needs internet).
        words: a list that gets (seconds, word) for each spoken word, when the voice service says them."""
        import edge_tts
        import miniaudio

        mp3 = bytearray()
        voice = self.voice_he if lang.has_hebrew(text) else self.voice
        try:  # newer edge-tts only reports sentences unless asked for words
            communicate = edge_tts.Communicate(text, voice, rate=self.rate, pitch=self.pitch,
                                               boundary="WordBoundary")
        except TypeError:
            communicate = edge_tts.Communicate(text, voice, rate=self.rate, pitch=self.pitch)
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3 += chunk["data"]
            elif chunk["type"] == "WordBoundary" and words is not None:
                words.append((chunk["offset"] / 1e7, chunk.get("text", "")))  # offset: 100 ns steps
        decoded = miniaudio.decode(bytes(mp3), output_format=miniaudio.SampleFormat.FLOAT32,
                                   nchannels=1, sample_rate=self.OUTPUT_RATE)
        return np.frombuffer(decoded.samples, dtype=np.float32).copy()

    async def play(self, audio, on_level=None, cancel=None):
        """Plays audio; calls on_level(0..1) about 15 times a second for the mouth."""
        import sounddevice as sd

        step = 1 / 15
        window = int(self.OUTPUT_RATE * step)
        levels = []
        for i in range(0, len(audio), window):
            piece = audio[i:i + window]
            levels.append(float(np.sqrt(np.mean(piece * piece))) if len(piece) else 0.0)
        peak = max(max(levels, default=0.0), 1e-6)

        sd.play(audio, self.OUTPUT_RATE)
        start = time.monotonic()
        try:
            for n, level in enumerate(levels):
                if cancel is not None and cancel.is_set():
                    break
                if on_level:
                    on_level(min(1.0, (level / peak) ** 0.7))
                delay = start + (n + 1) * step - time.monotonic()
                if delay > 0:
                    await asyncio.sleep(delay)
            # let the tail of the sound finish
            remaining = start + len(audio) / self.OUTPUT_RATE - time.monotonic()
            if remaining > 0 and not (cancel is not None and cancel.is_set()):
                await asyncio.sleep(remaining)
        finally:
            sd.stop()
            if on_level:
                on_level(0.0)

    async def say(self, text, on_level=None, cancel=None, on_start=None):
        """Says text. on_start(timings) (async) runs right before the sound starts, with
        word_timings() for the subtitle."""
        words = []
        audio = await self.synthesize(text, words)
        log.info("voice ready (%.1f s of audio, %d word times), playing", len(audio) / self.OUTPUT_RATE, len(words))
        if on_start is not None:
            await on_start(word_timings(text, words, len(audio) / self.OUTPUT_RATE))
        await self.play(audio, on_level, cancel)


def _plain(word):
    return re.sub(r"[\W_]", "", word.lower())  # letters and digits (Hebrew too)


def word_timings(text, spoken, duration):
    """When each word of `text` is said: [[ms, words shown], ...] for Gorgi's subtitle.
    spoken: (seconds, word) from the voice service; without them, a guess from the word lengths."""
    tokens = text.split()
    if not tokens:
        return []
    out = []
    if spoken:
        at = 0  # next text word to match
        for seconds, word in spoken:
            w = _plain(word)
            if not w or at >= len(tokens):
                continue
            # the next text word containing it (a few words ahead at most: numbers may be read differently)
            found = None
            for j in range(at, min(at + 4, len(tokens))):
                t = _plain(tokens[j])
                if t and (w in t or t in w):
                    found = j
                    break
            j = found if found is not None else at
            at = j + 1
            ms = int(seconds * 1000)
            if out and out[-1][1] >= at:
                continue
            out.append([ms, at])
    if not out:  # no word times: spread the words over the sound by their length
        total = sum(len(t) + 1 for t in tokens)
        pos = 0
        for i, t in enumerate(tokens):
            out.append([int(duration * 1000 * pos / total), i + 1])
            pos += len(t) + 1
    if out[-1][1] < len(tokens):  # the rest by the end
        out.append([max(out[-1][0] + 200, int(duration * 1000) - 300), len(tokens)])
    return out
