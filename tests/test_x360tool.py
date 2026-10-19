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

    strings = b"xboxkrnl.exe\0\0E\0xam.xex\0"  # each name 4-byte aligned; padding not always zero
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


SECTOR = 2048


def make_partition(files, at=None):
    """files: {"path": bytes}; one level of subdirectory supported. at: {"path": first sector} to
    place files at chosen sectors. Returns ({sector: bytes}, total sectors), with the volume
    descriptor at sector 32."""
    at = at or {}
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

    def place(path, v):
        if path in at:
            data[at[path]] = v
            return at[path]
        s = alloc(len(v))
        data[s] = v
        return s

    root_sector = alloc(SECTOR)
    entries = []
    for d, content in subdirs.items():
        tsec = alloc(SECTOR)
        sub = []
        for n, v in content.items():
            sub.append((n, place(d + "/" + n, v), len(v), 0x20))
        data[tsec] = build_table(sub)
        entries.append((d, tsec, SECTOR, 0x10))
    for n, v in root.items():
        entries.append((n, place(n, v), len(v), 0x20))
    data[root_sector] = build_table(entries)

    vd = bytearray(SECTOR)
    vd[0:20] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", vd, 20, root_sector, SECTOR)
    vd[0x7EC:0x800] = b"MICROSOFT*XBOX*MEDIA"
    data[32] = bytes(vd)
    total = max(s + (len(v) + SECTOR - 1) // SECTOR for s, v in data.items())
    return data, total


def make_iso(files):
    """Returns an XGD3-layout image holding `files`."""
    part = 0x02080000
    data, total = make_partition(files)
    img = bytearray(part + total * SECTOR)
    for s, v in data.items():
        img[part + s * SECTOR:part + s * SECTOR + len(v)] = v
    return bytes(img)


def content_header(magic, content_type, title_id, name, volume_type, descriptor, data_file_count=0):
    """An Xbox 360 content package header (0xB000 bytes, header size 0x971A)."""
    h = bytearray(0xB000)
    h[0:4] = magic
    struct.pack_into(">I", h, 0x340, 0x971A)
    struct.pack_into(">I", h, 0x344, content_type)
    struct.pack_into(">I", h, 0x360, title_id)
    h[0x379:0x379 + len(descriptor)] = descriptor
    struct.pack_into(">I", h, 0x39D, data_file_count)
    struct.pack_into(">I", h, 0x3A9, volume_type)
    encoded = name.encode("utf-16-be")
    h[0x411:0x411 + len(encoded)] = encoded
    struct.pack_into(">II", h, 0x1712, len(TITLE_IMAGE), len(TITLE_IMAGE))
    h[0x171A:0x171A + len(TITLE_IMAGE)] = TITLE_IMAGE
    return h


GOD_BLOCKS_PER_FILE = 0x14388
GOD_FILE_SIZE = 0xA290000


def god_map(block, start, egdf):
    """Games on Demand: (data file, offset) of a partition sector (Xenia's BlockToOffset)."""
    true_block = block - start * 2 + (2 if egdf else 0)
    file_block = true_block % GOD_BLOCKS_PER_FILE
    index = true_block // GOD_BLOCKS_PER_FILE
    l0 = file_block // 0x198 + 1
    l1 = l0 // 0xA1C4 + 1
    address = file_block * 0x800 + (l0 + l1) * 0x1000
    if address >= GOD_FILE_SIZE:
        index, address = index + 1, address % GOD_FILE_SIZE + 0x2000
    return index, address


def make_god(folder, files, egdf, at=None):
    """Writes a Games on Demand package (header + <header>.data\\DataNNNN) holding `files`.
    The data files are full size (sparse zeros) so sectors can be placed in the second one."""
    start = 17 if egdf else 16  # puts sector 32, the volume descriptor, at Data0000+0x2000
    data, total = make_partition(files, at)
    by_file = {}
    for s, v in data.items():
        for i in range(0, max(len(v), 1), SECTOR):
            index, address = god_map(s + i // SECTOR, start, egdf)
            by_file.setdefault(index, []).append((address, v[i:i + SECTOR]))
    count = max(by_file) + 1
    descriptor = bytearray(0x24)
    descriptor[0] = 0x24
    descriptor[0x18] = 0x40 if egdf else 0
    descriptor[0x19:0x1C] = total.to_bytes(3, "little")
    descriptor[0x1C:0x1F] = start.to_bytes(3, "little")
    header = os.path.join(folder, "4D5308AB", "00007000", "0123456789ABCDEF0123")
    os.makedirs(header + ".data", exist_ok=True)
    with open(header, "wb") as f:
        f.write(content_header(b"LIVE", 0x7000, 0x4D5308AB, "Test Game", 1, descriptor, count))
    for index in range(count):
        with open(os.path.join(header + ".data", "Data%04d" % index), "wb") as f:
            for address, chunk in by_file.get(index, []):
                f.seek(address)
                f.write(chunk)
            if index + 1 < count:
                f.truncate(GOD_FILE_SIZE)
    return header


def make_stfs(files):
    """A read-only STFS package (downloadable content) with `files` {"path": bytes} (one level of
    subdirectory). File blocks are chained out of order to exercise the hash-table links."""
    BLOCK = 0x1000
    entries = []  # (name, parent index, is_dir, blocks, length)
    dirs = {}
    for path in files:
        if "/" in path and path.split("/")[0] not in dirs:
            dirs[path.split("/")[0]] = len(entries)
            entries.append((path.split("/")[0], 0xFFFF, True, [], 0))
    next_block = 1  # block 0 is the file table
    for path, content in files.items():
        count = max(1, (len(content) + BLOCK - 1) // BLOCK)
        blocks = list(range(next_block, next_block + count))
        next_block += count
        if len(blocks) >= 3:  # 1, 3, 2, 4...: the chain decides the order
            blocks[1], blocks[2] = blocks[2], blocks[1]
        parent = dirs[path.split("/")[0]] if "/" in path else 0xFFFF
        entries.append((path.split("/")[-1], parent, False, blocks, len(content)))
    assert next_block <= 170, "keep the fixture under one level-0 hash table"
    table = bytearray(BLOCK)
    for i, (name, parent, is_dir, blocks, length) in enumerate(entries):
        e = i * 0x40
        table[e:e + len(name)] = name.encode()
        table[e + 0x28] = len(name) | (0x80 if is_dir else 0)
        table[e + 0x29:e + 0x2C] = len(blocks).to_bytes(3, "little")
        table[e + 0x2C:e + 0x2F] = len(blocks).to_bytes(3, "little")
        table[e + 0x2F:e + 0x32] = (blocks[0] if blocks else 0).to_bytes(3, "little")
        struct.pack_into(">HI", table, e + 0x32, parent, length)
    links = {0: 0xFFFFFF}
    content_blocks = {0: bytes(table)}
    for path, (name, parent, is_dir, blocks, length) in zip([None] * len(dirs) + list(files), entries):
        if is_dir:
            continue
        for k, b in enumerate(blocks):
            links[b] = blocks[k + 1] if k + 1 < len(blocks) else 0xFFFFFF
            content_blocks[b] = files[path][k * BLOCK:(k + 1) * BLOCK]
    # Read-only format: one level-0 hash table (physical block 0) before data blocks 0..169.
    hashes = bytearray(BLOCK)
    for b, nxt in links.items():
        struct.pack_into(">I", hashes, b * 0x18 + 0x14, 0x80000000 | nxt)
    data = bytearray(BLOCK * (next_block + 1))
    data[0:BLOCK] = hashes
    for b, v in content_blocks.items():
        data[(b + 1) * BLOCK:(b + 1) * BLOCK + len(v)] = v
    descriptor = bytearray(0x24)
    descriptor[0] = 0x24
    descriptor[2] = 1  # read-only format
    struct.pack_into("<H", descriptor, 3, 1)  # file table: one block, block 0
    struct.pack_into(">II", descriptor, 0x1C, next_block, 0)
    header = content_header(b"LIVE", 0x2, 0x4D5308AB, "Test Pack", 0, descriptor)
    header = header[:0xA000]  # the data starts at the header size (0x971A) rounded up to 0x1000
    return bytes(header) + bytes(data)


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

    # Games on Demand: one file crosses a level-0 hash table, another the end of Data0000.
    across_hash = bytes((i * 7) & 0xFF for i in range(5 * SECTOR + 100))
    across_files = bytes((i * 13) & 0xFF for i in range(6 * SECTOR))
    god_files = {"default.xex": cases["plain"], "media/a.bin": across_hash, "media/big.bin": across_files,
                 "z.txt": b"z"}
    for egdf in (False, True):
        start = 17 if egdf else 16
        # Sectors whose blocks straddle the boundaries (true block = sector - 2*start [+2 for EGDF]).
        to_true = (lambda t: t + start * 2 - (2 if egdf else 0))
        at = {"media/a.bin": to_true(0x198 * 3 - 2), "media/big.bin": to_true(GOD_BLOCKS_PER_FILE - 3)}
        folder = os.path.join(work, "god_egdf" if egdf else "god")
        header = make_god(folder, god_files, egdf, at)
        name = "god (enhanced GDF)" if egdf else "god"
        info = run(tool, "content-info", header)
        check(f"{name}: content-info", "Games on Demand" in info and "4D5308AB" in info and "Test Game" in info
              and "4 files" in info, info)
        out_dir = os.path.join(folder, "out")
        run(tool, "god-extract", header, out_dir)
        ok = all(open(os.path.join(out_dir, *p.split("/")), "rb").read() == v for p, v in god_files.items())
        check(f"{name}: god-extract content (across a hash table and two data files)", ok)

    # STFS (downloadable content): a three-block file chained out of order, a file in a folder.
    stfs_files = {"a.bin": bytes((i * 3) & 0xFF for i in range(0x2800)), "sub/b.txt": b"hello"}
    pack = os.path.join(work, "dlc.stfs")
    with open(pack, "wb") as f:
        f.write(make_stfs(stfs_files))
    info = run(tool, "content-info", pack)
    check("stfs: content-info", "downloadable content" in info and "Test Pack" in info and "2 files" in info, info)
    run(tool, "stfs-extract", pack, os.path.join(work, "dlc_out"))
    ok = all(open(os.path.join(work, "dlc_out", *p.split("/")), "rb").read() == v for p, v in stfs_files.items())
    check("stfs: stfs-extract content (chained blocks, subfolder)", ok)

    # Covers: the front of a wraparound (right 47.3 %), a front alone kept, never scaled up.
    wrap = os.path.join(work, "wrap.bmp")
    with open(wrap, "wb") as f:
        f.write(make_bmp(1495, 1000, lambda x, y: (200, 30, 30) if x < 787 else (30, 30, 200)))
    front = os.path.join(work, "front.bmp")
    with open(front, "wb") as f:
        f.write(make_bmp(400, 600, lambda x, y: (30, 160, 30)))
    for name, source, args, size in (("wraparound", wrap, [], (600, 847)),
                                     ("wraparound, no upscale", wrap, ["--size", "2000x2000"], (708, 1000)),
                                     ("front alone", front, ["--size", "2000x2000"], (400, 600))):
        out_jpg = os.path.join(work, "cover.jpg")
        out = run(tool, "cover", source, out_jpg, *args)
        got = jpeg_size(out_jpg)
        check(f"cover ({name}): {got}", got is not None and abs(got[0] - size[0]) <= 1 and abs(got[1] - size[1]) <= 1, out)

    print(f"\n{'OK' if failures == 0 else str(failures) + ' FAILURE(S)'}")
    return 1 if failures else 0


def make_bmp(width, height, color):
    """A 24-bit BMP; color(x, y) -> (r, g, b)."""
    stride = (width * 3 + 3) & ~3
    rows = bytearray()
    for y in range(height - 1, -1, -1):  # bottom-up
        row = bytearray()
        for x in range(width):
            r, g, b = color(x, y)
            row += bytes((b, g, r))
        rows += row + bytes(stride - len(row))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(rows), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(rows), 2835, 2835, 0, 0)
    return header + info + bytes(rows)


def jpeg_size(path):
    """(width, height) from a JPEG's start-of-frame marker."""
    with open(path, "rb") as f:
        data = f.read()
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            return None
        marker, length = data[i + 1], struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2):
            height, width = struct.unpack(">HH", data[i + 5:i + 9])
            return width, height
        i += 2 + length
    return None


if __name__ == "__main__":
    sys.exit(main())
