"""A window for trying the bridge without the game (--preview): what the virtual Kinect sends, the
color image with the skeletons and the depth image coloured by player."""

import cv2
import numpy as np

import camera_model
import kinect_mapping as km

_COLORS = [(80, 80, 80), (0, 200, 255), (255, 120, 0), (0, 220, 0), (200, 0, 200), (0, 0, 220), (220, 220, 0)]
WINDOW = "AnyX360 pose bridge"


def show(color, depth, bodies, status):
    """False once the window is closed (or Esc)."""
    canvas = color[..., :3].copy() if color is not None else np.zeros((480, 640, 3), np.uint8)
    for body in bodies:
        uv = camera_model.project(body["joints"], camera_model.COLOR_FOCAL, camera_model.COLOR_SIZE)
        for a, b in km.BONES:
            cv2.line(canvas, tuple(int(v) for v in uv[a]), tuple(int(v) for v in uv[b]), (0, 255, 0), 3)
        cv2.putText(canvas, str(body["id"]), tuple(int(v) for v in uv[km.HEAD]), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                    (0, 255, 255), 2)
    players = depth & 7
    millimetres = (depth >> 3).astype(np.float32)
    gray = np.clip(255 - millimetres / 4000.0 * 255, 0, 255).astype(np.uint8)
    tinted = np.stack([gray] * 3, axis=-1)
    for index in range(1, 7):
        tinted[players == index] = _COLORS[index]
    tinted = cv2.resize(tinted, (320, 240), interpolation=cv2.INTER_NEAREST)
    canvas[0:240, 640 - 320:640] = tinted
    cv2.putText(canvas, status, (10, 470), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    cv2.imshow(WINDOW, canvas)
    key = cv2.waitKey(1)
    return key != 27 and cv2.getWindowProperty(WINDOW, cv2.WND_PROP_VISIBLE) >= 1
