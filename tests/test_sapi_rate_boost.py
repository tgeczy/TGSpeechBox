"""Rate boost is the same size on SAPI as in the NVDA add-on (#209).

"Rate boost" doubled the rate in the NVDA add-on and was a 1.35x time
stretch in the SAPI engine, and nothing pinned either number.  The NVDA
driver is the reference: SAPI's factor is read from its source here and
compared with the driver's.  What the factor does to the audio is tested in
tests/sapi/test_sapi_engine.cpp, which needs the built engine.
"""

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def _read(rel):
    return (REPO / rel).read_text(encoding="utf-8")


def nvda_boost():
    src = _read("nvdaAddon/synthDrivers/tgSpeechBox/speech_pipeline.py")
    m = re.search(r"'_rateBoost', False\):\s*\n\s*_effectiveSpeed \*= ([0-9.]+)", src)
    assert m, "the NVDA driver's rate boost was not found in speech_pipeline.py"
    return float(m.group(1))


def sapi_boost():
    src = _read("src/sapi/ISpTTSEngineImpl.cpp")
    m = re.search(r"constexpr double k_rate_boost = ([0-9.]+);", src)
    assert m, "no k_rate_boost in src/sapi/ISpTTSEngineImpl.cpp"
    return float(m.group(1))


def test_sapi_boost_is_the_nvda_drivers():
    assert nvda_boost() == 2.0
    assert sapi_boost() == nvda_boost()


def test_sapi_multiplies_the_speed_before_the_2x_split():
    # As the driver does: the boost goes onto the speed, and only what is
    # then above the cap becomes time stretch.
    src = _read("src/sapi/ISpTTSEngineImpl.cpp")
    boost = src.index("params.speed *= k_rate_boost")
    split = src.index("if (params.speed > k_synth_speed_cap)")
    assert boost < split


def test_no_other_factor_is_left_in_the_sapi_sources():
    for path in sorted((REPO / "src" / "sapi").glob("*")):
        if path.suffix in (".cpp", ".hpp", ".h"):
            assert "1.35" not in path.read_text(encoding="utf-8"), path.name


def test_the_linux_readme_names_only_hosts_that_double():
    # The sentence used to list SAPI and "iOS" while SAPI and the iOS system
    # voice (the Audio Unit extension) used 1.35x.
    text = _read("README-linux.md")
    m = re.search(r"Rate boost doubles the effective speech rate[^\n]*", text)
    assert m, "the rate boost sentence is gone from README-linux.md"
    sentence = m.group(0)
    assert "SAPI" in sentence
    au = _read("src/platforms/ios/au-extension/TGSBAudioUnit.swift")
    au_doubles = re.search(r"rateBoost\s*\{?\s*\n?\s*speed \*= 2(\.0)?\b", au) is not None
    if not au_doubles:
        assert "the iOS app" in sentence, "the iOS system voice does not double; say which iOS host does"
