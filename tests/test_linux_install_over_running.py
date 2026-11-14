"""The Linux installer can replace a module and libraries that are in use.

Someone who reinstalls or upgrades while Orca is speaking through TGSpeechBox
has all three open: Speech Dispatcher runs sd_tgsb, which has
libtgspeechbox.so and libtgsbFrontend.so loaded.  install.sh copied onto
them with a plain cp, which writes into the existing file:

  * on a running program Linux refuses that with "Text file busy", and the
    installer (set -e) stopped half done;
  * on a loaded library it does not refuse: it changes the code under the
    process that has the file mapped, and the module that is speaking can
    crash.

The installer now writes each new file beside the old one and renames it over
it (tgsb_install_file in tools/linux/speechd-setup.sh).  Whatever is running
keeps the old file until it exits, and the next start gets the new one.

Linux only: "Text file busy" is Linux's answer, and a file held open by
another process cannot be renamed over on Windows at all.
"""
from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALL = ROOT / "tools" / "linux" / "install.sh"
SETUP = ROOT / "tools" / "linux" / "speechd-setup.sh"
EXTRAS = ROOT / "extras" / "speech-dispatcher"
BASH = shutil.which("bash")
SLEEP = shutil.which("sleep")

pytestmark = pytest.mark.skipif(
    not sys.platform.startswith("linux") or BASH is None or SLEEP is None,
    reason="needs Linux, bash and sleep: 'Text file busy' is what Linux answers")

# A new build of a program: not the bytes of the old one, and it runs.
NEW_PROGRAM = b"#!/bin/sh\necho new build\n"


@pytest.fixture
def running(tmp_path):
    """start(path): put a real program at path and leave it running."""
    procs = []

    def start(path):
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(SLEEP, path)
        path.chmod(0o755)
        p = subprocess.Popen([str(path), "60"])
        procs.append(p)
        return p

    yield start
    for p in procs:
        p.kill()
        p.wait()


def test_the_speech_dispatcher_module_is_replaced_while_it_runs(tmp_path, running):
    module_dir = tmp_path / "speech-dispatcher-modules"
    config_dir = tmp_path / "etc-speech-dispatcher"
    (config_dir / "modules").mkdir(parents=True)
    module = running(module_dir / "sd_tgsb")
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "sd_tgsb").write_bytes(NEW_PROGRAM)
    script = f"""
set -e
export TGSB_SD_MODULE_DIR='{module_dir}' TGSB_SD_CONFIG_DIR='{config_dir}'
. '{SETUP}'
tgsb_sd_install '{bin_dir}' '{EXTRAS}'
"""
    out = subprocess.run([BASH, "-c", script], capture_output=True, text=True, timeout=60)

    assert out.returncode == 0, out.stdout + out.stderr
    assert (module_dir / "sd_tgsb").read_bytes() == NEW_PROGRAM
    assert os.access(module_dir / "sd_tgsb", os.X_OK)
    assert module.poll() is None, "the running module was disturbed"
    assert [p.name for p in module_dir.iterdir()] == ["sd_tgsb"], "a temporary file was left behind"


def test_a_file_in_use_keeps_its_old_contents_for_whoever_has_it_open(tmp_path):
    """The library case: no error from a plain cp, but the process that has
    the old file mapped must go on seeing the old file."""
    old, new = tmp_path / "libtgspeechbox.so", tmp_path / "new.so"
    old.write_bytes(b"OLD " * 4096)
    new.write_bytes(b"NEW " * 5000)
    with open(old, "rb") as held:  # as a process holds a library it has loaded
        out = subprocess.run([BASH, "-c", f". '{SETUP}'; tgsb_install_file '{new}' '{old}'"],
                             capture_output=True, text=True, timeout=60)
        assert out.returncode == 0, out.stdout + out.stderr
        assert held.read() == b"OLD " * 4096, "the open file changed under its reader"
    assert old.read_bytes() == b"NEW " * 5000
    assert sorted(p.name for p in tmp_path.iterdir()) == ["libtgspeechbox.so", "new.so"]


def test_a_copy_that_fails_leaves_the_old_file_and_no_litter(tmp_path):
    old = tmp_path / "sd_tgsb"
    old.write_bytes(b"old")
    out = subprocess.run([BASH, "-c", f". '{SETUP}'; tgsb_install_file '{tmp_path / 'missing'}' '{old}'"],
                         capture_output=True, text=True, timeout=60)
    assert out.returncode != 0
    assert old.read_bytes() == b"old"
    assert [p.name for p in tmp_path.iterdir()] == ["sd_tgsb"]


def test_install_sh_finishes_with_the_program_and_the_libraries_in_use(tmp_path, running):
    """The whole installer, into a prefix where the previous install is running."""
    dist = tmp_path / "dist"
    (dist / "bin").mkdir(parents=True)
    (dist / "lib").mkdir()
    (dist / "share" / "tgspeechbox" / "packs").mkdir(parents=True)
    shutil.copy(INSTALL, dist / "install.sh")
    shutil.copy(SETUP, dist / "speechd-setup.sh")
    for name in ("tgsbRender", "tgsp", "sd_tgsb"):
        (dist / "bin" / name).write_bytes(NEW_PROGRAM)
    for name in ("libtgspeechbox.so", "libtgsbFrontend.so"):
        (dist / "lib" / name).write_bytes(b"NEW " * 5000)
    (dist / "share" / "tgspeechbox" / "packs" / "phonemes.yaml").write_text("phonemes: {}\n")

    prefix = tmp_path / "prefix"
    program = running(prefix / "bin" / "tgsbRender")
    (prefix / "lib").mkdir(parents=True)
    held = {}
    for name in ("libtgspeechbox.so", "libtgsbFrontend.so"):
        (prefix / "lib" / name).write_bytes(b"OLD " * 4096)
        held[name] = open(prefix / "lib" / name, "rb")
    try:
        env = dict(os.environ, HOME=str(tmp_path / "home"), XDG_CONFIG_HOME=str(tmp_path / "home" / ".config"))
        # "n" to the Speech Dispatcher question, should this machine have one.
        out = subprocess.run([BASH, str(dist / "install.sh"), str(prefix)], input="n\n", capture_output=True,
                             text=True, timeout=120, env=env)
        assert out.returncode == 0, out.stdout[-1500:] + out.stderr[-1500:]
        assert "Installation complete" in out.stdout
        assert (prefix / "bin" / "tgsbRender").read_bytes() == NEW_PROGRAM
        assert os.access(prefix / "bin" / "tgsbRender", os.X_OK)
        assert program.poll() is None, "the running program was disturbed"
        for name, f in held.items():
            assert f.read() == b"OLD " * 4096, f"{name} changed under the process that has it loaded"
            assert (prefix / "lib" / name).read_bytes() == b"NEW " * 5000
        leftovers = [p.name for d in ("bin", "lib") for p in (prefix / d).iterdir() if "tgsb-new" in p.name]
        assert not leftovers, leftovers
    finally:
        for f in held.values():
            f.close()
