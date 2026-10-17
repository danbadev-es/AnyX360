"""Reconnaissance of a library linked into an Xbox 360 title (written for the Kinect NUI library):
lists its entry points called from the rest of the game, with evidence for naming them.

usage: python -I nui_recon.py <image.bin> <xex> <xenia kernel dir> --range 0x82806000 0x82870000
                              [--callers 0x822D0000 0x827B0000] [--base 0x82000000]
                              [--json out.json] [--markdown out.md]

<image.bin> is the loaded PE image (x360tool xex-extract) and <xex> the original default.xex (for its
import table). For every function inside --range that is called from outside it (from --callers
only, when given: the game's own code rather than other libraries), it reports:
  - the call sites and the calling functions;
  - strings loaded near each call (Harmonix wraps NUI calls in profiling scopes named after them);
  - how the arguments r3-r10 / f1-f4 are set up before the call (constant, stack buffer, field,
    static address, copy of a register) and how the result is used afterwards (HRESULT test,
    BOOL, float, value);
  - the kernel/XAM imports reached from it inside the range, which say what it talks to (camera,
    motor, microphone, identity...), or "pure" when it reaches none;
  - functions of the range referenced from data (vtables, callbacks).
Only addresses, shapes and evidence are written: no code of the game.
"""

import argparse
import bisect
import collections
import json
import re
import struct
import sys

BL_SCOPE_WINDOW = 12   # instructions searched backwards from a call for setup and strings
AFTER_WINDOW = 4       # instructions after a call examined for the use of the result


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def be16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def s16(value):
    return value - 0x10000 if value & 0x8000 else value


class Image:
    def __init__(self, data, base):
        self.data, self.base = data, base
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count, optional_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            entry = pe + 24 + optional_size + i * 40
            name = data[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
            size, address = struct.unpack_from("<II", data, entry + 8)
            flags = struct.unpack_from("<I", data, entry + 36)[0]
            self.sections.append((name, base + address, size, flags))
        self.functions = []
        for name, start, size, _ in self.sections:
            if name == ".pdata":
                for at in range(start, start + size - 7, 8):
                    begin, packed = be32(data, at - base), be32(data, at - base + 4)
                    length = ((packed >> 8) & 0x3FFFFF) * 4
                    if begin and length:
                        self.functions.append((begin, begin + length))
        self.functions.sort()
        self.starts = [f[0] for f in self.functions]

    def word(self, address):
        return be32(self.data, address - self.base)

    def code_ranges(self):
        return [(s, s + n) for _, s, n, f in self.sections if f & 0x20000020]

    def data_ranges(self):
        return [(s, s + n) for name, s, n, f in self.sections if not f & 0x20000020 and name not in (".pdata", ".reloc")]

    def function_of(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and self.functions[i][0] <= address < self.functions[i][1]:
            return self.functions[i]
        return None

    def string_at(self, address):
        offset = address - self.base
        if not 0 <= offset < len(self.data):
            return None
        end = self.data.find(b"\0", offset, offset + 200)
        if end <= offset:
            return None
        text = self.data[offset:end]
        if len(text) >= 4 and all(32 <= c < 127 for c in text):
            return text.decode("ascii")
        return None


def imports(xex, image, kernel_dir):
    """{thunk address: name} of the imported functions."""
    names = {}
    for module, path in (("xam.xex", "xam/xam_table.inc"), ("xboxkrnl.exe", "xboxkrnl/xboxkrnl_table.inc")):
        with open(f"{kernel_dir}/{path}", encoding="utf-8") as f:
            names[module] = {int(m.group(1), 16): m.group(2)
                             for m in re.finditer(r"XE_EXPORT\(\w+,\s*0x([0-9A-Fa-f]+),\s*(\w+),", f.read())}
    header_count = be32(xex, 0x14)
    offset = next(be32(xex, 0x1C + i * 8) for i in range(header_count) if be32(xex, 0x18 + i * 8) == 0x000103FF)
    table_size, table_count = be32(xex, offset + 4), be32(xex, offset + 8)
    strings = [s.decode() for s in xex[offset + 12:offset + 12 + table_size].split(b"\0") if s][:table_count]
    library, end, thunks = offset + 12 + table_size, offset + be32(xex, offset), {}
    while library < end:
        module = strings[be16(xex, library + 0x24)]
        for r in range(be16(xex, library + 0x26)):
            record = be32(xex, library + 0x28 + r * 4)
            value = image.word(record)
            if value >> 24 == 1:
                thunks[record] = names.get(module, {}).get(value & 0xFFFF, f"{module}#{value & 0xFFFF:X}")
        library += be32(xex, library)
    return thunks


def branch_target(word, pc):
    """Target of a `b`/`bl` (relative), else None."""
    if word >> 26 != 18 or word & 2:
        return None
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    return (pc + displacement) & 0xFFFFFFFF


def describe_setup(image, call, register, kind="r"):
    """How register r<n> (or f<n>) gets its value before the call at `call`."""
    for pc in range(call - 4, call - 4 * BL_SCOPE_WINDOW, -4):
        w = image.word(pc)
        op, rt, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if branch_target(w, pc) is not None or op == 16 or (op == 19 and (w >> 1) & 0x3FF in (16, 528)):
            break  # earlier code may not run before the call
        if kind == "f":
            if op in (48, 50) and rt == register:  # lfs / lfd
                return f"loaded from {s16(w & 0xFFFF)}(r{ra})"
            if op == 63 and rt == register and (w >> 1) & 0x3FF == 72:  # fmr
                return f"copy of f{(w >> 11) & 31}"
            continue
        if op == 14 and rt == register:  # li / addi
            if ra == 0:
                return f"constant {s16(w & 0xFFFF)} (0x{w & 0xFFFF:X})"
            if ra in (1, 31):
                return f"stack buffer r{ra}{s16(w & 0xFFFF):+d}"
            base = static_base(image, pc, ra)
            if base is not None:
                address = (base + s16(w & 0xFFFF)) & 0xFFFFFFFF
                text = image.string_at(address)
                return f"static 0x{address:08X}" + (f' "{text}"' if text else "")
            return f"r{ra}{s16(w & 0xFFFF):+d}"
        if op == 15 and rt == register and ra == 0:
            return f"constant 0x{(w & 0xFFFF) << 16:08X}"
        if op == 32 and rt == register:  # lwz
            return f"field {s16(w & 0xFFFF)}(r{ra})"
        if op == 31 and (w >> 1) & 0x3FF == 444 and ra == register and rt == (w >> 11) & 31:  # mr
            return f"copy of r{rt}"
    return "unchanged (caller's value)"


def static_base(image, pc, register):
    """The lis value of `register` set shortly before pc, if any."""
    for back in range(pc - 4, pc - 4 * BL_SCOPE_WINDOW, -4):
        w = image.word(back)
        if w >> 26 == 15 and (w >> 16) & 31 == 0 and (w >> 21) & 31 == register:
            return (w & 0xFFFF) << 16
    return None


def describe_result(image, call):
    uses = []
    for pc in range(call + 4, call + 4 * (AFTER_WINDOW + 1), 4):
        w = image.word(pc)
        op, rt, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op in (10, 11) and ra == 3:
            uses.append(f"{'cmplwi' if op == 10 else 'cmpwi'} r3,{s16(w & 0xFFFF)}")
        elif op == 21 and rt == 3 and ((w >> 6) & 31, (w >> 1) & 31) == (24, 31):
            uses.append("low byte (BOOL)")
        elif op in (52, 54) and rt == 1:
            uses.append("stores f1 (float result)")
        elif op == 36 and rt == 3:
            uses.append(f"stores r3 to {s16(w & 0xFFFF)}(r{ra})")
        elif op == 31 and (w >> 1) & 0x3FF == 444 and rt == 3 and (w >> 11) & 31 == 3:
            uses.append(f"r{ra} = r3")
        elif branch_target(w, pc) is not None:
            break
    return ", ".join(uses) or "unused"


def scope_strings(image, call):
    """Strings whose address is built (lis + addi) shortly before the call."""
    found = []
    for pc in range(call - 4, call - 4 * BL_SCOPE_WINDOW, -4):
        w = image.word(pc)
        if w >> 26 == 14 and (w >> 16) & 31 != 0:
            base = static_base(image, pc, (w >> 16) & 31)
            if base is not None:
                text = image.string_at((base + s16(w & 0xFFFF)) & 0xFFFFFFFF)
                if text and text not in found:
                    found.append(text)
    return found


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image")
    parser.add_argument("xex")
    parser.add_argument("kernel_dir")
    parser.add_argument("--range", nargs=2, type=lambda v: int(v, 0), required=True)
    parser.add_argument("--callers", nargs=2, type=lambda v: int(v, 0))
    parser.add_argument("--base", type=lambda v: int(v, 0), default=0x82000000)
    parser.add_argument("--json")
    parser.add_argument("--markdown")
    args = parser.parse_args(argv)
    low, high = args.range

    with open(args.image, "rb") as f:
        image = Image(f.read(), args.base)
    with open(args.xex, "rb") as f:
        thunks = imports(f.read(), image, args.kernel_dir)

    # Every direct call in the code.
    calls = []  # (site, target)
    for start, end in image.code_ranges():
        for pc in range(start, end, 4):
            w = image.word(pc)
            if w >> 26 == 18 and w & 3 == 1:
                calls.append((pc, branch_target(w, pc)))
    callees = collections.defaultdict(set)
    for site, target in calls:
        function = image.function_of(site)
        if function:
            callees[function[0]].add(target)

    def reached_imports(entry):
        seen, todo, found = {entry}, [entry], set()
        while todo:
            for target in callees.get(todo.pop(), ()):
                if target in thunks:
                    found.add(thunks[target])
                elif low <= target < high and target not in seen:
                    seen.add(target)
                    todo.append(target)
        return sorted(found)

    entries = collections.defaultdict(list)
    callers_low, callers_high = args.callers or (0, 0)
    for site, target in calls:
        if low <= target < high and not low <= site < high:
            if not args.callers or callers_low <= site < callers_high:
                entries[target].append(site)

    # Range functions referenced from data (vtables, callbacks, initializers).
    starts = {s for s, _ in image.functions if low <= s < high}
    from_data = collections.defaultdict(list)
    for start, end in image.data_ranges():
        for at in range(start, end - 3, 4):
            value = image.word(at)
            if value in starts:
                from_data[value].append(at)

    report = []
    for target in sorted(entries):
        function = image.function_of(target)
        entry = {
            "address": f"0x{target:08X}",
            "size": f"0x{function[1] - function[0]:X}" if function and function[0] == target else "not a .pdata start",
            "imports": reached_imports(target),
            "referenced_from_data": [f"0x{a:08X}" for a in from_data.get(target, [])],
            "calls": [],
        }
        for site in sorted(entries[target]):
            caller = image.function_of(site)
            entry["calls"].append({
                "site": f"0x{site:08X}",
                "caller": f"0x{caller[0]:08X}" if caller else None,
                "strings": scope_strings(image, site),
                "args": {f"r{r}": describe_setup(image, site, r) for r in range(3, 11)},
                "fargs": {f"f{r}": describe_setup(image, site, r, "f") for r in range(1, 5)},
                "result": describe_result(image, site),
            })
        report.append(entry)
    data_only = [f"0x{a:08X}" for a in sorted(from_data) if a not in entries]

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"range": [f"0x{low:08X}", f"0x{high:08X}"], "entry_points": report,
                       "referenced_only_from_data": data_only}, f, indent=1)

    lines = [f"# Entry points of 0x{low:08X}-0x{high:08X} called from outside it", "",
             f"{len(report)} entry points; {len(data_only)} more functions of the range are only referenced from data.", ""]
    for entry in report:
        kind = ", ".join(entry["imports"]) or "pure (no imports reached)"
        lines.append(f"## {entry['address']} (size {entry['size']})")
        lines.append(f"- reaches: {kind}")
        if entry["referenced_from_data"]:
            lines.append(f"- referenced from data at {', '.join(entry['referenced_from_data'][:4])}")
        for call in entry["calls"]:
            strings = "; ".join(f'"{s}"' for s in call["strings"]) or "-"
            int_args = ", ".join(f"{k}={v}" for k, v in call["args"].items() if not v.startswith("unchanged"))
            float_args = ", ".join(f"{k}={v}" for k, v in call["fargs"].items() if not v.startswith("unchanged"))
            lines.append(f"- call {call['site']} in {call['caller']}: strings {strings}")
            lines.append(f"  - args: {int_args or '-'}{'; ' + float_args if float_args else ''}")
            lines.append(f"  - result: {call['result']}")
        lines.append("")
    if data_only:
        lines.append("## Referenced only from data")
        lines.append(", ".join(data_only))
    text = "\n".join(lines) + "\n"
    if args.markdown:
        with open(args.markdown, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
