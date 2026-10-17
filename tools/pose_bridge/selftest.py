"""--self-test: checks that a bundle works on this machine without opening a camera (the shared
memory, every model on a blank frame, the mapping on a synthetic person, the video backends)."""

import os
import time

import cv2
import numpy as np

import camera_model
import kinect_mapping as km
import protocol
import synthetic
from pipeline import Pipeline, Pose


def _log(message):
    print(time.strftime("[%H:%M:%S] ") + message, flush=True)


def _protocol():
    name = f"Local\\AnyX360-Nui-selftest-{os.getpid()}"
    game = protocol.Channel.create(name, name + "-frame")
    bridge = protocol.Channel.open(name, name + "-frame")
    try:
        assert bridge.check_layout() is None, bridge.check_layout()
        joints = synthetic.standing()
        body = {"id": 5, "state": protocol.BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                "joint_state": np.full(km.JOINTS, 2, np.uint8), "quality": 0}
        color = np.full((480, 640, 4), 7, np.uint8)
        depth = np.full((240, 320), 9, np.uint16)
        bridge.publish(1, protocol.qpc(), [body], color, depth)
        frame, bodies, color_read, depth_read = game.read_latest()
        assert frame == 1 and len(bodies) == 1 and bodies[0]["id"] == 5
        assert np.allclose(bodies[0]["joints"][:, :3], joints, atol=1e-5)
        assert color_read[100, 100, 0] == 7 and depth_read[10, 10] == 9
    finally:
        bridge.close()
        game.close()
    return "shared memory round trip"


def _models(directory):
    import estimator
    folder = estimator.models_dir() if directory is None else directory
    found = sorted(p for p in os.listdir(folder) if p.startswith("pose_landmarker_") and p.endswith(".task"))
    assert found, f"no models in {folder}"
    timings = []
    blank = np.full((720, 1280, 3), 96, np.uint8)
    for name in found:
        model = estimator.Estimator(os.path.join(folder, name), 2)
        try:
            model.detect(blank, 0.0)
            began = time.perf_counter()
            for i in range(5):
                poses = model.detect(blank, (i + 1) / 30)
            timings.append(f"{name} {(time.perf_counter() - began) * 200:.0f} ms")
            assert poses == [], "a person in a blank image"
        finally:
            model.close()
    return ", ".join(timings)


def _mapping():
    width, height = 1280, 720
    settings = {"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8}
    pipeline = Pipeline(settings)
    focal = camera_model.webcam_focal(width, settings["fov"])
    joints = synthetic.standing(x=0.3, z=1.9)
    joints[km.WRIST_RIGHT] += (0.3, 0.5, 0)
    normalized, world, visibility = synthetic.landmarks(joints, width, height, focal)
    bodies, info = pipeline.process([Pose(normalized, world, visibility)], (width, height), 0.0)
    assert len(bodies) == 1
    out = bodies[0]["joints"]
    assert abs(out[km.HIP_CENTER][2] - km.PlaySpace.DISTANCE) < 0.02, out[km.HIP_CENTER]
    assert out[km.WRIST_RIGHT][0] > out[km.SHOULDER_RIGHT][0] > out[km.SHOULDER_LEFT][0]
    color, depth = pipeline.render(np.zeros((height, width, 3), np.uint8), info)
    assert color.shape == (480, 640, 4) and depth.shape == (240, 320)
    return "landmarks to joints"


def _video():
    backends = [cv2.videoio_registry.getBackendName(b) for b in cv2.videoio_registry.getCameraBackends()]
    assert "MSMF" in backends, backends
    return "camera backends " + ", ".join(backends)


def run(models=None):
    failures = 0
    for name, check in (("protocol", _protocol), ("models", lambda: _models(models)), ("mapping", _mapping),
                        ("video", _video)):
        try:
            _log(f"self-test {name}: ok ({check()})")
        except Exception as error:  # report every check
            failures += 1
            _log(f"self-test {name}: FAILED {error!r}")
    _log("self-test passed" if not failures else f"self-test: {failures} failed")
    return 1 if failures else 0
