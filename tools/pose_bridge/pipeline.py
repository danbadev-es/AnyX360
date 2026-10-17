"""One webcam frame's poses to the virtual Kinect's bodies and images."""

from dataclasses import dataclass

import numpy as np

import camera_model
import depth_synth
import kinect_mapping as km
from filters import OneEuro
from protocol import BODY_TRACKED
from tracker import Tracker


@dataclass
class Pose:
    """One person as the estimator reports them."""
    normalized: np.ndarray  # (33, 2) image coordinates 0-1, unmirrored image
    world: np.ndarray       # (33, 3) metres around the hips, the image's axes
    visibility: np.ndarray  # (33,)
    mask: np.ndarray = None  # segmentation (h, w) float 0-1, the inference image's size


class Pipeline:
    def __init__(self, settings):
        self.tracker = Tracker()
        self.play = km.PlaySpace(settings["camera_height"])
        self._filters = {}
        self._reference = None  # the main player's distance from the webcam (smoothed)
        self.configure(settings)

    def configure(self, settings):
        self.fov = settings["fov"]
        self.camera_height = settings["camera_height"]
        self.play.camera_height = self.camera_height
        if getattr(self, "smoothing", None) != settings["smoothing"]:
            self.smoothing = settings["smoothing"]
            self._filters = {}

    def calibrate(self):
        self.play.request()

    def process(self, poses, frame_size, time):
        """Returns the bodies (dicts for protocol.Channel.publish) and what render() needs."""
        width, height = frame_size
        focal = camera_model.webcam_focal(width, self.fov)
        found = []
        for pose in poses:
            translation = km.solve_translation(pose.world, pose.normalized, pose.visibility, width, height, focal)
            if translation is None:
                continue
            camera = pose.world + translation
            joints, states = km.kinect_joints(camera_model.opencv_to_kinect(camera), pose.visibility)
            found.append((joints, states, pose.mask, float(translation[2])))
        ids = self.tracker.update([f[0][km.HIP_CENTER] for f in found], time)
        order = self.tracker.order(ids)
        main = found[order[0]] if found else None
        if main:
            self._reference = main[3] if self._reference is None else 0.8 * self._reference + 0.2 * main[3]
        offset = self.play.update(main[0] if main else None)

        bodies, joints_list, masks = [], [], []
        for i in order:
            joints, states, mask, _ = found[i]
            joints = joints + offset
            body_id = ids[i]
            if self.smoothing > 0.01:
                filter_ = self._filters.get(body_id)
                if filter_ is None:
                    filter_ = self._filters[body_id] = OneEuro.for_smoothing(self.smoothing)
                joints = filter_(joints, time)
            bodies.append({
                "id": body_id, "state": BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                "joint_state": states, "quality": km.clipping(joints),
            })
            joints_list.append(joints)
            masks.append(mask)
        for body_id in list(self._filters):
            if body_id not in ids:
                del self._filters[body_id]
        return bodies, {"joints": joints_list, "masks": masks, "offset": offset, "frame_size": frame_size,
                        "focal": focal}

    def render(self, frame_bgr, info):
        """The color (640x480 BGRA) and depth (320x240) images for this frame."""
        offset, (width, height), focal = info["offset"], info["frame_size"], info["focal"]
        reference = self._reference or km.PlaySpace.DISTANCE
        color = None
        if frame_bgr is not None:
            transform = depth_synth.image_transform((width, height), focal, reference, offset,
                                                    camera_model.COLOR_FOCAL, camera_model.COLOR_SIZE)
            color = depth_synth.color_image(frame_bgr, transform)
        transforms = []
        for mask in info["masks"]:
            if mask is None:
                transforms.append(None)
                continue
            mh, mw = mask.shape[:2]
            transforms.append(depth_synth.image_transform((mw, mh), focal * mw / width, reference, offset,
                                                          camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE))
        depth = depth_synth.depth_image(info["joints"], self.camera_height, info["masks"], transforms)
        return color, depth
