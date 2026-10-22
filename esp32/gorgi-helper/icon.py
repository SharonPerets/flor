"""Draws Gorgi's face as an icon (for the taskbar tray and the .exe)."""

from PIL import Image, ImageDraw

BG = (5, 7, 10, 255)
TEAL = (79, 227, 214, 255)
GREY = (120, 130, 140, 255)


def make_image(size=64, connected=True):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size / 64
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=14 * s, fill=BG)
    eye = TEAL if connected else GREY
    d.rounded_rectangle([12 * s, 16 * s, 26 * s, 36 * s], radius=6 * s, fill=eye)
    d.rounded_rectangle([38 * s, 16 * s, 52 * s, 36 * s], radius=6 * s, fill=eye)
    d.arc([22 * s, 36 * s, 42 * s, 50 * s], start=20, end=160, fill=eye, width=max(1, round(4 * s)))
    return img


if __name__ == "__main__":
    # Used by the build to make the .exe icon
    make_image(256).save("gorgi.ico", sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
