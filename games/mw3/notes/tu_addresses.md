# Modern Warfare 3: the disc's addresses in the title update

Both executables are recompiled from the title update (0.0.0.8 to 0.0.23.8). The guest addresses
given by hand in `config.toml` and `config_mp.toml`, and how each was found in the patched image.
"Shape" is the instructions with immediates, displacements and branch targets masked.

| Executable | Disc | Update | What | Found by |
|---|---|---|---|---|
| default.xex | `824C0AF0` | `824CBB90` | setjmp | the delta of the nearest uniquely matched function; 24 instructions of the same shape (22 identical); find_helpers.py's only candidate |
| default.xex | `824C1590` | `824CC5C0` | longjmp | its own shape (one match), 24/24 identical |
| default_mp.xex | `82544510` | `8255A250` | setjmp | as above; 24 of the same shape (22 identical); find_helpers.py's only candidate |
| default_mp.xex | `825452B0` | `8255AFF0` | longjmp | its own shape (one match), 23/24 identical |

The register helpers are find_helpers.py's on each patched image. Neither configuration has
`functions` entries, and the host sources have no guest addresses.
