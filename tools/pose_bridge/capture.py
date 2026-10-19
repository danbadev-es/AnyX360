"""Frames from the webcam (or a video file), read on their own thread; the bridge takes the newest.

Frames are unmirrored: a camera that mirrors its image (the "mirror" setting) is flipped back, so
the pose estimator sees people as they are. The mirror setting can change while the camera runs.

Frame numbers and times continue from one camera to the next (changing camera opens a new source):
the bridge waits for frames numbered above the last one it used, and the estimator needs times that
only grow.
"""

import itertools
import threading
import time
import winreg

import cv2

import protocol

CAMERA_WIDTH, CAMERA_HEIGHT, CAMERA_FPS = 1280, 720, 30
RETRY_SECONDS = 2.0

_EPOCH = time.perf_counter()     # the time base of every camera source
_NUMBERS = itertools.count(1)   # their frame numbers


class Frame:
    def __init__(self, image, number, qpc, seconds):
        self.image = image      # BGR, unmirrored
        self.number = number
        self.qpc = qpc          # when it was read
        self.seconds = seconds  # since the source started (the estimator's timestamps)


def camera_blocked():
    """Windows' privacy settings deny desktop apps the camera."""
    keys = [
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam"),
        (winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam"),
        (winreg.HKEY_CURRENT_USER,
         r"Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\webcam\NonPackaged"),
    ]
    for root, path in keys:
        try:
            with winreg.OpenKey(root, path) as key:
                value, _ = winreg.QueryValueEx(key, "Value")
                if str(value).lower() == "deny":
                    return True
        except OSError:
            pass
    return False


class _Source(threading.Thread):
    def __init__(self, mirror):
        super().__init__(daemon=True)
        self.mirror = mirror
        self.state = protocol.STARTING
        self.status = ""
        self.fps = 0.0
        self.size = (0, 0)
        self._frame = None
        self._condition = threading.Condition()
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()
        self.join(timeout=3)

    def latest(self, after, timeout):
        """The newest frame numbered above `after`, waiting up to `timeout` seconds; else None."""
        with self._condition:
            if self._frame is None or self._frame.number <= after:
                self._condition.wait(timeout)
            frame = self._frame
        return frame if frame is not None and frame.number > after else None

    def _deliver(self, image):
        if self.mirror:
            image = cv2.flip(image, 1)
        frame = Frame(image, next(_NUMBERS), protocol.qpc(), time.perf_counter() - _EPOCH)
        self.size = (image.shape[1], image.shape[0])
        with self._condition:
            self._frame = frame
            self._condition.notify_all()


class CameraSource(_Source):
    """A webcam through Media Foundation (DirectShow if that fails), MJPG 1280x720 at 30 fps when the
    camera offers it. Without a camera it keeps looking every 2 s."""

    def __init__(self, index, mirror):
        super().__init__(mirror)
        self.index = index
        self.start()

    def _open(self):
        for backend in (cv2.CAP_MSMF, cv2.CAP_DSHOW):
            capture = cv2.VideoCapture(self.index, backend)
            if not capture.isOpened():
                capture.release()
                continue
            capture.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter.fourcc(*"MJPG"))
            capture.set(cv2.CAP_PROP_FRAME_WIDTH, CAMERA_WIDTH)
            capture.set(cv2.CAP_PROP_FRAME_HEIGHT, CAMERA_HEIGHT)
            capture.set(cv2.CAP_PROP_FPS, CAMERA_FPS)
            ok, image = capture.read()
            if ok and image is not None:
                return capture, image
            capture.release()
        return None, None

    def run(self):
        while not self._stop.is_set():
            capture, image = self._open()
            if capture is None:
                if camera_blocked():
                    self.state, self.status = protocol.CAMERA_DENIED, "Windows blocks desktop apps from the camera"
                else:
                    self.state, self.status = protocol.NO_CAMERA, f"no webcam found (camera {self.index})"
                self._stop.wait(RETRY_SECONDS)
                continue
            self.state, self.status = protocol.RUNNING, ""
            failures = 0
            window_start, window_frames = time.perf_counter(), 0
            self._deliver(image)
            while not self._stop.is_set():
                ok, image = capture.read()
                if not ok or image is None:
                    failures += 1
                    if failures > 30:  # unplugged, or taken by another app
                        break
                    time.sleep(0.01)
                    continue
                failures = 0
                self._deliver(image)
                window_frames += 1
                elapsed = time.perf_counter() - window_start
                if elapsed >= 1.0:
                    self.fps = window_frames / elapsed
                    window_start, window_frames = time.perf_counter(), 0
            capture.release()
            if not self._stop.is_set():
                self.state, self.status = protocol.NO_CAMERA, "the webcam stopped sending images"


class VideoSource:
    """A video file, once through: each frame when its time comes, or each as soon as the bridge asks
    with `fast` (deterministic output, e.g. for --dump-jsonl). Timestamps follow the file."""

    def __init__(self, path, mirror=False, fast=False):
        self.mirror = mirror
        self.fast = fast
        self.finished = False
        self.status = ""
        self._capture = cv2.VideoCapture(path)
        self.fps = self._capture.get(cv2.CAP_PROP_FPS) or 30.0 if self._capture.isOpened() else 0.0
        self.state = protocol.RUNNING if self._capture.isOpened() else protocol.ERROR
        if self.state == protocol.ERROR:
            self.status = f"cannot open {path}"
            self.finished = True
        self.size = (0, 0)
        self._number = 0
        self._start = time.perf_counter()

    def latest(self, after, timeout):
        if self.finished:
            return None
        ok, image = self._capture.read()
        if not ok or image is None:
            self.finished = True
            return None
        if self.mirror:
            image = cv2.flip(image, 1)
        seconds = self._number / self.fps
        self._number += 1
        if not self.fast:
            delay = self._start + seconds - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
        self.size = (image.shape[1], image.shape[0])
        return Frame(image, self._number, protocol.qpc(), seconds)

    def stop(self):
        self._capture.release()
