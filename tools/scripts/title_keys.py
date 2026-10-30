"""Writes keys/title_keys.txt: the real values of keys a title keeps in the console's obscured form.

Some titles derive keys through the security engine's key slots: XeKeysSetKey with a key from their
own data, in the form the console obscures it, then XeKeysAesCbc (Dance Central 2 and 3 decrypt
their songs this way). The console turns the obscured key back into the real one; AnyX360 can't, so
a title that opts in (GameDescriptor::title_keys) looks the real key up in keys/title_keys.txt.

The real values come from a copy of the same executable whose table already holds them, such as a
re-packed dump patched to decrypt with software AES: give both images, extracted with
`x360tool xex-extract`, and where the table is. Lines already in the file are kept; the key bytes
are never printed.

usage: python -I title_keys.py <retail image.bin> <keyed image.bin> <table address hex> <entries>
                               <out title_keys.txt> [--label <text>]

Dance Central 3 (0.0.0.5, Games on Demand) against the re-packed disc dump: table 82D13000, 4
entries (the same obscured keys as Dance Central 2's table).

File format: one key per line, "<obscured: 32 hex digits> <real: 32 hex digits>", then optionally
"# <comment>"; blank lines and lines starting with '#' are ignored.
"""
import sys

BASE = 0x82000000


def main():
    args = sys.argv[1:]
    label = ""
    if "--label" in args:
        i = args.index("--label")
        label = args[i + 1]
        del args[i:i + 2]
    if len(args) != 5:
        sys.exit(__doc__)
    retail_path, keyed_path, table_hex, entries_text, out_path = args
    retail = open(retail_path, "rb").read()
    keyed = open(keyed_path, "rb").read()
    if len(retail) != len(keyed):
        sys.exit("the two images differ in size: not the same executable")
    table, entries = int(table_hex, 16), int(entries_text)
    offset = table - BASE
    if offset < 0 or offset + 16 * entries > len(retail):
        sys.exit("the table is outside the image")
    pairs = []
    for i in range(entries):
        obscured = retail[offset + 16 * i: offset + 16 * i + 16]
        real = keyed[offset + 16 * i: offset + 16 * i + 16]
        if obscured == real:
            sys.exit(f"entry {i} is the same in both images: the second one doesn't hold the real keys")
        pairs.append((obscured.hex().upper(), real.hex().upper()))

    lines, known = [], set()
    try:
        for line in open(out_path, encoding="utf-8"):
            lines.append(line.rstrip("\r\n"))
            fields = line.split("#", 1)[0].split()
            if len(fields) == 2:
                known.add(fields[0].upper())
    except FileNotFoundError:
        lines = ["# AnyX360 title keys: <obscured> <real>, 32 hex digits each (tools/scripts/title_keys.py).",
                 "# Never commit or share this file."]
    added = 0
    for i, (obscured, real) in enumerate(pairs):
        if obscured in known:
            continue
        comment = f"  # {label} {i}".rstrip() if label else f"  # entry {i}"
        lines.append(f"{obscured} {real}{comment}")
        known.add(obscured)
        added += 1
    with open(out_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(lines) + "\n")
    print(f"{out_path}: {added} key(s) added, {len(pairs) - added} already there")


main()
