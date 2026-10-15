# Roadmap

The first target is **Gears of War 3** (see [GEARS3.md](GEARS3.md)). Milestones are measured by what
the game does, not by lines of code.

## Phase 0: Tooling (done)
- [x] Repository layout, CMake + clang-cl + Ninja build, `build.bat`
- [x] `x360tool iso-*`: XDVDFS extraction (stripped xiso, XGD2, XGD3)
- [x] `x360tool xex-info`: XEX2 headers, execution info, imports, static SDK libraries, page map
- [x] `x360tool xex-extract`: AES-128 (retail or devkit key) + none/basic/LZX decompression → PE image,
      plus `*.imports.txt` and `*.sections.txt`
- [x] End-to-end tests on synthetic fixtures (`ctest`)

## Phase 1: Inventory (needs the ISO)
- [ ] Extract the disc into `extracted/gow3/` and record the layout and file list
- [ ] `xex-info` on `default.xex`: title ID, version, compression, SDK (XDK) build
- [ ] `xex-extract`: first real LZX run, verify the PE image and sections
- [ ] Count kernel and XAM imports → initial HLE workload estimate
- [ ] Load the basefile into Ghidra (big-endian PowerPC, 64-bit, VMX128 extension) and estimate the function count
- [ ] Identify the UE3 build (engine version strings) and locate the Xenon RHI and audio device functions
- [ ] Decide which Title Update (if any) to target

## Phase 2: CPU recompilation
- [ ] Fork XenonRecomp into `thirdparty/` and build it with this tree
- [ ] Locate `__savegprlr_14` / `__restgprlr_14`, FPR and VMX helpers, `setjmp` / `longjmp`
- [ ] Run XenonAnalyse → jump table TOML; review the failures by hand
- [ ] Recompile the full executable; get the generated C++ to compile and link (stub every import)

## Phase 3: Boot (kernel HLE)
- [ ] Loader: map sections, apply imports, set up TLS and the main thread stack
- [ ] Threads (`ExCreateThread`), TLS, critical sections, events, semaphores, `KeWaitFor*`
- [ ] Memory: `NtAllocateVirtualMemory`, `MmAllocatePhysicalMemoryEx`, heaps
- [ ] File system: `NtCreateFile`, `NtReadFile`, `NtQueryInformationFile`, directory enumeration
- [ ] XAM stubs: user sign-in, content enumeration, UI calls, achievements (no-op)
- [ ] Milestone: **the game reaches its main loop headless, with a log of unimplemented imports**

## Phase 4: Graphics
- [ ] Shader recompiler: extract Xenon shader caches and translate them to HLSL/DXIL
- [ ] D3D12 RHI replacement: resources, render targets (no eDRAM model needed at this level), draws
- [ ] Texture formats: untiling, big-endian formats, DXT/CTX1/DXN conversion
- [ ] Milestone: **menus render**, then campaign gameplay

## Phase 5: Audio, input, saves
- [ ] XMA → PCM (FFmpeg), XAudio2 replacement
- [ ] XInput → SDL3 gamepads; keyboard and mouse
- [ ] Save data in `%LOCALAPPDATA%`

## Phase 6: PC features
- [ ] Arbitrary resolution and aspect ratio, uncapped frame rate (fix timing-dependent logic)
- [ ] MSAA/SMAA, anisotropic filtering, configurable FOV
- [ ] Local split-screen and Horde; LAN multiplayer is a stretch goal (Xbox Live is gone)
