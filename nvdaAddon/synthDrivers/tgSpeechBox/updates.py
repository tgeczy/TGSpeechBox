# -*- coding: utf-8 -*-
"""Is there a newer add-on than the one running, on GitHub's releases?

Until 3.10 the add-on fetched one fixed file from the maintainer's own site,
with a version text file beside it, both uploaded by hand for every build.
Now the releases themselves are the source, and the listener chooses a
channel: stable releases only, or prereleases too (#103).

**Only when somebody presses the button.**  Nothing here runs on a timer or at
start-up: a screen reader that contacts a server on every launch tells that
server when its owner sits down, and an update dialog that appears on its own
takes the screen reader's focus away from what its owner was doing.

**Never on a secure screen.**  There NVDA runs as SYSTEM on the sign-in
desktop.  The caller hides the button there; this module refuses as well.

The choosing is pure (which release, which asset, is it newer) and tested in
tests/test_nvda_updates.py; the fetch is the smallest part and is handed in as
`opener` so the rest can be tested without a network.  The approach follows
the updater of the maintainer's panthera-speech add-on.
"""
import os
import re

#: The recent releases, newest first.  Not `/releases/latest`: that is one
#: release, never a prerelease, and its tag orders nothing we can compare.
RELEASES_API = ("https://api.github.com/repos/tgeczy/TGSpeechBox"
                "/releases?per_page=30")

#: Where to send somebody who would rather look for themselves.
RELEASES_PAGE = "https://github.com/tgeczy/TGSpeechBox/releases"

#: "stable": full releases only, which is what a release candidate is
#: published as.  "prerelease": what GitHub lists as a prerelease, the betas,
#: as well, and still any full release that is newer than those.
CHANNELS = ("stable", "prerelease")

#: Why a check has no version to give (latest_release).  Codes, not
#: sentences: the answer is shown in a dialog in NVDA's language, and this
#: module has no NVDA and no _().  The settings panel has the sentences.
FETCH_FAILED = "fetch-failed"        # the network, or an answer that is not GitHub's list
SECURE_SCREEN = "secure-screen"
#: Not a failure: GitHub answered, and no release on the channel carries a
#: dated add-on.
NOTHING_OFFERED = "nothing-offered"

#: The add-on is versioned by the date in its manifest (2026.11.10), not by the
#: release tag (v-310rc301): a release also carries the SAPI installer, the APK
#: and the Linux builds, and its tag orders none of them.  So the add-on's own
#: asset carries that date at the end of its name,
#: "TGSpeechBox-v310rc4-2026.11.14.nvda-addon", and the newest such asset wins.
#: A file without the date (every release before this updater) is never
#: offered: it cannot be newer than an add-on that has this code.
ADDON_ASSET = re.compile(r"^TGSpeechBox-(?:.*-)?\d{4}\.\d{1,2}\.\d{1,2}\.nvda-addon$", re.I)

_DATE = re.compile(r"(?<!\d)(\d{4})\.(\d{1,2})\.(\d{1,2})(?!\d)")

_USER_AGENT = "TGSpeechBox-NVDA-addon"


def parse_version(text):
    """-> (year, month, day) from a manifest version or an asset name, or None."""
    if not text:
        return None
    found = _DATE.search(str(text))
    if not found:
        return None
    return tuple(int(part) for part in found.groups())


def is_newer(latest, installed):
    """-> True when `latest` is a strictly later version than `installed`.

    Either side unreadable is False: an add-on that cannot tell says nothing
    rather than announce an update that may not exist.
    """
    a, b = parse_version(latest), parse_version(installed)
    if not a or not b:
        return False
    return a > b


def installed_version():
    """-> the running add-on's version string, or None.

    The manifest beside the add-on is what NVDA itself reads, and reading it
    directly works outside NVDA too.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    manifest = os.path.join(os.path.dirname(os.path.dirname(here)), "manifest.ini")
    try:
        with open(manifest, encoding="utf-8") as f:
            for line in f:
                key, sep, value = line.partition("=")
                if sep and key.strip().lower() == "version":
                    return value.strip().strip('"') or None
    except OSError:
        pass
    return None


def _secure():
    """-> True on NVDA's secure screens."""
    try:
        import globalVars
        return bool(globalVars.appArgs.secure)
    except Exception:
        return False  # not inside NVDA at all (the tests, a command line)


def newest_addon(releases, channel="stable"):
    """-> (versionString, releasePageUrl, addonUrl) for the newest add-on on `channel`.

    `releases` is GitHub's list, newest release first.  Drafts are never
    offered; prereleases only on the "prerelease" channel; anything that is not
    a known channel is "stable".  When the same add-on hangs on more than one
    release, the newest of those releases gives the page link.

    -> (None, None, None) when no release on the channel carries a dated add-on.
    """
    wantPrereleases = (channel == "prerelease")
    best = None
    for release in releases or []:
        if not isinstance(release, dict) or release.get("draft"):
            continue
        if release.get("prerelease") and not wantPrereleases:
            continue
        page = release.get("html_url") or RELEASES_PAGE
        for asset in release.get("assets") or []:
            name = asset.get("name") or ""
            url = asset.get("browser_download_url")
            if not url or not ADDON_ASSET.match(name):
                continue
            version = parse_version(name)
            if version is None:
                continue
            # Newest release first, so ">" and not ">=": the first release
            # carrying a given add-on keeps the page link.
            if best is None or version > best[0]:
                best = (version, page, url)
            break
    if best is None:
        return None, None, None
    return ".".join(str(n) for n in best[0]), best[1], best[2]


def latest_release(channel="stable", timeout=15, opener=None):
    """-> (versionString, releasePageUrl, addonUrl), or (None, reason, None).

    `reason` is (code, detail): FETCH_FAILED with the exception's own words,
    SECURE_SCREEN or NOTHING_OFFERED with none.

    `opener` is for the tests: anything that takes a URL and returns bytes.
    The default reaches the network, the one thing here that is not tested.
    """
    if _secure():
        return None, (SECURE_SCREEN, ""), None
    try:
        import json
        if opener is None:
            import urllib.request

            def opener(url):
                request = urllib.request.Request(url, headers={
                    # GitHub refuses a caller that does not say who it is.
                    "User-Agent": _USER_AGENT,
                    "Accept": "application/vnd.github+json",
                })
                with urllib.request.urlopen(request, timeout=timeout) as r:
                    return r.read()

        payload = json.loads(opener(RELEASES_API).decode("utf-8"))
    except Exception as e:
        return None, (FETCH_FAILED, str(e) or e.__class__.__name__), None
    if isinstance(payload, dict):
        payload = [payload]
    version, page, addon = newest_addon(payload, channel)
    if version is None:
        return None, (NOTHING_OFFERED, ""), None
    return version, page, addon


def fetch(url, timeout=120):
    """Download `url` into the temp folder and -> the file's path.

    An old file of the same name goes first, so a half-written download from a
    failed attempt is never what NVDA is handed.  Exceptions propagate; the
    caller turns them into words.
    """
    import tempfile
    import urllib.request

    name = url.rsplit("/", 1)[-1]
    if not name.lower().endswith(".nvda-addon"):
        name = "TGSpeechBox-update.nvda-addon"
    path = os.path.join(tempfile.gettempdir(), name)
    try:
        if os.path.isfile(path):
            os.remove(path)
    except OSError:
        pass
    request = urllib.request.Request(url, headers={"User-Agent": _USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as r:
        data = r.read()
    with open(path, "wb") as f:
        f.write(data)
    return path
