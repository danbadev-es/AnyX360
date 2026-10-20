# Architecture

AnyX360 turns an Xbox 360 title into a native Windows x64 executable by **static recompilation**:
the game's PowerPC machine code is translated ahead of time into C++, compiled with Clang, and linked
against a runtime that re-implements the console's operating system, GPU, and audio hardware at a high
level. No CPU emulator or JIT runs on the player's machine.

It follows the model proven by [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp)
(Sonic Unleashed) and by BLACK-XBERecomp (original Xbox): a reusable framework plus a per-game
configuration and per-game patches. A fully automatic "any game" converter, like AnyPS5, is not
realistic for this console (see [Why the Xbox 360 is harder](#why-the-xbox-360-is-harder)).

## Pipeline

```
 Raw/<game>.iso
      │  x360tool iso-extract              (XDVDFS, XGD2/XGD3 layouts)
      ▼
 extracted/<game>/default.xex + game files
      │  x360tool xex-info / xex-extract   (headers, imports, SDK libs; AES + LZX → PE image)
      ▼
 basefile.bin (big-endian PowerPC PE) ──► Ghidra / IDA analysis ──► games/<game>/config.toml
      │                                                              (functions, jump tables,
      │  recompiler (XenonRecomp fork)                                setjmp/longjmp, hooks)
      ▼
 games/<game>/ppc/*.cpp  (generated, git-ignored)          game shaders (Xenos microcode)
      │                                                        │  shader recompiler (XenosRecomp fork)
      │  clang-cl                                              ▼
      ▼                                                    HLSL → DXIL (D3D12)
 <game>.exe  ◄── links ── runtime/ (kernel HLE, GPU, audio, input, file system)
```

## Components

| Path | Role | Status |
|---|---|---|
| `tools/x360tool` | Disc extraction, XEX2 inspection, decryption and decompression, import and section inventory; Games on Demand (SVOD) and STFS packages | Working |
| `thirdparty/XenonRecomp` | PPC → C++ recompiler and XEX analyser (fork: devkit key, dataflow jump tables, missing instructions, fall-through into the next function) | Working |
| `runtime/` `x360/endian.h`, `x360/memory.h` | Big-endian access helpers and the 4 GiB guest address-space reservation | Working |
| `runtime/src/kernel`, `runtime/src/hle` | HLE of the `xboxkrnl.exe` and `xam.xex` exports the titles call; the shared host entry point | Working for both titles |
| `runtime/src/gpu` | Xenia's Direct3D 12 back end, display pacing, the F1 overlay and the F2 settings panel (ImGui in physical pixels, Inter, `ui_theme`); the games are per-monitor DPI aware (`runtime/game.manifest`) | Working |
| `runtime/src/input` | Keyboard and mouse, XInput controllers, per-player devices | Working |
| `runtime/src/nui` | The virtual Kinect: the NUI API on the host, with webcam, mouse and scripted sources | Working (Dance Central 3, Kinect Adventures, Fruit Ninja Kinect) |
| `runtime/kinect_window` | The Kinect debug window (F3): its own thread and Direct2D window, game-independent; calls the game's `nui::GetDebugView` | Working |
| `ui/` | `ui_d2d` (Direct2D and DirectWrite canvas, fonts from memory; no COM, usable in the games) and `ui_wic` (pictures through WIC: any format, box art's front, JPEG and PNG; launcher and x360tool only) | Working |
| `tools/pose_bridge` | The webcam pose bridge: MediaPipe pose estimation → Kinect skeletons and images | Working; webcam use in testing |
| `tools/launcher/core` | `launcher_core`: the library, recognising copies by content (archives through 7-Zip, disc images, Games on Demand and arcade packages, nested), installing them, the text, the terms, starting games; jobs report through an observer; tested by `test_launcher_core` | Working |
| `tools/launcher/ui`, `tools/package` | The launcher's Direct2D window (box art, the compatible drawer, overlays, mouse, keyboard and controller; `--screenshot`) and the one release for every game | Working |
| `games/gow3` | Gears of War 3 configuration, hooks and patches | Playable (0.1.0) |
| `games/dc3` | Dance Central 3 configuration, Milo hooks and NUI bindings | Playable, in testing |
| `games/ka` | Kinect Adventures configuration and NUI bindings | Partly played, in testing |
| `games/fnk` | Fruit Ninja Kinect (Xbox Live Arcade) configuration and NUI bindings | Playable, in testing |

## Key design decisions

### 1. CPU: build on XenonRecomp, do not write a recompiler from scratch
XenonRecomp already handles the hard parts of PowerPC → C++: big-endian loads and stores, VMX128 via
SIMDe and hand-written SSE, the condition register, the `save/restgprlr` helpers, jump tables (with
XenonAnalyse), and mid-asm hooks. AnyX360 forks it and adds what a much larger UE3 executable needs
(faster analysis, splitting output into more translation units, and whatever bugs Gears 3 exposes).

Generated code receives a pointer to the guest memory base. Every guest address is `base + addr`,
with no page-table lookup, which is why `GuestMemory` reserves one contiguous 4 GiB block.

### 2. Kernel: high-level emulation at the import boundary
A title calls the OS only through ordinals imported from `xboxkrnl.exe` and `xam.xex`. Each import thunk
becomes a call to a C++ function in `runtime/kernel`. `x360tool xex-extract` writes the exact list a game
needs (`*.imports.txt`), so we implement only what the game uses. Xenia is the reference for semantics.

### 3. Graphics: replace UE3's Xbox RHI, not the D3D library or the GPU
On the 360, D3D is statically linked into the game and writes PM4 command packets straight to the GPU,
so there is no API boundary to intercept. There are three possible cut points:

| Cut point | Pros | Cons |
|---|---|---|
| Emulate PM4 and eDRAM (Xenia) | Generic, works for any game | Brings emulation back; slow eDRAM resolve paths |
| Hook statically linked D3D functions (Unleashed) | Fast, clean | The D3D surface is large; every game has to be mapped |
| **Replace UE3's Xbox RHI functions** | Small, well-understood interface; UE3 source structure is widely documented; maps naturally onto D3D12 | Specific to UE3, though that covers many 360 titles |

We target the **UE3 RHI layer** first. Shaders are still Xenos microcode (UE3 ships precompiled shader
caches for Xenon), and they go through the shader recompiler. Falling back to D3D hooks for individual
functions is always possible.

**Outcome:** the shipped runtime runs the title's own D3D command stream on Xenia's Direct3D 12
back end instead, which needed no per-engine mapping. Replacing the RHI is still an option for
performance work.

### 4. Audio: replace at the UE3 audio device level
The same reasoning applies: hook UE3's XAudio2-based audio device and decode XMA with FFmpeg.

**Outcome:** XMA is decoded by Xenia's decoder and played through XAudio2, below the engine.

### 5. Kinect: replace the NUI library at its API, feed it a virtual sensor
Kinect titles link the NUI library into the executable. It reads raw depth images from the camera
driver (`PsCamDeviceRequest`) and runs the skeleton tracking itself. There are again three
possible cut points:

| Cut point | Pros | Cons |
|---|---|---|
| Emulate the camera driver | Generic, the title's tracking runs unchanged | Needs a real depth camera's images; the tracking is tuned to the Kinect's sensor |
| **Replace the NUI library's entry points** | About 20 functions per title, documented by the Kinect for Windows SDK 1.x; skeletons can come from anything | Each title's entry points must be found (they are statically linked) |
| Patch the title's gameplay code | Full control | Per title, deep, fragile |

AnyX360 replaces the library's entry points with strong overrides (`X360_GUEST_FUNCTION`), bound
to a generic virtual sensor in `runtime/src/nui`. The sensor produces skeleton frames and image
streams in the XDK's layouts. A frame follows each of the source's own when it has an event (the
pose bridge), stamped with the camera's capture time, else a 30 Hz clock. Its skeletons come from a
source:

- a webcam through the pose bridge, a separate process that shares memory with the game;
- the mouse;
- scripted poses for tests.

The pose bridge is Python (MediaPipe, OpenCV) packaged with PyInstaller. Keeping it out of the game
process means its runtime, models and crashes stay separate: the game restarts it, and the job
object ends it with the game.


## Why the Xbox 360 is harder

| | Original Xbox | PS5 (AnyPS5) | Xbox 360 |
|---|---|---|---|
| CPU | x86 | x86-64 (relink only) | PowerPC, big-endian, VMX128 |
| OS calls | XBE kernel thunks | Dynamic PRX libraries | `xboxkrnl` and `xam` ordinals |
| GPU API | D3D8 (statically linked) | AGC/GNM (libraries) | D3D9-like, statically linked, raw PM4 |
| Executable | Plain XBE | SELF | AES-encrypted, LZX-compressed XEX2 |

AnyPS5 can "just relink" because guest and host share an ISA and the system libraries are a dynamic
boundary. Here every instruction has to be translated, and per-game analysis (jump tables, hooks) is
unavoidable.
