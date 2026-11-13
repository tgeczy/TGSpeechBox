"""The settings panel's "Check for Add-on Updates" button asks GitHub's releases (#103).

The button fetched one fixed file from the maintainer's own site, with a
version text file beside it.  updates.py (tests/test_nvda_updates.py) chooses
from the repository's releases instead; this is the button wired to it, and
the "Update channel" choice beside it:

* the channel (Stable, Prerelease) is kept in NVDA's configuration, in the
  add-on's own section, saved with the panel and shown again when it opens;
* the release offered is the newest add-on on the channel that is chosen;
* a check that fails offers the releases page in the browser, and says why
  in NVDA's language;
* a check that finds nothing to offer on the channel has not failed: it is
  an information message, like "you have the latest";
* the readme says what the button and the channel do;
* nothing is asked of the network until the button is pressed, and on a
  secure screen there is no button at all;
* the network is not waited for on the GUI thread;
* the old site is gone from the add-on.

No NVDA, no wx and no network are needed: the panel class is built on a
stand-in for NVDA's SettingsPanel, wx is a recorder that answers the
dialogs, and updates.py is given GitHub's list from memory.
"""
from __future__ import annotations

import importlib
import json
import pathlib
import sys
import threading
import types

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
ADDON = REPO / "nvdaAddon"
DRIVER = ADDON / "synthDrivers" / "tgSpeechBox"
DL = "https://github.com/tgeczy/TGSpeechBox/releases/download"


def _release(tag, addon, prerelease=False, draft=False):
    return {"tag_name": tag, "prerelease": prerelease, "draft": draft,
            "html_url": f"https://github.com/tgeczy/TGSpeechBox/releases/tag/{tag}",
            "assets": [{"name": f"TGSpeechSapiSetup-{tag}.exe", "browser_download_url": f"{DL}/{tag}/setup.exe"},
                       {"name": addon, "browser_download_url": f"{DL}/{tag}/{addon}"}]}


# Newest first, as GitHub lists them.
RELEASES = [
    _release("v-311b2", "TGSpeechBox-v311b2-2027.1.20.nvda-addon", draft=True),
    _release("v-311b1", "TGSpeechBox-v311b1-2027.1.9.nvda-addon", prerelease=True),
    _release("v-310", "TGSpeechBox-v310-2026.11.20.nvda-addon"),
    _release("v-310rc4", "TGSpeechBox-v310rc4-2026.11.14.nvda-addon"),
]
OFFERED = {"stable": ("2026.11.20", f"{DL}/v-310/TGSpeechBox-v310-2026.11.20.nvda-addon"),
           "prerelease": ("2027.1.9", f"{DL}/v-311b1/TGSpeechBox-v311b1-2027.1.9.nvda-addon")}


# ---------------------------------------------------------------------------
# Stand-ins
# ---------------------------------------------------------------------------

class FakeConf(dict):
    """NVDA's config.conf as an add-on meets it: a section exists once its
    spec is registered, and a key that was never set reads as the spec's
    default."""

    def __init__(self):
        super().__init__()
        self.spec = {}

    def __getitem__(self, name):
        if name not in self:
            import re
            defaults = {key: re.search(r"default='([^']*)'", rule).group(1) for key, rule in self.spec[name].items()}
            dict.__setitem__(self, name, defaults)
        return dict.__getitem__(self, name)


class FakeChoice:
    def __init__(self, parent=None, choices=()):
        self.choices, self.selection = list(choices), -1

    def SetSelection(self, n): self.selection = n
    def GetSelection(self): return self.selection


class FakeButton:
    def __init__(self, parent=None, label=""):
        self.label, self.bound, self.disabled = label, None, 0

    def Bind(self, event, handler): self.bound = handler
    def Enable(self, enable=True): self.disabled += not enable
    def Disable(self): self.disabled += 1


class FakeSizerHelper:
    """guiHelper.BoxSizerHelper: makes the labelled control, hands items back."""

    def __init__(self):
        self.labels = []

    def addLabeledControl(self, label, cls, **kwargs):
        self.labels.append(label)
        return cls(None, **kwargs)

    def addItem(self, item):
        return item


class FakeWx(types.ModuleType):
    # wx's own values, each its own bit: a test can tell an information box
    # from a warning, and a question from a message with only OK.
    YES, OK, NO, YES_NO = 0x2, 0x4, 0x8, 0xA
    YES_DEFAULT, NO_DEFAULT = 0, 0x80
    ICON_WARNING, ICON_ERROR, ICON_QUESTION, ICON_INFORMATION = 0x100, 0x200, 0x400, 0x800
    EVT_BUTTON = "EVT_BUTTON"
    Choice, Button = FakeChoice, FakeButton

    def __init__(self, answers=None):
        super().__init__("wx")
        self.answers = dict(answers or {})  # title -> YES/NO; Yes when not named
        self.messages = []                  # (title, text)
        self.styles = []                    # the style of each, in step with messages
        self.opened = []                    # pages sent to the browser
        self.later = []                     # wx.CallAfter's queue: (fn, args)
        self.posted = threading.Event()

    @property
    def titles(self):
        return [title for title, _ in self.messages]

    def is_information(self, n=-1):
        """Message n is an ordinary information box: that icon, and OK alone."""
        style = self.styles[n]
        return bool(style & self.ICON_INFORMATION) and style & self.OK and not style & (
            self.ICON_WARNING | self.ICON_ERROR | self.ICON_QUESTION | self.YES | self.NO)

    def MessageBox(self, message, caption="", style=0, parent=None):
        self.messages.append((caption, message))
        self.styles.append(style)
        return self.answers.get(caption, self.YES)

    def LaunchDefaultBrowser(self, url):
        self.opened.append(url)
        return True

    def CallAfter(self, fn, *args, **kwargs):
        self.later.append((fn, args))
        self.posted.set()


class Env:
    """The panel module with NVDA faked around it, and what happened."""

    def __init__(self, monkeypatch, tmp_path):
        self.monkeypatch = monkeypatch
        self.conf = FakeConf()
        self.wx = FakeWx()
        self.spoken = []      # ui.message
        self.asked = []       # URLs updates.py asked for
        self.fetched = []     # URLs downloaded
        self.started = []     # files handed to Windows to open
        self.secure = False
        self.installed = "2026.11.14"
        self.releases = RELEASES
        self.down = None      # an exception the network raises
        self.answer = None    # what comes back in place of GitHub's list
        self.addon_file = str(tmp_path / "TGSpeechBox-update.nvda-addon")

        config = types.ModuleType("config")
        config.conf = self.conf
        gv = types.ModuleType("globalVars")
        gv.appArgs = types.SimpleNamespace(secure=False)
        ui = types.ModuleType("ui")
        ui.message = lambda text, *a, **k: self.spoken.append(text)
        gui = types.ModuleType("gui")
        dialogs = types.ModuleType("gui.settingsDialogs")
        dialogs.SettingsPanel = type("SettingsPanel", (), {})
        gui.settingsDialogs = dialogs
        sdh = types.ModuleType("synthDriverHandler")
        sdh.getSynth = lambda: None  # no synthesizer for onSave to reload
        for name, mod in (("config", config), ("globalVars", gv), ("ui", ui), ("wx", self.wx),
                          ("gui", gui), ("gui.settingsDialogs", dialogs), ("synthDriverHandler", sdh)):
            monkeypatch.setitem(sys.modules, name, mod)
        self.globalVars = gv

        # The add-on's modules as a package, so the panel's relative imports
        # are the real ones (langPackYaml, updates).
        pkg = types.ModuleType("_tgsb_addon")
        pkg.__path__ = [str(DRIVER)]
        monkeypatch.setitem(sys.modules, "_tgsb_addon", pkg)
        for name in ("langPackSettingsPanel", "updates", "langPackYaml"):
            monkeypatch.delitem(sys.modules, "_tgsb_addon." + name, raising=False)
        self.mod = importlib.import_module("_tgsb_addon.langPackSettingsPanel")
        self.updates = self.mod.updates

        def opener(url):
            self.asked.append(url)
            if self.down is not None:
                raise self.down
            if self.answer is not None:
                return self.answer
            return json.dumps(self.releases).encode("utf-8")

        real_latest = self.updates.latest_release
        monkeypatch.setattr(self.updates, "latest_release",
                            lambda channel="stable", **kw: real_latest(channel, opener=opener))
        monkeypatch.setattr(self.updates, "installed_version", lambda: self.installed)

        def fetch(url, **kw):
            self.fetched.append(url)
            if self.down is not None:
                raise self.down
            return self.addon_file

        monkeypatch.setattr(self.updates, "fetch", fetch)
        monkeypatch.setattr(self.updates, "_secure", lambda: bool(self.globalVars.appArgs.secure))
        monkeypatch.setattr(self.mod.os, "startfile", lambda path: self.started.append(path), raising=False)
        # The worker thread, run in place: the result is handed over at once.
        self.real_worker = self.mod._runInBackground
        monkeypatch.setattr(self.mod, "_runInBackground", self._inline)

    @staticmethod
    def _inline(work, done):
        try:
            result = (work(), None)
        except Exception as e:  # as the real one hands an exception over
            result = (None, e)
        done(*result)

    def panel(self, built=True):
        """A panel as NVDA makes one; `built` runs the part of makeSettings
        that makes the update controls."""
        panel = self.mod._getPanelClass()()
        if built:
            panel._makeUpdateControls(FakeSizerHelper(), self.wx)
        return panel

    def press(self, panel):
        panel._onUpdateClick(types.SimpleNamespace(Skip=lambda: None))


@pytest.fixture
def env(monkeypatch, tmp_path):
    return Env(monkeypatch, tmp_path)


# ---------------------------------------------------------------------------
# The old site
# ---------------------------------------------------------------------------

def test_no_file_of_the_add_on_mentions_the_old_site():
    found = [str(p.relative_to(REPO)) for p in sorted(ADDON.rglob("*.py"))
             if "eurpod" in p.read_text(encoding="utf-8").lower()]
    assert not found, f"the old update site is still named in: {found}"


def test_the_fixed_file_and_its_version_file_are_gone(env):
    assert not hasattr(env.mod, "ADDON_UPDATE_URL") and not hasattr(env.mod, "ADDON_VERSION_URL")
    # One way to read a version and to compare two: updates.py's.
    for old in ("_getInstalledAddonVersion", "_parseVersionTuple", "_isNewerVersion"):
        assert not hasattr(env.mod, old)


# ---------------------------------------------------------------------------
# The channel
# ---------------------------------------------------------------------------

def test_the_channel_is_stable_until_somebody_chooses(env):
    sizer = FakeSizerHelper()
    panel = env.panel(built=False)
    panel._makeUpdateControls(sizer, env.wx)
    assert sizer.labels == ["Update channel:"]
    assert panel.updateChannelCtrl.choices == ["Stable", "Prerelease"]
    assert panel.updateChannelCtrl.GetSelection() == 0
    # NVDA is told the setting's type and default, in the add-on's own section.
    assert env.conf.spec["tgSpeechBox"]["updateChannel"] == "string(default='stable')"
    assert env.conf["tgSpeechBox"]["updateChannel"] == "stable"


def test_the_choices_are_in_the_order_of_the_channels(env):
    # The selection is an index into updates.CHANNELS.
    assert env.updates.CHANNELS == ("stable", "prerelease")


@pytest.mark.parametrize("pending", [False, True], ids=["nothing-else-changed", "with-a-setting-changed"])
def test_the_channel_is_saved_with_the_panel_and_shown_again(env, pending):
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(1)
    assert env.conf["tgSpeechBox"]["updateChannel"] == "stable"  # not before OK
    if pending:
        written = []
        env.monkeypatch.setattr(env.mod.langPackYaml, "upsertSetting", lambda *a: written.append(a))
        panel._pending = {"en-us": {"primaryStressDiv": "1.1"}}
    panel.onSave()
    assert env.conf["tgSpeechBox"]["updateChannel"] == "prerelease"
    if pending:
        assert len(written) == 1  # the panel's other settings are saved as before

    again = env.panel()  # the dialog opened another time
    assert again.updateChannelCtrl.GetSelection() == 1
    again.updateChannelCtrl.SetSelection(0)
    again.onSave()
    assert env.conf["tgSpeechBox"]["updateChannel"] == "stable"


def test_cancel_does_not_change_the_channel(env):
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(1)
    panel.onDiscard()
    assert env.conf["tgSpeechBox"]["updateChannel"] == "stable"
    assert env.panel().updateChannelCtrl.GetSelection() == 0


@pytest.mark.parametrize("stored", ["nightly", "", None, 3])
def test_a_channel_nobody_knows_is_the_stable_one(env, stored):
    env.conf.spec["tgSpeechBox"] = {"updateChannel": "string(default='stable')"}
    dict.__setitem__(env.conf, "tgSpeechBox", {"updateChannel": stored})
    panel = env.panel()
    assert panel.updateChannelCtrl.GetSelection() == 0
    env.press(panel)
    assert env.fetched == [OFFERED["stable"][1]]


def test_a_configuration_that_cannot_be_read_is_the_stable_channel(env):
    # The driver's test harness, and anything else without NVDA's spec.
    env.monkeypatch.setattr(sys.modules["config"], "conf", {"speech": {}})
    panel = env.panel()
    assert panel.updateChannelCtrl.GetSelection() == 0
    panel.updateChannelCtrl.SetSelection(1)
    panel.onSave()  # must not raise


# ---------------------------------------------------------------------------
# The button
# ---------------------------------------------------------------------------

def test_nothing_is_asked_until_the_button_is_pressed(env):
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(1)
    panel.onSave()
    env.panel().onDiscard()
    assert env.asked == [] and env.fetched == [] and env.wx.messages == []
    assert panel.updateButton.bound == panel._onUpdateClick
    assert panel.updateButton.label == "Check for Add-on Updates"


@pytest.mark.parametrize("channel", ["stable", "prerelease"])
def test_the_newest_add_on_on_the_chosen_channel_is_offered_and_installed(env, channel):
    panel = env.panel()
    # Chosen in the panel, not yet saved: the button goes by what is shown.
    panel.updateChannelCtrl.SetSelection(env.updates.CHANNELS.index(channel))
    env.press(panel)

    version, url = OFFERED[channel]
    assert env.asked and all(u.startswith("https://api.github.com/repos/tgeczy/TGSpeechBox/releases")
                             for u in env.asked)
    title, text = env.wx.messages[0]
    assert title == "Update Available" and version in text and env.installed in text
    assert env.fetched == [url]
    assert env.started == [env.addon_file]


def test_the_saved_channel_is_the_one_used(env):
    env.conf.spec["tgSpeechBox"] = {"updateChannel": "string(default='stable')"}
    dict.__setitem__(env.conf, "tgSpeechBox", {"updateChannel": "prerelease"})
    panel = env.panel()
    env.press(panel)
    assert env.fetched == [OFFERED["prerelease"][1]]


def test_answering_no_downloads_nothing(env):
    env.wx.answers["Update Available"] = env.wx.NO
    panel = env.panel()
    env.press(panel)
    assert env.wx.titles == ["Update Available"]
    assert env.fetched == [] and env.started == []


@pytest.mark.parametrize("installed", ["2026.11.20", "2026.12.1"], ids=["the-same", "newer-than-the-release"])
def test_an_add_on_that_is_up_to_date_is_told_so(env, installed):
    env.installed = installed
    panel = env.panel()
    env.press(panel)
    assert env.wx.titles == ["No Update Available"]
    assert installed in env.wx.messages[0][1] and "2026.11.20" in env.wx.messages[0][1]
    assert env.wx.is_information()
    assert env.fetched == [] and env.started == [] and env.wx.opened == []


# Every release before the add-on's file carried its date: GitHub's real list
# today.
UNDATED = [{"tag_name": "v-310rc301", "prerelease": False, "draft": False, "assets": [
    {"name": "TGSpeechBox-v310rc301.nvda-addon", "browser_download_url": f"{DL}/x/y.nvda-addon"}]}]
# A beta with a dated add-on, and nothing else: something on one channel only.
BETA_ONLY = [_release("v-311b1", "TGSpeechBox-v311b1-2027.1.9.nvda-addon", prerelease=True)] + UNDATED
CHANNEL_NAMES = {"stable": "Stable", "prerelease": "Prerelease"}


@pytest.mark.parametrize("releases,channel", [(UNDATED, "stable"), (UNDATED, "prerelease"), ([], "stable"),
                                              (BETA_ONLY, "stable")],
                         ids=["undated-stable", "undated-prerelease", "no-releases", "a-beta-only-stable"])
def test_nothing_on_the_channel_is_an_answer_not_a_failure(env, releases, channel):
    """The check worked: GitHub answered, and no release on the channel
    carries an add-on to offer.  It was shown as "Version Check Failed", a
    warning with the releases page offered, on every press against GitHub's
    real list."""
    env.releases = releases
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(env.updates.CHANNELS.index(channel))
    env.press(panel)

    assert env.asked, "GitHub was not asked"
    assert env.wx.titles == ["No Update Available"]
    assert env.wx.is_information()
    text = env.wx.messages[0][1]
    assert "No newer add-on" in text and CHANNEL_NAMES[channel] in text and env.installed in text
    assert env.wx.opened == [] and env.fetched == [] and env.started == []


def test_the_answer_names_the_channel_that_was_asked_about(env):
    # The check is out on a worker thread; the choice can be moved meanwhile.
    env.releases = UNDATED
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(1)
    env.monkeypatch.setattr(env.mod, "_runInBackground",
                            lambda work, done: (panel.updateChannelCtrl.SetSelection(0), env._inline(work, done)))
    env.press(panel)
    assert env.wx.titles == ["No Update Available"]
    assert "Prerelease" in env.wx.messages[0][1] and "Stable" not in env.wx.messages[0][1]


def test_a_beta_only_is_offered_on_the_prerelease_channel(env):
    env.releases = BETA_ONLY
    panel = env.panel()
    panel.updateChannelCtrl.SetSelection(1)
    env.press(panel)
    assert env.wx.titles[0] == "Update Available" and env.fetched == [OFFERED["prerelease"][1]]


@pytest.mark.parametrize("answer_yes", [True, False], ids=["yes", "no"])
@pytest.mark.parametrize("why", ["network", "not-githubs-list", "secure-screen", "the-worker-raised",
                                 "installed-version-unreadable", "installed-version-unreadable-nothing-offered"])
def test_a_check_that_fails_offers_the_releases_page(env, why, answer_yes):
    panel = env.panel()
    if why == "network":
        env.down = OSError("no route to host")
    elif why == "not-githubs-list":
        env.answer = b"<html>Service unavailable</html>"
    elif why == "secure-screen":
        env.globalVars.appArgs.secure = True  # after the button was made
    elif why == "the-worker-raised":
        def broken(channel="stable", **kw):
            raise RuntimeError("no thread for you")
        env.monkeypatch.setattr(env.updates, "latest_release", broken)
    else:
        env.installed = None
        if why.endswith("nothing-offered"):
            env.releases = UNDATED
    env.wx.answers["Version Check Failed"] = env.wx.YES if answer_yes else env.wx.NO
    env.press(panel)

    assert env.wx.titles == ["Version Check Failed"]
    assert env.wx.styles[0] & env.wx.ICON_WARNING and env.wx.styles[0] & env.wx.YES_NO == env.wx.YES_NO
    if why == "network":
        assert "no route to host" in env.wx.messages[0][1]
    if why == "the-worker-raised":
        assert "no thread for you" in env.wx.messages[0][1]
    assert env.wx.opened == ([env.updates.RELEASES_PAGE] if answer_yes else [])
    assert env.updates.RELEASES_PAGE == "https://github.com/tgeczy/TGSpeechBox/releases"
    assert env.fetched == [] and env.started == []


# ---------------------------------------------------------------------------
# In NVDA's language
# ---------------------------------------------------------------------------

def _pseudo(s):
    """A stand-in translation: the sentence in capitals, its {} left alone."""
    import re
    return "".join(part if part.startswith("{") else part.upper() for part in re.split(r"(\{[^{}]*\})", s))


def _small_letters(text, *filled_in):
    """The words of a dialog that did not go through _(), apart from what
    was filled into its sentences."""
    import re
    for detail in filled_in:
        text = text.replace(detail, "")
    return re.findall(r"[a-z]+", text)


@pytest.mark.parametrize("why,detail", [("network", "no route to host"), ("secure-screen", ""),
                                        ("installed-version-unreadable", ""), ("nothing-offered", ""),
                                        ("up-to-date", ""), ("the-worker-raised", "no thread for you")])
def test_what_the_check_says_is_in_nvdas_language(env, why, detail):
    """The reason came from updates.py as an English sentence ("no release
    offers an add-on to update to", "not while NVDA is on a secure screen")
    and was put inside the translated dialog.  Here _() puts its sentence in
    capitals; what is left in small letters never went through it.  Only the
    system's own words for a failure may be: they follow a sentence that is
    translated."""
    env.monkeypatch.setattr(env.mod, "_", _pseudo, raising=False)
    panel = env.panel()
    if why == "network":
        env.down = OSError(detail)
    elif why == "secure-screen":
        env.globalVars.appArgs.secure = True
    elif why == "installed-version-unreadable":
        env.installed = None
    elif why == "nothing-offered":
        env.releases = UNDATED
    elif why == "up-to-date":
        env.installed = "2026.11.20"
    else:
        def broken(channel="stable", **kw):
            raise RuntimeError(detail)
        env.monkeypatch.setattr(env.updates, "latest_release", broken)
    env.press(panel)

    assert len(env.wx.messages) == 1
    title, text = env.wx.messages[0]
    assert title == title.upper() and not _small_letters(text, detail), text
    if detail:
        # The system's words come after a sentence of ours, not in place of one.
        before = text[:text.index(detail)]
        assert len(before.split("\n")) >= 2 and before.split("\n")[1].strip(), text


def test_every_reason_updates_gives_has_a_sentence(env):
    env.monkeypatch.setattr(env.mod, "_", _pseudo, raising=False)
    reasons = [getattr(env.updates, name) for name in ("FETCH_FAILED", "SECURE_SCREEN")]
    sentences = {env.mod._updateFailureText(code, "") for code in reasons}
    assert len(sentences) == 2 and not any(_small_letters(s) for s in sentences)
    # A code this panel does not know is still a failed check, with its code shown.
    assert "from-the-future" in env.mod._updateFailureText("from-the-future", "")


def test_a_download_that_fails_says_so_and_installs_nothing(env):
    panel = env.panel()
    real_fetch = env.updates.fetch

    def fetch(url, **kw):
        env.down = OSError("connection reset")
        return real_fetch(url, **kw)

    env.monkeypatch.setattr(env.updates, "fetch", fetch)
    env.press(panel)
    assert env.wx.titles == ["Update Available", "Download Error"]
    assert "connection reset" in env.wx.messages[1][1]
    assert env.started == []


def test_a_file_windows_will_not_open_is_named(env):
    def startfile(path):
        raise OSError("no application is associated")

    env.monkeypatch.setattr(env.mod.os, "startfile", startfile, raising=False)
    panel = env.panel()
    env.press(panel)
    assert env.wx.titles == ["Update Available", "Warning"]
    assert env.addon_file in env.wx.messages[1][1]


def test_the_listener_hears_that_it_is_working(env):
    # The network takes a moment and the dialog only comes afterwards.
    panel = env.panel()
    env.press(panel)
    assert len(env.spoken) == 2  # checking, then downloading


# ---------------------------------------------------------------------------
# The documentation
# ---------------------------------------------------------------------------

def test_the_readme_says_what_the_button_and_the_channel_do(env):
    """By the names the panel shows, so a renamed control is renamed there too."""
    sizer = FakeSizerHelper()
    panel = env.panel(built=False)
    panel._makeUpdateControls(sizer, env.wx)
    readme = " ".join((REPO / "readme.md").read_text(encoding="utf-8").split())
    assert panel.updateButton.label in readme
    assert sizer.labels[0].rstrip(":") in readme
    stable, prerelease = panel.updateChannelCtrl.choices
    assert f"**{stable}**" in readme and f"**{prerelease}**" in readme
    # Which releases each channel is: release candidates are full releases
    # on GitHub, the betas are its prereleases.
    at = readme.index(f"**{stable}**")
    assert "release candidates" in readme[at:readme.index(f"**{prerelease}**")]
    assert "betas" in readme[readme.index(f"**{prerelease}**"):][:200]


# ---------------------------------------------------------------------------
# Secure screens
# ---------------------------------------------------------------------------

def test_a_secure_screen_has_no_update_controls(env):
    # There NVDA runs as SYSTEM on the sign-in desktop.
    env.globalVars.appArgs.secure = True
    sizer = FakeSizerHelper()
    panel = env.panel(built=False)
    panel._makeUpdateControls(sizer, env.wx)
    assert sizer.labels == []
    assert getattr(panel, "updateButton", None) is None
    assert getattr(panel, "updateChannelCtrl", None) is None
    env.press(panel)   # a handler reached anyway does nothing
    panel.onSave()     # and saving the panel does not trip over the missing choice
    assert env.asked == [] and env.fetched == [] and env.wx.messages == []


# ---------------------------------------------------------------------------
# Off the GUI thread
# ---------------------------------------------------------------------------

def test_the_network_is_not_waited_for_on_the_gui_thread(env, monkeypatch):
    monkeypatch.setattr(env.mod, "_runInBackground", env.real_worker)  # the module's own, with its thread
    gate = threading.Event()
    threads = []
    real_latest = env.updates.latest_release

    def slow(channel="stable", **kw):
        threads.append(threading.current_thread())
        assert gate.wait(10)
        return real_latest(channel)

    monkeypatch.setattr(env.updates, "latest_release", slow)
    panel = env.panel()
    env.press(panel)

    # The press returned while the check is still out: the dialog is not frozen.
    assert env.wx.messages == [] and not env.wx.later
    assert env.spoken == ["Checking for updates..."]
    # A second press starts no second check; the listener hears again that
    # the first is still out.
    env.press(panel)
    assert env.spoken == ["Checking for updates..."] * 2
    # The button has the focus and is never disabled: a disabled control
    # with the focus leaves the keyboard nowhere to go.
    assert panel.updateButton.disabled == 0
    gate.set()
    assert env.wx.posted.wait(10)
    for t in threads:
        t.join(10)
    assert len(threads) == 1, "a press while a check is running started another"
    assert threads[0] is not threading.main_thread()
    # The result came back through wx.CallAfter; nothing was shown from the worker.
    assert env.wx.messages == [] and len(env.wx.later) == 1
    env.wx.answers["Update Available"] = env.wx.NO
    fn, args = env.wx.later.pop()
    fn(*args)  # the GUI thread's turn
    assert env.wx.titles == ["Update Available"]
    # The check is over: the next press asks again.
    env.wx.posted.clear()
    env.press(panel)
    assert env.wx.posted.wait(10) and len(threads) == 2
    threads[1].join(10)
    assert env.spoken == ["Checking for updates..."] * 3
    assert panel.updateButton.disabled == 0


def test_a_panel_that_is_gone_when_the_answer_arrives_shows_nothing(env):
    # The settings dialog was closed while the check was out; a destroyed wx
    # window is false.
    cls = env.mod._getPanelClass()
    gone = type("Gone", (cls,), {"__bool__": lambda self: False})()
    gone._onUpdateChecked(("2027.1.9", "page", OFFERED["prerelease"][1]), None)
    gone._onUpdateFetched(env.addon_file, None)
    assert env.wx.messages == [] and env.fetched == [] and env.started == []
