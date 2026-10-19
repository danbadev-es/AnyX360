"""End-to-end tests for x360tool using synthetic fixtures (no game data needed).

Builds a fake XGD3 disc image and fake XEX2 executables (uncompressed, basic-compressed,
AES-encrypted with the devkit key and with a test "retail" key), runs x360tool against them and
checks the results. The LZX path is only exercised with real game data.

usage: python -I tests/test_x360tool.py <path/to/x360tool.exe> <work_dir>
"""

import os
import struct
import subprocess
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

BASE = 0x82000000
ENTRY = 0x82001000
IMAGE_SIZE = 0x3000
TEST_RETAIL_KEY = bytes(range(0x10, 0x20))  # NOT a real key, test-only
SESSION_KEY = bytes(range(0xA0, 0xB0))
TITLE_IMAGE = bytes.fromhex("89504e470d0a1a0a") + bytes(range(40))  # stands in for the title's PNG icon
XDBF_AT = 0x2010  # image offset of the title's XDBF resource (in .rdata)


def make_xdbf():
    """XDBF with a string table (section 3) and two images (section 2): another one and the title's."""
    entries = [(3, 0x0000000000000001, b"XSTR" + bytes(12)), (2, 0x10, b"other image"), (2, 0x8000, TITLE_IMAGE)]
    table, data = b"", b""
    for section, entry_id, blob in entries:
        table += struct.pack(">HQII", section, entry_id, len(data), len(blob))
        data += blob
    free = struct.pack(">II", len(data), 0)
    return b"XDBF" + struct.pack(">IIIII", 0x10000, len(entries), len(entries), 1, 1) + table + free + data


def aes_ecb_encrypt(key, data):
    e = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return e.update(data) + e.finalize()


def aes_cbc_encrypt(key, data):
    e = Cipher(algorithms.AES(key), modes.CBC(bytes(16))).encryptor()
    return e.update(data) + e.finalize()


def make_pe():
    img = bytearray(IMAGE_SIZE)
    img[0:2] = b"MZ"
    struct.pack_into("<I", img, 0x3C, 0x80)
    img[0x80:0x84] = b"PE\0\0"
    # IMAGE_FILE_HEADER: machine, sections, timestamp, symtab, nsyms, opt size, characteristics
    struct.pack_into("<HHIIIHH", img, 0x84, 0x01F2, 2, 0, 0, 0, 0xE0, 0x0102)
    sec = 0x84 + 20 + 0xE0
    struct.pack_into("<8sIIII12xI", img, sec, b".text", 0x1000, 0x1000, 0x1000, 0x1000, 0x60000020)
    struct.pack_into("<8sIIII12xI", img, sec + 40, b".rdata", 0x1000, 0x2000, 0x1000, 0x2000, 0x40000040)
    img[0x1000:0x1010] = bytes.fromhex("7C0802A6 9421FFF0 38600000 4E800020".replace(" ", ""))
    # Import records (big-endian): var + thunk for ordinal 0x0A, thunk for 0x1B (xboxkrnl), thunk 0x2C (xam)
    for i, rec in enumerate([0x0000000A, 0x0100000A, 0x0100001B, 0x0101002C]):
        struct.pack_into(">I", img, 0x2000 + i * 4, rec)
    xdbf = make_xdbf()
    img[XDBF_AT:XDBF_AT + len(xdbf)] = xdbf
    return bytes(img)


def make_xex(pe, compression, key):
    """compression: 0 none, 1 basic. key: None (unencrypted) or 16-byte key."""
    if compression == 1:
        # data/zero pairs that rebuild the image; data sizes are multiples of 16 for AES
        blocks = [(0x200, 0xE00), (0x100, 0xF00), (0x200, 0xE00)]
        payload = b"".join(pe[sum(d + z for d, z in blocks[:i]):][:d] for i, (d, z) in enumerate(blocks))
        ffi = struct.pack(">IHH", 8 + 8 * len(blocks), 1 if key else 0, 1)
        ffi += b"".join(struct.pack(">II", d, z) for d, z in blocks)
    else:
        payload = pe
        ffi = struct.pack(">IHH", 8, 1 if key else 0, 0)
    if key:
        payload = aes_cbc_encrypt(SESSION_KEY, payload)

    strings = b"xboxkrnl.exe\0xam.xex\0\0\0\0"  # padded to 4
    def lib(idx, records):
        body = struct.pack(">I20sIIIHH", 0, bytes(20), 0x12345678, 0x20000A00 | (idx << 8) * 0, 0x20000000, idx, len(records))
        body += b"".join(struct.pack(">I", r) for r in records)
        return struct.pack(">I", len(body)) + body[4:]
    libs = lib(0, [BASE + 0x2000, BASE + 0x2004, BASE + 0x2008]) + lib(1, [BASE + 0x200C])
    imports = struct.pack(">III", 12 + len(strings) + len(libs), len(strings), 2) + strings + libs

    exec_info = struct.pack(">IIIIBBBBI", 0xDEADBEEF, 0x20000A01, 0x20000A00, 0x4D5308AB, 0, 0, 1, 1, 0x4D5308AB)
    pe_name = b"gears_test.exe\0\0"
    pe_name = struct.pack(">I", 4 + len(pe_name)) + pe_name
    statics = struct.pack(">I", 4 + 16) + struct.pack(">8sHHHH", b"XAPILIB", 2, 0, 20871, 0x4001)

    # RESOURCE_INFO: the title's XDBF, named after the title ID.
    resources = struct.pack(">8sII", b"4D5308AB", BASE + XDBF_AT, len(make_xdbf()))
    resources = struct.pack(">I", 4 + len(resources)) + resources

    records = [  # (key, data or None for inline value, inline value)
        (0x000002FF, resources), (0x000003FF, ffi), (0x00010100, ENTRY), (0x00010201, BASE), (0x000103FF, imports),
        (0x000183FF, pe_name), (0x000200FF, statics), (0x00040006, exec_info),
    ]
    header_len = 0x18 + 8 * len(records)
    blob = bytearray()
    table = []
    for k, v in records:
        if isinstance(v, int):
            table.append((k, v))
        else:
            table.append((k, header_len + len(blob)))
            blob += v
            blob += bytes((-len(blob)) % 4)
    security_offset = header_len + len(blob)

    sec = bytearray(0x184 + 24)
    struct.pack_into(">II", sec, 0, len(sec), IMAGE_SIZE)
    struct.pack_into(">I", sec, 0x110, BASE)
    sec[0x150:0x160] = aes_ecb_encrypt(key, SESSION_KEY) if key else bytes(16)
    struct.pack_into(">I", sec, 0x180, 1)
    struct.pack_into(">I", sec, 0x184, (3 << 4) | 1)  # 3 pages of code

    pe_offset = (security_offset + len(sec) + 0xFFF) & ~0xFFF
    out = bytearray(b"XEX2" + struct.pack(">IIIII", 1, pe_offset, 0, security_offset, len(table)))
    for k, v in table:
        out += struct.pack(">II", k, v)
    out += blob + sec
    out += bytes(pe_offset - len(out))
    out += payload
    return bytes(out)


def make_iso(files):
    """files: {"path": bytes}; one level of subdirectory supported. Returns XGD3-layout image."""
    SECTOR = 2048
    part = 0x02080000
    root = {k: v for k, v in files.items() if "/" not in k}
    subdirs = {}
    for k, v in files.items():
        if "/" in k:
            d, n = k.split("/", 1)
            subdirs.setdefault(d, {})[n] = v

    next_sector = [33]
    def alloc(nbytes):
        s = next_sector[0]
        next_sector[0] += max(1, (nbytes + SECTOR - 1) // SECTOR)
        return s

    data = {}
    def build_table(entries):
        # entries: list of (name, sector, size, attr); chain them through the right pointer
        raw = bytearray()
        offs = []
        for name, *_ in entries:
            offs.append(len(raw))
            raw += bytes((14 + len(name) + 3) & ~3)
        for i, (name, sector, size, attr) in enumerate(entries):
            right = offs[i + 1] // 4 if i + 1 < len(entries) else 0
            struct.pack_into("<HHIIBB", raw, offs[i], 0, right, sector, size, attr, len(name))
            raw[offs[i] + 14:offs[i] + 14 + len(name)] = name.encode()
        raw += b"\xff" * ((-len(raw)) % SECTOR)
        return bytes(raw)

    root_sector = alloc(SECTOR)
    entries = []
    for d, content in subdirs.items():
        tsec = alloc(SECTOR)
        sub = []
        for n, v in content.items():
            fs_ = alloc(len(v))
            data[fs_] = v
            sub.append((n, fs_, len(v), 0x20))
        data[tsec] = build_table(sub)
        entries.append((d, tsec, SECTOR, 0x10))
    for n, v in root.items():
        fs_ = alloc(len(v))
        data[fs_] = v
        entries.append((n, fs_, len(v), 0x20))
    data[root_sector] = build_table(entries)

    img = bytearray(part + next_sector[0] * SECTOR)
    vd = part + 32 * SECTOR
    img[vd:vd + 20] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", img, vd + 20, root_sector, SECTOR)
    img[vd + 0x7EC:vd + 0x800] = b"MICROSOFT*XBOX*MEDIA"
    for s, v in data.items():
        img[part + s * SECTOR:part + s * SECTOR + len(v)] = v
    return bytes(img)


failures = 0


def run(tool, *args, expect_ok=True):
    r = subprocess.run([tool, *args], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if (r.returncode == 0) != expect_ok:
        raise AssertionError(f"x360tool {' '.join(args)} -> exit {r.returncode}\n{out}")
    return out


def check(name, cond, detail=""):
    global failures
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        failures += 1
        if detail:
            print(detail)


def main():
    tool, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    pe = make_pe()

    cases = {
        "plain": make_xex(pe, 0, None),
        "basic_devkit": make_xex(pe, 1, bytes(16)),
        "basic_retail": make_xex(pe, 1, TEST_RETAIL_KEY),
    }
    key_file = os.path.join(work, "test.key")
    with open(key_file, "w") as f:
        f.write(TEST_RETAIL_KEY.hex())

    for name, xex in cases.items():
        p = os.path.join(work, name + ".xex")
        with open(p, "wb") as f:
            f.write(xex)
        info = run(tool, "xex-info", p)
        check(f"{name}: xex-info title id", "4D5308AB" in info, info)
        check(f"{name}: xex-info entry point", "0x82001000" in info, info)
        check(f"{name}: xex-info imports", "xboxkrnl.exe" in info and "xam.xex" in info, info)
        check(f"{name}: xex-info static libs", "XAPILIB  2.0.20871" in info, info)

        out_bin = os.path.join(work, name + ".bin")
        args = ["xex-extract", p, out_bin] + (["--key", key_file] if name == "basic_retail" else [])
        out = run(tool, *args)
        with open(out_bin, "rb") as f:
            check(f"{name}: extracted image matches", f.read() == pe, out)
        expected_key = {"plain": "key: none", "basic_devkit": "key: devkit", "basic_retail": "key: retail"}[name]
        check(f"{name}: key used", expected_key in out, out)
        with open(out_bin + ".imports.txt") as f:
            imports = f.read()
        check(f"{name}: import ordinals", "xboxkrnl.exe  0x00A  thunk" in imports and "xam.xex  0x02C  thunk" in imports, imports)
        with open(out_bin + ".sections.txt") as f:
            sections = f.read()
        check(f"{name}: sections", ".text" in sections and "0x82001000" in sections and "[code]" in sections, sections)

        out_png = os.path.join(work, name + ".png")
        out = run(tool, *(["xex-icon", p, out_png] + args[3:]))
        with open(out_png, "rb") as f:
            check(f"{name}: xex-icon writes the title image", f.read() == TITLE_IMAGE, out)

    # Encrypted with a retail key but no key given -> clear error
    out = run(tool, "xex-extract", os.path.join(work, "basic_retail.xex"), os.path.join(work, "x.bin"),
              "--key", os.path.join(work, "missing.key"), expect_ok=False)
    check("missing key file reported", "key file not found" in out, out)

    # ISO
    iso = os.path.join(work, "test.iso")
    with open(iso, "wb") as f:
        f.write(make_iso({"default.xex": cases["plain"], "media/a.bin": b"hello" * 1000, "z.txt": b"z"}))
    info = run(tool, "iso-info", iso)
    check("iso-info detects XGD3", "XGD3" in info and "3 files" in info, info)
    listing = run(tool, "iso-list", iso)
    check("iso-list paths", "media/a.bin" in listing and "default.xex" in listing, listing)
    out_dir = os.path.join(work, "iso_out")
    run(tool, "iso-extract", iso, out_dir)
    with open(os.path.join(out_dir, "media", "a.bin"), "rb") as f:
        check("iso-extract content", f.read() == b"hello" * 1000)
    run(tool, "iso-extract", iso, os.path.join(work, "iso_one"), "default.xex")
    check("iso-extract single file", os.path.exists(os.path.join(work, "iso_one", "default.xex"))
          and not os.path.exists(os.path.join(work, "iso_one", "z.txt")))

    print(f"\n{'OK' if failures == 0 else str(failures) + ' FAILURE(S)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
