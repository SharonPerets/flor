"""Gorgi's ears and voice on the PC.

- Recorder: listens to the PC microphone and stops when you stop talking.
- Transcriber: turns speech into text with Whisper, on your PC (free, private).
- Speaker: says Gorgi's answers with a free Microsoft Edge voice and reports
  how loud each moment is, so Gorgi's mouth can follow the voice.
"""

import asyncio
import logging
import time

import numpy as np

log = logging.getLogger("gorgi.voice")

SAMPLE_RATE = 16000  # what Whisper wants
FRAME_SECONDS = 0.03


class Recorder:
    def __init__(self, device=None):
        self.device = device

    async def record(self, wait_for_speech=7.0, max_seconds=15.0, end_silence=0.9, cancel=None):
        """Records one utterance. Returns float32 audio at 16 kHz, or None if nobody spoke."""
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
                threshold = max(noise * 3.0, 0.012)

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

        if speech_seconds < 0.25:  # just a click or a cough
            return None
        return np.concatenate(chunks)


class Transcriber:
    def __init__(self, model_name, models_dir):
        self.model_name = model_name
        self.models_dir = models_dir
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

            log.info("loading Whisper model %s (downloads once, ~150 MB)...", self.model_name)
            self._model = WhisperModel(self.model_name, device="cpu", compute_type="int8",
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
        segments, _info = self._model.transcribe(audio, language="en", beam_size=1, vad_filter=False,
                                                 condition_on_previous_text=False)
        text = " ".join(s.text.strip() for s in segments).strip()
        # Whisper sometimes "hears" these in silence
        if text.lower().strip(" .!?") in ("", "you", "thank you", "thanks for watching", "bye"):
            return ""
        return text


class Speaker:
    OUTPUT_RATE = 24000

    def __init__(self, voice, rate="+0%", pitch="+0Hz"):
        self.voice = voice
        self.rate = rate
        self.pitch = pitch

    async def synthesize(self, text):
        """Text -> float32 audio at 24 kHz, using a free Edge voice (needs internet)."""
        import edge_tts
        import miniaudio

        mp3 = bytearray()
        communicate = edge_tts.Communicate(text, self.voice, rate=self.rate, pitch=self.pitch)
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3 += chunk["data"]
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

    async def say(self, text, on_level=None, cancel=None):
        audio = await self.synthesize(text)
        await self.play(audio, on_level, cancel)
