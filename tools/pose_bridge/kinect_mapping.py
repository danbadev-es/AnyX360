"""From MediaPipe's 33 pose landmarks to the Kinect's 20 joints, in metres in Kinect camera space.

MediaPipe gives each pose twice: normalized image coordinates, and "world" coordinates in metres
around the hips with the image's axes. The world pose is placed in the webcam's camera space by the
translation whose projection best matches the image landmarks (a linear least-squares solve), then
turned into Kinect axes. The play space moves the scene so the player stands where a Kinect expects
them: centred, about 2.3 m away, their feet on the virtual floor.
"""

import numpy as np

import camera_model

# MediaPipe pose landmarks.
L_EAR, R_EAR = 7, 8
L_SHOULDER, R_SHOULDER, L_ELBOW, R_ELBOW, L_WRIST, R_WRIST = 11, 12, 13, 14, 15, 16
L_PINKY, R_PINKY, L_INDEX, R_INDEX, L_THUMB, R_THUMB = 17, 18, 19, 20, 21, 22
L_HIP, R_HIP, L_KNEE, R_KNEE, L_ANKLE, R_ANKLE = 23, 24, 25, 26, 27, 28
L_HEEL, R_HEEL, L_FOOT, R_FOOT = 29, 30, 31, 32
LANDMARKS = 33

# Kinect joints (x360::nui::Joint).
(HIP_CENTER, SPINE, SHOULDER_CENTER, HEAD,
 SHOULDER_LEFT, ELBOW_LEFT, WRIST_LEFT, HAND_LEFT,
 SHOULDER_RIGHT, ELBOW_RIGHT, WRIST_RIGHT, HAND_RIGHT,
 HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, FOOT_LEFT,
 HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT, FOOT_RIGHT) = range(20)
JOINTS = 20

JOINT_NOT_TRACKED, JOINT_INFERRED, JOINT_TRACKED = 0, 1, 2
CLIPPED_RIGHT, CLIPPED_LEFT, CLIPPED_TOP, CLIPPED_BOTTOM = 1, 2, 4, 8

# Bones for drawing (joint pairs).
BONES = [
    (HIP_CENTER, SPINE), (SPINE, SHOULDER_CENTER), (SHOULDER_CENTER, HEAD),
    (SHOULDER_CENTER, SHOULDER_LEFT), (SHOULDER_LEFT, ELBOW_LEFT), (ELBOW_LEFT, WRIST_LEFT), (WRIST_LEFT, HAND_LEFT),
    (SHOULDER_CENTER, SHOULDER_RIGHT), (SHOULDER_RIGHT, ELBOW_RIGHT), (ELBOW_RIGHT, WRIST_RIGHT),
    (WRIST_RIGHT, HAND_RIGHT),
    (HIP_CENTER, HIP_LEFT), (HIP_LEFT, KNEE_LEFT), (KNEE_LEFT, ANKLE_LEFT), (ANKLE_LEFT, FOOT_LEFT),
    (HIP_CENTER, HIP_RIGHT), (HIP_RIGHT, KNEE_RIGHT), (KNEE_RIGHT, ANKLE_RIGHT), (ANKLE_RIGHT, FOOT_RIGHT),
]

# Each Kinect joint as the mean of landmarks (the hip centre and spine are set below).
_DIRECT = {
    SHOULDER_CENTER: (L_SHOULDER, R_SHOULDER),
    HEAD: (L_EAR, R_EAR),
    SHOULDER_LEFT: (L_SHOULDER,), ELBOW_LEFT: (L_ELBOW,), WRIST_LEFT: (L_WRIST,),
    HAND_LEFT: (L_PINKY, L_INDEX, L_THUMB),
    SHOULDER_RIGHT: (R_SHOULDER,), ELBOW_RIGHT: (R_ELBOW,), WRIST_RIGHT: (R_WRIST,),
    HAND_RIGHT: (R_PINKY, R_INDEX, R_THUMB),
    HIP_LEFT: (L_HIP,), KNEE_LEFT: (L_KNEE,), ANKLE_LEFT: (L_ANKLE,),
    FOOT_LEFT: (L_HEEL, L_FOOT),
    HIP_RIGHT: (R_HIP,), KNEE_RIGHT: (R_KNEE,), ANKLE_RIGHT: (R_ANKLE,),
    FOOT_RIGHT: (R_HEEL, R_FOOT),
}


def joint_visibility(visibility):
    """Each Kinect joint's visibility (20,): the least of the landmarks it comes from."""
    visibility = np.asarray(visibility, dtype=np.float64)
    seen = np.empty(JOINTS)
    for joint, sources in _DIRECT.items():
        seen[joint] = visibility[list(sources)].min()
    seen[HIP_CENTER] = seen[SPINE] = min(visibility[L_HIP], visibility[R_HIP], visibility[L_SHOULDER],
                                         visibility[R_SHOULDER])
    return seen


_LEGS = [L_KNEE, R_KNEE, L_ANKLE, R_ANKLE, L_HEEL, R_HEEL, L_FOOT, R_FOOT]


def legs_in_picture(normalized, visibility):
    """The visibility with the legs' landmarks outside the webcam's image at 0: out there the pose
    model guesses them, however sure it says it is (a player close to a laptop's camera)."""
    visibility = np.array(visibility, dtype=np.float64)
    points = np.asarray(normalized, dtype=np.float64)[_LEGS, :2]
    outside = (points < -_EDGE).any(axis=1) | (points > 1 + _EDGE).any(axis=1)
    visibility[np.array(_LEGS)[outside]] = 0.0
    return visibility


def kinect_joints(points, visibility, threshold=0.5):
    """points: (33, 3) landmark positions in Kinect axes; visibility: (33,).

    Returns joints (20, 3) and their states (20,): tracked if every landmark they come from is
    visible enough, else inferred.
    """
    points = np.asarray(points, dtype=np.float64)
    joints = np.empty((JOINTS, 3))
    for joint, sources in _DIRECT.items():
        joints[joint] = points[list(sources)].mean(axis=0)
    hips = (points[L_HIP] + points[R_HIP]) / 2
    shoulders = joints[SHOULDER_CENTER]
    joints[HIP_CENTER] = hips + 0.10 * (shoulders - hips)
    joints[SPINE] = hips + 0.35 * (shoulders - hips)
    states = np.where(joint_visibility(visibility) >= threshold, JOINT_TRACKED, JOINT_INFERRED).astype(np.uint8)
    return joints, states


class JointStates:
    """Tracked or inferred, with hysteresis: a joint whose visibility hovers about the threshold
    doesn't flicker between the two (games treat inferred joints differently, some ignore them)."""

    TRACK_ABOVE, INFER_BELOW = 0.6, 0.4

    def __init__(self):
        self._tracked = None

    def __call__(self, seen):
        """seen: joint_visibility(). Returns the states (20,)."""
        if self._tracked is None:
            tracked = seen >= 0.5
        else:
            tracked = np.where(self._tracked, seen >= self.INFER_BELOW, seen >= self.TRACK_ABOVE)
        self._tracked = tracked
        return np.where(tracked, JOINT_TRACKED, JOINT_INFERRED).astype(np.uint8)


def solve_translation(world, normalized, visibility, width, height, focal):
    """Where the world pose (33, 3; webcam axes, metres around the hips) is in the webcam's space.

    Each landmark i with pixel (u, v) gives two equations linear in the translation T:
      f Tx - (u - cx) Tz = (u - cx) z_i - f x_i      f Ty - (v - cy) Tz = (v - cy) z_i - f y_i
    solved by least squares weighted by visibility.
    """
    world = np.asarray(world, dtype=np.float64)
    normalized = np.asarray(normalized, dtype=np.float64)
    weights = np.clip(np.asarray(visibility, dtype=np.float64), 0.0, 1.0)
    inside = (normalized[:, 0] > -0.1) & (normalized[:, 0] < 1.1) & (normalized[:, 1] > -0.1) & (normalized[:, 1] < 1.1)
    weights = np.where(inside & (weights > 0.3), weights, 0.0)
    if np.count_nonzero(weights) < 4:
        weights = np.where(inside, 1.0, 0.0)
        if np.count_nonzero(weights) < 4:
            return None
    du = normalized[:, 0] * width - width / 2
    dv = normalized[:, 1] * height - height / 2
    n = len(world)
    a = np.zeros((2 * n, 3))
    b = np.zeros(2 * n)
    a[:n, 0] = focal
    a[:n, 2] = -du
    b[:n] = du * world[:, 2] - focal * world[:, 0]
    a[n:, 1] = focal
    a[n:, 2] = -dv
    b[n:] = dv * world[:, 2] - focal * world[:, 1]
    w = np.sqrt(np.concatenate([weights, weights]))
    t, *_ = np.linalg.lstsq(a * w[:, None], b * w, rcond=None)
    if not np.all(np.isfinite(t)) or t[2] < 0.3:  # behind or touching the camera: not a person in front of it
        return None
    return t


def place(world, normalized, visibility, translation, distance, width, height, focal):
    """The 33 landmarks in the webcam's space (OpenCV axes), the hips `distance` metres away.

    Depth comes from the world pose; x and y from the image landmarks back-projected at that depth
    where they're seen (the model's image output is far more precise than its world output's x and
    y), else from the world pose. distance is the solved translation's, usually filtered: x and y
    of the translation are scaled with it, as they're proportional to it.
    """
    world = np.asarray(world, dtype=np.float64)
    normalized = np.asarray(normalized, dtype=np.float64)
    shift = np.array([translation[0], translation[1], translation[2]]) * (distance / translation[2])
    camera = world + shift
    z = np.maximum(camera[:, 2], 0.3)
    image = (normalized - 0.5) * (width, height)
    back = image * (z / focal)[:, None]
    inside = (normalized[:, 0] >= 0) & (normalized[:, 0] <= 1) & (normalized[:, 1] >= 0) & (normalized[:, 1] <= 1)
    weight = (np.clip((np.asarray(visibility, dtype=np.float64) - 0.5) / 0.3, 0.0, 1.0) * inside)[:, None]
    camera[:, :2] = weight * back + (1 - weight) * camera[:, :2]
    camera[:, 2] = z
    return camera


def natural_legs(joints, states, up):
    """The legs the webcam doesn't see, standing naturally under the hips.

    A player close to a laptop's camera has their legs out of the picture, and the pose model's
    guesses for them flail about (the game's floor, jumps and steps follow them). A knee, ankle or
    foot that isn't tracked, and the joints below it, are put straight down along `up` from the
    joint above, at lengths in proportion to the player's own torso, the feet pointing at the
    camera. They stay inferred. Returns the joints (a copy) and how many were placed.
    """
    joints = np.array(joints, dtype=np.float64)
    up = np.asarray(up, dtype=np.float64) / max(np.linalg.norm(up), 1e-9)
    hips = (joints[HIP_LEFT] + joints[HIP_RIGHT]) / 2
    torso = float(np.clip(np.linalg.norm(joints[SHOULDER_CENTER] - hips), 0.35, 0.65))
    thigh = shin = 0.85 * torso  # an adult's proportions: 0.245 of their height each, the torso 0.288
    towards = np.array([0.0, 0.0, -1.0])  # the camera, square to up
    towards = towards - up * np.dot(up, towards)
    towards /= max(np.linalg.norm(towards), 1e-9)
    placed = 0
    for hip, knee, ankle, foot in ((HIP_LEFT, KNEE_LEFT, ANKLE_LEFT, FOOT_LEFT),
                                   (HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT, FOOT_RIGHT)):
        unseen = [j for j in (knee, ankle, foot) if states[j] != JOINT_TRACKED]
        if not unseen:
            continue
        first = (knee, ankle, foot).index(unseen[0])
        if first <= 0:
            joints[knee] = joints[hip] - up * thigh
        if first <= 1:
            joints[ankle] = joints[knee] - up * shin
        joints[foot] = joints[ankle] - up * 0.06 + towards * 0.10
        placed += 3 - first
    return joints, placed


def leg_length(joints):
    """Hip to ankle along the leg (the longer of the two)."""
    def one(hip, knee, ankle):
        return np.linalg.norm(joints[hip] - joints[knee]) + np.linalg.norm(joints[knee] - joints[ankle])
    return max(one(HIP_LEFT, KNEE_LEFT, ANKLE_LEFT), one(HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT))


class PlaySpace:
    """Moves the scene so a player stands centred, `distance` metres from the virtual sensor, on its floor.

    It's set from the player in view when asked (calibrate), or automatically from the first
    player: a first guess at once, then the median of a second of frames. Only frames where the
    whole body is in view, at a distance a player stands from the webcam, count: someone sitting at
    the desk doesn't set it. The floor is the measured one when the camera's height is known
    (level.py), else a leg's length below the hips.
    """

    DISTANCE = 2.3
    SETTLE_FRAMES = 30
    MIN_DISTANCE, MAX_DISTANCE = 1.2, 4.5  # metres from the webcam
    _BODY = (HEAD, SHOULDER_LEFT, SHOULDER_RIGHT, HIP_LEFT, HIP_RIGHT, KNEE_LEFT, KNEE_RIGHT, ANKLE_LEFT, ANKLE_RIGHT)

    def __init__(self, camera_height):
        self.camera_height = camera_height
        self.offset = None
        self._pending = True
        self._samples = []

    def request(self):
        self._pending = True
        self._samples = []

    def update(self, joints, states=None, floor_height=0.0):
        """joints: the main player's joints (level frame, before the offset) or None; states: their
        states (None: all tracked); floor_height: the camera's measured height above the floor (0:
        unknown). Returns the offset."""
        if self._pending and joints is not None and self._plausible(joints, states):
            self._samples.append(self._target(joints, floor_height))
            if self.offset is None:
                self.offset = self._samples[-1]
            if len(self._samples) >= self.SETTLE_FRAMES:
                self.offset = np.median(np.array(self._samples), axis=0)
                self._pending = False
                self._samples = []
        return self.current()

    def current(self):
        return self.offset if self.offset is not None else np.zeros(3)

    def measuring(self):
        """True while the offset is being measured (the first frames, or after request())."""
        return self._pending

    def _plausible(self, joints, states):
        if not np.all(np.isfinite(joints)):
            return False
        if states is not None and any(states[j] != JOINT_TRACKED for j in self._BODY):
            return False
        return self.MIN_DISTANCE < joints[HIP_CENTER][2] < self.MAX_DISTANCE

    def _target(self, joints, floor_height):
        hip = joints[HIP_CENTER]
        if floor_height > 0:  # the measured floor (camera at the origin, level frame) to the virtual one
            dy = floor_height - self.camera_height
        else:  # the hip centre sits a leg (plus ankle height and the raise above the hip joints) above it
            dy = -self.camera_height + leg_length(joints) + 0.08 + 0.05 - hip[1]
        return np.array([-hip[0], dy, self.DISTANCE - hip[2]])


# The landmarks that say whether the webcam sees the whole body: nose, shoulders to feet.
_OUTLINE = [0] + list(range(L_SHOULDER, LANDMARKS))
_EDGE = 0.01  # how far outside the image (a fraction of it) a landmark counts as cut off


def image_clipping(normalized):
    """Clipping flags for the edges of the webcam's own view the body crosses. The play space can
    put a player well inside the virtual view while the webcam doesn't see their feet; the game
    should still ask them to step back. normalized: (33, 2), the unmirrored image, where the
    player's right side is on the left (the Kinect's images are mirrored: CLIPPED_RIGHT)."""
    points = np.asarray(normalized, dtype=np.float64)[_OUTLINE]
    flags = 0
    if (points[:, 0] < -_EDGE).any():
        flags |= CLIPPED_RIGHT
    if (points[:, 0] > 1 + _EDGE).any():
        flags |= CLIPPED_LEFT
    if (points[:, 1] < -_EDGE).any():
        flags |= CLIPPED_TOP
    if (points[:, 1] > 1 + _EDGE).any():
        flags |= CLIPPED_BOTTOM
    return flags


def clipping(joints, focal=camera_model.DEPTH_FOCAL, size=camera_model.DEPTH_SIZE):
    """NUI_SKELETON_QUALITY_CLIPPED_* flags: the edges of the depth view the body crosses."""
    uv = camera_model.project(joints, focal, size)
    flags = 0
    if (uv[:, 0] >= size[0]).any():
        flags |= CLIPPED_RIGHT
    if (uv[:, 0] < 0).any():
        flags |= CLIPPED_LEFT
    if (uv[:, 1] < 0).any():
        flags |= CLIPPED_TOP
    if (uv[:, 1] >= size[1]).any():
        flags |= CLIPPED_BOTTOM
    return flags
