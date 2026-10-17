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


def kinect_joints(points, visibility, threshold=0.5):
    """points: (33, 3) landmark positions in Kinect axes; visibility: (33,).

    Returns joints (20, 3) and their states (20,): tracked if every landmark they come from is
    visible enough, else inferred.
    """
    points = np.asarray(points, dtype=np.float64)
    visibility = np.asarray(visibility, dtype=np.float64)
    joints = np.empty((JOINTS, 3))
    seen = np.empty(JOINTS)
    for joint, sources in _DIRECT.items():
        joints[joint] = points[list(sources)].mean(axis=0)
        seen[joint] = visibility[list(sources)].min()
    hips = (points[L_HIP] + points[R_HIP]) / 2
    shoulders = joints[SHOULDER_CENTER]
    joints[HIP_CENTER] = hips + 0.10 * (shoulders - hips)
    joints[SPINE] = hips + 0.35 * (shoulders - hips)
    torso = min(visibility[L_HIP], visibility[R_HIP], visibility[L_SHOULDER], visibility[R_SHOULDER])
    seen[HIP_CENTER] = seen[SPINE] = torso
    states = np.where(seen >= threshold, JOINT_TRACKED, JOINT_INFERRED).astype(np.uint8)
    return joints, states


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
    if t[2] < 0.3:  # behind or touching the camera: not a person in front of it
        return None
    return t


def leg_length(joints):
    """Hip to ankle along the leg (the longer of the two)."""
    def one(hip, knee, ankle):
        return np.linalg.norm(joints[hip] - joints[knee]) + np.linalg.norm(joints[knee] - joints[ankle])
    return max(one(HIP_LEFT, KNEE_LEFT, ANKLE_LEFT), one(HIP_RIGHT, KNEE_RIGHT, ANKLE_RIGHT))


class PlaySpace:
    """Moves the scene so a player stands centred, `distance` metres from the virtual sensor, on its floor.

    It's set from the player in view when asked (calibrate), or automatically from the first
    player once they have been in view for a second.
    """

    DISTANCE = 2.3
    SETTLE_FRAMES = 30

    def __init__(self, camera_height):
        self.camera_height = camera_height
        self.offset = None
        self._pending = True
        self._seen = 0

    def request(self):
        self._pending = True
        self._seen = 0

    def update(self, joints):
        """joints: the main player's joints before the offset, or None. Returns the offset."""
        if self._pending and joints is not None:
            self._seen += 1
            if self.offset is None:  # a first guess at once, settled a second later
                self._set(joints, final=False)
            elif self._seen >= self.SETTLE_FRAMES:
                self._set(joints, final=True)
        elif joints is None:
            self._seen = 0
        return self.current()

    def current(self):
        return self.offset if self.offset is not None else np.zeros(3)

    def _set(self, joints, final):
        hip = joints[HIP_CENTER]
        # The hip centre sits a leg (plus ankle height and the raise above the hip joints) above the floor.
        above_floor = leg_length(joints) + 0.08 + 0.05
        target = np.array([0.0, -self.camera_height + above_floor, self.DISTANCE])
        self.offset = target - hip
        if final:
            self._pending = False
            self._seen = 0


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
