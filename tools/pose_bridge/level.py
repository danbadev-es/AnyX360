"""The webcam's tilt, and a level frame for the skeleton.

A Kinect stands level (and its accelerometer tells the console how it's tilted); a webcam is often
pitched down from a monitor or a laptop lid, or rolled. Seen through a tilted camera a standing
player looks like they're leaning, and walking towards it changes their height. The bridge finds
which way is up in the webcam's (Kinect-axes) space and rotates every joint so up is +y:

- the body: while the main player stands still with straight legs and their whole body in view,
  the line from their ankles to their shoulders is about vertical; the median over a second and a
  half gives a first estimate quickly;
- the floor: the ankles of a player on the ground trace the floor plane as they move about the
  room; once their positions cover enough of it (in two directions), the plane's normal is up and
  its distance below the camera is the camera's height.

Only the pitch (a camera pointing down from a monitor or a laptop lid) is measured; the roll (a
camera turned about its lens) is the one set by hand, 0 unless changed: measured, it followed the
player's lean and a lopsided floor and turned the picture of a camera that was level.

The camera is usually a laptop's, standing still on a desk with its lid turned to face the player:
a measured pitch within 5 degrees of 0 is taken as a level camera (the estimates' noise is about
that), and once an estimate has settled (3 s) it holds, unless the readings stay 4 degrees off for
5 s (the lid was moved). It is at most 35 degrees from the camera's own up. A manual tilt (the
settings panel) sets both.
"""

import collections
import math

import numpy as np

import kinect_mapping as km

MAX_TILT = math.radians(35.0)
UP = np.array([0.0, 1.0, 0.0])
ANKLE_HEIGHT = 0.08  # metres from the ankle joints to the floor

SOURCE_NONE, SOURCE_BODY, SOURCE_FLOOR, SOURCE_MANUAL = range(4)


def rotation_between(a, b):
    """The smallest rotation (3x3) taking unit vector a to unit vector b."""
    a = np.asarray(a, dtype=np.float64) / max(np.linalg.norm(a), 1e-12)
    b = np.asarray(b, dtype=np.float64) / max(np.linalg.norm(b), 1e-12)
    v = np.cross(a, b)
    s = np.linalg.norm(v)
    c = float(np.dot(a, b))
    if s < 1e-9:
        return np.eye(3) if c > 0 else np.diag([1.0, -1.0, -1.0])
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k * ((1 - c) / (s * s))


def up_from_angles(pitch_deg, roll_deg):
    """Where up is, in the camera's space, for a camera pitched down by `pitch` and rolled `roll`
    (clockwise as seen from behind it). Kinect axes: x the player's right, y up, z away."""
    p, r = math.radians(pitch_deg), math.radians(roll_deg)
    # Pitching the camera down makes the room's up lean towards the camera (-z); rolling it
    # clockwise makes up lean to the camera's left, which is +x (the sensor's left).
    up = np.array([math.sin(r) * math.cos(p), math.cos(r) * math.cos(p), -math.sin(p)])
    return up / np.linalg.norm(up)


def angles_from_up(up):
    """(pitch, roll) in degrees for a camera-space up vector (the inverse of up_from_angles)."""
    up = np.asarray(up, dtype=np.float64) / max(np.linalg.norm(up), 1e-12)
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, -up[2]))))
    roll = math.degrees(math.atan2(up[0], up[1]))
    return pitch, roll


class Leveler:
    BODY_SAMPLES = 45       # 1.5 s of a still, upright player
    MIN_BODY_SAMPLES = 20
    FLOOR_SAMPLES = 900     # 30 s of ankle positions
    MIN_FLOOR_SAMPLES = 150
    FLOOR_SPREAD = 0.25     # metres: the ankles must cover at least this much in both directions
    ADAPT = 0.02            # how far an estimate moves towards a new reading per frame (~1.7 s)
    # The camera is usually a laptop's, still on a desk, its lid turned to face the player: a pitch
    # this close to 0 (degrees) is a level camera, the rest the estimate's noise.
    LEVEL_PITCH = 5.0
    # A still camera: after SETTLE_FRAMES of adapting (3 s), the estimate holds. It follows again only
    # when the readings stay MOVED degrees away for MOVED_FRAMES (5 s): the lid was moved.
    SETTLE_FRAMES = 90
    MOVED = 4.0
    MOVED_FRAMES = 150

    def __init__(self):
        self.up = UP.copy()
        self.source = SOURCE_NONE
        self.floor_height = 0.0  # the camera's height above the floor (m), 0: not measured
        self._body = collections.deque(maxlen=self.BODY_SAMPLES)
        self._floor = collections.deque(maxlen=self.FLOOR_SAMPLES)
        self._previous_hip = None
        self._manual = None
        self._roll = 0.0  # the roll set by hand, kept while the pitch is measured
        self._adapting = 0  # frames the estimate has adapted for since it started (or the camera moved)
        self._settled = False  # the estimate holds still (see SETTLE_FRAMES)
        self._away = 0  # consecutive frames the readings were MOVED degrees off a settled estimate

    def reset(self):
        roll = self._roll
        self.__init__()
        self._roll = roll

    def set_manual(self, pitch_deg, roll_deg):
        """A tilt set by the player; `pitch_deg` None: the pitch is measured, with `roll_deg` (the
        manual roll, None: 0) as the roll."""
        if pitch_deg is None:
            self._roll = roll_deg or 0.0
            if self._manual is not None:
                self._manual = None
                self.up, self.source = up_from_angles(0.0, self._roll), SOURCE_NONE
            elif self.source != SOURCE_NONE:
                self.up = up_from_angles(self.angles()[0], self._roll)  # the measured pitch stays
            return
        self._manual = (pitch_deg, roll_deg)
        self.up = up_from_angles(pitch_deg, roll_deg)
        self.source = SOURCE_MANUAL

    @property
    def rotation(self):
        """Camera space to the level frame."""
        return rotation_between(self.up, UP)

    def angles(self):
        return angles_from_up(self.up)

    def update(self, joints, states):
        """joints: the main player's joints in camera space (Kinect axes), states their states."""
        if self._manual is not None or joints is None:
            self._previous_hip = None
            return
        joints = np.asarray(joints, dtype=np.float64)
        needed = (km.ANKLE_LEFT, km.ANKLE_RIGHT, km.KNEE_LEFT, km.KNEE_RIGHT, km.HIP_LEFT, km.HIP_RIGHT,
                  km.SHOULDER_LEFT, km.SHOULDER_RIGHT)
        if not np.all(np.isfinite(joints)) or any(states[j] != km.JOINT_TRACKED for j in needed):
            self._previous_hip = None
            return
        hip = joints[km.HIP_CENTER]
        still = self._previous_hip is not None and np.linalg.norm(hip - self._previous_hip) < 0.02
        self._previous_hip = hip.copy()

        ankles = (joints[km.ANKLE_LEFT] + joints[km.ANKLE_RIGHT]) / 2
        shoulders = (joints[km.SHOULDER_LEFT] + joints[km.SHOULDER_RIGHT]) / 2
        axis = shoulders - ankles
        if still and self._legs_straight(joints) and 1.0 < np.linalg.norm(axis) < 1.9:
            self._body.append(axis / np.linalg.norm(axis))
        # Both feet down: the ankles at about the same height along the current up.
        if abs(np.dot(joints[km.ANKLE_LEFT] - joints[km.ANKLE_RIGHT], self.up)) < 0.05:
            self._floor.append(joints[km.ANKLE_LEFT].copy())
            self._floor.append(joints[km.ANKLE_RIGHT].copy())
        self._estimate()

    @staticmethod
    def _legs_straight(joints):
        def angle(hip, knee, ankle):
            a, b = joints[hip] - joints[knee], joints[ankle] - joints[knee]
            cos = np.dot(a, b) / max(np.linalg.norm(a) * np.linalg.norm(b), 1e-9)
            return math.degrees(math.acos(max(-1.0, min(1.0, cos))))
        return (angle(km.HIP_LEFT, km.KNEE_LEFT, km.ANKLE_LEFT) > 160
                and angle(km.HIP_RIGHT, km.KNEE_RIGHT, km.ANKLE_RIGHT) > 160)

    def _estimate(self):
        reading, source, height = None, SOURCE_NONE, 0.0
        if len(self._floor) >= self.MIN_FLOOR_SAMPLES:
            points = np.array(self._floor)
            centre = points.mean(axis=0)
            _, spread, axes = np.linalg.svd(points - centre, full_matrices=False)
            spread = spread / math.sqrt(len(points))  # standard deviations along the plane's axes
            if spread[1] >= self.FLOOR_SPREAD / 2:
                normal = axes[2] if np.dot(axes[2], UP) > 0 else -axes[2]
                reading, source = normal, SOURCE_FLOOR
                height = float(np.dot(-centre, normal)) + ANKLE_HEIGHT
        if reading is None and len(self._body) >= self.MIN_BODY_SAMPLES:
            median = np.median(np.array(self._body), axis=0)
            reading, source = median / np.linalg.norm(median), SOURCE_BODY
        if reading is None:
            return
        # Only the pitch is measured: the roll is the manual one. A camera within a few degrees of level
        # is level: the estimate's noise (the ankles' depth, the player's posture) otherwise tilted the
        # picture and the player of a camera standing level.
        pitch = angles_from_up(reading)[0]
        reading = up_from_angles(0.0 if abs(pitch) < self.LEVEL_PITCH else pitch, self._roll)
        if math.acos(max(-1.0, min(1.0, float(np.dot(reading, UP))))) > MAX_TILT:
            return
        if self.source == SOURCE_NONE:
            self.up = reading  # the first estimate at once
            self._adapting, self._settled = 0, False
        else:
            if source != self.source:
                self._adapting, self._settled = 0, False  # the floor's estimate replacing the body's
            off = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(reading, self.up))))))
            if self._settled:
                # Hold still unless the readings stay far off (the camera was moved).
                self._away = self._away + 1 if off > self.MOVED else 0
                if self._away >= self.MOVED_FRAMES:
                    self._settled, self._adapting, self._away = False, 0, 0
            if not self._settled:
                self._adapting += 1
                self.up = self.up + (reading - self.up) * self.ADAPT
                self.up /= np.linalg.norm(self.up)
                # Settled once it has adapted for a while and caught up with the readings (or, with
                # readings too noisy to catch up with, after four times as long).
                if self._adapting >= self.SETTLE_FRAMES and (off < 1.0 or self._adapting >= 4 * self.SETTLE_FRAMES):
                    self._settled = True
        self.source = source
        if height > 0.2:
            self.floor_height = height if self.floor_height == 0 else self.floor_height + (height - self.floor_height) * self.ADAPT
