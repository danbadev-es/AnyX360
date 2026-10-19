"""How closely the bridge follows a person: the whole pipeline on synthetic MediaPipe output, with
noise like the real model's, measured against the true joints.

  python -I tools/pose_bridge/benchmark.py     (prints the numbers; run_tests.py holds the limits)

The movements are made by rotating limbs about their joints, so bone lengths stay true. The noise:
image landmarks a few pixels off (more at the hands and feet), world landmarks a few centimetres
off (more in depth) and their scale a little off each frame (which moves the solved distance), all
correlated from frame to frame as a tracker's output is.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

import camera_model  # noqa: E402
import kinect_mapping as km  # noqa: E402
import synthetic  # noqa: E402
from pipeline import Pipeline, Pose  # noqa: E402

WIDTH, HEIGHT, FOV, FPS = 640, 480, 60.0, 30
WEBCAM_HEIGHT = 1.0  # metres above the floor (on top of a TV or monitor)
SETTLE = 2.0  # seconds before measuring: calibration and the filters' start
SETTINGS = {"fov": FOV, "smoothing": 0.5, "camera_height": 0.8}

# Image noise (pixels at 640x480) and world noise (metres) per landmark: the torso is steadier than
# the hands and feet. World depth is the model's weakest output.
_ENDS = {km.L_WRIST, km.R_WRIST, km.L_ANKLE, km.R_ANKLE}
_TIPS = {km.L_PINKY, km.R_PINKY, km.L_INDEX, km.R_INDEX, km.L_THUMB, km.R_THUMB,
         km.L_HEEL, km.R_HEEL, km.L_FOOT, km.R_FOOT}
_MIDDLE = {km.L_ELBOW, km.R_ELBOW, km.L_KNEE, km.R_KNEE}


def _per_landmark(torso, middle, ends, tips):
    values = np.full(km.LANDMARKS, torso)
    for group, value in ((_MIDDLE, middle), (_ENDS, ends), (_TIPS, tips)):
        values[list(group)] = value
    return values


IMAGE_PX = _per_landmark(2.0, 3.0, 4.0, 6.0)
WORLD_XY = _per_landmark(0.010, 0.015, 0.020, 0.025)
WORLD_Z = _per_landmark(0.025, 0.035, 0.045, 0.055)
SCALE = 0.02  # the world pose's size, relative
CORRELATION = 0.5  # of each noise from one frame to the next


def rotate(points, pivot, axis, angle):
    """Rotates points (n, 3) about the line through pivot along axis (Rodrigues)."""
    axis = np.asarray(axis, dtype=np.float64)
    axis = axis / np.linalg.norm(axis)
    c, s = math.cos(angle), math.sin(angle)
    k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]])
    rotation = np.eye(3) + s * k + (1 - c) * (k @ k)
    return (np.asarray(points) - pivot) @ rotation.T + pivot


_RIGHT_ARM = [km.ELBOW_RIGHT, km.WRIST_RIGHT, km.HAND_RIGHT]
_LEFT_ARM = [km.ELBOW_LEFT, km.WRIST_LEFT, km.HAND_LEFT]
_LEFT_LEG = [km.KNEE_LEFT, km.ANKLE_LEFT, km.FOOT_LEFT]
_RIGHT_LEG = [km.KNEE_RIGHT, km.ANKLE_RIGHT, km.FOOT_RIGHT]


def raise_arm(joints, right, angle):
    """Raises an arm sideways by angle (radians) in the frontal plane."""
    arm, shoulder = (_RIGHT_ARM, km.SHOULDER_RIGHT) if right else (_LEFT_ARM, km.SHOULDER_LEFT)
    joints[arm] = rotate(joints[arm], joints[shoulder], (0, 0, 1), angle if right else -angle)


def leg_out(joints, right, angle):
    leg, hip = (_RIGHT_LEG, km.HIP_RIGHT) if right else (_LEFT_LEG, km.HIP_LEFT)
    joints[leg] = rotate(joints[leg], joints[hip], (0, 0, 1), angle if right else -angle)


def bend_knees(joints, drop):
    """Lowers everything above the knees by drop, the knees moving forward to keep the legs' lengths."""
    upper = [j for j in range(km.JOINTS) if j not in _LEFT_LEG + _RIGHT_LEG]
    for hip, knee, ankle in ((km.HIP_LEFT, km.KNEE_LEFT, km.ANKLE_LEFT), (km.HIP_RIGHT, km.KNEE_RIGHT, km.ANKLE_RIGHT)):
        thigh = np.linalg.norm(joints[knee] - joints[hip])
        shin = np.linalg.norm(joints[ankle] - joints[knee])
        new_hip = joints[hip] - (0, drop, 0)
        # The knee: on circles of the thigh's and shin's lengths around hip and ankle, in front.
        d = new_hip - joints[ankle]
        distance = np.linalg.norm(d)
        along = (distance ** 2 + shin ** 2 - thigh ** 2) / (2 * distance)
        across = math.sqrt(max(shin ** 2 - along ** 2, 0.0))
        forward = np.cross(d / distance, (1, 0, 0))
        forward = -forward if forward[2] > 0 else forward  # towards the sensor (smaller z)
        joints[knee] = joints[ankle] + d / distance * along + forward / np.linalg.norm(forward) * across
    joints[upper] -= (0, drop, 0)


def smoothstep(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def standing(x=0.0, z=2.5):
    return synthetic.standing(x=x, z=z, camera_height=WEBCAM_HEIGHT)


def still(t):
    return standing()


ARM_WAVE_HZ = 0.5


def arm_wave(t):
    """The right arm waving sideways between 35 and 90 degrees: the hand at about 1 m/s."""
    joints = standing()
    raise_arm(joints, True, math.radians(62.5) + math.radians(27.5) * math.sin(2 * math.pi * ARM_WAVE_HZ * t))
    return joints


STEP_START, STEP_SECONDS, STEP_DISTANCE = 3.0, 0.4, 0.45


def side_step(t):
    """A step to the player's left (towards -x) at 3 s, and back at 5 s."""
    there = smoothstep((t - STEP_START) / STEP_SECONDS) - smoothstep((t - STEP_START - 2.0) / STEP_SECONDS)
    return standing(x=-STEP_DISTANCE * there)


TAP_START, TAP_SECONDS, TAP_ANGLE = 3.0, 0.25, math.radians(25)


def leg_tap(t):
    """The left leg out to the side at 3 s (a quarter of a second), held, back at 4 s."""
    joints = standing()
    out = smoothstep((t - TAP_START) / TAP_SECONDS) - smoothstep((t - TAP_START - 1.0) / TAP_SECONDS)
    leg_out(joints, False, TAP_ANGLE * out)
    return joints


PUSH_START, PUSH_SECONDS, PUSH_ANGLE = 3.0, 0.35, math.radians(75)


def push(t):
    """The right arm swung forward towards the webcam at 3 s (the hand travels half a metre in
    depth), held, back at 4 s: reaching for a ball."""
    joints = standing()
    out = smoothstep((t - PUSH_START) / PUSH_SECONDS) - smoothstep((t - PUSH_START - 1.0) / PUSH_SECONDS)
    joints[_RIGHT_ARM] = rotate(joints[_RIGHT_ARM], joints[km.SHOULDER_RIGHT], (1, 0, 0), PUSH_ANGLE * out)
    return joints


WALK_START, WALK_SECONDS, WALK_FROM, WALK_TO = 3.0, 0.8, 3.0, 2.2


def walk_in(t):
    """Two steps towards the webcam at 3 s (3 m to 2.2 m), and back at 5 s."""
    there = smoothstep((t - WALK_START) / WALK_SECONDS) - smoothstep((t - WALK_START - 2.0) / WALK_SECONDS)
    return standing(z=WALK_FROM + (WALK_TO - WALK_FROM) * there)


def dance(t):
    """Arms swinging out of phase, a sway, knees bending on the beat, a leg out every other bar."""
    joints = standing(x=0.08 * math.sin(math.pi * t))
    raise_arm(joints, True, 0.3 + 0.6 * (1 + math.sin(2 * math.pi * t)))
    raise_arm(joints, False, 0.3 + 0.6 * (1 - math.sin(2 * math.pi * t)))
    leg_out(joints, (int(t) // 2) % 2 == 0, math.radians(20) * max(0.0, math.sin(math.pi * t)) ** 2)
    bend_knees(joints, 0.04 * (1 + math.sin(4 * math.pi * t)))
    return joints


def stand_then_dance(t):
    """Standing still for 3 s (at the game's menus), then dancing."""
    return standing() if t < 3.0 else dance(t - 3.0)


class Webcam:
    """What MediaPipe reports for a person in front of the webcam, with correlated noise."""

    def __init__(self, seed, pitch_deg=0.0, noise=1.0):
        self.rng = np.random.default_rng(seed)
        self.focal = camera_model.webcam_focal(WIDTH, FOV)
        self.noise = noise
        pitch = math.radians(pitch_deg)
        # The webcam looking down by pitch: a point's coordinates in its frame are rotated up.
        c, s = math.cos(pitch), math.sin(pitch)
        self.view = np.array([[1, 0, 0], [0, c, s], [0, -s, c]])
        self._state = None

    def _correlated(self, sigmas):
        fresh = [self.rng.normal(0.0, 1.0, np.shape(s)) * s for s in sigmas]
        if self._state is None:
            self._state = fresh
        else:
            keep = CORRELATION
            self._state = [keep * old + math.sqrt(1 - keep * keep) * new for old, new in zip(self._state, fresh)]
        return self._state

    def observe(self, joints):
        joints = joints @ self.view.T
        normalized, world, visibility = synthetic.landmarks(joints, WIDTH, HEIGHT, self.focal)
        n = self.noise
        image_noise, world_noise, scale_noise = self._correlated([
            np.stack([IMAGE_PX, IMAGE_PX], axis=1) * n,
            np.stack([WORLD_XY, WORLD_XY, WORLD_Z], axis=1) * n,
            np.array([SCALE * n]),
        ])
        normalized = normalized + image_noise / (WIDTH, HEIGHT)
        world = (world + world_noise) * (1.0 + scale_noise[0])
        return Pose(normalized, world, visibility)


def run(motion, seconds, seed=1, pitch_deg=0.0, settings=None, noise=1.0, tuning=None, settle=SETTLE):
    """The pipeline's output and the truth: times (n,), joints (n, 20, 3) each, from settle on; the
    output shifted by the play space's constant offset (the median difference at the hip centre)."""
    pipeline = Pipeline(dict(SETTINGS, **(settings or {})), tuning)
    webcam = Webcam(seed, pitch_deg, noise)
    times, out, truth = [], [], []
    for i in range(int(seconds * FPS)):
        t = i / FPS
        joints = motion(t)
        bodies, _ = pipeline.process([webcam.observe(joints)], (WIDTH, HEIGHT), t)
        if t >= settle and bodies:
            times.append(t)
            out.append(bodies[0]["joints"])
            truth.append(joints)
    times, out, truth = np.array(times), np.array(out), np.array(truth)
    shift = np.median(out[:, km.HIP_CENTER] - truth[:, km.HIP_CENTER], axis=0)
    return times, out - shift, truth


_HANDS = [km.HAND_LEFT, km.HAND_RIGHT]
_FEET = [km.FOOT_LEFT, km.FOOT_RIGHT, km.ANKLE_LEFT, km.ANKLE_RIGHT]


def _jitter(out, joints):
    """Root mean square of the joints' standard deviations over time, millimetres."""
    return float(np.sqrt(np.mean(out[:, joints].std(axis=0) ** 2)) * 1000 * math.sqrt(3))


def _crossing(times, values, level):
    """The first time values cross level (linear interpolation)."""
    above = values >= level
    i = int(np.argmax(above))
    if not above[i] or i == 0:
        return float("nan")
    return float(times[i - 1] + (level - values[i - 1]) / (values[i] - values[i - 1]) * (times[i] - times[i - 1]))


def _phase(times, values, hz):
    w = 2 * math.pi * hz
    values = values - values.mean()
    return math.atan2(float(np.sum(values * np.cos(w * times))), float(np.sum(values * np.sin(w * times))))


def measure(settings=None, seed=1, tuning=None):
    """The benchmark's numbers (a dict) for these pipeline settings."""
    m = {}
    settings = dict(settings or {})

    def run_(motion, seconds, pitch_deg=0.0, settle=SETTLE):
        return run(motion, seconds, seed, pitch_deg=pitch_deg, settings=settings, tuning=tuning, settle=settle)

    _, out, truth = run_(still, 6)
    m["still_hands_jitter_mm"] = _jitter(out, _HANDS)
    m["still_feet_jitter_mm"] = _jitter(out, _FEET)
    m["still_hip_depth_jitter_mm"] = float(out[:, km.HIP_CENTER, 2].std() * 1000)

    times, out, truth = run_(arm_wave, 8)
    delay = (_phase(times, truth[:, km.HAND_RIGHT, 1], ARM_WAVE_HZ)
             - _phase(times, out[:, km.HAND_RIGHT, 1], ARM_WAVE_HZ)) / (2 * math.pi * ARM_WAVE_HZ)
    m["arm_wave_lag_ms"] = delay * 1000
    m["arm_wave_hand_error_mm"] = float(np.sqrt(np.mean(np.sum((out[:, km.HAND_RIGHT] - truth[:, km.HAND_RIGHT]) ** 2, axis=1))) * 1000)
    forearm = np.linalg.norm(out[:, km.WRIST_RIGHT] - out[:, km.ELBOW_RIGHT], axis=1)
    m["arm_wave_forearm_length_sd_mm"] = float(forearm.std() * 1000)

    times, out, truth = run_(side_step, 7)
    # Towards -x: crossing half the step as -x rises.
    half = STEP_DISTANCE / 2
    m["side_step_lag_ms"] = (_crossing(times, -out[:, km.HIP_CENTER, 0], half)
                             - _crossing(times, -truth[:, km.HIP_CENTER, 0], half)) * 1000
    there = (times > STEP_START + STEP_SECONDS) & (times < STEP_START + 2.0)
    m["side_step_overshoot_mm"] = float(max(0.0, (-out[there, km.HIP_CENTER, 0]).max() - STEP_DISTANCE) * 1000)

    times, out, truth = run_(leg_tap, 6)
    ankle_out = truth[:, km.ANKLE_LEFT, 0].max() - truth[:, km.ANKLE_LEFT, 0]
    level_ = ankle_out.max() / 2
    m["leg_tap_lag_ms"] = (_crossing(times, out[0, km.ANKLE_LEFT, 0] - out[:, km.ANKLE_LEFT, 0], level_)
                           - _crossing(times, ankle_out, level_)) * 1000
    planted = times < TAP_START
    m["leg_tap_planted_foot_jitter_mm"] = _jitter(out[planted], [km.FOOT_RIGHT, km.ANKLE_RIGHT])

    times, out, truth = run_(push, 6)
    reach = truth[0, km.HAND_RIGHT, 2] - truth[:, km.HAND_RIGHT, 2]
    m["push_lag_ms"] = (_crossing(times, out[0, km.HAND_RIGHT, 2] - out[:, km.HAND_RIGHT, 2], reach.max() / 2)
                        - _crossing(times, reach, reach.max() / 2)) * 1000
    moving = (times > PUSH_START - 0.2) & (times < PUSH_START + 1.6)
    m["push_hand_error_mm"] = float(np.sqrt(np.mean(np.sum((out[moving, km.HAND_RIGHT] - truth[moving, km.HAND_RIGHT]) ** 2, axis=1))) * 1000)

    times, out, truth = run_(walk_in, 7)
    half = (WALK_FROM - WALK_TO) / 2
    m["walk_in_lag_ms"] = (_crossing(times, WALK_FROM - out[:, km.HIP_CENTER, 2], half)
                           - _crossing(times, WALK_FROM - truth[:, km.HIP_CENTER, 2], half)) * 1000
    there = (times > WALK_START + WALK_SECONDS) & (times < WALK_START + 2.0)
    m["walk_in_overshoot_mm"] = float(max(0.0, WALK_TO - out[there, km.HIP_CENTER, 2].min()) * 1000)

    # Tilted: standing at the menus first, which is when the tilt is measured.
    for name, pitch, motion, settle in (("dance", 0.0, dance, SETTLE), ("dance_tilted_15", 15.0, stand_then_dance, 4.5)):
        _, out, truth = run_(motion, 12 + settle, pitch, settle)
        error = np.linalg.norm(out - truth, axis=2)
        m[name + "_error_mm"] = float(np.sqrt(np.mean(error ** 2)) * 1000)
        m[name + "_hands_p95_mm"] = float(np.percentile(error[:, _HANDS], 95) * 1000)
        m[name + "_feet_p95_mm"] = float(np.percentile(error[:, _FEET], 95) * 1000)
    return m


def main():
    m = measure()
    width = max(len(k) for k in m)
    for key, value in m.items():
        print(f"{key:<{width}}  {value:8.1f}")


if __name__ == "__main__":
    main()
