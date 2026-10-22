"""Album art for Gorgi's music screen.

The picture Windows has for the song (from Spotify, a browser, ...) is cut to a
square, made 240 x 240 and turned into Gorgi's screen format (RGB565). It's
sent in small pieces, because Gorgi takes at most about 15 KB per message.
"""

import io
import struct

import numpy as np
from PIL import Image, ImageOps

SIZE = 240
CHUNK = 8192

# A 4 x 4 pattern that spreads the rounding of colours, so smooth gradients
# don't turn into bands with only 32/64 shades per colour
_BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0


def to_rgb565(image_bytes, size=SIZE):
    """A picture file (PNG, JPEG...) -> size x size RGB565 pixels, little-endian."""
    im = Image.open(io.BytesIO(image_bytes)).convert("RGB")
    im = ImageOps.fit(im, (size, size), Image.LANCZOS)
    a = np.asarray(im, dtype=np.float32)
    pattern = np.tile(_BAYER, (size // 4 + 1, size // 4 + 1))[:size, :size]
    r = np.clip(a[..., 0] + pattern * 8, 0, 255).astype(np.uint16) >> 3
    g = np.clip(a[..., 1] + pattern * 4, 0, 255).astype(np.uint16) >> 2
    b = np.clip(a[..., 2] + pattern * 8, 0, 255).astype(np.uint16) >> 3
    return ((r << 11) | (g << 5) | b).astype("<u2").tobytes()


def pieces(art_id, data, chunk=CHUNK):
    """The binary messages that carry one picture to Gorgi."""
    for offset in range(0, len(data), chunk):
        yield b"GART" + struct.pack("<III", art_id, len(data), offset) + data[offset:offset + chunk]
