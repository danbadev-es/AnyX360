"""The launcher's terms-accepted hash: FNV-1a 64 of its two terms texts (the R"terms(...)terms" raw
strings in build/launcher/launcher_terms.h, with \\r removed). A sandbox LOCALAPPDATA needs it in
AnyX360/terms_accepted.txt, else the launcher stops at the terms screen. release.ps1 computes the same
in PowerShell (tools/scripts/lib.ps1, Get-TermsHash).

usage: python -I tools/scripts/terms_hash.py <launcher_terms.h> [<out terms_accepted.txt>]"""
import sys


def terms_hash(header: bytes) -> str:
    parts, start = [], 0
    for _ in range(2):
        a = header.index(b'R"terms(', start) + 8
        b = header.index(b')terms"', a)
        parts.append(header[a:b].replace(b"\r\n", b"\n"))
        start = b
    value = 14695981039346656037
    for part in parts:
        for c in part:
            value = ((value ^ c) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % value


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        result = terms_hash(f.read())
    if len(sys.argv) == 3:
        with open(sys.argv[2], "w") as f:
            f.write(result + "\n")
    print(result)
