# Resident Evil 6: the prologue route after the first dial prompt (for re6_prompts.py; see
# docs/RESIDENTEVIL6.md, Scripted route). Times are seconds on the game's script clock
# (ANYX360_INPUT_CLOCK=frames:30) from the end of that prompt; re6_prompts.py must start with the game.


def every(t0, t1, period, button, hold=0.1):
    out, t = [], t0
    while t < t1:
        out.append(f"{t:.2f}:{button}/{hold}")
        t += period
    return out


def stick_circles(t0, t1, step=0.07):
    out, t, i = [], t0, 0
    while t < t1:
        out.append(f"{t:.2f}:{['LUP', 'LRIGHT', 'LDOWN', 'LLEFT'][i % 4]}/{step}")
        t += step
        i += 1
    return out


def follow(t, n, walk=2.5):
    """LB (the camera turns to the objective), then walk; n times."""
    out = []
    for _ in range(n):
        out += [f"{t:.2f}:LB/0.4", f"{t + 0.5:.2f}:LUP/{walk}"]
        t += 0.6 + walk
    return out, t


r = []
# Out of the room with Helena on his shoulder, through the corridors (A: doors and the stairs).
r += ["0:LUP/75"] + every(1, 75, 1, "A")
r += ["77:RRIGHT/0.5", "79:LUP/14"] + every(80, 94, 0.7, "A")
r += every(97, 107, 0.7, "X")  # the herb
# The item menu: combine the herbs (Y, A ...), close it; then heal Helena (RB next to her).
r += ["124:Y", "135:A", "150:A", "156:A", "161:A", "165:A", "170:A", "176:B", "181:RB"]
r += ["186:LDOWN/3", "189.5:LUP/2.2", "189.5:LLEFT%21/2.2"] + every(191.8, 200, 0.6, "RB")
# The soldier: aim and fire, then break his grab (the RT dial is answered by re6_prompts.py).
r += ["225:LT/6"] + every(226, 231, 0.4, "RT") + stick_circles(231, 247)
r += every(258, 275, 0.7, "RB")
r += ["290:LUP/40"] + every(290.3, 330, 0.5, "RB")
# The alley after the phone call (from about 336 s): doors, the partner action (B), the railing.
t = 336.0
c, t = follow(t, 3)
r += c + [f"{t + 0.5:.2f}:A"]
t += 4.5
c, t = follow(t, 6)
r += c + [f"{t + 0.5:.2f}:B"]
t += 7.5
c, t = follow(t, 2)
r += c + [f"{t + 6:.2f}:A", f"{t + 16:.2f}:A"]  # over the railing into the street

# The first dial (X) comes about 300 s after the game starts; the earlier A prompts are not it.
ROUTE = [("dial_done", ",".join(r), 280)]
