# thirdparty/

Planned dependencies, added as git submodules (forks under the project's account) in Phase 2:

| Project | License | Use |
|---|---|---|
| [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | MIT | PowerPC → C++ recompiler, XenonAnalyse (jump tables) |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) | MIT | Xenos shader microcode → HLSL |
| [SDL3](https://github.com/libsdl-org/SDL) | Zlib | Window, input, gamepads |
| FFmpeg (libavcodec) | LGPL-2.1 | XMA audio decoding |

Already fetched at configure time by CMake (not vendored):

| Project | License | Use |
|---|---|---|
| [libmspack](https://github.com/kyz/libmspack) v1.11 (`lzxd.c` only) | LGPL-2.1 | LZX decompression of XEX images |

Reference only (no code copied): [Xenia](https://github.com/xenia-project/xenia) (BSD) for kernel
semantics and file formats.
