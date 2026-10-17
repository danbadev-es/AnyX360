"""MediaPipe's Pose Landmarker (Tasks API, video mode): up to two people per frame, with their 3D
landmarks and segmentation masks."""

import sys
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks.python import BaseOptions
from mediapipe.tasks.python.vision import PoseLandmarker, PoseLandmarkerOptions, RunningMode

from pipeline import Pose

INFERENCE_WIDTH = 640  # frames are scaled down to this width for the model


def models_dir():
    """The bundled models (PyInstaller), else the developer build's build/pose_bridge/models."""
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "models"
    here = Path(__file__).resolve().parent
    for directory in [here, *here.parents]:
        candidate = directory / "build" / "pose_bridge" / "models"
        if candidate.is_dir():
            return candidate
    return here / "models"


def model_path(name, directory=None):
    """The model file for "lite", "full" or "heavy", falling back to the ones present."""
    directory = Path(directory) if directory else models_dir()
    for candidate in (name, "full", "lite"):
        path = directory / f"pose_landmarker_{candidate}.task"
        if path.is_file():
            return path
    raise FileNotFoundError(f"no pose model in {directory}")


class Estimator:
    def __init__(self, path, poses):
        self.path = Path(path)
        self.poses = poses
        options = PoseLandmarkerOptions(
            base_options=BaseOptions(model_asset_path=str(self.path)),
            running_mode=RunningMode.VIDEO,
            num_poses=poses,
            min_pose_detection_confidence=0.5,
            min_pose_presence_confidence=0.5,
            min_tracking_confidence=0.5,
            output_segmentation_masks=True,
        )
        self._landmarker = PoseLandmarker.create_from_options(options)
        self._last_ms = -1

    def close(self):
        self._landmarker.close()

    def detect(self, frame_bgr, seconds):
        """The people in an unmirrored BGR frame, as pipeline.Pose."""
        height, width = frame_bgr.shape[:2]
        if width > INFERENCE_WIDTH:
            scaled = cv2.resize(frame_bgr, (INFERENCE_WIDTH, round(height * INFERENCE_WIDTH / width)),
                                interpolation=cv2.INTER_AREA)
        else:
            scaled = frame_bgr
        rgb = np.ascontiguousarray(cv2.cvtColor(scaled, cv2.COLOR_BGR2RGB))
        timestamp = max(int(seconds * 1000), self._last_ms + 1)  # strictly increasing
        self._last_ms = timestamp
        result = self._landmarker.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp)
        poses = []
        masks = result.segmentation_masks or []
        for i, landmarks in enumerate(result.pose_landmarks):
            if i >= len(result.pose_world_landmarks):
                break
            world = result.pose_world_landmarks[i]
            normalized = np.array([[p.x, p.y] for p in landmarks], dtype=np.float64)
            visibility = np.array([p.visibility if p.visibility is not None else 1.0 for p in landmarks])
            world_points = np.array([[p.x, p.y, p.z] for p in world], dtype=np.float64)
            mask = masks[i].numpy_view().copy() if i < len(masks) else None
            if mask is not None and mask.ndim == 3:
                mask = mask[..., 0]
            poses.append(Pose(normalized, world_points, visibility, mask))
        return poses
