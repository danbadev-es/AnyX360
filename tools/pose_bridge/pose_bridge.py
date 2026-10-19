"""AnyX360 pose bridge: a webcam as the virtual Kinect's sensor.

The game starts it (runtime/src/nui/bridge_process.cpp) with --shm, --event and --parent, and reads
its frames from shared memory (protocol.py); its settings come from the game. Without --shm it runs
on its own, to try it out:

  pose_bridge --preview                                 the webcam, in a window
  pose_bridge --video clip.mp4 --fast --dump-jsonl out.jsonl
  pose_bridge --synthetic --preview                     a dancer, no camera
  pose_bridge --self-test                               checks the bundle (no camera)

The log goes to stderr (the game appends it to logs\\pose_bridge.log).
"""

import os
import sys

if not getattr(sys, "frozen", False):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # run with python -I
# Media Foundation opens many webcams in seconds instead of tens with its hardware transforms off.
os.environ.setdefault("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0")

import argparse  # noqa: E402
import ctypes  # noqa: E402
import json  # noqa: E402
import threading  # noqa: E402
import time  # noqa: E402
import traceback  # noqa: E402
from ctypes import wintypes  # noqa: E402

import cv2  # noqa: E402
import numpy as np  # noqa: E402

import camera_model  # noqa: E402
import capture  # noqa: E402
import depth_synth  # noqa: E402
import kinect_mapping as km  # noqa: E402
import protocol  # noqa: E402
import synthetic  # noqa: E402
from pipeline import Pipeline  # noqa: E402

VERSION = "1.1"
MAX_FRAME_FAILURES = 60  # consecutive frames that fail before the bridge gives up (the game restarts it)


def log(message):
    print(time.strftime("[%H:%M:%S] ") + message, file=sys.stderr, flush=True)


class Parent:
    """The game's process: the bridge ends with it."""

    _SYNCHRONIZE = 0x00100000

    def __init__(self, pid):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self._kernel32 = kernel32
        self._handle = kernel32.OpenProcess(self._SYNCHRONIZE, False, pid) if pid else None

    def gone(self):
        return bool(self._handle) and self._kernel32.WaitForSingleObject(self._handle, 0) == 0


class KeepAlive:
    """Beats the heartbeat from a thread while the bridge does something slow (importing MediaPipe,
    loading a model, opening or closing a camera), so the game doesn't take it for stuck."""

    def __init__(self, channel):
        self._channel = channel
        self._stop = threading.Event()
        self._thread = None

    def __enter__(self):
        if self._channel:
            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        if self._thread:
            self._thread.join()

    def _run(self):
        while not self._stop.wait(0.2):
            self._channel.beat()


def standalone_settings(args):
    return {
        "seq": 0, "camera": args.camera, "mirror": args.mirror, "players": args.players, "model": args.model,
        "fov": args.fov, "smoothing": args.smoothing, "camera_height": args.camera_height, "calibrate": 0,
        "quit": False,
    }


def synthetic_color(joints):
    """A plain room with the dancer's bones, 640x480 BGRA."""
    color = np.empty((480, 640, 4), np.uint8)
    color[:] = (60, 50, 40, 255)
    color[300:] = (40, 70, 90, 255)
    uv = camera_model.project(joints, camera_model.COLOR_FOCAL, camera_model.COLOR_SIZE)
    for a, b in km.BONES:
        cv2.line(color, tuple(int(v) for v in uv[a]), tuple(int(v) for v in uv[b]), (200, 200, 220, 255), 14)
    cv2.circle(color, tuple(int(v) for v in uv[km.HEAD]), 26, (200, 200, 220, 255), -1)
    return color


class Bridge:
    def __init__(self, args):
        self.args = args
        self.channel = None
        self.settings = None
        self.source = None
        self.estimator = None
        self.preview = None
        self.dump = None
        self.frame_number = 0
        self.inference_ms = 0.0
        self.calibrated = 0  # the game's calibrate counter last seen

    # ---- setup ----

    def connect(self):
        """The game's channel; False if this bridge can't speak its protocol."""
        if not self.args.shm:
            self.settings = standalone_settings(self.args)
            return True
        self.channel = protocol.Channel.open(self.args.shm, self.args.event)
        self.channel.hello()
        problem = self.channel.check_layout()
        if problem:
            self.channel.set_state(protocol.ERROR, problem)
            log(problem)
            return False
        self.channel.set_state(protocol.STARTING, "loading the pose model")
        self.channel.beat()
        self.settings = self.channel.control()
        self.calibrated = self.settings["calibrate"]
        return True

    def load_estimator(self):
        if self.args.synthetic:
            return
        import estimator  # MediaPipe takes a moment to import
        if self.estimator:
            self.estimator.close()
        path = estimator.model_path(self.settings["model"], self.args.models)
        self.estimator = estimator.Estimator(path, self.settings["players"])
        log(f"model {path.name}, up to {self.settings['players']} people")

    def open_source(self):
        if self.args.synthetic:
            return
        if self.source:
            self.source.stop()
        if self.args.video:
            self.source = capture.VideoSource(self.args.video, self.settings["mirror"], self.args.fast)
        else:
            self.source = capture.CameraSource(self.settings["camera"], self.settings["mirror"])
            log(f"opening camera {self.settings['camera']}{' (mirrored image)' if self.settings['mirror'] else ''}")

    # ---- the loop ----

    def apply_control(self, pipeline):
        """The game's settings; False when it asks the bridge to quit."""
        control = self.channel.control()
        if control["quit"]:
            log("the game asked the bridge to quit")
            return False
        if control["seq"] != self.settings["seq"]:
            keys = ("camera", "mirror", "players", "model", "fov", "smoothing", "camera_height")
            changed = {k for k in keys if control[k] != self.settings[k]}
            self.settings = control
            pipeline.configure(control)
            if changed:
                log(f"settings changed: {', '.join(sorted(changed))}")
            with KeepAlive(self.channel):
                if changed & {"players", "model"}:
                    self.load_estimator()
                if "camera" in changed and not self.args.video:
                    self.open_source()
                    pipeline.tracker.reset()
            if "mirror" in changed and "camera" not in changed and self.source:
                self.source.mirror = control["mirror"]  # takes effect on the next frame
        if control["calibrate"] != self.calibrated:
            self.calibrated = control["calibrate"]
            pipeline.calibrate()
            log("recentring the play space on the player")
        return True

    def publish(self, capture_qpc, bodies, color, depth, seconds):
        self.frame_number += 1
        if self.channel:
            h = self.channel.header
            self.channel.publish(self.frame_number, capture_qpc, bodies, color, depth)
            if self.source:
                h.camera_fps = self.source.fps
                h.camera_width, h.camera_height = self.source.size
            else:
                h.camera_fps, h.camera_width, h.camera_height = 30.0, 640, 480
            h.inference_ms = self.inference_ms
            self.channel.set_state(protocol.RUNNING)
        if self.dump:
            self.dump.write(json.dumps({
                "frame": self.frame_number, "seconds": round(seconds, 4),
                "bodies": [{"id": int(b["id"]), "joints": np.round(b["joints"], 4).tolist(),
                            "states": [int(s) for s in b["joint_state"]]} for b in bodies],
            }) + "\n")
        if self.args.preview:
            import preview
            status = f"{len(bodies)} people, inference {self.inference_ms:.0f} ms"
            if not preview.show(color, depth, bodies, status):
                return False
        return True

    def run(self):
        if not self.connect():
            return protocol.EXIT_INCOMPATIBLE
        parent = Parent(self.args.parent)
        pipeline = Pipeline(self.settings)
        if self.args.dump_jsonl:
            self.dump = open(self.args.dump_jsonl, "w", encoding="utf-8")
        with KeepAlive(self.channel):
            self.load_estimator()
            self.open_source()
        start = time.perf_counter()
        last = 0
        failures = 0
        try:
            while True:
                now = time.perf_counter() - start
                if self.channel:
                    self.channel.beat()
                    if not self.apply_control(pipeline):
                        break
                if parent.gone():
                    log("the game has exited")
                    break
                if self.args.seconds and now >= self.args.seconds:
                    break

                if self.args.synthetic:
                    due = start + (self.frame_number + 1) / 30.0
                    time.sleep(max(0.0, due - time.perf_counter()))
                    joints = synthetic.dance(now, camera_height=self.settings["camera_height"])
                    body = {"id": 1, "state": protocol.BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                            "joint_state": np.full(km.JOINTS, km.JOINT_TRACKED, np.uint8),
                            "quality": km.clipping(joints)}
                    depth = depth_synth.depth_image([joints], self.settings["camera_height"])
                    if not self.publish(protocol.qpc(), [body], synthetic_color(joints), depth, now):
                        break
                    continue

                if self.source.state != protocol.RUNNING:
                    if self.channel:
                        self.channel.set_state(self.source.state, self.source.status)
                    if self.args.video and self.source.finished:
                        break
                    time.sleep(0.1)
                    continue
                frame = self.source.latest(last, 0.1)
                if frame is None:
                    if self.args.video and self.source.finished:
                        break
                    continue
                last = frame.number
                try:
                    began = time.perf_counter()
                    poses = self.estimator.detect(frame.image, frame.seconds)
                    elapsed = (time.perf_counter() - began) * 1000
                    self.inference_ms = elapsed if not self.inference_ms else 0.9 * self.inference_ms + 0.1 * elapsed
                    size = (frame.image.shape[1], frame.image.shape[0])
                    bodies, info = pipeline.process(poses, size, frame.seconds)
                    color, depth = pipeline.render(frame.image, info)
                    failures = 0
                except Exception:
                    # One bad frame is skipped; only a run of them ends the bridge.
                    failures += 1
                    if failures == 1 or failures % 20 == 0:
                        log(f"frame {frame.number} failed ({failures} in a row):\n{traceback.format_exc()}")
                    if failures >= MAX_FRAME_FAILURES:
                        raise
                    continue
                if not self.publish(frame.qpc, bodies, color, depth, frame.seconds):
                    break
        finally:
            if self.source:
                self.source.stop()
            if self.estimator:
                self.estimator.close()
            if self.dump:
                self.dump.close()
            if self.channel:
                self.channel.set_state(protocol.STOPPED)
        log(f"stopped after {self.frame_number} frames")
        return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="pose_bridge", description="A webcam as AnyX360's virtual Kinect.")
    parser.add_argument("--shm", help="the game's shared memory (set by the game)")
    parser.add_argument("--event", help="the game's frame event (set by the game)")
    parser.add_argument("--parent", type=int, default=0, help="exit with this process (set by the game)")
    parser.add_argument("--camera", type=int, default=0, help="webcam index")
    parser.add_argument("--mirror", action="store_true", help="the webcam already mirrors its image")
    parser.add_argument("--players", type=int, default=2, choices=(1, 2))
    parser.add_argument("--model", default="full", choices=("lite", "full", "heavy"))
    parser.add_argument("--fov", type=float, default=60.0, help="the webcam's horizontal field of view (degrees)")
    parser.add_argument("--smoothing", type=float, default=0.5, help="0 raw - 1 heavy")
    parser.add_argument("--camera-height", type=float, default=0.8, help="the virtual sensor's height (m)")
    parser.add_argument("--models", help="folder with the pose_landmarker_*.task models")
    parser.add_argument("--video", help="read this video file instead of the webcam")
    parser.add_argument("--fast", action="store_true", help="with --video: every frame, as fast as possible")
    parser.add_argument("--synthetic", action="store_true", help="a synthetic dancer, no camera")
    parser.add_argument("--dump-jsonl", help="write each frame's bodies to this file")
    parser.add_argument("--preview", action="store_true", help="show what the virtual Kinect sends")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this long")
    parser.add_argument("--self-test", action="store_true", help="check the bundle without a camera and exit")
    args = parser.parse_args(argv)
    log(f"AnyX360 pose bridge {VERSION}, protocol {protocol.VERSION_MAJOR}.{protocol.VERSION_MINOR}")
    if args.self_test:
        import selftest
        return selftest.run(args.models)
    bridge = Bridge(args)
    try:
        return bridge.run()
    except Exception as error:  # the game shows the message; the log has the details
        log(traceback.format_exc())
        if bridge.channel:
            bridge.channel.set_state(protocol.ERROR, f"pose bridge error: {error}"[:127])
        return 1


if __name__ == "__main__":
    sys.exit(main())
