# AGENTS.md

Guidance for AI coding agents (and the people driving them) working on TGSpeechBox. Read this before you change
anything or open a pull request.

## What this project is

A formant speech synthesizer: a Klatt-style DSP engine with an LF glottal source, a frontend that turns IPA into
timed frames (coarticulation, allophones, timing, intonation), and language packs for 26+ languages. It ships as an
NVDA add-on, a SAPI 5 engine, Android and iOS/macOS apps, a Linux Speech Dispatcher module, a phoneme editor, and a
Python wheel.

**The engine is phonemizer-agnostic.** It takes IPA, and anything that produces IPA can feed it. eSpeak NG is the
default phonemizer today, and the only one on the phone apps for now; the desktop hosts and the Python wheel leave
more room, and a contributor has already run it with Gruut. Keeping it that way is the goal: do not tie the engine
or the frontend to one phonemizer. A quirk in one phonemizer's IPA (eSpeak writing "ɹɹ" for one r, say) is fixed in
the language pack's normalization rules, and says which phonemizer it is for.

The people who use it are mostly blind screen reader users who listen to it all day, often at high rates. Clarity at
speed, stability, and not surprising them matter more than any new feature.

## The four rules

Read these even if you read nothing else. A pull request that breaks one of them will not be merged.

1. **No tests, no merge.** Without tests we have no ground to stand on. Every pull request comes with a
   comprehensive set of tests covering the change: what it fixes, what it adds, and what it must leave alone.
   "It sounded fine to me" is not a test.
2. **Work only on languages you know.** Change a language only if you speak it natively or near-fluently *and* have
   lived or worked among the people who speak it. A voice is part of a culture, and its speakers hear it every day.
3. **Every feature is multilingual.** Nothing may be hard-coded for one language. We made that mistake early, we
   learnt it the hard way, and we are still far from good localization in every corner of the apps. Do not add to it.
4. **Back it up.** Cite research papers for how a language or a sound works. A claim without a source is a weak
   claim, and a pull request built on one is weaker for it.

## How the repository is organized

| Folder | What is there |
|---|---|
| `src/` | the DSP engine (`speechPlayer.dll`): voice source, resonators, frame queue |
| `src/frontend/` | the frontend (`nvspFrontend.dll`): pack loading, text preparation, IPA normalization, timing, pitch, frame emission; `passes/` holds the per-stage passes |
| `src/sapi/` | the SAPI 5 engine |
| `src/platforms/` | Android, iOS/macOS, Speech Dispatcher |
| `nvdaAddon/` | the NVDA add-on (synth driver, global plugin, locale) |
| `packs/` | everything that is language: `phonemes.yaml` (the phoneme table and voice profiles), `lang/<lang>.yaml` (settings, normalization rules, number words), `dict/<lang>-<type>.tsv` (pronunciation, stress, compound and character dictionaries) |
| `python/` | the `tgspeechbox` Python wheel (IPA in, audio and frames out) |
| `tools/` | the phoneme editor (`tgsbPhonemeEditorWin32/`), the SAPI settings app, `tgsbRender`, analysis scripts |
| `tests/` | pytest suites (frontend, wheel, NVDA driver in `tests/nvda/`, SAPI in `tests/sapi/`) and the doctest C++ suite in `tests/cpp/` |

`Developers.md` documents the APIs, `Tuning.md` the pack settings, `Dictionary-editing.md` the dictionaries.

## Language lives in data, not in code

Rule 3 in practice. It is the one pull requests most often break.

- **No words of any language in C++.** Unit names, number words, letter names, month names, joiners like "and" or
  "minus": all of it goes in the packs. The C++ supplies the mechanism; each language's pack supplies the words and
  the order they go in. `numberExpansion` in `packs/lang/en.yaml` is the model: the logic is shared, the words are
  per language.
- **A text feature is built for every language,** even if you only fill in one. If another language would need a C++
  change to use your feature, it is not done. Spanish, Hungarian, Portuguese and the rest must be able to add theirs by
  editing a pack or a dictionary.
- **A new kind of data** (a new dictionary type, a new block in the language yaml) is a design decision. Open an issue
  and agree the shape with the maintainer before you write it. Dictionary types show up in the editors on Windows,
  Android and iOS, so a new one touches all three.
- **Do not change other languages as a side effect.** A setting that is on for one language must leave every other
  language's output byte for byte the same, and a test should say so.

## Languages and the people who speak them

- **Native or near-fluent, and lived it.** Work on a language only if you speak it natively or near-fluently and
  have spent time living or working among its speakers. Knowing the grammar is not the same as knowing how people
  sound. If you are not that person for a language, find the person who is (below) instead of guessing.
- **A change that touches many languages needs their speakers.** If your pull request changes more than the
  languages you know, it is your job to ask for feedback from people who know the others. Look them up: go through
  the issues, discussions and past pull requests, find the contributors who reported or fixed things in each affected
  language, and mention them in your pull request asking them to listen. Do not leave it to the maintainer, and do not
  skip it because it takes time. A pull request that changes a language nobody who speaks it has heard is not ready.
- **Respect the language as it is spoken.** Regional varieties (Brazilian and European Portuguese, Mexican and
  Castilian Spanish, American and British English) are languages in their own right, not variants of a "real" one.
  Do not flatten one into another.

## Research and citations

- Cite your sources: phonetics papers, acoustic studies, reference grammars, a native speaker's report (link it).
  Put the citation in the code comment or the yaml comment next to the change, and in the pull request.
- Prefer measured data (formant studies, duration studies, published recordings) over impressions.
- When no source exists, say so plainly. An uncited change can still be right, but it carries less weight, and
  it will be held to a higher bar of listening.

## How we work: tests first, ears last

**No tests, no ground to stand on.** Every bug this project has shipped could have been caught by a test, and every
change without one is a guess that the next change can silently undo. A pull request without a comprehensive set of
tests covering the change will not be approved, however good it sounds.

**Test-driven.** A fix starts with a test that fails on the bug. A feature starts with the tests that say what it
must do, including what it must leave alone (other languages, other settings, other platforms). Run the tests before
your change and say in the pull request that they failed; a test that passes without your change is not testing it.
Cover the edges, not just the example in the issue.

**Every rate.** Anything that touches timing, duration or a phoneme's frames is checked at speeds 0.5, 1.0, 1.6, 2.0
and 3.0 (`pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])`). A rule that only works at 1.0 breaks for the
people who use the engine most.

**The human ear is the final gate.** Numbers can be right and the voice still wrong. A change to how anything sounds
is not done until a person who speaks the language has listened to it on a real add-on or app. A speech recognizer's
transcript is not evidence that a synthetic voice sounds right.

**Defaults are for everyone.** Turning a new behaviour on by default changes what thousands of people hear tomorrow.
Say why it should be on, or leave it off.

**Evidence rules.**
- Say what a result rests on, and what was not tested. "Not run" is a fine answer; a guess presented as a result is not.
- When an analysis stalls, stop and report what you found rather than trying variation after variation.

## Branches and releases

- Work goes on the **current version branch** (`v-` and the version, e.g. `v-310`), never on `master`. `master` only
  takes the final release merge. Point your pull request at the version branch.
- While a release candidate is out, the version branch takes **fixes only**. Features wait for the next version.
- Do not bump version numbers (the Android `versionCode` and `ASSET_VERSION`, the add-on manifest, the installers).
  The maintainer does that when releasing.

## Licences

The engine, frontend, packs and tools are MIT. eSpeak NG, the default phonemizer, is GPL-3.0 and is linked only by
the hosts that use it (see the readme). **No GPL code goes into the engine, the frontend or the packs.** Code ported from somewhere else says
where it came from and under which licence; if you port your own code from a GPL project, say that you hold its
copyright. Do not add a dependency without its licence and a reason.

## Pull requests

- **Open an issue first for anything bigger than a fix,** and wait for the maintainer to agree on the approach. An
  unasked-for feature in a pull request is likely to be turned down, however well made.
- **Include a comprehensive set of new tests** in `tests/` (pytest) or `tests/cpp/` (doctest), at every rate where
  timing is involved. No tests, no approval.
- **Say which languages you speak,** and for any other language the change touches, link where you asked its speakers
  to listen.
- **Cite your sources** for any change to how a language sounds or reads.
- **Say exactly what you ran**: which suites, on which platform, which tests failed before the change, and what you did
  not run. Say whether anyone has listened to it.
- **A human is in the loop for the final test.** A maintainer runs the suites and listens before merging. Write the
  description so they can: what changed, how to hear it, and where it could go wrong.
- Keep a pull request to one subject. Rebase on the version branch; do not reformat code you did not change.
- New files in `src/frontend/passes/` must also be listed in `Makefile.linux`, which names its sources by hand.
- **No machine paths** (anything naming a person's own folders or drives) in code, docs or commit messages. Use paths
  relative to the repository.
- **No one's personal handle in commits.** Credit people by first name, and only once they have agreed to it.
- Write code that reads like the code around it: its naming, its comment density, its idiom. Comments say why, and
  cite the evidence (an issue, a paper, a measurement, a test) when there is one.
- AI-written code is welcome, under the same rules. The person opening the pull request is responsible for it: read
  it, understand it, and run it before you ask someone else to.

## Building and testing

- Windows: Visual Studio 2022 and CMake.
  `cmake -S . -B build-x64-nvda -A x64 -DTGSB_BUILD_TESTS=ON`, then `cmake --build build-x64-nvda --config MinSizeRel`
  (`-A Win32` and `build-x86-nvda` for 32-bit; the NVDA add-on needs both).
- C++ tests: `ctest --test-dir build-x64-nvda -C MinSizeRel`, or run `tgsb_unit_tests` directly.
- Python tests: `python -m pytest -q tests` from the repository root, with a Python whose bitness matches the build.
  `tests/nvda/` needs an installed NVDA (`TGSB_NVDA_DIR`); `tests/sapi/` needs the SAPI engine registered.
- Linux: `make -f Makefile.linux` (see `README-linux.md`). Android: Gradle in `src/platforms/android`. Apple: Xcode on
  a Mac.
