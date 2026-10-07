"""The Linux installer leaves speechd.conf alone (Speech Dispatcher setup).

Garrote Klein: "stop putting AddModule lines in speech-dispatcher
configuration files. This breaks dynamic autodiscovery, makes your synth the
only output module available, and most importantly, forceably switches your
default synth."  He was right about ours: install.sh appended an AddModule
line (and re-enabled espeak-ng's to make up for it) and offered to replace
DefaultModule.

Speech Dispatcher finds sd_<name> programs and <name>-generic.conf files by
itself as long as speechd.conf has no AddModule lines, so the installer now
puts sd_tgsb and tgsb.conf where it looks and never edits speechd.conf
unasked; it takes back only what earlier installers added, and asks before
touching a list a distro or a person wrote.  tools/linux/speechd-setup.sh is
run here against sample folders, the way install.sh runs it.
"""
from __future__ import annotations

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SETUP = ROOT / "tools" / "linux" / "speechd-setup.sh"
EXTRAS = ROOT / "extras" / "speech-dispatcher"
BASH = shutil.which("bash")

pytestmark = pytest.mark.skipif(BASH is None, reason="needs bash")

DISTRO_CONF = """# speechd.conf as distributions ship it: modules are found automatically.
#AddModule "espeak-ng"                "sd_espeak-ng" "espeak-ng.conf"
#AddModule "festival"                 "sd_festival"  "festival.conf"
DefaultModule espeak-ng
"""

OLD_INSTALL_BLOCK = """
# --- TGSpeechBox (added by install.sh) ---
AddModule "tgsb" "sd_tgsb" "tgsb-native.conf"
"""


def run(tmp_path, conf_text, answers="", native=True, modules=None):
    """Set up a sample Speech Dispatcher, run the setup as install.sh does,
    and return (speechd.conf after, the module name, the folders, output)."""
    module_dir = tmp_path / "speech-dispatcher-modules"
    config_dir = tmp_path / "etc-speech-dispatcher"
    (config_dir / "modules").mkdir(parents=True)
    module_dir.mkdir()
    (config_dir / "speechd.conf").write_text(conf_text, encoding="utf-8", newline="\n")
    for name, text in (modules or {}).items():
        (config_dir / "modules" / name).write_text(text, encoding="utf-8", newline="\n")
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    if native:
        (bin_dir / "sd_tgsb").write_text("#!/bin/sh\n", encoding="utf-8", newline="\n")
    script = f"""
set -e
export TGSB_SD_MODULE_DIR='{module_dir.as_posix()}' TGSB_SD_CONFIG_DIR='{config_dir.as_posix()}'
. '{SETUP.as_posix()}'
tgsb_sd_install '{bin_dir.as_posix()}' '{EXTRAS.as_posix()}'
tgsb_sd_cleanup_conf '{(config_dir / "speechd.conf").as_posix()}'
tgsb_sd_check_explicit_list '{(config_dir / "speechd.conf").as_posix()}' "$TGSB_SD_MODULE_NAME"
echo "NAME=$TGSB_SD_MODULE_NAME"
"""
    out = subprocess.run([BASH, "-c", script], input=answers, capture_output=True, text=True,
                         encoding="utf-8", timeout=60)
    assert out.returncode == 0, out.stdout + out.stderr
    name = out.stdout.rsplit("NAME=", 1)[1].strip()
    after = (config_dir / "speechd.conf").read_text(encoding="utf-8")
    return after, name, module_dir, config_dir / "modules", out.stdout


def test_a_fresh_install_leaves_speechd_conf_untouched(tmp_path):
    after, name, module_dir, modules, _ = run(tmp_path, DISTRO_CONF)
    assert after == DISTRO_CONF
    assert name == "tgsb"
    assert (module_dir / "sd_tgsb").is_file()
    assert (modules / "tgsb.conf").is_file()  # module "tgsb" reads <name>.conf
    assert not (modules / "tgsb-generic.conf").exists()  # no second TGSpeechBox
    assert not (modules.parent / "speechd.conf.tgsb-backup").exists()


def test_an_earlier_install_is_taken_back_and_the_default_kept(tmp_path):
    conf = DISTRO_CONF.replace("DefaultModule espeak-ng", "# DefaultModule espeak-ng\nDefaultModule tgsb")
    conf += OLD_INSTALL_BLOCK
    after, name, _, modules, _ = run(tmp_path, conf, modules={
        "tgsb-native.conf": "TGSBDefaultVoice Robert\n",  # someone's own setting
        "tgsb-generic.conf": "# generic\n",
    })
    assert "AddModule" not in after.replace("#AddModule", "")
    assert "TGSpeechBox" not in after
    assert "DefaultModule tgsb" in after  # the user chose it; still the module's name
    assert (modules / "tgsb.conf").read_text() == "TGSBDefaultVoice Robert\n"
    assert not (modules / "tgsb-native.conf").exists()
    assert not (modules / "tgsb-generic.conf").exists()
    assert (modules.parent / "speechd.conf.tgsb-backup").read_text() == conf


def test_the_espeak_line_an_earlier_install_inserted_goes_too(tmp_path):
    conf = DISTRO_CONF + '\nAddModule "espeak-ng" "sd_espeak-ng" "espeak-ng.conf"' + OLD_INSTALL_BLOCK
    after, _, _, _, out = run(tmp_path, conf)
    assert after == DISTRO_CONF
    assert "lists its synthesizers" not in out  # nothing left to ask about


@pytest.mark.parametrize("answers,expect", [
    ("n\nn\n", "unchanged"),
    ("y\n", "commented"),
    ("n\ny\n", "added"),
])
def test_a_list_someone_wrote_is_changed_only_when_asked(tmp_path, answers, expect):
    conf = 'AddModule "espeak-ng" "sd_espeak-ng" "espeak-ng.conf"\nDefaultModule espeak-ng\n'
    after, _, _, _, out = run(tmp_path, conf, answers=answers)
    assert "lists its synthesizers" in out
    if expect == "unchanged":
        assert after == conf
    elif expect == "commented":
        assert after == '#AddModule "espeak-ng" "sd_espeak-ng" "espeak-ng.conf"\nDefaultModule espeak-ng\n'
    else:
        assert after == conf + 'AddModule "tgsb" "sd_tgsb" "tgsb.conf"\n'
    assert after.count("DefaultModule espeak-ng") == 1


def test_without_the_native_module_the_generic_one_is_found(tmp_path):
    after, name, module_dir, modules, _ = run(tmp_path, DISTRO_CONF, native=False)
    assert after == DISTRO_CONF
    assert name == "tgsb-generic"  # Speech Dispatcher names *-generic.conf modules so
    assert (modules / "tgsb-generic.conf").is_file()
    assert not (module_dir / "sd_tgsb").exists()


def test_install_sh_no_longer_writes_speechd_conf():
    text = (ROOT / "tools" / "linux" / "install.sh").read_text(encoding="utf-8")
    assert "AddModule" not in text and "DefaultModule" not in text
