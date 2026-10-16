"""Finds the compiler's register save/restore helpers in an extracted Xbox 360 image, for the
[main] section of a XenonRecomp config.toml, and lists setjmp/longjmp candidates.

usage: python -I find_helpers.py <image.bin> [--base 0x82000000]

<image.bin> is the loaded PE image (x360tool xex-extract). Each byte pattern from the XenonRecomp
README must occur exactly once in the executable sections; otherwise nothing is printed and the
exit code is 1. setjmp/longjmp are only candidates (functions that save or restore most
non-volatile registers through r3): check them before using them.
"""

import argparse
import struct
import sys

# (config key, pattern): the first instructions of each helper.
HELPERS = [
    ("restgprlr_14_address", bytes.fromhex("e9c1ff68")),  # ld r14, -0x98(r1)
    ("savegprlr_14_address", bytes.fromhex("f9c1ff68")),  # std r14, -0x98(r1)
    ("restfpr_14_address", bytes.fromhex("c9ccff70")),  # lfd f14, -0x90(r12)
    ("savefpr_14_address", bytes.fromhex("d9ccff70")),  # stfd f14, -0x90(r12)
    ("restvmx_14_address", bytes.fromhex("3960fee07dcb60ce")),  # li r11, -0x120; lvx v14, r11, r12
    ("savevmx_14_address", bytes.fromhex("3960fee07dcb61ce")),  # li r11, -0x120; stvx v14, r11, r12
    ("restvmx_64_address", bytes.fromhex("3960fc00100b60cb")),  # li r11, -0x400; lvx128 v64, r11, r12
    ("savevmx_64_address", bytes.fromhex("3960fc00100b61cb")),  # li r11, -0x400; stvx128 v64, r11, r12
]

SCN_CNT_CODE = 0x20
SCN_MEM_EXECUTE = 0x20000000


def sections(image):
    """(name, virtual address, virtual size, characteristics) from the PE headers (little-endian)."""
    if image[:2] != b"MZ":
        raise ValueError("not a PE image (no MZ header)")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image (no PE signature)")
    count, optional_size = struct.unpack_from("<H", image, pe + 6)[0], struct.unpack_from("<H", image, pe + 20)[0]
    table = pe + 24 + optional_size
    result = []
    for i in range(count):
        entry = table + i * 40
        name = image[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
        size, address = struct.unpack_from("<II", image, entry + 8)
        flags = struct.unpack_from("<I", image, entry + 36)[0]
        result.append((name, address, size, flags))
    return result


def code_ranges(image):
    return [(a, a + s) for _, a, s, f in sections(image) if f & (SCN_CNT_CODE | SCN_MEM_EXECUTE)]


def find_helpers(image, base):
    """{key: address} and a list of problems (patterns missing or not unique)."""
    found, problems = {}, []
    ranges = code_ranges(image)
    for key, pattern in HELPERS:
        hits = []
        for start, end in ranges:
            at = image.find(pattern, start, end)
            while at >= 0:
                if at % 4 == 0:
                    hits.append(base + at)
                at = image.find(pattern, at + 1, end)
        if len(hits) == 1:
            found[key] = hits[0]
        else:
            problems.append(f"{key}: pattern {pattern.hex()} found {len(hits)} times")
    return found, problems


def functions(image, base):
    """(start, end) of every function in .pdata (begin address, packed length in instructions)."""
    result = []
    for name, address, size, _ in sections(image):
        if name != ".pdata":
            continue
        for at in range(address, address + size - 7, 8):
            begin, packed = struct.unpack_from(">II", image, at)
            length = ((packed >> 8) & 0x3FFFFF) * 4
            if begin and length:
                result.append((begin, begin + length))
    return result


LI_R3_0 = 0x38600000  # li r3, 0 (setjmp returns 0)
MR_R3_R4 = 0x7C832378  # mr r3, r4 (longjmp makes setjmp return its second argument)


def jmp_candidates(image, base):
    """Short functions that store (setjmp) or load (longjmp) at least 18 GPRs/FPRs relative to r3,
    with the link register and the return value setjmp/longjmp have."""
    setjmp, longjmp = [], []
    for start, end in functions(image, base):
        if end - start > 0x200:
            continue
        stores = loads = 0
        words = set()
        for pc in range(start, end, 4):
            word = struct.unpack_from(">I", image, pc - base)[0]
            words.add(word & 0xFC1FFFFF if word & 0xFC1FFFFF in (0x7C0802A6, 0x7C0803A6) else word)
            op, ra = word >> 26, (word >> 16) & 31
            if ra == 3 and ((op == 62 and word & 3 == 0) or op == 54):  # std / stfd rS, d(r3)
                stores += 1
            if ra == 3 and ((op == 58 and word & 3 == 0) or op == 50):  # ld / lfd rT, d(r3)
                loads += 1
        if stores >= 18 and 0x7C0802A6 in words and LI_R3_0 in words:  # mflr
            setjmp.append(start)
        if loads >= 18 and 0x7C0803A6 in words and MR_R3_R4 in words:  # mtlr
            longjmp.append(start)
    return setjmp, longjmp


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image")
    parser.add_argument("--base", type=lambda v: int(v, 0), default=0x82000000)
    args = parser.parse_args(argv)
    with open(args.image, "rb") as f:
        image = f.read()
    found, problems = find_helpers(image, args.base)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    for key, _ in HELPERS:
        print(f"{key} = 0x{found[key]:08X}")
    setjmp, longjmp = jmp_candidates(image, args.base)
    print(f"# setjmp candidates: {', '.join(f'0x{a:08X}' for a in setjmp) or 'none'}")
    print(f"# longjmp candidates: {', '.join(f'0x{a:08X}' for a in longjmp) or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
