# Pose bridge

The pose bridge turns a webcam into the virtual Kinect's sensor. A Kinect game (Dance Central 3)
starts it, without a window, when its `[kinect] source` is `webcam`. The game stops it again when
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

`tools\package\make_dist.ps1 -Game dc3` copies the bundle into the distribution, as
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
3. **Mapping** (`kinect_mapping.py`):
   - The metric "world" pose is placed in the webcam's space. A linear least-squares solve finds the
     translation that best matches the image landmarks, using the webcam focal length from `fov`.
   - The pose is then turned into Kinect axes: x towards the player's right, y up, z away.
   - The 33 landmarks become the Kinect's 20 joints. A joint whose landmarks are not visible enough
     is marked inferred.
4. **Play space:** the scene moves so the first player stands centred, 2.3 m from the virtual
   sensor, with their feet on its floor. This happens automatically after a second in view, and
   again when the game asks.
5. **Filtering and tracking:** a One-Euro filter is applied per person (`filters.py`). IDs stay
   stable from the distance between hips (`tracker.py`).
6. **Images** (`depth_synth.py`):
   - **Color** is the webcam image, mirrored and scaled to sit where the virtual color camera sees
     the skeleton.
   - **Depth** is the room (floor and a wall at 4 m) plus each person's mask and bone capsules. Each
     pixel packs millimetres << 3 | player index.

## Tests

```
build\pose_bridge\venv\Scripts\python.exe -I tools\pose_bridge\run_tests.py
```

The tests cover:

- the protocol offsets, checked against the header's `static_assert`s;
- the axes (the right wrist must come out at +x);
- the translation solve and the play space;
- the filter, the tracker and the images.

They use no camera and no model.
