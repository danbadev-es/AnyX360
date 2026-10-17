"""The virtual Kinect's images from the webcam.

Color: the webcam image, mirrored and scaled so the main player appears where the virtual color
camera sees their skeleton. Depth: a room (the floor below the sensor, a wall at 4 m) with each
person drawn from their segmentation mask and capsules along their bones, as the Kinect packs it:
millimetres << 3 | (1 + the body's index), 0 for no body.
"""

import cv2
import numpy as np

import camera_model
import kinect_mapping as km

_BONE_RADIUS = 0.06  # metres
_TORSO_RADIUS = 0.15
_HEAD_RADIUS = 0.11


def image_transform(source_size, source_focal, reference_depth, offset, focal, size):
    """2x3 affine from the unmirrored webcam image to a virtual camera's mirrored image.

    source_size/source_focal: the webcam image's size and focal length in pixels; reference_depth:
    how far the main player is from the webcam (m); offset: the play space's offset (Kinect axes).
    Exact for points at the reference depth: a point at (u, v) there is at X = (u - cx) Z / f, and
    in Kinect space at (-X + ox, -Y + oy, Z + oz).
    """
    w, h = source_size
    kz = max(reference_depth + offset[2], 0.3)
    s = focal * reference_depth / (source_focal * kz)
    return np.array([
        [-s, 0.0, size[0] / 2 + s * w / 2 + focal * offset[0] / kz],
        [0.0, s, size[1] / 2 - s * h / 2 - focal * offset[1] / kz],
    ], dtype=np.float64)


def color_image(frame_bgr, transform):
    """640x480 BGRA."""
    color = cv2.warpAffine(frame_bgr, transform, camera_model.COLOR_SIZE, flags=cv2.INTER_LINEAR,
                           borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
    return cv2.cvtColor(color, cv2.COLOR_BGR2BGRA)


_background_cache = {}


def background_depth(camera_height):
    """The empty room in millimetres (240x320 float32): the floor camera_height below the sensor,
    the wall at 4 m."""
    key = round(camera_height, 3)
    if key not in _background_cache:
        width, height = camera_model.DEPTH_SIZE
        v = np.arange(height, dtype=np.float64) + 0.5
        slope = (v - height / 2) / camera_model.DEPTH_FOCAL  # floor rows: y / z = -slope
        with np.errstate(divide="ignore"):
            z = np.where(slope > 1e-6, camera_height / np.maximum(slope, 1e-6), camera_model.WALL_DISTANCE)
        z = np.minimum(z, camera_model.WALL_DISTANCE) * 1000.0
        _background_cache[key] = np.repeat(z[:, None], width, axis=1).astype(np.float32)
    return _background_cache[key].copy()


def _capsules(joints):
    """The body's capsules in the depth image as millimetres (0: none), nearer ones drawn last."""
    width, height = camera_model.DEPTH_SIZE
    focal = camera_model.DEPTH_FOCAL
    canvas = np.zeros((height, width), np.uint16)
    uv = camera_model.project(joints, focal, camera_model.DEPTH_SIZE)
    shapes = []
    for a, b in km.BONES:
        z = float(joints[a][2] + joints[b][2]) / 2
        shapes.append((z, "line", a, b, _BONE_RADIUS))
    shapes.append((float(joints[km.SPINE][2]) + 0.05, "line", km.HIP_CENTER, km.SHOULDER_CENTER, _TORSO_RADIUS))
    shapes.append((float(joints[km.HEAD][2]), "circle", km.HEAD, km.HEAD, _HEAD_RADIUS))
    for z, kind, a, b, radius in sorted(shapes, key=lambda s: -s[0]):
        if z < 0.3:
            continue
        r = max(1, int(round(radius * focal / z)))
        mm = int(min(z * 1000.0, 8000.0))
        pa = (int(round(uv[a][0])), int(round(uv[a][1])))
        if kind == "circle":
            cv2.circle(canvas, pa, r, mm, -1, cv2.LINE_8)
        else:
            pb = (int(round(uv[b][0])), int(round(uv[b][1])))
            cv2.line(canvas, pa, pb, mm, 2 * r, cv2.LINE_8)
    return canvas


def depth_image(bodies, camera_height, masks=None, mask_transforms=None):
    """bodies: joints (20, 3) per body, Kinect space; masks: per body a segmentation mask (float,
    webcam space) or None, mapped to the depth image by mask_transforms. Returns 240x320 uint16."""
    depth = background_depth(camera_height)
    index = np.zeros(depth.shape, np.uint16)
    order = sorted(range(len(bodies)), key=lambda i: -float(bodies[i][km.SPINE][2]))
    for i in order:  # farthest first
        joints = np.asarray(bodies[i], dtype=np.float64)
        layer = _capsules(joints).astype(np.float32)
        layer[layer == 0] = np.inf
        mask = masks[i] if masks else None
        if mask is not None and mask_transforms is not None and mask_transforms[i] is not None:
            silhouette = cv2.warpAffine(mask.astype(np.float32), mask_transforms[i], camera_model.DEPTH_SIZE,
                                        flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0) > 0.5
            spine_mm = float(joints[km.SPINE][2]) * 1000.0
            layer = np.where(silhouette & ~np.isfinite(layer), spine_mm, layer).astype(np.float32)
        closer = layer < depth
        depth[closer] = layer[closer]
        index[closer] = i + 1
    packed = (np.clip(depth, 0, 8191).astype(np.uint16) << 3) | index
    return packed
