# Resident Evil 6: the disc's addresses in the title update

The game is recompiled from its title update (0.0.0.3 to 0.0.6.3). Every guest address given by
hand in `config.toml`, how it was found in the patched image and how it was checked. "Shape" is
the instructions with immediates, displacements and branch targets masked.

| Disc | Update | What | Found by |
|---|---|---|---|
| `83A65250` | `83AD6930` | setjmp | the delta of the nearest uniquely matched function; 24 instructions of the same shape (22 identical); find_helpers.py's only candidate |
| `83A64F30` | `83AD6610` | longjmp | its own shape (one match), 24/24 identical |
| `83726460` (0x25C) | `83797B20` (0x25C) | `functions` entry: maps a code to an HRESULT | the whole function's shape over the image (the only match), 151/151 instructions identical |
| `824DDB90` (0xA4) | `82502938` (0xB8) | `functions` entry: a check on a global state table | the whole function's shape over the image (the only match, at `8250294C`, 39/41 identical); the update put five instructions of its own before it (a check on another global that branches to its `li r3,1; blr`), so the entry starts there and is 0x14 longer |

The register helpers are find_helpers.py's on the patched image (each pattern occurs once).
