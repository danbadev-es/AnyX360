"""Makes the icon of AnyX360's executables (launcher.ico: the launcher's and every game's) from the
mark, brand/anyx360-logo-clean.png: its margins trimmed, on a rounded square of the logo's own black
(the white chevron is drawn for a dark background, and a light taskbar would wash it out). 16-64
pixels as bitmaps, each resampled from the logo and the small ones sharpened a little; 256 as a PNG.
Needs Pillow.

usage: python -I make_icon.py <anyx360-logo-clean.png> <out.ico>"""
import io
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
FILL = 0.86      # the mark's width in the square
RADIUS = 0.2     # the square's corners
BLACK_LEVEL = 3  # the logo's background is black with noise up to this


def square(logo):
    """The mark centered on a rounded square of its own background."""
    r, g, b = logo.split()
    level = ImageChops.lighter(ImageChops.lighter(r, g), b)
    left, top, right, bottom = level.point(lambda v: 255 if v > BLACK_LEVEL else 0).getbbox()
    side = round(max(right - left, bottom - top) / FILL)
    cx, cy = (left + right) / 2, (top + bottom) / 2
    x, y = round(cx - side / 2), round(cy - side / 2)
    out = Image.new("RGB", (side, side))
    out.paste(logo.crop((max(x, 0), max(y, 0), min(x + side, logo.width), min(y + side, logo.height))),
              (max(-x, 0), max(-y, 0)))
    corners = Image.new("L", (side, side), 0)
    ImageDraw.Draw(corners).rounded_rectangle((0, 0, side - 1, side - 1), round(side * RADIUS), fill=255)
    out.putalpha(corners)
    return out


def sized(image, size):
    small = image.resize((size, size), Image.LANCZOS)
    if size <= 32:  # crisper edges where every pixel counts
        rgb = small.convert("RGB").filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=0))
        small = Image.merge("RGBA", (*rgb.split(), small.getchannel("A")))
    return small


def bitmap(image):
    """An icon's 32-bit bitmap: the header (twice the height: color and mask), BGRA rows bottom-up,
    then the 1-bit mask (transparent pixels set), rows padded to 4 bytes."""
    size = image.width
    pixels = image.tobytes("raw", "BGRA", 0, -1)
    alpha = image.getchannel("A").transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    stride = (size + 31) // 32 * 4
    mask = bytearray(stride * size)
    for y in range(size):
        for x in range(size):
            if alpha[y * size + x] == 0:
                mask[y * stride + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + bytes(mask)


def png(image):
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def main():
    logo = square(Image.open(sys.argv[1]).convert("RGB"))
    images = [(size, png(sized(logo, size)) if size >= 256 else bitmap(sized(logo, size))) for size in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, image in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset + len(data))
        data += image
    with open(sys.argv[2], "wb") as f:
        f.write(header + entries + data)


if __name__ == "__main__":
    main()
