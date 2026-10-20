"""Makes the logos the launcher draws (logo_mark.png and logo_full.png, embedded in AnyX360.exe by
launcher.rc) from brand/anyx360-logo-clean.png and anyx360-logo-with-text.png: the black background
made transparent, so they sit on the launcher's charcoal as they do on black, trimmed, and at twice
the largest size the launcher draws them at 96 DPI (the launcher scales them down to the exact pixel
size for the screen's DPI). Needs Pillow.

usage: python -I make_logos.py <brand folder> <out folder>"""
import os
import sys

from PIL import Image, ImageChops, ImageFilter

LOGOS = (  # source, output, height in pixels
    ("anyx360-logo-clean.png", "logo_mark.png", 256),
    ("anyx360-logo-with-text.png", "logo_full.png", 512),
)
BLACK_LEVEL = 3  # the background is black with noise up to this


def transparent(path):
    """The logo with its black background transparent, premultiplied (RGBa), trimmed."""
    rgb = Image.open(path).convert("RGB")
    r, g, b = rgb.split()
    level = ImageChops.lighter(ImageChops.lighter(r, g), b)
    # Over black, a pixel is its shape's color times how much of it the shape covers: its brightness
    # against the brightest pixel around it. The 0.85 keeps the shapes' gradients opaque, and the
    # floor of 64 keeps stray dim pixels far from any shape faint.
    near = level.filter(ImageFilter.MaxFilter(9))
    alpha = bytes(0 if v <= BLACK_LEVEL else min(255, round(v * 255 / (0.85 * max(n, 64))))
                  for v, n in zip(level.tobytes(), near.tobytes()))
    a = Image.frombytes("L", rgb.size, alpha)
    # The background's noise goes with its alpha (premultiplied color never exceeds alpha).
    opaque = a.point(lambda v: 255 if v else 0)
    r, g, b = (ImageChops.multiply(channel, opaque) for channel in (r, g, b))
    return Image.merge("RGBa", (r, g, b, a)).crop(a.getbbox())


def main():
    brand, out = sys.argv[1], sys.argv[2]
    for source, name, height in LOGOS:
        logo = transparent(os.path.join(brand, source))
        width = round(logo.width * height / logo.height)
        # Resampled premultiplied, so the transparent background doesn't darken the edges.
        logo.resize((width, height), Image.LANCZOS).convert("RGBA").save(os.path.join(out, name), optimize=True)


if __name__ == "__main__":
    main()
