"""Gathers the licenses of everything in the pose bridge bundle into one folder (build_bridge.ps1):

  python -I collect_licenses.py <distributions.txt> <output folder> <models folder> <bundle folder>

For each Python distribution the bundle holds: the license files its metadata ships, or a note with
its declared license when it ships none. Then Python's own license, PyInstaller's (its bootloader is
in pose_bridge.exe), the pose models', the Microsoft C++ runtime's and OpenSSL's (Python's ssl and
hashlib modules).
"""

import hashlib
import shutil
import sys
from importlib import metadata
from pathlib import Path

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_{0}/float16/1/pose_landmarker_{0}.task"


def license_files(distribution):
    files = []
    for file in distribution.files or []:
        name = file.name.upper()
        parts = [p.lower() for p in file.parts]
        in_metadata = parts[0].endswith(".dist-info")
        if in_metadata and ("licenses" in parts or name.startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE"))):
            files.append(file)
        elif not in_metadata and name.startswith(("LICENSE", "COPYING")) and len(parts) == 2:
            files.append(file)  # e.g. cv2/LICENSE-3RD-PARTY.txt
    return files


def copy_distribution(name, out):
    distribution = metadata.distribution(name)
    folder = out / f"{distribution.metadata['Name']}-{distribution.version}"
    folder.mkdir(parents=True, exist_ok=True)
    copied = 0
    for file in license_files(distribution):
        source = Path(distribution.locate_file(file))
        if source.is_file():
            shutil.copyfile(source, folder / source.name)
            copied += 1
    declared = distribution.metadata.get("License-Expression") or distribution.metadata.get("License") or "see the project"
    if not copied:
        homepage = distribution.metadata.get("Home-page") or ""
        for url in distribution.metadata.get_all("Project-URL") or []:
            homepage = homepage or url.split(",", 1)[-1].strip()
        (folder / "LICENSE-NOTE.txt").write_text(
            f"{distribution.metadata['Name']} {distribution.version} ships no license file.\n"
            f"Declared license: {declared.splitlines()[0]}\n{homepage}\n", encoding="utf-8")
    return f"{distribution.metadata['Name']} {distribution.version}: {declared.splitlines()[0][:60]}"


def main():
    distributions_file, out, models, bundle = (Path(a) for a in sys.argv[1:5])
    out.mkdir(parents=True, exist_ok=True)
    summary = []
    names = [line.strip() for line in distributions_file.read_text(encoding="utf-8").splitlines() if line.strip()]
    for name in sorted(set(names) | {"pyinstaller"}):
        summary.append(copy_distribution(name, out))

    python = out / f"Python-{sys.version.split()[0]}"
    python.mkdir(exist_ok=True)
    shutil.copyfile(Path(sys.base_prefix) / "LICENSE.txt", python / "LICENSE.txt")
    summary.append(f"Python {sys.version.split()[0]}: PSF-2.0")

    lines = ["MediaPipe Pose Landmarker models, Apache License 2.0 (see the mediapipe folder).",
             "Model cards: https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker", ""]
    for model in sorted(models.glob("pose_landmarker_*.task")):
        kind = model.stem.removeprefix("pose_landmarker_")
        digest = hashlib.sha256(model.read_bytes()).hexdigest()
        lines.append(f"{model.name}  sha256 {digest}\n  {MODEL_URL.format(kind)}")
    (out / "MODELS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    summary.append("pose models: Apache-2.0")

    (out / "MSVC-RUNTIME.txt").write_text(
        "vcruntime140.dll, vcruntime140_1.dll and msvcp140*.dll are the Microsoft Visual C++ runtime, and\n"
        "the api-ms-win-*.dll files the Universal C Runtime's forwarders, redistributed with the programs\n"
        "built with them under the Visual Studio redistribution terms\n"
        "(https://learn.microsoft.com/visualstudio/releases/2022/redistribution).\n", encoding="utf-8")
    summary.append("Microsoft C++ runtime: Visual Studio redistributable")

    openssl = sorted(p.name for p in bundle.rglob("lib*-3*.dll") if p.name.startswith(("libcrypto", "libssl")))
    if openssl:
        (out / "OPENSSL.txt").write_text(
            f"{', '.join(openssl)}: OpenSSL 3, used by Python's ssl and hashlib modules, under the Apache\n"
            "License 2.0 (the text is in the mediapipe folder; https://openssl-library.org/source/license/).\n",
            encoding="utf-8")
        summary.append("OpenSSL 3: Apache-2.0")

    (out / "README.txt").write_text(
        "Licenses of the pose bridge (pose_bridge\\), the program that turns a webcam into the virtual\n"
        "Kinect's skeletons. It bundles:\n\n" + "\n".join(f"  {line}" for line in summary) + "\n",
        encoding="utf-8")
    print("\n".join(summary))


if __name__ == "__main__":
    main()
