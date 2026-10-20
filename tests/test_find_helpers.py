"""Tests for tools/scripts/find_helpers.py on synthetic PE images (no game data needed).

usage: python -I tests/test_find_helpers.py
"""

import importlib.util
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "tools", "scripts", "find_helpers.py")
spec = importlib.util.spec_from_file_location("find_helpers", SCRIPT)
find_helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(find_helpers)

BASE = 0x82000000
TEXT, TEXT_SIZE = 0x1000, 0x2000
PDATA, PDATA_SIZE = 0x3000, 0x100
DATA = 0x3100  # not executable


def make_image(text_words=None, pdata=()):
    image = bytearray(0x4000)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84, 0x01F2, 3, 0, 0, 0, 0xE0, 0x0102)
    table = 0x80 + 24 + 0xE0
    for i, (name, address, size, flags) in enumerate(
        [(b".text", TEXT, TEXT_SIZE, 0x60000020), (b".pdata", PDATA, PDATA_SIZE, 0x40000040),
         (b".data", DATA, 0x100, 0xC0000040)]):
        entry = table + i * 40
        image[entry:entry + 8] = name.ljust(8, b"\0")
        struct.pack_into("<II", image, entry + 8, size, address)
        struct.pack_into("<I", image, entry + 36, flags)
    for at, word in (text_words or {}).items():
        struct.pack_into(">I", image, at, word)
    for i, (begin, instructions) in enumerate(pdata):
        struct.pack_into(">II", image, PDATA + i * 8, begin, instructions << 8)
    return image


def plant_helpers(image, skip=()):
    addresses = {}
    at = TEXT + 0x100
    for key, pattern in find_helpers.HELPERS:
        if key not in skip:
            image[at:at + len(pattern)] = pattern
            addresses[key] = BASE + at
        at += 0x40
    return addresses


class FindHelpersTest(unittest.TestCase):
    def test_finds_every_helper(self):
        image = make_image()
        planted = plant_helpers(image)
        found, problems = find_helpers.find_helpers(bytes(image), BASE)
        self.assertEqual(problems, [])
        self.assertEqual(found, planted)

    def test_missing_and_duplicate_patterns(self):
        image = make_image()
        plant_helpers(image, skip={"restfpr_14_address"})
        pattern = dict(find_helpers.HELPERS)["savegprlr_14_address"]
        image[TEXT + 0x1000:TEXT + 0x1004] = pattern
        found, problems = find_helpers.find_helpers(bytes(image), BASE)
        self.assertEqual(len(problems), 2)
        self.assertTrue(any("restfpr_14_address" in p and "0 times" in p for p in problems))
        self.assertTrue(any("savegprlr_14_address" in p and "2 times" in p for p in problems))

    def test_patterns_outside_code_are_ignored(self):
        image = make_image()
        planted = plant_helpers(image)
        pattern = dict(find_helpers.HELPERS)["restgprlr_14_address"]
        image[DATA:DATA + 4] = pattern
        found, problems = find_helpers.find_helpers(bytes(image), BASE)
        self.assertEqual(problems, [])
        self.assertEqual(found["restgprlr_14_address"], planted["restgprlr_14_address"])

    def test_setjmp_and_longjmp_candidates(self):
        words = {}
        setjmp, longjmp = TEXT + 0x800, TEXT + 0xA00
        body = [0x7D8802A6]  # mflr r12
        body += [0xF8030000 | (r << 21) | (r * 8) for r in range(14, 32)]  # std r14..r31, d(r3)
        body += [find_helpers.LI_R3_0, 0x4E800020]
        for i, word in enumerate(body):
            words[setjmp + i * 4] = word
        restore = [0xE8030000 | (r << 21) | (r * 8) for r in range(14, 32)]  # ld r14..r31, d(r3)
        restore += [0x7D8803A6, 0x7C832378, 0x4E800020]  # mtlr r12; mr r3, r4
        for i, word in enumerate(restore):
            words[longjmp + i * 4] = word
        image = make_image(words, [(BASE + setjmp, len(body)), (BASE + longjmp, len(restore))])
        self.assertEqual(find_helpers.jmp_candidates(bytes(image), BASE), ([BASE + setjmp], [BASE + longjmp]))

    def test_leaf_setjmp_and_longjmp_through_another_register(self):
        # Fruit Ninja Kinect's: setjmp has no .pdata entry and starts with a hook (bnectr) after
        # padding; longjmp copies r3 to r7 and returns r6.
        words = {}
        other, setjmp, longjmp = TEXT + 0x700, TEXT + 0x800, TEXT + 0xA00
        words[other] = 0x4E800020  # blr: a function .pdata knows ends before the padding
        body = [0x7C0903A6, 0x4C820420, 0x7C0802A6]  # mtctr r0; bnectr; mflr r0
        body += [0xF8030000 | (r << 21) | (r * 8) for r in range(13, 32)]  # std r13..r31, d(r3)
        body += [find_helpers.LI_R3_0, 0x4E800020]
        for i, word in enumerate(body):
            words[setjmp + i * 4] = word
        restore = [0x7C671B78]  # mr r7, r3
        restore += [0xE8070000 | (r << 21) | (r * 8) for r in range(13, 32)]  # ld r13..r31, d(r7)
        restore += [0x7CA803A6, 0x7CC33378, 0x4E800020]  # mtlr r5; mr r3, r6
        for i, word in enumerate(restore):
            words[longjmp + i * 4] = word
        image = make_image(words, [(BASE + other, 1), (BASE + longjmp, len(restore))])
        self.assertEqual(find_helpers.jmp_candidates(bytes(image), BASE), ([BASE + setjmp], [BASE + longjmp]))

    def test_command_line(self):
        image = make_image()
        planted = plant_helpers(image)
        with tempfile.TemporaryDirectory() as work:
            path = os.path.join(work, "image.bin")
            with open(path, "wb") as f:
                f.write(image)
            ok = subprocess.run([sys.executable, "-I", SCRIPT, path], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            self.assertIn(f"restgprlr_14_address = 0x{planted['restgprlr_14_address']:08X}", ok.stdout)
            image[TEXT + 0x100:TEXT + 0x104] = bytes(4)  # remove one helper
            with open(path, "wb") as f:
                f.write(image)
            bad = subprocess.run([sys.executable, "-I", SCRIPT, path], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 1)
            self.assertEqual(bad.stdout, "")


if __name__ == "__main__":
    unittest.main()
