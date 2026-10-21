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

import benchmark  # noqa: E402
import camera_model  # noqa: E402
import capture  # noqa: E402
import depth_synth  # noqa: E402
import kinect_mapping as km  # noqa: E402
import level  # noqa: E402
import protocol  # noqa: E402
import synthetic  # noqa: E402
from feet import FootLock  # noqa: E402
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
            self.assertFalse(control["centre_player"])  # the game's default: where the player is
            h.play_space = 1
            self.assertTrue(game.control()["centre_player"])
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

    def test_clipping_by_the_webcams_own_view(self):
        """The play space puts a player in the virtual view; the webcam not seeing their feet (or
        their right side, on the left of its unmirrored image) still says so."""
        width, height = 640, 480
        focal = camera_model.webcam_focal(width, 60)
        normalized, _, _ = synthetic.landmarks(synthetic.standing(z=2.5), width, height, focal)
        self.assertEqual(km.image_clipping(normalized), 0)
        normalized, _, _ = synthetic.landmarks(synthetic.standing(z=1.2), width, height, focal)
        self.assertTrue(km.image_clipping(normalized) & km.CLIPPED_BOTTOM)
        normalized, _, _ = synthetic.landmarks(synthetic.standing(x=1.6, z=2.5), width, height, focal)
        self.assertEqual(km.image_clipping(normalized) & (km.CLIPPED_LEFT | km.CLIPPED_RIGHT), km.CLIPPED_RIGHT)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.5, "camera_height": 0.8})
        normalized, world, visibility = synthetic.landmarks(synthetic.standing(z=1.2), width, height, focal)
        bodies, _ = pipeline.process([Pose(normalized, world, visibility)], (width, height), 0.0)
        self.assertTrue(bodies[0]["quality"] & km.CLIPPED_BOTTOM)

    def test_joints_are_placed_on_their_image_landmarks(self):
        """x and y come from the image landmarks: a world pose that's off (the model's world output
        is noisier) still projects where the image says, at the distance given."""
        width, height = 640, 480
        focal = camera_model.webcam_focal(width, 60)
        normalized, world, visibility = synthetic.landmarks(synthetic.standing(x=0.3, z=2.5), width, height, focal)
        t = km.solve_translation(world, normalized, visibility, width, height, focal)
        off = world + np.random.default_rng(4).normal(0, 0.03, world.shape)
        camera = km.place(off, normalized, visibility, t, t[2], width, height, focal)
        projected = camera[:, :2] / camera[:, 2:] * focal / (width, height) + 0.5
        np.testing.assert_allclose(projected, normalized, atol=1e-9)
        # A landmark the model can't see keeps its world position.
        visibility = visibility.copy()
        visibility[km.L_WRIST] = 0.1
        camera = km.place(off, normalized, visibility, t, t[2], width, height, focal)
        np.testing.assert_allclose(camera[km.L_WRIST], off[km.L_WRIST] + t)

    def test_joint_states_do_not_flicker(self):
        states = km.JointStates()
        seen = np.full(km.JOINTS, 0.9)
        self.assertTrue((states(seen) == km.JOINT_TRACKED).all())
        seen[km.KNEE_LEFT] = 0.45  # below 0.5, above the hysteresis' 0.4: still tracked
        self.assertEqual(states(seen)[km.KNEE_LEFT], km.JOINT_TRACKED)
        seen[km.KNEE_LEFT] = 0.3
        self.assertEqual(states(seen)[km.KNEE_LEFT], km.JOINT_INFERRED)
        seen[km.KNEE_LEFT] = 0.55  # back above 0.5 but not 0.6: still inferred
        self.assertEqual(states(seen)[km.KNEE_LEFT], km.JOINT_INFERRED)
        seen[km.KNEE_LEFT] = 0.7
        self.assertEqual(states(seen)[km.KNEE_LEFT], km.JOINT_TRACKED)


class Legs(unittest.TestCase):
    LEG_LANDMARKS = [km.L_KNEE, km.R_KNEE, km.L_ANKLE, km.R_ANKLE, km.L_HEEL, km.R_HEEL, km.L_FOOT, km.R_FOOT]

    def test_unseen_legs_stand_under_the_hips(self):
        joints = synthetic.standing(z=2.3)
        states = np.full(km.JOINTS, km.JOINT_TRACKED, np.uint8)
        states[[km.KNEE_LEFT, km.ANKLE_LEFT, km.FOOT_LEFT]] = km.JOINT_INFERRED
        flailing = joints.copy()
        flailing[[km.KNEE_LEFT, km.ANKLE_LEFT, km.FOOT_LEFT]] += (0.4, 0.3, -0.5)
        out, placed = km.natural_legs(flailing, states, (0, 1, 0))
        self.assertEqual(placed, 3)
        hip, knee, ankle, foot = out[[km.HIP_LEFT, km.KNEE_LEFT, km.ANKLE_LEFT, km.FOOT_LEFT]]
        np.testing.assert_allclose(knee[[0, 2]], hip[[0, 2]])
        np.testing.assert_allclose(ankle[[0, 2]], hip[[0, 2]])
        self.assertLess(abs(ankle[1] - joints[km.ANKLE_LEFT][1]), 0.1)  # about the player's own leg
        self.assertLess(foot[2], ankle[2])  # the foot points at the camera
        np.testing.assert_allclose(out[km.FOOT_RIGHT], joints[km.FOOT_RIGHT])  # the seen leg is kept
        # Only below the first joint it doesn't see: a seen knee stays, the shin hangs from it.
        states[km.KNEE_LEFT] = km.JOINT_TRACKED
        out, placed = km.natural_legs(flailing, states, (0, 1, 0))
        self.assertEqual(placed, 2)
        np.testing.assert_allclose(out[km.KNEE_LEFT], flailing[km.KNEE_LEFT])
        np.testing.assert_allclose(out[km.ANKLE_LEFT][[0, 2]], flailing[km.KNEE_LEFT][[0, 2]])

    def test_legs_outside_the_picture_are_not_seen(self):
        """However sure the model says it is about them (it guesses legs outside the image)."""
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        normalized, _, visibility = synthetic.landmarks(synthetic.standing(z=1.0), width, height, focal)
        self.assertTrue((visibility[self.LEG_LANDMARKS] > 0.5).all())
        seen = km.legs_in_picture(normalized, visibility)
        self.assertTrue((seen[self.LEG_LANDMARKS] == 0).all())  # knees down below the image at 1 m
        np.testing.assert_allclose(seen[:km.L_KNEE], visibility[:km.L_KNEE])  # the rest as it was
        normalized, _, visibility = synthetic.landmarks(synthetic.standing(z=3.2), width, height, focal)
        np.testing.assert_allclose(km.legs_in_picture(normalized, visibility), visibility)

    def test_a_player_close_to_the_camera_keeps_still_legs(self):
        """Close to a laptop's camera the legs are out of the picture and the model's guesses for
        them flail about: the game sees still legs under the player, inferred, on a steady floor."""
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.5, "camera_height": 0.8, "centre_player": False,
                             "tilt_manual": True})
        rng = np.random.default_rng(7)
        feet, floors = [], []
        for i in range(60):
            normalized, world, visibility = synthetic.landmarks(synthetic.standing(z=1.0), width, height, focal)
            world[self.LEG_LANDMARKS] += rng.normal(0, 0.15, (len(self.LEG_LANDMARKS), 3))
            normalized[self.LEG_LANDMARKS, 1] += np.abs(rng.normal(0, 0.1, len(self.LEG_LANDMARKS)))
            bodies, _ = pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
            feet.append(bodies[0]["joints"][km.FOOT_LEFT])
            floors.append(pipeline.floor_height())
        self.assertEqual(bodies[0]["joint_state"][km.ANKLE_LEFT], km.JOINT_INFERRED)
        self.assertLess(np.ptp(np.array(feet[30:]), axis=0).max(), 0.01)
        self.assertLess(abs(feet[-1][0] - bodies[0]["joints"][km.HIP_LEFT][0]), 0.02)
        self.assertLess(abs(floors[-1] - 0.8), 0.15)  # about the synthetic room's floor
        self.assertEqual(len(set(floors[30:])), 1)


class Precision(unittest.TestCase):
    """The whole pipeline on noisy synthetic landmarks (benchmark.py). The limits leave room above
    today's numbers; the comments give the pipeline's before the precision work (0.5.0), on the same
    noise."""

    @classmethod
    def setUpClass(cls):
        cls.m = benchmark.measure(seed=1)

    def check(self, key, limit):
        self.assertLess(self.m[key], limit, f"{key}: {self.m[key]:.1f}")

    def test_lag(self):
        self.check("arm_wave_lag_ms", 30)      # was 76
        self.check("side_step_lag_ms", 25)     # was 62
        self.check("side_step_overshoot_mm", 30)
        self.check("leg_tap_lag_ms", 35)       # was 60
        self.check("push_lag_ms", 35)          # was 67
        self.check("walk_in_lag_ms", 150)      # was 55: the distance is smoothed more

    def test_error(self):
        self.check("dance_error_mm", 55)                # was 71
        self.check("dance_hands_p95_mm", 95)            # was 157
        self.check("dance_tilted_15_error_mm", 60)      # was 200
        self.check("dance_tilted_15_feet_p95_mm", 100)  # was 520
        self.check("arm_wave_hand_error_mm", 48)        # was 61
        self.check("push_hand_error_mm", 50)            # was 84

    def test_jitter(self):
        self.check("still_hands_jitter_mm", 46)          # was 40
        self.check("still_feet_jitter_mm", 32)           # was 42
        self.check("still_hip_depth_jitter_mm", 28)      # was 34
        self.check("leg_tap_planted_foot_jitter_mm", 15)  # was 31


class Feet(unittest.TestCase):
    def test_a_planted_foot_holds_still_and_a_step_moves_it_at_once(self):
        rng = np.random.default_rng(5)
        lock = FootLock()
        planted = synthetic.standing()
        speeds = np.full(km.JOINTS, 0.05)
        outs = []
        for _ in range(60):  # standing, with jitter
            noisy = planted + rng.normal(0, 0.008, planted.shape)
            out = lock(noisy, speeds)
            outs.append(out[km.ANKLE_LEFT])
        self.assertLess(np.std(outs[30:], axis=0).max(), 0.002)  # the input jitters by 8 mm
        right = out[km.ANKLE_RIGHT].copy()
        # A step: the ankle moves 5 cm per frame at 1.5 m/s.
        joints = planted + rng.normal(0, 0.008, planted.shape)
        for i in range(1, 4):
            joints[[km.ANKLE_LEFT, km.FOOT_LEFT], 0] -= 0.05
            out = lock(joints, np.where(np.arange(km.JOINTS) == km.ANKLE_LEFT, 1.5, 0.05))
        # After three frames the foot is near the measured one (rejoining, not jumping).
        self.assertLess(abs(out[km.ANKLE_LEFT, 0] - joints[km.ANKLE_LEFT, 0]), 0.03)
        self.assertLess(out[km.ANKLE_LEFT, 0], planted[km.ANKLE_LEFT, 0] - 0.1)
        # The other foot stayed held where it was.
        self.assertLess(np.abs(out[km.ANKLE_RIGHT] - right).max(), 0.003)


class Filters(unittest.TestCase):
    def test_still_values_stay_and_steps_converge(self):
        f = OneEuro(1.5, beta=0.4)
        for i in range(30):
            out = f(np.array([1.0, 2.0]), i / 30)
        self.assertTrue(np.allclose(out, [1.0, 2.0]))
        for i in range(30, 60):
            out = f(np.array([2.0, 2.0]), i / 30)
        self.assertLess(abs(out[0] - 2.0), 0.05)

    def test_jitter_is_reduced(self):
        rng = np.random.default_rng(1)
        f = OneEuro(1.5, beta=0.4)
        noisy = 1.0 + rng.normal(0, 0.02, 300)
        out = np.array([f(np.array([x]), i / 30)[0] for i, x in enumerate(noisy)])
        self.assertLess(out[100:].std(), noisy[100:].std() * 0.5)

    def test_lag(self):
        # A fast movement (1 m/s) lags less than a frame's worth of travel times a few.
        f = OneEuro(1.5, beta=0.4)
        for i in range(60):
            out = f(np.array([i / 30.0]), i / 30)
        self.assertLess(59 / 30 - out[0], 0.15)

    def test_a_joint_moving_along_one_axis_opens_its_others(self):
        """With axis weights a joint's speed is its own (all its axes), so a hand moving sideways
        isn't held back in height; a weight of 0 leaves that axis out of the speed."""
        def run(weights):
            f = OneEuro(np.array([0.3, 0.3, 0.3]), beta=2.0, axis_weights=weights)
            for i in range(30):
                out = f(np.array([[i / 30.0, i / 60.0, 0.0]]), i / 30)  # mostly sideways
            return f.speed[0, 0], (29 / 60) - out[0, 1]  # the speed, the lag in height
        speed, lag = run((1.0, 1.0, 1.0))
        self.assertGreater(speed, 0.8)
        _, lag_alone = run(None)  # each value opens only with its own speed
        self.assertLess(lag, lag_alone)
        speed_without_x, _ = run((0.0, 1.0, 1.0))
        self.assertLess(speed_without_x, speed * 0.6)

    def test_prediction_only_when_moving(self):
        f = OneEuro(1.0, beta=1.0, axis_weights=(1.0,), lead=1 / 30)
        g = OneEuro(1.0, beta=1.0, axis_weights=(1.0,))
        for i in range(30):
            out, plain = f(np.array([[i / 30.0]]), i / 30), g(np.array([[i / 30.0]]), i / 30)
        self.assertGreater(out[0, 0], plain[0, 0] + 0.02)  # 1 m/s: about a frame ahead
        rng = np.random.default_rng(3)
        for i in range(30, 90):  # at rest with jitter: no prediction
            value = np.array([[1.0 + rng.normal(0, 0.003)]])
            out, plain = f(value, i / 30), g(value, i / 30)
        self.assertAlmostEqual(out[0, 0], plain[0, 0], places=6)


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

    def test_two_players_crossing_keep_their_ids(self):
        """Two players walking past each other (1.5 m/s each, 15 cm apart in depth): where they
        were heading tells them apart where their hips come close."""
        t = Tracker()
        ids = None
        for i in range(31):
            time_ = i / 30
            a = np.array([-0.75 + 1.5 * time_, 0, 2.3])
            b = np.array([0.75 - 1.5 * time_, 0, 2.45])
            got = t.update([a, b] if i % 2 else [b, a], time_)  # in no particular order
            got = got if i % 2 else got[::-1]
            if ids is None:
                ids = got
            self.assertEqual(got, ids, i)


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
        # The pitch is measured; the roll is the one set by hand (here the camera's own 5 degrees).
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8, "tilt_roll": 5.0})
        seen = tilted_view(synthetic.standing(z=2.4), 15, 5)
        normalized, world, visibility = synthetic.landmarks(seen, width, height, focal)
        for i in range(60):
            bodies, _ = pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
        pitch, roll = pipeline.level.angles()
        self.assertEqual(pipeline.level.source, level.SOURCE_BODY)
        self.assertLess(abs(pitch - 15), 2.0, (pitch, roll))
        self.assertAlmostEqual(roll, 5.0, places=5)
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

    def test_the_roll_is_not_measured(self):
        # A player leaning to one side (or a rolled camera) doesn't turn the picture: the roll stays
        # the manual one, 0 unless set.
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8})
        seen = tilted_view(synthetic.standing(z=2.4), 15, 8)
        normalized, world, visibility = synthetic.landmarks(seen, width, height, focal)
        for i in range(60):
            pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
        pitch, roll = pipeline.level.angles()
        self.assertEqual(pipeline.level.source, level.SOURCE_BODY)
        self.assertLess(abs(pitch - 15), 2.5, (pitch, roll))
        self.assertAlmostEqual(roll, 0.0, places=5)

    def test_a_nearly_level_camera_is_level(self):
        # A camera a degree or two off level (or a level one, with the estimates' noise) is taken as
        # level: neither the picture nor the player tilts.
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8})
        for tilt in (2.0, 4.0):  # a laptop lid turned by eye to face the player
            pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8})
            seen = tilted_view(synthetic.standing(z=2.4), tilt, 0)
            normalized, world, visibility = synthetic.landmarks(seen, width, height, focal)
            for i in range(60):
                pipeline.process([Pose(normalized, world, visibility)], (width, height), i / 30)
            self.assertEqual(pipeline.level.source, level.SOURCE_BODY)
            pitch, roll = pipeline.level.angles()
            self.assertAlmostEqual(pitch, 0.0, places=5)
            self.assertAlmostEqual(roll, 0.0, places=5)

    def test_a_settled_tilt_holds_until_the_camera_moves(self):
        # Readings that wander for a moment (a player bending, an estimate's noise) don't move a
        # settled tilt; readings that stay off for longer (the lid was moved) do.
        leveler = level.Leveler()

        def readings(pitch, frames):
            leveler._body.clear()
            for _ in range(frames):
                leveler._body.append(level.up_from_angles(pitch, 0.0))
                leveler._estimate()

        readings(15.0, 120)
        self.assertLess(abs(leveler.angles()[0] - 15.0), 0.5)
        readings(25.0, 60)  # 2 s off: held
        self.assertLess(abs(leveler.angles()[0] - 15.0), 0.5)
        readings(15.0, 30)
        readings(25.0, 400)  # the lid moved: after 5 s it follows
        self.assertLess(abs(leveler.angles()[0] - 25.0), 1.0, leveler.angles())

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
        sees that joint: where the player is, and after the play space moved them."""
        width, height = 1280, 720
        focal = camera_model.webcam_focal(width, 60.0)
        joints = synthetic.standing(x=-0.3, z=1.8)
        hand = joints[km.HAND_RIGHT]
        normalized, world, visibility = synthetic.landmarks(joints, width, height, focal)
        frame = np.zeros((height, width, 3), np.uint8)
        camera = camera_model.opencv_to_kinect(hand)
        mark = (int(width / 2 + focal * camera[0] / camera[2]), int(height / 2 + focal * camera[1] / camera[2]))
        frame[mark[1] - 3:mark[1] + 4, mark[0] - 3:mark[0] + 4] = 255
        for centre in (False, True):
            pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8, "centre_player": centre})
            for _ in range(3):  # let the reference depth settle
                bodies, info = pipeline.process([Pose(normalized, world, visibility)], (width, height), 0.0)
            color, _ = pipeline.render(frame, info)
            expected = camera_model.project(bodies[0]["joints"][km.HAND_RIGHT], camera_model.COLOR_FOCAL,
                                            camera_model.COLOR_SIZE)
            ys, xs = np.nonzero(color[..., 0] > 128)
            self.assertGreater(len(xs), 0)
            self.assertLess(math.hypot(xs.mean() - expected[0], ys.mean() - expected[1]), 6.0, centre)
            # The right hand on the body's right in the image (mirrored like a Kinect).
            hips = camera_model.project(bodies[0]["joints"][km.HIP_CENTER], camera_model.COLOR_FOCAL,
                                        camera_model.COLOR_SIZE)
            self.assertGreater(expected[0], hips[0])

    def test_where_the_player_is_the_picture_is_the_webcams_own(self):
        """The default: the virtual Kinect stands where the webcam is. Its picture is the whole webcam
        image scaled to the Kinect's lens about the centre (no zoom onto the player), the same wherever
        the player walks, and the room's floor is where their feet are."""
        width, height = 1280, 720
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8, "centre_player": False,
                             "tilt_manual": True})
        focal = camera_model.webcam_focal(width, 60.0)
        frame = np.zeros((height, width, 3), np.uint8)
        frame[150:157, 250:257] = 255  # a mark on the wall, away from the image's centre
        marks = []
        for i, (x, z) in enumerate(((0.0, 2.4), (-0.5, 1.6), (0.4, 3.2))):
            normalized, world, visibility = synthetic.landmarks(synthetic.standing(x=x, z=z), width, height, focal)
            for k in range(20):
                bodies, info = pipeline.process([Pose(normalized, world, visibility)], (width, height),
                                                (i * 20 + k) / 30)
            np.testing.assert_allclose(info["offset"], 0)
            color, depth = pipeline.render(frame, info)
            ys, xs = np.nonzero(color[..., 0] > 128)
            marks.append((xs.mean(), ys.mean()))
        self.assertLess(np.ptp(np.array(marks), axis=0).max(), 0.5)
        scale = camera_model.COLOR_FOCAL / focal
        expected = (320 - (253.5 - width / 2) * scale, 240 + (153.5 - height / 2) * scale)  # mirrored
        self.assertLess(math.hypot(marks[0][0] - expected[0], marks[0][1] - expected[1]), 1.5)
        # The floor: 3 s with the feet in view (closer, the legs stood naturally out of view, a few
        # centimetres longer than this player's).
        for k in range(90):
            _, info = pipeline.process([Pose(normalized, world, visibility)], (width, height), (60 + k) / 30)
        _, depth = pipeline.render(frame, info)
        self.assertLess(abs(pipeline.floor_height() - 0.83), 0.025)  # the ankles' drop and their height
        # The synthetic room's floor at the same height: the bottom row's depth.
        slope = (239.5 - 120) / camera_model.DEPTH_FOCAL
        self.assertLess(abs((depth[239, 5] >> 3) - pipeline.floor_height() / slope * 1000), 15)

    def test_the_picture_holds_still_while_the_player_moves(self):
        """Moved to the Kinect's spot, the picture is set when the play space is measured: walking
        towards and away from the camera doesn't zoom it (it followed every frame's distance), and
        recentring sets it again."""
        width, height = 1280, 720
        pipeline = Pipeline({"fov": 60.0, "smoothing": 0.0, "camera_height": 0.8, "centre_player": True})
        focal = camera_model.webcam_focal(width, 60.0)
        frame = np.zeros((height, width, 3), np.uint8)
        frame[150:157, 250:257] = 255  # a mark on the wall, away from the image's centre

        def mark_after(z, frames, start):
            normalized, world, visibility = synthetic.landmarks(synthetic.standing(z=z), width, height, focal)
            for i in range(frames):
                _, info = pipeline.process([Pose(normalized, world, visibility)], (width, height), (start + i) / 30)
            color, _ = pipeline.render(frame, info)
            ys, xs = np.nonzero(color[..., 0] > 128)
            self.assertGreater(len(xs), 0)
            return np.array([xs.mean(), ys.mean()])

        settled = mark_after(2.4, 40, 0)  # the play space measured with the player at 2.4 m
        for z, start in ((1.7, 40), (3.1, 100), (2.0, 160)):
            self.assertLess(np.linalg.norm(mark_after(z, 60, start) - settled), 0.5, z)
        pipeline.calibrate()  # "Recentre on me" at 3.1 m: the picture follows the new play space
        moved = mark_after(3.1, 40, 220)
        self.assertGreater(np.linalg.norm(moved - settled), 2.0)


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=1).result
    sys.exit(0 if result.wasSuccessful() else 1)
