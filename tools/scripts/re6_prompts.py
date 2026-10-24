"""Answers Resident Evil 6's timed prompts while a scripted route plays (test runs only).

usage: python -I re6_prompts.py <exe path> <live file> <log file> <seconds> [route.py]

The game runs with ANYX360_INPUT_LIVE=<live file> (see docs/GEARS3.md). This script finds its window
(exact title "AnyX360 - RESIDENT EVIL 6", owned by <exe path>), captures it with PrintWindow about 30
times a second and:
- dial prompts (a needle turning round a button, either way): presses the button (X, A, B, Y or
  RT, told by the icon's colour) when the needle is about to be inside the coloured zone;
- the stick prompt (a yellow ring round the stick): turns the left stick for a moment.
With route.py, which defines ROUTE = [(trigger, "t:BUTTON[/hold],...", earliest), ...], each route is
appended to the live file when its trigger happens, <earliest> seconds or more after this script
started: "start" (the window appears) or "dial_done" (no turning needle for 6 s after one seen
from <earliest> on). The
times are on the game's script clock (ANYX360_INPUT_CLOCK) from that moment.
See docs/RESIDENTEVIL6.md, Scripted route.
"""
import ctypes
import ctypes.wintypes as wt
import math
import os
import sys
import time

from PIL import Image

user32 = ctypes.WinDLL("user32")
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # per-monitor v2: client sizes in pixels
gdi32 = ctypes.WinDLL("gdi32")
kernel32 = ctypes.WinDLL("kernel32")

TITLE = "AnyX360 - RESIDENT EVIL 6"
EXE = os.path.normcase(os.path.abspath(sys.argv[1]))
LIVE, LOG, SECONDS = sys.argv[2], sys.argv[3], float(sys.argv[4])
ROUTE = []
if len(sys.argv) > 5:
    scope = {}
    exec(open(sys.argv[5], encoding="utf-8").read(), scope)
    ROUTE = scope["ROUTE"]
log = open(LOG, "a", encoding="utf-8")
t0 = time.perf_counter()


def say(msg):
    log.write(f"{time.perf_counter() - t0:8.2f} {msg}\n")
    log.flush()


def image_of(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)
    if not h:
        return ""
    buf = ctypes.create_unicode_buffer(1024)
    n = wt.DWORD(1024)
    ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    kernel32.CloseHandle(h)
    return os.path.normcase(buf.value) if ok else ""


def find_window():
    found = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        buf = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(hwnd, buf, 512)
        if buf.value != TITLE:
            return True
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if image_of(pid.value) == EXE:
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(proc(cb), 0)
    return found[0] if found else None


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


def capture(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    if w <= 0 or h <= 0:
        return None
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, 3)
    bi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def classify(p):
    """'w' white, 'y' yellow (or yellow-green), 'z' a zone colour (cyan, blue, green), 'r' red,
    '' dark or dull."""
    r, g, b = p
    hi, lo = max(p), min(p)
    if lo > 170:
        return "w"
    if hi < 110 or hi - lo < 70:
        return ""
    if r > 140 and g > 140 and b < r - 60:
        return "y"
    if r == hi:
        return "r"
    return "z"


def dial(im, cx=0.5, cy=0.675):
    """The prompt dial at (cx, cy): (needle angle or None, zone angles, yellow count, button).

    Angles are clockwise from 12 o'clock. The needle is the white ray outside the icon (radius
    52-120 at 1080 lines); the zone the cyan, blue or green arc the needle must be in when the
    button is pressed; yellow arcs belong to the stick-turning prompt; the button is told by the
    icon's colour (blue X, green A, red B, yellow Y, white RT)."""
    w, h = im.size
    X, Y = cx * w, cy * h
    s = h / 1080
    best, zone, yellow = (0, None), [], 0
    for a in range(0, 360, 4):
        sa, ca = math.sin(math.radians(a)), math.cos(math.radians(a))
        white = colour = 0
        for r in range(28, 121, 4):
            c = classify(im.getpixel((int(X + r * s * sa), int(Y - r * s * ca))))
            yellow += c == "y"
            if r >= 52:
                white += c == "w"
            if r >= 60:
                colour += c == "z"
        if white > best[0]:
            best = (white, a)
        if colour >= 2:
            zone.append(a)
    box = [im.getpixel((int(X + dx * s), int(Y + dy * s))) for dx in (-20, -10, 0, 10, 20) for dy in (-20, -10, 0, 10, 20)]
    R, G, B = (sum(p[k] for p in box) / len(box) for k in range(3))
    if yellow >= 6:
        # The stick-turning prompt (its icon, the stick, is white), or yellow light in a scene.
        button = "TURN" if min(R, G, B) > 130 else None
    elif B > R + 25 and B >= G:
        button = "X"
    elif G > R + 25 and G > B:
        button = "A"
    elif R > G + 40 and R > B + 40:
        button = "B"
    elif R > B + 40 and G > B + 40:
        button = "Y"
    elif min(R, G, B) > 150:
        button = "RT"
    else:
        button = None
    needle_angle = best[1] if best[0] >= 4 and button and button != "TURN" else None
    if len(zone) > 50:  # a glow all round, not an arc
        zone = []
    return needle_angle, zone, yellow, button


def in_zone(angle, zone, margin=10):
    """Whether the zone has arc on both sides of `angle` within `margin` degrees (the needle hides
    the arc where it lies)."""
    a = angle % 360
    before = any(0 <= (a - z) % 360 <= margin for z in zone)
    after = any(0 <= (z - a) % 360 <= margin for z in zone)
    return before and after


def send(lines):
    with open(LIVE, "a", encoding="ascii") as f:
        f.write("".join(line + "\n" for line in lines))


hwnd = None
samples = []  # (time, unwrapped needle angle)
last_press = -10.0
dial_seen = None  # when a turning needle was last seen
last_zone = None  # (time, zone angles): the zone fades as the needle comes round
step = 0
while time.perf_counter() - t0 < SECONDS:
    if not hwnd or not user32.IsWindow(hwnd):
        hwnd = find_window()
        if not hwnd:
            time.sleep(0.5)
            continue
        say(f"window {hwnd}")
    im = capture(hwnd)
    now = time.perf_counter() - t0
    if im is None:
        time.sleep(0.1)
        continue
    if step < len(ROUTE):
        trigger, route, earliest = ROUTE[step]
        if now >= earliest and (trigger == "start" or (trigger == "dial_done" and dial_seen is not None
                                                        and dial_seen >= earliest and now - dial_seen > 6)):
            send(item for item in route.split(",") if item)
            say(f"route {step} ({trigger})")
            step += 1
            dial_seen = None
    hit, zone, yellow, button = dial(im)
    if zone and hit is not None:
        last_zone = (now, zone)
    elif last_zone and now - last_zone[0] < 2.0:
        zone = last_zone[1]
    if button == "TURN" and now - last_press > 0.5:
        send(f"{n * 0.06:.2f}:{d}/0.06" for n, d in enumerate(["LUP", "LRIGHT", "LDOWN", "LLEFT"] * 3))
        last_press = now
        say("stick prompt")
    if hit is None:
        samples.clear()
        time.sleep(0.02)
        continue
    if samples:
        prev = samples[-1][1]
        hit = prev + (hit - prev % 360 + 540) % 360 - 180
    samples = [p for p in samples if now - p[0] < 1.0] + [(now, hit)]
    if len(samples) < 3 or samples[-1][0] - samples[0][0] < 0.1:
        continue
    (ta, aa), (tb, ab) = samples[0], samples[-1]
    speed = (ab - aa) / (tb - ta)  # degrees per second, clockwise > 0
    if not 40 < abs(speed) < 900:
        continue
    dial_seen = now
    if now - last_press < 0.8 or not button:
        continue
    if not zone:
        zone = list(range(340, 360, 4)) + list(range(0, 24, 4))  # the first prompt's: at the top
    if in_zone(ab + speed * 0.08, zone):  # where the needle is when the game reads the press
        send([f"{button}/0.1"])
        say(f"dial {button}: needle {ab % 360:.0f} speed {speed:.0f} zone {min(zone)}-{max(zone)}")
        last_press = now
say("done")
