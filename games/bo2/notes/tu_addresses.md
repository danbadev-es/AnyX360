# Black Ops II: the disc's addresses in the title update

Both executables are recompiled from the title update (0.0.0.14 to 0.0.18.14). The guest
addresses given by hand in `config.toml`, `config_mp.toml` and `src/main_mp.cpp`, and how each was
found in the patched image. "Shape" is the instructions with immediates, displacements and branch
targets masked.

| Executable | Disc | Update | What | Found by |
|---|---|---|---|---|
| default.xex | `826CA660` | `826EC320` | setjmp | the delta of the nearest uniquely matched function; 24 instructions of the same shape (22 identical); find_helpers.py's only candidate |
| default.xex | `826CAAF0` | `826EC7B0` | longjmp | its own shape (one match), 23/24 identical |
| default.xex | `82539CB8` (0x130) | `8255AC00` (0x130) | `functions` entry (the `bdz` switch) | the delta of its neighbours, 24/24; the whole function compared: the same instructions, only the jump table's 14 words (absolute addresses) differ; its default case `82539DD0` -> `8255AD18` |
| default_mp.xex | `827D8600` | `82918CA0` | setjmp | as above, 24 of the same shape (22 identical); find_helpers.py's only candidate |
| default_mp.xex | `827D9790` | `82919E50` | longjmp | its own shape (one match), 23/24 identical |
| default_mp.xex | `8261D1D8` (0x130) | `826CE198` (0x130) | `functions` entry (the same `bdz` switch) | as for the campaign's; default case `8261D2F0` -> `826CE2B0` |
| default_mp.xex | `827D5D40` | `82916330` | the title's `memcpy`, replaced by the host's (`X360_GUEST_ORIGINAL`, 0.16.1) | the delta of its neighbour; its 48 instructions over the whole image: the only match, all identical |

The register helpers are find_helpers.py's on each patched image.
