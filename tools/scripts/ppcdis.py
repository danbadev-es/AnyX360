"""Minimal PowerPC disassembler for quick inspection of an extracted basefile (subset of opcodes).

usage: python -I ppcdis.py <basefile.bin> <image_base hex> <start hex> <count>
"""
import struct
import sys


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def dis(w, pc):
    op = w >> 26
    rt, ra, rb = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    imm = w & 0xFFFF
    if op == 15: return f"lis r{rt}, 0x{imm:X}" if ra == 0 else f"addis r{rt}, r{ra}, 0x{imm:X}"
    if op == 14: return f"li r{rt}, {s16(imm)}" if ra == 0 else f"addi r{rt}, r{ra}, {s16(imm)}"
    if op == 24: return f"ori r{ra}, r{rt}, 0x{imm:X}"
    if op == 10: return f"cmplwi cr{rt >> 2}, r{ra}, {imm}"
    if op == 11: return f"cmpwi cr{rt >> 2}, r{ra}, {s16(imm)}"
    if op == 32: return f"lwz r{rt}, {s16(imm)}(r{ra})"
    if op == 34: return f"lbz r{rt}, {s16(imm)}(r{ra})"
    if op == 36: return f"stw r{rt}, {s16(imm)}(r{ra})"
    if op == 58: return f"ld r{rt}, {s16(imm & ~3)}(r{ra})"
    if op == 62: return f"std r{rt}, {s16(imm & ~3)}(r{ra})"
    if op == 21:
        sh, mb, me = rb, (w >> 6) & 31, (w >> 1) & 31
        return f"rlwinm r{ra}, r{rt}, {sh}, {mb}, {me}"
    if op == 18:
        off = w & 0x03FFFFFC
        if off & 0x02000000: off -= 0x04000000
        return f"b{'l' if w & 1 else ''} 0x{pc + off:X}"
    if op == 16:
        bo, bi = rt, ra
        off = s16(w & 0xFFFC)
        return f"bc {bo}, cr{bi >> 2}.{['lt','gt','eq','so'][bi & 3]}, 0x{pc + off:X}"
    if op == 19:
        xo = (w >> 1) & 0x3FF
        if xo == 528: return "bctr" if not w & 1 else "bctrl"
        if xo == 16: return "blr" if (w >> 21) == 0x14 << 0 or True else "bclr"
    if op == 31:
        xo = (w >> 1) & 0x3FF
        names = {23: "lwzx", 87: "lbzx", 279: "lhzx", 266: "add", 40: "subf", 444: "or"}
        if xo == 467: return f"mtspr {((rb << 5) | ra)}, r{rt}" if ((rb << 5) | ra) != 9 else f"mtctr r{rt}"
        if xo == 339: return f"mfspr r{rt}, {((rb << 5) | ra)}"
        if xo in names:
            if xo == 444: return f"or r{ra}, r{rt}, r{rb}"
            return f"{names[xo]} r{rt}, r{ra}, r{rb}"
        return f"x31.{xo} r{rt}, r{ra}, r{rb}"
    return f".long 0x{w:08X}  (op{op})"


def main():
    path, base, start, count = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4])
    data = open(path, "rb").read()
    for i in range(count):
        pc = start + i * 4
        w = struct.unpack_from(">I", data, pc - base)[0]
        print(f"{pc:08X}  {w:08X}  {dis(w, pc)}")


if __name__ == "__main__":
    main()
