"""Unit tests of the pose bridge, without a camera or the pose model:
  python -I tools/pose_bridge/run_tests.py     (the bridge's venv: numpy and OpenCV)
"""

import math
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.dirname(os.path.dirname(HERE))

import numpy as np  # noqa: E402

import camera_model  # noqa: E402
import capture  # noqa: E402
import depth_synth  # noqa: E402
import kinect_mapping as km  # noqa: E402
import level  # noqa: E402
import protocol  # noqa: E402
import synthetic  # noqa: E402
from filters import OneEuro  # noqa: E402
from pipeline import Pipeline, Pose  # noqa: E402
from tracker import Tracker  # noqa: E402


def read(path):
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return f.read()


def cpp_constants(text):
    """constexpr numbers (and a, b = ... pairs) of a C++ header."""
    values = {}
    for match in re.finditer(r"constexpr\s+[\w:]+\s+([^;]+);", text):
        for part in re.split(r",\s*(?=k\w+\s*=)", match.group(1)):
            name, _, value = part.partition("=")
            value = value.strip().rstrip("f")
            try:
                values[name.strip()] = float(int(value, 0)) if re.fullmatch(r"0x[0-9A-Fa-f]+|\d+", value) else float(value)
            except ValueError:
                pass
    return values


class ProtocolLayout(unittest.TestCase):
    """protocol.py against the static_asserts of runtime/include/x360/nui_bridge.h."""

    def test_offsets_match_the_header(self):
        text = read("runtime/include/x360/nui_bridge.h")
        constants = cpp_constants(text)
        structs = {"Header": protocol.Header, "Body": protocol.Body, "Slot": protocol.Slot}
        checked = 0
        for struct, field, expression in re.findall(r"static_assert\(offsetof\((\w+), (\w+)\) == ([^;]+)\);", text):
            expected = eval(expression.rstrip(")"), {}, constants)  # numbers and the header's constants
            self.assertEqual(getattr(structs[struct], field).offset, expected, f"{struct}.{field}")
            checked += 1
        for struct, expected in re.findall(r"static_assert\(sizeof\((\w+)\) == (\d+)\);", text):
            self.assertEqual(protocol.ctypes.sizeof(structs[struct]), int(expected), f"sizeof({struct})")
            checked += 1
        self.assertGreater(checked, 30)

    def test_constants_match_the_header(self):
        constants = cpp_constants(read("runtime/include/x360/nui_bridge.h"))
        self.assertEqual(constants["kMagic"], protocol.MAGIC)
        self.assertEqual(constants["kVersionMajor"], protocol.VERSION_MAJOR)
        self.assertEqual(constants["kVersionMinor"], protocol.VERSION_MINOR)
        self.assertEqual(constants["kSlotCount"], protocol.SLOT_COUNT)
        self.assertEqual(constants["kMaxBodies"], protocol.MAX_BODIES)
        self.assertEqual(constants["kSlotHasImages"], protocol.SLOT_HAS_IMAGES)

    def test_round_trip_and_version_check(self):
        name = f"Local\\AnyX360-Nui-pytest-{os.getpid()}"
        game = protocol.Channel.create(name, name + "-frame")
        bridge = protocol.Channel.open(name, name + "-frame")
        try:
            self.assertIsNone(bridge.check_layout())
            joints = synthetic.standing()
            body = {"id": 3, "state": protocol.BODY_TRACKED, "position": joints[km.SPINE], "joints": joints,
                    "joint_state": np.full(20, 2, np.uint8), "quality": km.CLIPPED_BOTTOM}
            self.assertIsNone(game.read_latest())  # nothing yet
            for frame in (1, 2, 3, 4):
                bridge.publish(frame, protocol.qpc(), [body], np.full((480, 640, 4), frame, np.uint8),
                               np.full((240, 320), frame, np.uint16))
                number, bodies, color, depth = game.read_latest()
                self.assertEqual(number, frame)
                self.assertEqual(game.slots[game.header.latest_slot].seq % 2, 0)
                self.assertEqual(color[479, 639, 3], frame)
                self.assertEqual(depth[0, 0], frame)
            self.assertEqual(bodies[0]["quality"], km.CLIPPED_BOTTOM)
            self.assertTrue(np.allclose(bodies[0]["joints"][:, 3], 1.0))
            bridge.publish(5, protocol.qpc(), [body])  # skeleton only
            self.assertIsNone(game.read_latest()[2])
            game.header.version_major = protocol.VERSION_MAJOR + 1
            self.assertIn(f"protocol {protocol.VERSION_MAJOR + 1}.{protocol.VERSION_MINOR}", bridge.check_layout())
        finally:
            bridge.close()
            game.close()

    def test_control_block(self):
        name = f"Local\\AnyX360-Nui-pytest-control-{os.getpid()}"
        game = protocol.Channel.create(name)
        try:
            h = game.header
            h.camera_index, h.mirror, h.players, h.model, h.fov_deg = 2, 1, 1, 0, 75.0
            h.smoothing, h.camera_height_m, h.control_seq = 0.2, 1.1, 4
            control = game.control()
            self.assertEqual((control["camera"], control["mirror"], control["players"], control["model"]),
                             (2, True, 1, "lite"))
            self.assertAlmostEqual(control["fov"], 75.0)
            self.assertAlmostEqual(control["camera_height"], 1.1, places=5)
            self.assertEqual(control["seq"], 4)
            del h  # views must go before the mapping closes
        finally:
            game.close()


class Axes(unittest.TestCase):
    def test_right_wrist_is_positive_x(self):
        """A person facing an unmirrored webcam: their right wrist is on the image's left (MediaPipe
        world x < 0), and must come out at Kinect x > 0, the player's right."""
        world = np.zeros((33, 3))
        world[km.R_WRIST] = (-0.8, -0.4, 0.0)  # image left, raised
        world[km.L_WRIST] = (0.8, 0.1, 0.0)
        world[km.R_SHOULDER] = (-0.18, -0.45, 0.0)
        world[km.L_SHOULDER] = (0.18, -0.45, 0.0)
        joints, _ = km.kinect_joints(camera_model.opencv_to_kinect(world + (0, 0, 2.0)), np.ones(33))
        self.assertGreater(joints[km.WRIST_RIGHT][0], 0.5)
        self.assertLess(joints[km.WRIST_LEFT][0], -0.5)
        self.assertGreater(joints[km.WRIST_RIGHT][1], joints[km.WRIST_LEFT][1])  # y up: the raised one is higher
        self.assertGreater(joints[km.SHOULDER_RIGHT][1], 0.0)

    def test_mirrored_camera_is_flipped_back(self):
        """With the mirror setting the capture flips the image, so the estimator sees it unmirrored."""
        image = np.zeros((4, 6, 3), np.uint8)
        image[:, 0] = 255  # the left column
        source = capture.VideoSource.__new__(capture.VideoSource)
        source.mirror, source.fast, source.finished, source._number = True, True, False, 0

        class Fake:
            def read(self):
                return True, image.copy()
        source._capture = Fake()
        source.fps, source._start = 30.0, 0.0
        frame = source.latest(0, 0)
        self.assertEqual(frame.image[0, 5, 0], 255)
        self.assertEqual(frame.image[0, 0, 0], 0)

    def test_projection_matches_the_runtime(self):
        constants = cpp_constants(read("runtime/include/x360/nui.h"))
        self.assertAlmostEqual(constants["kColorFocal"], camera_model.COLOR_FOCAL, places=4)
        self.assertAlmostEqual(constants["kDepthFocal"], camera_model.DEPTH_FOCAL, places=4)
        # nui.h: (x, y, z) projects to (w/2 + x f / z, h/2 - y f / z).
        u, v = camera_model.project([0.5, 0.25, 2.0], camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE)
        self.assertAlmostEqual(u, 160 + 0.5 * 285.63 / 2.0)
        self.assertAlmostEqual(v, 120 - 0.25 * 285.63 / 2.0)


class Mapping(unittest.TestCase):
    def test_translation_is_recovered(self):
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 70)
        joints = synthetic.dance(0.3, x=-0.4, z=2.1)
        normalized, world, visibility = synthetic.landmarks(joints, width, height, focal)
        t = km.solve_translation(world, normalized, visibility, width, height, focal)
        hips = camera_model.opencv_to_kinect((joints[km.HIP_LEFT] + joints[km.HIP_RIGHT]) / 2)
        self.assertTrue(np.allclose(t, hips, atol=0.01), (t, hips))

    def test_round_trip_through_landmarks(self):
        width, height = 640, 480
        focal = camera_model.webcam_focal(width, 60)
        joints = synthetic.standing(x=0.2, z=2.5)
        normalized, world, visibility = synthetic.landmarks(joints, width, height, focal)
        t = km.solve_translation(world, normalized, visibility, width, height, focal)
        out, states = km.kinect_joints(camera_model.opencv_to_kinect(world + t), visibility)
        for joint in (km.SHOULDER_LEFT, km.ELBOW_RIGHT, km.WRIST_RIGHT, km.HAND_LEFT, km.KNEE_RIGHT, km.FOOT_LEFT):
            self.assertLess(np.linalg.norm(out[joint] - joints[joint]), 0.03, joint)
        self.assertTrue((states == km.JOINT_TRACKED).all())
        visibility[km.L_KNEE] = 0.2
        _, states = km.kinect_joints(world, visibility)
        self.assertEqual(states[km.KNEE_LEFT], km.JOINT_INFERRED)

    def test_play_space_centres_the_player(self):
        play = km.PlaySpace(0.8)
        joints = synthetic.standing(x=0.5, z=1.6)
        offset = play.update(joints)
        placed = joints + offset
        self.assertAlmostEqual(placed[km.HIP_CENTER][0], 0.0, places=6)
        self.assertAlmostEqual(placed[km.HIP_CENTER][2], km.PlaySpace.DISTANCE, places=6)
        self.assertLess(abs(placed[km.FOOT_LEFT][1] - (-0.8)), 0.1)  # feet on the virtual floor
        # Settles after a second; later movement is kept.
        for _ in range(km.PlaySpace.SETTLE_FRAMES):
            play.update(joints)
        moved = joints + (0.3, 0, 0)
        self.assertAlmostEqual((moved + play.update(moved))[km.HIP_CENTER][0], 0.3, places=6)
        play.request()
        for _ in range(km.PlaySpace.SETTLE_FRAMES):
            offset = play.update(moved)
        self.assertAlmostEqual((moved + offset)[km.HIP_CENTER][0], 0.0, places=6)

    def test_clipping_flags(self):
        self.assertEqual(km.clipping(synthetic.standing(z=2.3)), 0)
        close = synthetic.standing(z=0.9)
        self.assertTrue(km.clipping(close) & km.CLIPPED_BOTTOM)
        self.assertTrue(km.clipping(synthetic.standing(x=1.5)) & km.CLIPPED_RIGHT)


class Filters(unittest.TestCase):
    def test_still_values_stay_and_steps_converge(self):
        f = OneEuro.for_smoothing(0.5)
        for i in range(30):
            out = f(np.array([1.0, 2.0]), i / 30)
        self.assertTrue(np.allclose(out, [1.0, 2.0]))
        for i in range(30, 60):
            out = f(np.array([2.0, 2.0]), i / 30)
        self.assertLess(abs(out[0] - 2.0), 0.05)

    def test_jitter_is_reduced(self):
        rng = np.random.default_rng(1)
        f = OneEuro.for_smoothing(0.5)
        noisy = 1.0 + rng.normal(0, 0.02, 300)
        out = np.array([f(np.array([x]), i / 30)[0] for i, x in enumerate(noisy)])
        self.assertLess(out[100:].std(), noisy[100:].std() * 0.5)

    def test_raw_and_lag(self):
        self.assertIsNone(OneEuro.for_smoothing(0.0))
        # A fast movement (1 m/s) lags less than a frame's worth of travel times a few.
        f = OneEuro.for_smoothing(0.5)
        for i in range(60):
            out = f(np.array([i / 30.0]), i / 30)
        self.assertLess(59 / 30 - out[0], 0.15)


class Tracking(unittest.TestCase):
    def test_ids_follow_people_not_order(self):
        t = Tracker()
        a, b = np.array([-0.5, 0, 2.3]), np.array([0.6, 0, 2.4])
        first = t.update([a, b], 0.0)
        swapped = t.update([b + 0.02, a - 0.02], 1 / 30)
        self.assertEqual(swapped, [first[1], first[0]])
        self.assertEqual(t.order([7, 3]), [1, 0])

    def test_short_dropout_keeps_the_id_long_one_does_not(self):
        t = Tracker()
        a = np.array([0, 0, 2.3])
        (id_a,) = t.update([a], 0.0)
        t.update([], 0.2)
        self.assertEqual(t.update([a], 0.3), [id_a])
        t.update([], 0.4)
        self.assertNotEqual(t.update([a], 1.5), [id_a])

    def test_a_jump_is_a_new_person(self):
        t = Tracker()
        (id_a,) = t.update([np.array([-1.0, 0, 2.3])], 0.0)
        (id_b,) = t.update([np.array([1.0, 0, 2.3])], 1 / 30)
        self.assertNotEqual(id_a, id_b)


def tilted_view(joints, pitch, roll):
    """Joints of a level room as a camera pitched down `pitch` and rolled `roll` degrees sees them."""
    to_camera = level.rotation_between(level.UP, level.up_from_angles(pitch, roll))
    return np.asarray(joints) @ to_camera.T


class Levelling(unittest.TestCase):
    def test_angles_round_trip(self):
        for pitch, roll in ((0, 0), (15, 0), (0, -8), (22, 6)):
            got = level.angles_from_up(level.up_from_angles(pitch, roll))
            self.assertAlmostEqual(got[0], pitch, places=6)
            self.assertAlmostEqual(got[1], roll, places=6)
        up = level.up_from_angles(20, 0)
        self.assertLess(up[2], 0)  # pitched down: up leans towards the camera
        self.assertTrue(np.allclose(level.rotation_between(up, level.UP) @ up, level.UP))

    def test_a_tilted_camera_is_levelled_from_a_still_player(self):
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8})
        seen = tilted_view(synthetic.standing(z=2.4), 15, 5)
        normalized, world, visibility = synthetic.landmarks(seen, width, height, focal)
        for i in range(60):
            bodies, _ = pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
        pitch, roll = pipeline.level.angles()
        self.assertEqual(pipeline.level.source, level.SOURCE_BODY)
        self.assertLess(abs(pitch - 15), 2.0, (pitch, roll))
        self.assertLess(abs(roll - 5), 2.0, (pitch, roll))
        joints = bodies[0]["joints"]
        axis = joints[km.SHOULDER_CENTER] - (joints[km.ANKLE_LEFT] + joints[km.ANKLE_RIGHT]) / 2
        tilt = math.degrees(math.acos(axis[1] / np.linalg.norm(axis)))
        self.assertLess(tilt, 2.5)  # standing straight up in the game's space
        self.assertLess(abs(joints[km.FOOT_LEFT][1] - (-0.8)), 0.1)  # on the virtual floor

    def test_walking_about_a_tilted_room_measures_the_floor(self):
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 70.0)
        pipeline = Pipeline({"fov": 70.0, "smoothing": 0.0, "camera_height": 0.8})
        hips = []
        for i in range(400):  # walking about: across and towards and away from the camera
            x = 0.6 * math.sin(i / 23.0)
            z = 2.4 + 0.6 * math.sin(i / 37.0)
            seen = tilted_view(synthetic.standing(x=x, z=z, camera_height=1.1), 20, 0)
            normalized, world, visibility = synthetic.landmarks(seen, width, height, focal)
            bodies, _ = pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
            if i >= 300:
                hips.append(bodies[0]["joints"][km.HIP_CENTER][1])
        self.assertEqual(pipeline.level.source, level.SOURCE_FLOOR)
        pitch, roll = pipeline.level.angles()
        self.assertLess(abs(pitch - 20), 1.5, (pitch, roll))
        self.assertLess(abs(roll), 1.5, (pitch, roll))
        # The synthetic person's ankles are 5 cm above the floor; ANKLE_HEIGHT assumes 8.
        self.assertLess(abs(pipeline.level.floor_height - 1.1), 0.06, pipeline.level.floor_height)
        self.assertLess(max(hips) - min(hips), 0.03)  # walking towards the camera doesn't change the height

    def test_a_manual_tilt_replaces_the_estimate(self):
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8, "tilt_manual": True,
                             "tilt_pitch": 12.0, "tilt_roll": -3.0})
        self.assertEqual(pipeline.level.source, level.SOURCE_MANUAL)
        pitch, roll = pipeline.level.angles()
        self.assertAlmostEqual(pitch, 12.0, places=5)
        self.assertAlmostEqual(roll, -3.0, places=5)

    def test_calibration_waits_for_a_player_standing_back(self):
        play = km.PlaySpace(0.8)
        close = synthetic.standing(z=0.7)  # at the desk
        for _ in range(40):
            self.assertTrue(np.allclose(play.update(close), 0.0))
        states = np.full(km.JOINTS, km.JOINT_TRACKED, np.uint8)
        states[km.ANKLE_LEFT] = km.JOINT_INFERRED  # feet out of view
        for _ in range(40):
            self.assertTrue(np.allclose(play.update(synthetic.standing(z=2.0), states), 0.0))
        offset = play.update(synthetic.standing(z=2.0))
        self.assertAlmostEqual((synthetic.standing(z=2.0) + offset)[km.HIP_CENTER][2], km.PlaySpace.DISTANCE)


class Stability(unittest.TestCase):
    def test_frame_numbers_and_times_continue_across_cameras(self):
        """Changing camera opens a new source: the bridge waits for frames numbered above the last
        one it used, so numbering and time must not start again."""
        image = np.zeros((4, 4, 3), np.uint8)
        first = capture._Source(False)
        first._deliver(image)
        a = first.latest(0, 0)
        second = capture._Source(False)
        second._deliver(image)
        b = second.latest(a.number, 0)
        self.assertIsNotNone(b)
        self.assertGreater(b.number, a.number)
        self.assertGreaterEqual(b.seconds, a.seconds)

    def test_mirror_changes_without_reopening(self):
        image = np.zeros((2, 3, 3), np.uint8)
        image[:, 0] = 255
        source = capture._Source(False)
        source._deliver(image.copy())
        self.assertEqual(source.latest(0, 0).image[0, 0, 0], 255)
        source.mirror = True
        source._deliver(image.copy())
        self.assertEqual(source._frame.image[0, 2, 0], 255)

    def test_a_short_miss_holds_the_pose_then_lets_go(self):
        width, height = 1280, 720
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.5, "camera_height": 0.8})
        focal = camera_model.webcam_focal(width, 60.0)
        normalized, world, visibility = synthetic.landmarks(synthetic.standing(x=0.0, z=2.3), width, height, focal)
        pose = Pose(normalized, world, visibility)
        for i in range(3):
            bodies, _ = pipeline.process([pose], (width, height), i / 30)
        (seen,) = bodies
        bodies, info = pipeline.process([], (width, height), 4 / 30)  # missed for one frame
        self.assertEqual([b["id"] for b in bodies], [seen["id"]])
        self.assertTrue(np.all(bodies[0]["joint_state"] == km.JOINT_INFERRED))
        np.testing.assert_allclose(bodies[0]["joints"], seen["joints"])
        self.assertEqual(len(info["joints"]), 1)
        bodies, _ = pipeline.process([], (width, height), 0.5)  # gone longer than the hold
        self.assertEqual(bodies, [])
        bodies, _ = pipeline.process([pose], (width, height), 0.55)  # back within the tracker's memory
        self.assertEqual(bodies[0]["id"], seen["id"])
        self.assertLess(np.abs(bodies[0]["joints"] - seen["joints"]).max(), 0.01)  # the filter wasn't restarted


class Images(unittest.TestCase):
    def test_depth_packing_and_players(self):
        near = synthetic.standing(x=0.0, z=2.0)
        far = synthetic.standing(x=0.3, z=3.0)
        depth = depth_synth.depth_image([near, far], 0.8)
        self.assertEqual(depth.shape, (240, 320))
        self.assertEqual(depth.dtype, np.uint16)
        u, v = camera_model.project(near[km.SPINE], camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE)
        pixel = depth[int(v), int(u)]
        self.assertEqual(pixel & 7, 1)  # the first body: player index 1
        self.assertLess(abs((pixel >> 3) - near[km.SPINE][2] * 1000), 80)
        u, v = camera_model.project(far[km.HAND_RIGHT], camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE)
        self.assertEqual(depth[int(v), int(u)] & 7, 2)
        self.assertEqual(depth[5, 5] & 7, 0)  # the room
        # The floor gets farther towards the horizon; the wall is at 4 m.
        self.assertLess(depth[239, 5] >> 3, depth[200, 5] >> 3)
        self.assertEqual(depth[10, 5] >> 3, 4000)
        # The near body hides the far one where they overlap.
        overlap = synthetic.standing(x=0.0, z=3.0)
        depth = depth_synth.depth_image([overlap, near], 0.8)
        u, v = camera_model.project(near[km.SPINE], camera_model.DEPTH_FOCAL, camera_model.DEPTH_SIZE)
        self.assertEqual(depth[int(v), int(u)] & 7, 2)

    def test_the_image_follows_the_skeleton(self):
        """A mark on the player's right hand in the webcam image lands where the virtual color camera
        sees that joint, after the play space moved them."""
        width, height = 1280, 720
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8})
        focal = camera_model.webcam_focal(width, 60.0)
        joints = synthetic.standing(x=-0.3, z=1.8)
        hand = joints[km.HAND_RIGHT]
        normalized, world, visibility = synthetic.landmarks(joints, width, height, focal)
        frame = np.zeros((height, width, 3), np.uint8)
        camera = camera_model.opencv_to_kinect(hand)
        mark = (int(width / 2 + focal * camera[0] / camera[2]), int(height / 2 + focal * camera[1] / camera[2]))
        frame[mark[1] - 3:mark[1] + 4, mark[0] - 3:mark[0] + 4] = 255
        for _ in range(3):  # let the reference depth settle
            bodies, info = pipeline.process([Pose(normalized, world, visibility)], (width, height), 0.0)
        color, _ = pipeline.render(frame, info)
        expected = camera_model.project(bodies[0]["joints"][km.HAND_RIGHT], camera_model.COLOR_FOCAL,
                                        camera_model.COLOR_SIZE)
        ys, xs = np.nonzero(color[..., 0] > 128)
        self.assertGreater(len(xs), 0)
        self.assertLess(math.hypot(xs.mean() - expected[0], ys.mean() - expected[1]), 6.0)
        self.assertGreater(expected[0], 320)  # the right hand on the image's right (mirrored like a Kinect)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=1).result
    sys.exit(0 if result.wasSuccessful() else 1)
