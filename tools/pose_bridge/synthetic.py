"""Synthetic people, for --synthetic (the game sees a dancer without a camera) and the tests.

standing() and dance() give Kinect joints directly; landmarks() turns joints into what MediaPipe
would report for them, to test the mapping without a camera.
"""

import math

import numpy as np

import camera_model
import kinect_mapping as km

# An adult about 1.75 m tall, arms down, facing the sensor: offsets from the hip centre, Kinect axes
# (the same pose as the runtime's StandingPose).
_STANDING = {
    km.HIP_CENTER: (0, 0, 0), km.SPINE: (0, 0.22, 0.02), km.SHOULDER_CENTER: (0, 0.50, 0), km.HEAD: (0, 0.68, 0),
    km.SHOULDER_LEFT: (-0.18, 0.46, 0), km.ELBOW_LEFT: (-0.22, 0.20, 0), km.WRIST_LEFT: (-0.24, -0.02, 0),
    km.HAND_LEFT: (-0.25, -0.10, -0.02),
    km.SHOULDER_RIGHT: (0.18, 0.46, 0), km.ELBOW_RIGHT: (0.22, 0.20, 0), km.WRIST_RIGHT: (0.24, -0.02, 0),
    km.HAND_RIGHT: (0.25, -0.10, -0.02),
    km.HIP_LEFT: (-0.10, -0.04, 0), km.KNEE_LEFT: (-0.11, -0.50, -0.02), km.ANKLE_LEFT: (-0.12, -0.90, 0),
    km.FOOT_LEFT: (-0.12, -0.95, -0.10),
    km.HIP_RIGHT: (0.10, -0.04, 0), km.KNEE_RIGHT: (0.11, -0.50, -0.02), km.ANKLE_RIGHT: (0.12, -0.90, 0),
    km.FOOT_RIGHT: (0.12, -0.95, -0.10),
}


def standing(x=0.0, z=2.3, camera_height=0.8):
    joints = np.zeros((km.JOINTS, 3))
    hip = np.array([x, -camera_height + 0.95, z])
    for joint, offset in _STANDING.items():
        joints[joint] = hip + offset
    return joints


def dance(t, x=0.0, z=2.3, camera_height=0.8):
    """Arms swinging up and down out of phase, hips swaying, knees bending (120 BPM)."""
    joints = standing(x, z, camera_height)
    beat = t * 2 * math.pi * 2.0
    up = 0.5 + 0.5 * math.sin(beat)
    joints[:, 0] += 0.08 * math.sin(beat / 2)
    for joint, k in ((km.ELBOW_RIGHT, 0.3), (km.WRIST_RIGHT, 0.6), (km.HAND_RIGHT, 0.6)):
        joints[joint, 1] += up * k
    for joint, k in ((km.ELBOW_LEFT, 0.3), (km.WRIST_LEFT, 0.6), (km.HAND_LEFT, 0.6)):
        joints[joint, 1] += (1 - up) * k
    bend = 0.06 * (0.5 + 0.5 * math.sin(beat * 2))
    feet = {km.ANKLE_LEFT, km.ANKLE_RIGHT, km.FOOT_LEFT, km.FOOT_RIGHT}
    for joint in range(km.JOINTS):
        if joint not in feet:
            joints[joint, 1] -= bend
    return joints


# Each landmark from the Kinect joints (with a small offset for the ones a joint stands for).
_LANDMARKS = {
    0: (km.HEAD, (0, 0, -0.10)), 7: (km.HEAD, (-0.08, 0, 0)), 8: (km.HEAD, (0.08, 0, 0)),
    11: (km.SHOULDER_LEFT, (0, 0, 0)), 12: (km.SHOULDER_RIGHT, (0, 0, 0)),
    13: (km.ELBOW_LEFT, (0, 0, 0)), 14: (km.ELBOW_RIGHT, (0, 0, 0)),
    15: (km.WRIST_LEFT, (0, 0, 0)), 16: (km.WRIST_RIGHT, (0, 0, 0)),
    17: (km.HAND_LEFT, (0, -0.02, 0)), 18: (km.HAND_RIGHT, (0, -0.02, 0)),
    19: (km.HAND_LEFT, (0, 0.02, 0)), 20: (km.HAND_RIGHT, (0, 0.02, 0)),
    21: (km.HAND_LEFT, (0, 0, 0)), 22: (km.HAND_RIGHT, (0, 0, 0)),
    23: (km.HIP_LEFT, (0, 0, 0)), 24: (km.HIP_RIGHT, (0, 0, 0)),
    25: (km.KNEE_LEFT, (0, 0, 0)), 26: (km.KNEE_RIGHT, (0, 0, 0)),
    27: (km.ANKLE_LEFT, (0, 0, 0)), 28: (km.ANKLE_RIGHT, (0, 0, 0)),
    29: (km.FOOT_LEFT, (0, 0, 0.05)), 30: (km.FOOT_RIGHT, (0, 0, 0.05)),
    31: (km.FOOT_LEFT, (0, 0, -0.05)), 32: (km.FOOT_RIGHT, (0, 0, -0.05)),
}


def landmarks(joints, width, height, focal):
    """What MediaPipe reports for a person whose joints (Kinect axes) are in front of an unmirrored
    webcam: (normalized (33, 2), world (33, 3), visibility (33,))."""
    points = np.zeros((km.LANDMARKS, 3))
    for i in range(km.LANDMARKS):
        joint, offset = _LANDMARKS.get(i, (km.HEAD, (0, 0, -0.08)))  # face landmarks near the head
        points[i] = joints[joint] + offset
    camera = camera_model.opencv_to_kinect(points)  # the same flip both ways
    hips = (camera[23] + camera[24]) / 2
    world = camera - hips
    normalized = np.stack([(width / 2 + focal * camera[:, 0] / camera[:, 2]) / width,
                           (height / 2 + focal * camera[:, 1] / camera[:, 2]) / height], axis=1)
    return normalized, world, np.ones(km.LANDMARKS)
