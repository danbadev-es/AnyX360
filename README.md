# AnyX360

Native PC ports of Xbox 360 games through **static recompilation**: PowerPC code is translated ahead of
time into C++, compiled with Clang, and run on a runtime that re-implements the console's OS, GPU and
audio. There is no emulator at runtime.

First target: **Gears of War 3**.

> **Status: Phase 2 (CPU recompilation).** The whole Gears of War 3 executable recompiles to C++ and
> builds. Nothing runs yet (no loader, kernel or GPU). See [docs/ROADMAP.md](docs/ROADMAP.md).

Inspired by [BLACK-XBERecomp](https://github.com/danielbarretoes/BLACK-XBERecomp) (original Xbox),
[UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) / XenonRecomp (Xbox 360) and
[AnyPS5](https://github.com/boykopovar/AnyPS5).

## Legal

- This repository contains **no game code, assets or console keys**, and never will. Note that the
  third-party XenonRecomp submodule (like Xenia) embeds the publicly known XEX2 retail key in its
  source to load executables. AnyX360's own tools read the key from `keys/` instead.
- You need your own legally obtained copy of the game. Disc images go in `Raw/`, which is git-ignored,
  along with `*.iso`, `*.xex`, `extracted/`, `keys/` and all recompiler output (which is derived from the
  game's code).
- Not affiliated with or endorsed by Microsoft, Xbox Game Studios, The Coalition or Epic Games.

## Building

Requirements: Windows 10/11 x64, Visual Studio 2022 or later with *Desktop development with C++* and
*C++ Clang tools*, Python 3 with `cryptography` (tests only).

```bat
git clone --recursive https://github.com/danielbarretoes/AnyX360
build.bat            :: release build
build.bat debug
ctest --test-dir build -C Release
```

This builds `x360tool`, the forked XenonAnalyse, XenonRecomp and XenosRecomp, and (once its
`recomp/` folder has been generated) the game. The game workflow is in [docs/GEARS3.md](docs/GEARS3.md).

## Tools

`x360tool` (in `build\tools\x360tool\Release\`):

| Command | What it does |
|---|---|
| `iso-info <image>` | Detects the layout (xiso, XGD2, XGD3) and counts the files |
| `iso-list <image>` | Lists every file in the game partition |
| `iso-extract <image> <dir> [paths…]` | Extracts all files, or only the given ones |
| `xex-info <xex>` | Headers, title ID, version, compression, imports, XDK libraries, page map |
| `xex-extract <xex> <out.bin> [--key f]` | Decrypts and decompresses the PE image; writes `.imports.txt` and `.sections.txt` |

Retail executables are encrypted; see [keys/README.md](keys/README.md).

## Layout

```
docs/            Architecture, roadmap, format notes, per-game notes
tools/x360tool/  Disc and XEX tooling
runtime/         Shared runtime (guest memory, endian helpers; kernel/GPU/audio to come)
games/gow3/      Gears of War 3 recompiler config, hooks and patches
tests/           Synthetic end-to-end tests (no game data)
thirdparty/      External dependencies (see its README)
Raw/             Your disc images (git-ignored)
```

Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
