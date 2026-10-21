"""One webcam frame's poses to the virtual Kinect's bodies and images."""

import math
from dataclasses import dataclass

import numpy as np

import camera_model
import depth_synth
import kinect_mapping as km
from filters import OneEuro
from level import Leveler
from protocol import BODY_TRACKED
from feet import FootLock
from tracker import Tracker


@dataclass
class Pose:
    """One person as the estimator reports them."""
    normalized: np.ndarray  # (33, 2) image coordinates 0-1, unmirrored image
    world: np.ndarray       # (33, 3) metres around the hips, the image's axes
    visibility: np.ndarray  # (33,)
    mask: np.ndarray = None  # segmentation (h, w) float 0-1, the inference image's size


# The joints' filters (at the default smoothing), in the webcam's frame: minimum cutoffs (Hz) across
# the image and in depth, the depth (the model's weakest output) smoothed far more. Tuned with
# benchmark.py (lag, jitter, steps, a reach towards the camera, dancing); the hips' distance is
# slower than the benchmark alone would pick (0.1 Hz), so walking towards the webcam still shows.
_TORSO = (km.HIP_CENTER, km.SPINE, km.SHOULDER_CENTER, km.HEAD, km.SHOULDER_LEFT, km.SHOULDER_RIGHT,
          km.HIP_LEFT, km.HIP_RIGHT)
_MIDDLE = (km.ELBOW_LEFT, km.ELBOW_RIGHT, km.KNEE_LEFT, km.KNEE_RIGHT)
_HANDS = (km.WRIST_LEFT, km.WRIST_RIGHT, km.HAND_LEFT, km.HAND_RIGHT)
_FEET = (km.ANKLE_LEFT, km.ANKLE_RIGHT, km.FOOT_LEFT, km.FOOT_RIGHT)
TUNING = {
    "cutoff_xy": {_TORSO: 4.0, _MIDDLE: 4.0, _HANDS: 5.0, _FEET: 4.8},
    "cutoff_z": {_TORSO: 0.6, _MIDDLE: 1.6, _HANDS: 0.5, _FEET: 0.6},
    "beta": 4.0,
    "derivative_cutoff": 3.0,
    "depth_speed": 0.375,  # how much speed in depth (noisy) counts in opening a joint's filter
    "lead": 1 / 120,  # seconds of prediction for a joint moving fast
    "distance_cutoff": 0.2,  # the hips' distance from the webcam
    "distance_beta": 0.6,
    "distance_derivative_cutoff": 0.665,
    "foot_lock": True,  # planted feet held still (feet.FootLock)
}


def _per_joint(groups):
    values = np.empty(km.JOINTS)
    for joints, value in groups.items():
        values[list(joints)] = value
    return values


class _Person:
    """One person's filters, in the webcam's frame (before levelling and the play space, which only
    move the result): the hips' distance from the webcam on its own and strongly (the model's
    scale wobbles from frame to frame, which moves the whole body back and forth), then each joint,
    then the feet. No filters at smoothing 0."""

    def __init__(self, smoothing, tuning):
        self.states = km.JointStates()
        self.joints = self.distance = self.feet = None
        if smoothing <= 0.01:
            return
        scale = 0.08 ** (smoothing - 0.5)  # 0.5: the tuned values; 1 smoother; towards 0 nearly raw
        xy, z = _per_joint(tuning["cutoff_xy"]), _per_joint(tuning["cutoff_z"])
        self.joints = OneEuro(np.stack([xy, xy, z], axis=1) * scale, tuning["beta"], tuning["derivative_cutoff"],
                              axis_weights=(1.0, 1.0, tuning["depth_speed"]), lead=tuning["lead"])
        self.distance = OneEuro(tuning["distance_cutoff"] * scale, tuning["distance_beta"],
                                tuning["distance_derivative_cutoff"])
        self.feet = FootLock() if tuning["foot_lock"] else None

    def filter_distance(self, distance, time):
        return float(self.distance(np.array([distance]), time)[0]) if self.distance else distance

    def filter_joints(self, joints, time):
        if self.joints:
            joints = self.joints(joints, time)
        if self.feet:
            joints = self.feet(joints, self.joints.speed[:, 0])
        return joints


class Pipeline:
    # A person the estimator misses for a moment (a fast move, an arm across the body) keeps their
    # last pose, marked inferred, for this long instead of vanishing from the game.
    HOLD_SECONDS = 0.3

    def __init__(self, settings, tuning=None):
        self.tuning = tuning or TUNING
        self.tracker = Tracker()
        self.play = km.PlaySpace(settings["camera_height"])
        self.level = Leveler()
        self._level_up = None  # the up direction the play space was set with
        self._rotation = None  # the level frame of the last frame
        self._offset = None  # the play space's offset of the last published poses
        self._people = {}  # body ID -> _Person
        self._last = {}  # body ID -> (joints, quality, time) of their last published pose
        self._reference = None  # the main player's distance from the webcam (smoothed)
        # What the color picture is drawn with: (distance, level rotation, play space offset), set
        # when the play space is measured. Following every frame's distance and tilt instead made
        # the picture zoom in and out and turn as the player moved and the estimates wobbled.
        self._picture = None
        self._was_measuring = True
        self.configure(settings)

    def configure(self, settings):
        # The field of view scales every distance, and the virtual camera's height moves the floor:
        # both measure the room again.
        if (getattr(self, "fov", None) not in (None, settings["fov"])
                or getattr(self, "camera_height", None) not in (None, settings["camera_height"])):
            self.level.reset()
            self.play.request()
        self.fov = settings["fov"]
        self.camera_height = settings["camera_height"]
        self.play.camera_height = self.camera_height
        if settings.get("tilt_manual"):
            self.level.set_manual(settings.get("tilt_pitch", 0.0), settings.get("tilt_roll", 0.0))
        else:
            # The pitch is measured; the roll stays the one set by hand.
            self.level.set_manual(None, settings.get("tilt_roll", 0.0))
        if getattr(self, "smoothing", None) != settings["smoothing"]:
            self.smoothing = settings["smoothing"]
            self._people = {}

    def calibrate(self):
        self.play.request()

    def _person(self, body_id):
        person = self._people.get(body_id)
        if person is None:
            person = self._people[body_id] = _Person(self.smoothing, self.tuning)
        return person

    def process(self, poses, frame_size, time):
        """Returns the bodies (dicts for protocol.Channel.publish) and what render() needs."""
        width, height = frame_size
        focal = camera_model.webcam_focal(width, self.fov)
        solved = []
        for pose in poses:
            translation = km.solve_translation(pose.world, pose.normalized, pose.visibility, width, height, focal)
            if translation is not None:
                solved.append((pose, translation))
        ids = self.tracker.update([camera_model.opencv_to_kinect(t) for _, t in solved], time)
        found, found_ids = [], []  # found: (joints, states, mask, distance, the webcam's clipping flags)
        for (pose, translation), body_id in zip(solved, ids):
            person = self._person(body_id)
            distance = person.filter_distance(float(translation[2]), time)
            camera = km.place(pose.world, pose.normalized, pose.visibility, translation, distance, width, height, focal)
            joints, _ = km.kinect_joints(camera_model.opencv_to_kinect(camera), pose.visibility)
            if not np.all(np.isfinite(joints)):
                continue
            joints = person.filter_joints(joints, time)
            states = person.states(km.joint_visibility(pose.visibility))
            found.append((joints, states, pose.mask, distance, km.image_clipping(pose.normalized)))
            found_ids.append(body_id)
        ids = found_ids
        order = self.tracker.order(ids)
        main = found[order[0]] if found else None
        if main:
            self._reference = main[3] if self._reference is None else 0.8 * self._reference + 0.2 * main[3]
        # The webcam's tilt, from the main player in the camera's space; then every body level.
        self.level.update(main[0] if main else None, main[1] if main else None)
        if self._level_up is None or np.dot(self.level.up, self._level_up) < math.cos(math.radians(2.0)):
            # A new estimate of the tilt (the first one, or the floor's replacing the body's): the
            # play space measured in the old frame is off; measure it again.
            if self._level_up is not None:
                self.play.request()
            self._level_up = self.level.up.copy()
        rotation = self.level.rotation
        if self._rotation is not None and main and self.play.offset is not None and not np.allclose(rotation, self._rotation):
            # The level frame turned (about the webcam): the main player would swing with it, by
            # 4 cm per degree at 2.3 m. Keep them where they are until the play space is measured again.
            hip = main[0][km.HIP_CENTER]
            self.play.offset = self.play.offset + self._rotation @ hip - rotation @ hip
        self._rotation = rotation
        found = [(f[0] @ rotation.T,) + f[1:] for f in found]
        main = found[order[0]] if found else None
        offset = self.play.update(main[0] if main else None, main[1] if main else None, self.level.floor_height)
        recentred = self._offset is not None and np.linalg.norm(offset - self._offset) > 0.05
        if recentred:
            self._last = {}  # recentred: a held pose would be in the old place
        self._offset = offset.copy()
        # The picture's frame: the first player seen, then each time the play space is measured (or
        # recentred). Between those it holds still, like a real camera's picture.
        measuring = self.play.measuring()
        if main and self._reference is not None and (
                self._picture is None or recentred or (self._was_measuring and not measuring)):
            self._picture = (self._reference, rotation.copy(), offset.copy())
        self._was_measuring = measuring

        bodies, joints_list, masks, distances = [], [], [], []
        for i in order:
            joints, states, mask, distance, seen_clipping = found[i]
            joints = joints + offset
            body_id = ids[i]
            quality = km.clipping(joints) | seen_clipping
            bodies.append({
                "id": body_id, "state": BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                "joint_state": states, "quality": quality,
            })
            joints_list.append(joints)
            masks.append(mask)
            distances.append(distance)
            self._last[body_id] = (joints, quality, time)
        for body_id, (joints, quality, seen) in sorted(self._last.items()):
            if body_id in ids or time - seen > self.HOLD_SECONDS:
                continue
            bodies.append({
                "id": body_id, "state": BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                "joint_state": np.full(km.JOINTS, km.JOINT_INFERRED, np.uint8), "quality": quality,
            })
            joints_list.append(joints)
            masks.append(None)
            distances.append(None)
        # Filters and held poses last as long as the tracker remembers the person, so a short miss
        # doesn't restart the filter from a raw value (a jump in the skeleton).
        known = self.tracker.known()
        for table in (self._people, self._last):
            for body_id in list(table):
                if body_id not in known:
                    del table[body_id]
        return bodies, {"joints": joints_list, "masks": masks, "distances": distances, "offset": offset,
                        "frame_size": frame_size, "focal": focal, "rotation": rotation}

    def render(self, frame_bgr, info):
        """The color (640x480 BGRA) and depth (320x240) images for this frame."""
        offset, (width, height), focal = info["offset"], info["frame_size"], info["focal"]
        rotation = info.get("rotation", np.eye(3))
        reference = self._reference or km.PlaySpace.DISTANCE
        color = None
        if frame_bgr is not None:
            # The picture with the frame it was last set with (see _picture), else this frame's.
            picture_reference, picture_rotation, picture_offset = self._picture or (reference, rotation, offset)
            transform = depth_synth.image_homography((width, height), focal, picture_reference, picture_offset,
                                                     picture_rotation, camera_model.COLOR_FOCAL,
                                                     camera_model.COLOR_SIZE)
            color = depth_synth.color_image(frame_bgr, transform)
        transforms = []
        distances = info.get("distances") or [None] * len(info["masks"])
        for mask, distance in zip(info["masks"], distances):
            if mask is None:
                transforms.append(None)
                continue
            # Each silhouette at its own body's distance: a second player farther back is warped
            # with the play space's shift as it applies at their depth.
            mh, mw = mask.shape[:2]
            transforms.append(depth_synth.image_homography((mw, mh), focal * mw / width, distance or reference, offset,
                                                           rotation, camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE))
        depth = depth_synth.depth_image(info["joints"], self.camera_height, info["masks"], transforms)
        return color, depth
