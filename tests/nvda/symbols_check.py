"""Spanish ¿ and ¡ through NVDA's own symbol processing (#133).

Runs characterProcessing from an NVDA install's library.zip, with and without
the add-on's symbol dictionaries (nvdaAddon/locale/*/symbols-invertedmarks.dic),
and prints which marks reach the driver at each symbol level.  The driver
never sees text before this step, so the driver harness can't check it.
Adapted from ssi263-speech's stacked_q_symbols.py (the Braille Lite add-on's
stacked "?").  Run in its own process, with -S:

    python -S symbols_check.py "C:\\Program Files\\NVDA" <addon locale dir>

Exit 0 when the add-on keeps every mark at every level and NVDA alone does
not (so the check can tell the difference), 1 otherwise.
"""
import builtins
import os
import sys
import types

NVDA_DIR, ADDON_LOCALE = sys.argv[1], sys.argv[2]
sys.path[:] = [os.path.join(NVDA_DIR, "library.zip"), NVDA_DIR]


def stub(name, **kw):
    m = types.ModuleType(name)
    m.__dict__.update(kw)
    sys.modules[name] = m


class _Log:
    def __getattr__(self, n):
        return lambda *a, **k: None


builtins._ = lambda s: s
builtins.pgettext = lambda c, s: s
stub("logHandler", log=_Log())
stub("globalVars", appDir=NVDA_DIR, appArgs=types.SimpleNamespace(configPath="", secure=False))
stub("config", conf={"speech": {"symbolDictionaries": ["cldr"]}})
import characterProcessing as cp  # noqa: E402

LINES = {"Si vienes \u00bfme avisas?": "\u00bf", "dijo \u00a1basta! ya": "\u00a1"}
LEVELS = (cp.SymbolLevel.NONE, cp.SymbolLevel.SOME, cp.SymbolLevel.MOST, cp.SymbolLevel.ALL)
ok = True
for loc in ("en", "es"):
    base = cp.SpeechSymbols()
    base.load(os.path.join(NVDA_DIR, "locale", loc, "symbols.dic"))
    addon = cp.SpeechSymbols()
    addon.load(os.path.join(ADDON_LOCALE, loc, "symbols-invertedmarks.dic"), allowComplexSymbols=False)
    for label, sources in (("NVDA alone", (base, cp.SpeechSymbols())),
                           ("with the add-on", (base, addon, cp.SpeechSymbols()))):

        class Fetch:
            def fetchLocaleData(self, locale, fallback=True, s=sources):
                return s

        cp.SpeechSymbolProcessor.localeSymbols = Fetch()
        proc = cp.SpeechSymbolProcessor(loc)
        kept = all(mark in proc.processText(text, level) for text, mark in LINES.items() for level in LEVELS)
        print("%s %-16s \u00bf and \u00a1 reach the driver at every level: %s" % (loc, label, kept))
        if label == "with the add-on" and not kept:
            ok = False
        if label == "NVDA alone" and kept:
            ok = False  # nothing to fix, or the check can't see the difference
print("ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
