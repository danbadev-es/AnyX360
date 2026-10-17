"""The virtual Kinect's cameras (runtime/include/x360/nui.h) and the webcam's.

Kinect camera space: metres, x towards the sensor's left (the player's right), y up, z away from the
sensor. Both virtual cameras share their optical centre; a point projects to
(w/2 + x f / z, h/2 - y f / z), so the images are mirrored like the Kinect's (the player's right hand
on the image's right).

The webcam uses OpenCV's axes on its unmirrored image: x right, y down, z forward. A point there is
(-x, -y, z) in Kinect axes.
"""

import math

import numpy as np

COLOR_FOCAL = 531.15  # NUI_CAMERA_COLOR_NOMINAL_FOCAL_LENGTH_IN_PIXELS (640x480)
DEPTH_FOCAL = 285.63  # NUI_CAMERA_DEPTH_NOMINAL_FOCAL_LENGTH_IN_PIXELS (320x240)
COLOR_SIZE = (640, 480)
DEPTH_SIZE = (320, 240)
WALL_DISTANCE = 4.0  # metres: the room's back wall in the synthesized depth


def project(points, focal, size):
    """Kinect-space points (..., 3) to pixel coordinates (..., 2) of a virtual camera."""
    points = np.asarray(points, dtype=np.float64)
    z = np.maximum(points[..., 2], 1e-3)
    u = size[0] / 2 + points[..., 0] * focal / z
    v = size[1] / 2 - points[..., 1] * focal / z
    return np.stack([u, v], axis=-1)


def webcam_focal(width, fov_deg):
    """Focal length in pixels of a webcam image `width` wide with that horizontal field of view."""
    return (width / 2) / math.tan(math.radians(min(max(fov_deg, 20.0), 150.0)) / 2)


def opencv_to_kinect(points):
    return np.asarray(points, dtype=np.float64) * np.array([-1.0, -1.0, 1.0])
