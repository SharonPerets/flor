"""Reads handwriting from Gorgi's Sketch screen with Windows' own handwriting
recognition (the one Windows uses for pen input). Gorgi sends the pen strokes
of one word; we answer with the best guess and a few other guesses.

Works only on Windows; elsewhere read() returns no guesses (Gorgi then keeps
the handwriting as it is)."""

import logging
import sys

log = logging.getLogger("gorgi.ink")

IS_WINDOWS = sys.platform == "win32"

# "hello" in joined-up writing (Gorgi box pixels), for the self-test
SAMPLE_HELLO = [
    [22, 93, 29, 84, 40, 69, 44, 63, 48, 53, 49, 47, 46, 44, 39, 47, 36, 53, 31, 66, 26, 84, 20, 108],
    [20, 108, 24, 99, 28, 93, 35, 84, 41, 81, 48, 81, 50, 84, 49, 90, 45, 99, 45, 105, 47, 108, 50, 108, 57, 105,
     60, 102, 67, 93],
    [69, 102, 76, 99, 79, 96, 83, 90, 84, 84, 81, 81, 78, 81, 71, 84, 68, 90, 67, 99, 69, 105, 75, 108, 81, 108,
     87, 105, 90, 102, 98, 93],
    [98, 93, 105, 84, 116, 69, 119, 63, 124, 53, 124, 47, 122, 44, 115, 47, 111, 53, 107, 66, 101, 87, 99, 105,
     102, 108, 105, 108, 111, 105, 115, 102, 122, 93],
    [122, 93, 129, 84, 140, 69, 144, 63, 148, 53, 149, 47, 146, 44, 139, 47, 136, 53, 131, 66, 126, 87, 123, 105,
     126, 108, 129, 108, 136, 105, 139, 102, 146, 93],
    [166, 81, 160, 81, 153, 84, 150, 87, 146, 93, 145, 99, 148, 105, 153, 108, 159, 108, 166, 105, 169, 102, 173,
     96, 174, 90, 171, 84, 166, 81, 162, 84, 162, 90, 164, 96, 170, 99, 179, 99, 185, 96, 189, 93],
]

_recognizer = None
_recognizer_name = ""


def _english_recognizer():
    """Windows' handwriting recognizer, set to English when there is one."""
    global _recognizer, _recognizer_name
    if _recognizer is not None:
        return _recognizer
    from winrt.windows.ui.input.inking import InkRecognizerContainer

    rec = InkRecognizerContainer()
    names = []
    try:
        for r in rec.get_recognizers():
            names.append(r.name)
            if "english" in r.name.lower() and ("u.s." in r.name.lower() or "united states" in r.name.lower()
                                                or not _recognizer_name):
                rec.set_default_recognizer(r)
                _recognizer_name = r.name
    except Exception as e:
        log.warning("handwriting recognizers: %s", e)
    log.info("handwriting recognizers: %s; using %s", names, _recognizer_name or "the default")
    _recognizer = rec
    return rec


def recognizers():
    """Names of the handwriting languages Windows has (for the log and self-test)."""
    from winrt.windows.ui.input.inking import InkRecognizerContainer

    return [r.name for r in InkRecognizerContainer().get_recognizers()]


async def read(strokes):
    """strokes: list of [x0, y0, x1, y1, ...] in pixels. Returns a list of guesses,
    best first (empty when nothing could be read)."""
    if not IS_WINDOWS or not strokes:
        return []
    from winrt.windows.foundation import Point
    from winrt.windows.ui.input.inking import InkRecognitionTarget, InkStrokeBuilder, InkStrokeContainer

    builder = InkStrokeBuilder()
    container = InkStrokeContainer()
    for s in strokes:
        pts = [Point(float(s[i]), float(s[i + 1])) for i in range(0, len(s) - 1, 2)]
        if len(pts) == 1:  # a dot (the dot on an i): make it a tiny line
            pts.append(Point(pts[0].x + 1.0, pts[0].y + 1.0))
        if pts:
            container.add_stroke(builder.create_stroke(pts))
    results = await _english_recognizer().recognize_async(container, InkRecognitionTarget.ALL)
    words = [list(r.get_text_candidates()) for r in results]
    words = [w for w in words if w]
    if not words:
        return []
    if len(words) == 1:
        guesses = words[0]
    else:  # a few words written in one go: the best of each, then the first word's other guesses
        best = " ".join(w[0] for w in words)
        guesses = [best] + [" ".join([alt] + [w[0] for w in words[1:]]) for alt in words[0][1:]]
    seen, out = set(), []
    for g in guesses:
        g = g.strip()
        if g and g not in seen:
            seen.add(g)
            out.append(g)
    return out[:4]
