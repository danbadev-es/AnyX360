"""Merges Xenia shader caches into the ones a release ships.

The release seeds each player's cache with the shaders and pipelines collected while testing
(make_dist.ps1 copies shader_cache\\<game id>\\ into bin\\shader_cache\\<game id>\\), so a first play
doesn't compile them during the game: an empty cache made Modern Warfare 3 stutter and, with
Xenia's background compilation, briefly draw effects black.

usage: python -I merge_shader_caches.py <output folder (shader_cache)> <folder>...
Every <title>.xsh and <title>.*.xpso found under the folders (a game's cache\\shaders\\shareable,
anywhere below) and already in <output> is merged into <output>\\<game id>\\shareable\\, so nothing
the output had is lost; the game id is taken from games\\*\\game.json
by title ID. Shaders (.xsh) are kept once per hash and only whole (a file cut off mid-write ends
with a partial entry, where Xenia stops reading too); pipelines
(.xpso) once per description hash, whole records only, and only with the same header (format
version) as the newest file. Nothing is written for a title no game has.
"""

import json
import pathlib
import struct
import sys

SHADER_MAGIC = 0x48534558  # 'XESH'
PIPELINE_MAGIC = 0x53504558  # 'XEPS'
PIPELINE_RECORD = 72  # D3D12 PipelineStoredDescription: a u64 hash and a 64-byte description


def read_shaders(path):
    """[(hash, entry bytes)] of an .xsh file's valid entries, or None for another format."""
    data = path.read_bytes()
    if len(data) < 8 or struct.unpack_from("<I", data, 0)[0] != SHADER_MAGIC:
        return None
    header = data[:8]
    entries, pos = [], 8
    while pos + 12 <= len(data):
        ucode_hash, packed = struct.unpack_from("<QI", data, pos)
        size = (packed & 0x7FFFFFFF) * 4
        if pos + 12 + size > len(data):
            break  # a cut-off tail
        entries.append((ucode_hash, data[pos:pos + 12 + size]))
        pos += 12 + size
    return header, entries


def read_pipelines(path):
    data = path.read_bytes()
    if len(data) < 12 or struct.unpack_from("<I", data, 0)[0] != PIPELINE_MAGIC:
        return None
    header = data[:12]
    count = (len(data) - 12) // PIPELINE_RECORD
    records = [data[12 + i * PIPELINE_RECORD:12 + (i + 1) * PIPELINE_RECORD] for i in range(count)]
    return header, records


def games_by_title(root):
    games = {}
    for game_json in (root / "games").glob("*/game.json"):
        info = json.loads(game_json.read_text(encoding="utf-8"))
        games[info["title_id"].upper()] = info["id"]
    return games


def main(argv):
    out = pathlib.Path(argv[1])
    root = pathlib.Path(__file__).resolve().parents[2]
    games = games_by_title(root)
    files = {}  # (title, suffix) -> [paths], newest first
    # The output's own caches are inputs too, so merging adds to what the release already ships.
    for folder in [out] + argv[2:]:
        for path in pathlib.Path(folder).rglob("*"):
            if path.parent.name != "shareable" or not path.is_file():
                continue
            name = path.name
            title, _, suffix = name.partition(".")
            if title.upper() in games and (suffix == "xsh" or suffix.endswith("xpso")):
                files.setdefault((title.upper(), suffix), []).append(path)
    for (title, suffix), paths in sorted(files.items()):
        paths.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        target = out / games[title] / "shareable" / f"{title}.{suffix}"
        header, seen, body = None, set(), []
        for path in paths:
            parsed = read_shaders(path) if suffix == "xsh" else read_pipelines(path)
            if parsed is None:
                continue
            file_header, items = parsed
            if header is None:
                header = file_header
            elif file_header != header:
                continue  # another format version: Xenia would reject it
            for item in items:
                key = item[0] if suffix == "xsh" else item[:8]
                if key not in seen:
                    seen.add(key)
                    body.append(item[1] if suffix == "xsh" else item)
        if header is None:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(header + b"".join(body))
        print(f"{games[title]:5} {title}.{suffix}: {len(body)} from {len(paths)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
