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
| `tools/x360tool` | Disc extraction, XEX2 inspection, decryption and decompression, import and section inventory | Working (synthetic tests; LZX untested until real data) |
| `runtime/` `x360/endian.h`, `x360/memory.h` | Big-endian access helpers and the 4 GiB guest address-space reservation | Base in place |
| `thirdparty/XenonRecomp` (planned) | PPC → C++ recompiler and XEX analyser | To fork |
| `thirdparty/XenosRecomp` (planned) | Xenos shader microcode → HLSL | To fork |
| `runtime/kernel` (planned) | HLE of `xboxkrnl.exe` and `xam.xex` exports | Not started |
| `runtime/gpu` (planned) | D3D12 renderer and the RHI replacement (see below) | Not started |
| `runtime/audio` (planned) | XMA decoding (FFmpeg) and an XAudio2 replacement | Not started |
| `runtime/vfs` (planned) | `game:\`, `dvd:\`, `cache:\` and save paths mapped to host folders | Not started |
| `games/gow3` | Gears of War 3 configuration, hooks and patches | Template |

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

### 4. Audio: replace at the UE3 audio device level
The same reasoning applies: hook UE3's XAudio2-based audio device and decode XMA with FFmpeg.

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
