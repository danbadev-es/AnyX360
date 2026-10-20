# Pose bridge

The pose bridge turns a webcam into the virtual Kinect's sensor. A Kinect game (Dance Central 3,
Kinect Adventures) starts it, without a window, when its `[kinect] source` is `webcam`. The game stops it again when
it exits. The bridge reads the camera and estimates up to two people's poses with MediaPipe. It then
publishes Kinect skeletons and images through shared memory:

- the protocol is defined in [runtime/include/x360/nui_bridge.h](../../runtime/include/x360/nui_bridge.h);
- the game's side is in [runtime/src/nui/bridge_source.cpp](../../runtime/src/nui/bridge_source.cpp).

## Building

```
powershell -ExecutionPolicy Bypass -File tools\pose_bridge\build_bridge.ps1
```

This needs Python 3.14 for Windows x64, because the pinned wheels are built for it. Pass
`-Python <python.exe>` if the `py` launcher doesn't find it. The script runs these steps:

1. **Packages:** it installs `requirements.txt` into `build\pose_bridge\venv` with
   `pip --require-hashes`.
2. **Models:** it downloads the MediaPipe pose models and checks their SHA-256.
3. **Bundle:** PyInstaller builds `build\pose_bridge\dist\pose_bridge\` (about 260 MB).
4. **Licenses:** it collects them into `build\pose_bridge\licenses\`.
5. **Self-test:** it runs the bundle's self-test, which doesn't use the camera.

After a build, two things work:

- A developer build of the game finds the bridge in `build\pose_bridge\dist`.
- A reconfigure of CMake adds the bridge's Python tests to `ctest`.

`tools\package\make_dist.ps1` copies the bundle into the release when a game is played with a webcam, as
`bin\pose_bridge\` and `licenses\pose_bridge\`.

## Running it on its own

```
pose_bridge --preview                       the webcam, in a window: skeletons and depth
pose_bridge --synthetic --preview           a synthetic dancer, no camera
pose_bridge --video clip.mp4 --fast --dump-jsonl out.jsonl
pose_bridge --self-test
```

From the sources, run `build\pose_bridge\venv\Scripts\python.exe -I tools\pose_bridge\pose_bridge.py ...`.

`ANYX360_POSE_BRIDGE` replaces the command the game starts. The game appends its own arguments to it.
For example, this runs the sources in synthetic mode:

```
set ANYX360_POSE_BRIDGE="...\venv\Scripts\python.exe" -I "...\tools\pose_bridge\pose_bridge.py" --synthetic
```

## How a frame is made

1. **Capture** (`capture.py`): Media Foundation, or DirectShow as a fallback, MJPG at 1280x720 and
   30 fps. A camera that mirrors its image is flipped back first.
2. **Estimation** (`estimator.py`): MediaPipe Pose Landmarker in video mode, on the frame scaled to
   640 wide. Each person comes with 33 landmarks and a segmentation mask.
3. **Placing the pose** (`kinect_mapping.py`, `pipeline.py`):
   - A linear least-squares solve finds where the metric "world" pose sits in the webcam's space:
     the translation that best matches the image landmarks, with the focal length from `fov`.
   - The hips' distance is filtered on its own, strongly. The model's scale wobbles from frame to
     frame, which otherwise moves the whole body back and forth.
   - Each landmark's depth comes from the world pose at that distance. Its x and y come from its
     image landmark, back-projected at that depth, where it's seen. The image output is far more
     precise than the world output's x and y.
   - Kinect axes (x towards the player's right, y up, z away), then the Kinect's 20 joints. A joint
     is tracked or inferred from its landmarks' visibility, with hysteresis (tracked above 0.6,
     inferred below 0.4), so it doesn't flicker.
4. **Filtering** (`filters.py`, `feet.py`), per person, in the webcam's frame:
   - One-Euro filters per joint, with cutoffs across the image and in depth. The speed that opens a
     joint's filter is its own 3D speed, with depth counting for less (it's the noisiest).
   - Once a joint moves fast, it gets a quarter of a frame of prediction.
   - A planted foot is held still until it moves off.
   - The values come from `benchmark.py` (see Tests).
5. **Levelling** (`level.py`): the webcam's pitch is measured, first from the player's body axis
   while they stand still, then from a plane fitted through the ankles as they move about. That
   plane also gives the camera's height. The roll is set by hand (0 unless changed): measured, it
   followed the player's lean and turned the picture of a level camera. Every joint is rotated into
   a level frame. A new estimate doesn't move the player, and F2 shows it or lets both be set by
   hand.
6. **Play space:** the scene moves so the first player stands centred, 2.3 m from the virtual
   sensor, with their feet on its floor (the measured one when known). It's measured from a second
   of frames with the whole body in view at 1.2-4.5 m, and again when the game asks or the tilt
   changes.
7. **Tracking and clipping:** IDs stay stable from the hips' positions and where they were heading
   (`tracker.py`), so two players crossing keep theirs. A body's clipping flags come from the
   virtual view's edges and from the webcam's own: a player whose feet the webcam doesn't see is
   told to step back.
8. **Images** (`depth_synth.py`):
   - **Color** is the webcam image, mirrored and scaled to sit where the virtual color camera sees
     the skeleton.
   - **Depth** is the room (floor and a wall at 4 m) plus each person's mask and bone capsules. Each
     pixel packs millimetres << 3 | player index.
   - The images are skipped when the game has no image stream open.

The skeleton reaches the game as soon as it's published: the game's sensor makes its frame on the
bridge's event (at most every 28 ms), stamped with the camera's capture time. Every 30 s the log
gets a line with the frame rate, the pose model's median and slowest times, and the frames dropped.
F2 > Kinect shows the same breakdown, the color image the game gets with the skeleton over it, and
each joint's state.

## Tests

```
build\pose_bridge\venv\Scripts\python.exe -I tools\pose_bridge\run_tests.py
```

The tests cover:

- the protocol offsets, checked against the header's `static_assert`s;
- the axes (the right wrist must come out at +x);
- the translation solve, the back-projection and the play space;
- the levelling, the filters, the foot lock, the tracker and the images;
- precision limits from the benchmark.

They use no camera and no model.

### Benchmark

```
build\pose_bridge\venv\Scripts\python.exe -I tools\pose_bridge\benchmark.py
```

The benchmark runs the whole pipeline on synthetic MediaPipe output against the true joints. The
moves are made by rotating limbs about their joints:

- standing still, an arm waving, a side step, a leg out, a reach towards the camera;
- walking in, and dancing, with the webcam level and 15 degrees down.

The noise is like the model's: 2-6 px on the image landmarks, 1-5 cm on the world landmarks (more
in depth), and 2 % on the pose's scale, correlated from frame to frame. It prints lag, jitter and
error. Over three seeds, against 0.5.0's pipeline:

| | before | now |
|---|---|---|
| Lag, arm wave / side step / leg out / reach | 76 / 62 / 60 / 67 ms | 7 / 3 / 9 / 17 ms |
| Error while dancing, level / webcam 15° down | 71 / 200 mm | 48 / 50 mm |
| Hands at their worst (p95) while dancing | 157 mm | 77 mm |
| Hand error reaching towards the camera | 84 mm | 33 mm |
| Jitter standing still: hands / feet / a planted foot | 40 / 42 / 31 mm | 36 / 26 / 17 mm |
| Lag walking towards the webcam | 55 ms | 95 ms |

The last row is the price of filtering the distance more strongly. `run_tests.py` holds limits a
little above today's numbers. These numbers come from synthetic noise; a person in front of a
webcam is the real test.
