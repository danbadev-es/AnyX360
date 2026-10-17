# PyInstaller spec of the pose bridge (build_bridge.ps1 runs it): one folder, a console program (the
# game starts it without a window and keeps its output), the pose models inside.
#
# MediaPipe's drawing helpers import matplotlib, which the bridge never uses: estimator.py stubs it,
# and the bundle leaves it out with the other packages only MediaPipe's extras need.

import os
from importlib.metadata import packages_distributions

from PyInstaller.utils.hooks import collect_dynamic_libs

here = os.path.dirname(os.path.abspath(SPEC))  # noqa: F821 (PyInstaller defines SPEC)
build = os.environ.get("POSE_BRIDGE_BUILD") or os.path.join(here, "..", "..", "build", "pose_bridge")
models = os.path.join(build, "models")

datas = [(os.path.join(models, name), "models") for name in sorted(os.listdir(models)) if name.endswith(".task")]
binaries = collect_dynamic_libs("mediapipe")  # mediapipe/tasks/c/libmediapipe.dll, found through importlib.resources

a = Analysis(  # noqa: F821
    [os.path.join(here, "pose_bridge.py")],
    pathex=[here],
    binaries=binaries,
    datas=datas,
    hiddenimports=["estimator", "preview", "selftest"],  # imported when needed
    excludes=["matplotlib", "PIL", "fontTools", "kiwisolver", "contourpy", "cycler", "pyparsing", "dateutil",
              "sounddevice", "cffi", "pycparser", "tkinter", "unittest", "pydoc", "setuptools", "pkg_resources"],
    noarchive=False,
)
pyz = PYZ(a.pure)  # noqa: F821
exe = EXE(  # noqa: F821
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="pose_bridge",
    console=True,
    upx=False,
)
COLLECT(exe, a.binaries, a.datas, name="pose_bridge", upx=False)  # noqa: F821

# The distributions in the bundle, for collect_licenses.py.
tops = {name.split(".")[0] for name, *_ in a.pure}
tops |= {dest.replace("\\", "/").split("/")[0] for dest, *_ in a.binaries}
providers = packages_distributions()
distributions = sorted({d for top in tops for d in providers.get(top, [])})
with open(os.path.join(build, "distributions.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(distributions) + "\n")
