"""Draws the launcher's icon (launcher.ico): a green ring and a play triangle on a charcoal rounded
square, at 16-256 pixels, antialiased by supersampling. No Xbox marks.

usage: python -I make_icon.py <out.ico>"""
import struct
import sys
import zlib

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
SAMPLES = 4  # per axis


def color_at(x, y):
    """RGBA of a point in the unit square."""
    cx, cy = x - 0.5, y - 0.5
    # The rounded square.
    r = 0.2
    qx, qy = max(abs(cx) - (0.5 - r), 0), max(abs(cy) - (0.5 - r), 0)
    if qx * qx + qy * qy > r * r:
        return (0, 0, 0, 0)
    d = (cx * cx + cy * cy) ** 0.5
    # The ring, lighter towards the top.
    if 0.255 <= d <= 0.355:
        t = (cy + 0.36) / 0.72
        return (int(0x3C + (0x10 - 0x3C) * t), int(0xB0 + (0x7C - 0xB0) * t), int(0x43 + (0x10 - 0x43) * t), 255)
    # The play triangle.
    ax, ay, bx, by, px, py = -0.075, -0.13, -0.075, 0.13, 0.135, 0.0
    def side(x1, y1, x2, y2):
        return (x2 - x1) * (cy - y1) - (y2 - y1) * (cx - x1)
    s1, s2, s3 = side(ax, ay, bx, by), side(bx, by, px, py), side(px, py, ax, ay)
    if (s1 <= 0 and s2 <= 0 and s3 <= 0) or (s1 >= 0 and s2 >= 0 and s3 >= 0):
        return (255, 255, 255, 255)
    return (0x1C, 0x1C, 0x1C, 255)


def render(size):
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(SAMPLES):
                for sx in range(SAMPLES):
                    c = color_at((x + (sx + 0.5) / SAMPLES) / size, (y + (sy + 0.5) / SAMPLES) / size)
                    for i in range(3):
                        acc[i] += c[i] * c[3]
                    acc[3] += c[3]
            a = acc[3] / (SAMPLES * SAMPLES)
            if acc[3]:
                row += bytes([round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a)])
            else:
                row += bytes(4)
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    images = [(size, png(size, render(size))) for size in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, image in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset + len(data))
        data += image
    with open(sys.argv[1], "wb") as f:
        f.write(header + entries + data)


if __name__ == "__main__":
    main()
