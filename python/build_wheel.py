#!/usr/bin/env python3
"""Build the tgspeechbox wheel from already-built engine libraries.

    python python/build_wheel.py --lib-dir build-x64/MinSizeRel --plat win_amd64

Packs the package (python/tgspeechbox), the two libraries from --lib-dir, the
repo's packs/ and LICENSE into dist/tgspeechbox-<version>-py3-none-<plat>.whl.
Standard library only: a wheel is a zip with METADATA, WHEEL and RECORD, and
writing them here gives every platform, including cross-compiled Linux, the
exact tag it needs.  The package loads the libraries with ctypes, so one wheel
serves every Python 3 version on its platform.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import re
import sys
import zipfile

REPO = pathlib.Path(__file__).resolve().parent.parent
PKG = REPO / "python" / "tgspeechbox"

LIB_NAMES = [
    "speechPlayer.dll", "nvspFrontend.dll",                    # Windows (CMake)
    "libspeechPlayer.dylib", "libnvspFrontend.dylib",          # macOS (CMake)
    "libspeechPlayer.so", "libnvspFrontend.so",                # Linux (CMake)
    "libtgspeechbox.so", "libtgsbFrontend.so",                 # Linux (Makefile.linux)
]


def version() -> str:
    text = (PKG / "_version.py").read_text(encoding="utf-8")
    return re.search(r'__version__\s*=\s*"([^"]+)"', text).group(1)


def record_line(arcname: str, data: bytes) -> str:
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    return f"{arcname},sha256={digest},{len(data)}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lib-dir", required=True, help="directory with the built engine libraries")
    ap.add_argument("--plat", required=True, help="platform tag, e.g. win_amd64, macosx_11_0_universal2, "
                                                  "manylinux_2_35_x86_64")
    ap.add_argument("--out", default=str(REPO / "dist"))
    a = ap.parse_args()

    ver = version()
    lib_dir = pathlib.Path(a.lib_dir)
    libs = [lib_dir / n for n in LIB_NAMES if (lib_dir / n).is_file()]
    kinds = {"dsp": any("speechPlayer" in p.name or "tgspeechbox" in p.name for p in libs),
             "frontend": any("Frontend" in p.name for p in libs)}
    if not all(kinds.values()):
        print(f"build_wheel: both engine libraries are needed in {lib_dir}; found {[p.name for p in libs]}",
              file=sys.stderr)
        return 1

    files: list[tuple[str, bytes]] = []
    for p in sorted(PKG.rglob("*.py")):
        if "__pycache__" in p.parts or "_libs" in p.parts or "packs" in p.relative_to(PKG).parts[:1]:
            continue
        files.append((f"tgspeechbox/{p.relative_to(PKG).as_posix()}", p.read_bytes()))
    for p in libs:
        files.append((f"tgspeechbox/_libs/{p.name}", p.read_bytes()))
    packs = REPO / "packs"
    for p in sorted(packs.rglob("*")):
        if p.is_file() and ".defaults" not in p.parts and "__pycache__" not in p.parts:
            files.append((f"tgspeechbox/packs/{p.relative_to(packs).as_posix()}", p.read_bytes()))

    dist_info = f"tgspeechbox-{ver}.dist-info"
    readme = (REPO / "python" / "README.md").read_text(encoding="utf-8")
    metadata = (
        "Metadata-Version: 2.1\n"
        "Name: tgspeechbox\n"
        f"Version: {ver}\n"
        "Summary: TGSpeechBox's formant speech engine (DSP and IPA frontend) from Python\n"
        "Home-page: https://github.com/tgeczy/TGSpeechBox\n"
        "License: MIT\n"
        "Requires-Python: >=3.8\n"
        "Classifier: License :: OSI Approved :: MIT License\n"
        "Classifier: Topic :: Multimedia :: Sound/Audio :: Speech\n"
        "Description-Content-Type: text/markdown\n"
        "\n" + readme)
    wheel_meta = ("Wheel-Version: 1.0\n"
                  "Generator: tgspeechbox build_wheel.py\n"
                  "Root-Is-Purelib: false\n"
                  f"Tag: py3-none-{a.plat}\n")
    files += [(f"{dist_info}/METADATA", metadata.encode("utf-8")),
              (f"{dist_info}/WHEEL", wheel_meta.encode("utf-8")),
              (f"{dist_info}/LICENSE", (REPO / "LICENSE").read_bytes())]
    record = "\n".join(record_line(n, d) for n, d in files) + f"\n{dist_info}/RECORD,,\n"

    out_dir = pathlib.Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"tgspeechbox-{ver}-py3-none-{a.plat}.whl"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            # Libraries stay executable when unpacked on Linux and macOS.
            info.external_attr = (0o755 if "/_libs/" in name else 0o644) << 16
            z.writestr(info, data)
        z.writestr(f"{dist_info}/RECORD", record)
    print(f"built {path.name}: {len(libs)} libraries, {len(files)} files, {path.stat().st_size // 1024} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
