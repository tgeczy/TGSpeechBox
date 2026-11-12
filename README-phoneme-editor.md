# TGSpeechBox Phoneme Editor (Windows)

The Phoneme Editor is where you change how TGSpeechBox sounds and reads, and hear the result straight away. With it you can:

- tune any phoneme (formants, bandwidths, noise, timing) and play it;
- change how a language turns eSpeak's IPA into TGSpeechBox's phonemes, and the settings it uses;
- edit a language's dictionaries: pronunciation, stress, compounds, letter names and your own user entries;
- make and edit **voice profiles**, from the sound of the voice down to single phonemes;
- edit allophone and coarticulation rules;
- type text or IPA, speak it and save it as a WAV file.

It's a plain Win32 program with standard Windows controls, so every part of it works from the keyboard and with screen readers.

**Contents**

- [Getting started](#getting-started)
- [Keyboard](#keyboard)
- [The main window](#the-main-window)
- [Editing a phoneme](#editing-a-phoneme)
- [Language settings and classes](#language-settings-and-classes)
- [Dictionaries](#dictionaries)
- [Voice profiles](#voice-profiles)
- [Speech settings](#speech-settings)
- [Allophone rules](#allophone-rules)
- [Special coarticulation rules](#special-coarticulation-rules)
- [Phonemizer settings](#phonemizer-settings)
- [Saving, files and what is kept](#saving-files-and-what-is-kept)
- [Taking your work to NVDA, SAPI and the phone apps](#taking-your-work-to-nvda-sapi-and-the-phone-apps)
- [Sharing your changes](#sharing-your-changes)
- [Building from source](#building-from-source)

Related guides: [Tuning.md](Tuning.md) (every language setting, phoneme field and rule), [Dictionary-editing.md](Dictionary-editing.md) (dictionary formats), [Developers.md](Developers.md) (the APIs).

## Getting started

Download `TGSBPhonemeEditor-<version>.zip` from the [releases page](https://github.com/tgeczy/TGSpeechBox/releases) and unzip it to a folder you can write to (not Program Files). The folder holds:

| File or folder | What it is |
|---|---|
| `tgsbPhonemeEditor.exe` | the editor |
| `speechPlayer.dll`, `nvspFrontend.dll` | the TGSpeechBox engine (DSP and frontend) |
| `packs\` | the language packs you edit: `phonemes.yaml`, `lang\*.yaml`, `dict\*.tsv` |
| `eSpeak NG\` | eSpeak NG, for turning text into IPA |
| `tgsbPhonemeEditor.ini` | your settings (made on first run) |

Start `tgsbPhonemeEditor.exe`. On first run it finds the engine DLLs, the `packs` folder and eSpeak NG next to itself, loads the packs, and you can start right away. It remembers the last language you had open.

If you move things around, or build from source, point it at them from the menus:

- **File > Open pack root...** (Ctrl+O): the folder that *contains* `packs`.
- **Settings > Set DLL directory...**: the folder with `speechPlayer.dll` and `nvspFrontend.dll`.
- **Settings > Set eSpeak directory...**: the folder with `espeak-ng.exe`. Without it, "Convert to IPA" and speaking plain text don't work, but IPA input still does.

The editor is 32-bit, so it also runs on older Windows; use the DLLs from the same release.

## Keyboard

Menus and file commands:

| Keys | Command |
|---|---|
| Ctrl+O | File > Open pack root |
| Ctrl+S (or Ctrl+L) | File > Save language YAML |
| Ctrl+P | File > Save phonemes YAML |
| F5 | File > Reload language YAML |
| Shift+F5 | File > Reload phonemes YAML |

Buttons in the main window, from anywhere in it:

| Keys | Button |
|---|---|
| Alt+P | Play (the phoneme selected in All phonemes) |
| Alt+C | Clone... |
| Alt+E | Edit... |
| Alt+G | Add to language... |
| Alt+L | Play from language |
| Alt+D | Edit phoneme in language... |
| Alt+S | Language settings... |
| Alt+A | Add mapping... |
| Alt+M | Edit mapping... |
| Alt+R | Remove mapping |
| Alt+I | Convert to IPA |
| Alt+K | Speak |
| Alt+W | Save WAV... |

The **Editor** menu opens the dictionary, voice profile, allophone rule and special coarticulation editors. The **Settings** menu holds the eSpeak and DLL folders, the phonemizer settings and the speech settings.

## The main window

The window has five parts, top to bottom. Tab moves between them.

### All phonemes

Every phoneme in `phonemes.yaml`, with a **Filter phonemes** box above it (type part of a key, such as `a_` or `ɾ`).

- **Play**: synthesizes the selected phoneme on its own and plays it. It plays what is in the editor now, saved or not.
- **Clone...**: copies a phoneme to a new key, so you can make a variant (for example `ɾ` to `ɾ_hu`) without touching the original.
- **Edit...**: opens the phoneme's fields (see [Editing a phoneme](#editing-a-phoneme)).
- **Add to language...**: starts a new normalization mapping in the current language whose target is the selected phoneme.

### Language

Pick a language from **Language**. The editor loads `packs\lang\<language>.yaml`, and the language-dependent lists below follow it.

- **Phonemes in language**: the phonemes the language's mappings use.
- **Play from language** and **Edit phoneme in language...**: the same as Play and Edit, for a phoneme in this list.
- **Language settings...**: the language's `settings:` (see [Language settings and classes](#language-settings-and-classes)).

### Normalization mappings

The language's `normalization: replacements:` rules, which rewrite eSpeak's IPA into the phoneme keys TGSpeechBox plays (for example `r` to `ɾ_es`). Each row shows **From**, **To** and **When**.

**Add mapping...** and **Edit mapping...** ask for:

- **From (IPA)** and **To (phoneme key)**;
- **At word start**, **At word end**;
- **Before class**, **After class**, **Not before class**, **Not after class**: limit the rule to sounds next to a class of phonemes (vowels, stops, a class you made);
- **Edit Classes...**: the language's phoneme classes (see below).

A rule's cross-word conditions (`nextWordStartsClass`, `nextWordStartsNotClass`, `prevWordEndsClass`, `prevWordEndsNotClass`) and the further candidates of a `to` written as a list have no fields in the dialog; the When column shows the conditions, and adding, editing or removing mappings keeps both. Change them in the YAML.

**Remove mapping** deletes the selected rule. Order and conditions are explained in [Tuning.md, Normalization](Tuning.md#normalization).

### Skip inherited rules

A regional pack (`es-mx`, `en-gb`) inherits its base language's rules. **Add skip rule...** lists a From (and optionally a To) from the base language that this language should not use; **Remove skip rule** takes one back. See [Tuning.md, skipReplacements](Tuning.md#skipreplacements).

### Speak and IPA

- **Input text**: type text, or IPA if you tick **Input is IPA**.
- **Convert to IPA**: runs the text through eSpeak (or your own phonemizer) and shows the IPA in **IPA output**. Copy it into Input text, tick Input is IPA, and change it by hand to try a pronunciation.
- **Speak**: speaks the input with the current language and [speech settings](#speech-settings).
- **Save WAV...**: the same, into a WAV file.

Speak and Save WAV read the pack files **as saved on disk**. After changing a phoneme, a mapping or a setting, save (Ctrl+P or Ctrl+S) before you speak to hear it. Play is the exception: it plays the phoneme as it is in the editor now.

Speak uses the language's stress and compound dictionaries. It doesn't yet use the Pronunciation, User and Character dictionaries, number reading or letter names: those run in the text pass the screen readers use before eSpeak. To hear a pronunciation entry in the editor, put its IPA in Input text with Input is IPA ticked; to hear it in context, use it in NVDA or SAPI (see [Taking your work to NVDA](#taking-your-work-to-nvda-sapi-and-the-phone-apps)).

## Editing a phoneme

**Edit...** (or **Edit phoneme in language...**) lists the phoneme's fields, each with a short description, for example "F1 freq (mouth openness)", "burst length (ms)", "closure voicing level". Select a field and choose **Edit value...**. Tick **Preview while typing** to hear the phoneme change as you type a value. OK keeps the change in the editor; Ctrl+P writes it to `phonemes.yaml`.

`phonemes.yaml` is shared by every language. When you change a phoneme while a language is loaded, the editor warns you first and lists the other languages that use it. You can then:

- **Yes**: make a copy just for this language. The editor clones the phoneme with the language's suffix (`ɾ` becomes `ɾ_hu`, `a` in pt-br becomes `a_ptbr`), puts your change in the copy, and adds a mapping from the original to the copy in this language. If the language already has its copy, only the fields you changed go into it; the rest of the copy's own tuning stays. Save both files (Ctrl+P and Ctrl+S).
- **No**: change the shared phoneme, in every language.
- **Cancel**: change nothing.

The editor also asks before you change a flag that decides what a phoneme *is* (`_isVowel`, `_isVoiced`, `_isStop`, `_isNasal`, `_isSemivowel`, `_isLiquid`, `_isAffricate`). Those change timing, prominence and syllables in every language.

When you save `phonemes.yaml`, the editor checks that each phoneme's formants rise in order (F1 below F2 below F3...) and asks before saving if one doesn't.

## Language settings and classes

**Language settings...** lists the language's `settings:` as key and value. **Add...** offers every known setting in a list (you can also type a new one), **Edit...** changes a value, **Remove** deletes it. A setting inside a block is listed under the block's name and its own joined together (`rateCompensation:` `enabled:` is `rateCompensationEnabled`), and an edit goes back into the block. To add a setting inside a block, type its path with dots (`rateCompensation.minimumDurations.vowelMs`); a name without dots is written as a plain key. Rule lists are not shown here; they have their own editors. **OK** changes only the settings you added, edited or removed, and nothing at all if you changed nothing. The change is kept in the editor until you save (Ctrl+S). What each setting does is in [Tuning.md, Language pack settings reference](Tuning.md#language-pack-settings-reference).

**Edit Classes...** (in the mapping dialog) lists the language's classes. Choose one to see its members, one IPA symbol per line; **Add...** and **Remove** make and delete classes.

## Dictionaries

**Editor > Edit dictionary...** opens the dictionary editor. Pick a **Language** and a **Type**:

| Type | File in `packs\dict\` | Each entry |
|---|---|---|
| Pronunciation | `<lang>-dict.tsv` | From, To, From IPA, To IPA, Category: the shipped pronunciation fixes |
| User | `<lang>-user.tsv` | the same five columns, for your own entries; they win over the Pronunciation dictionary |
| Stress | `<lang>-stress.tsv` | Word and its stress pattern (`1 0` = stress on the first syllable) |
| Compound | `<lang>-compounds.tsv` | Word and the parts it splits into |
| Character | `<lang>-letters.tsv` | Symbol and what to say for it (letter names, `#` as "hash") |

Some dictionaries are shared by every regional variety of a language: English compounds are in `en-compounds.tsv`, Spanish and Portuguese letter names in `es-letters.tsv` and `pt-letters.tsv`. For Pronunciation, Compound and Character, a regional language (en-us, es-mx, pt-br) with no entries of its own opens the base language's file, which is where it speaks from; a change there is heard in every variety of the language. The window title names the file that is open. Once a regional file has entries, the frontend reads it *instead of* the shared one, and so does the editor.

- **Search** narrows the list to entries starting with what you type.
- **Add**, **Edit**, **Delete** change entries. For Pronunciation and User entries:
  - **From** is the word, **To** what to say instead (a respelling).
  - **From IPA** and **To IPA** are optional. Put IPA in **To IPA** to say the word with exactly those phonemes; it can use the pack's own keys (`a_es`, `ɾ_hu`).
  - **Convert to IPA** fills the IPA box from the text above it, through eSpeak.
  - **Insert phoneme into From/To IPA** opens a list of the language's phonemes. **Preview** plays the selected one; pick one to insert it.
  - **Category** is a free label for grouping entries.
- **Import...** adds the entries of another TSV file (duplicates are skipped); **Export...** writes the list to a TSV file of your choice.
- **Save** writes the file. Close asks if there are unsaved changes.

The formats and how lookups work are in [Dictionary-editing.md](Dictionary-editing.md).

## Voice profiles

A voice profile is a voice of its own, built on top of the phoneme table. It lives in the `voiceProfiles:` section of `phonemes.yaml` and can hold:

- **a voice source** (`voicingTone`): the shape of the voice itself, such as voice tilt, speed quotient, head size, breathiness of the source, tremor, chorus;
- **class scales** (`classScales`): multipliers for groups of sounds (vowels, nasals, stops, voiced fricatives...), such as pitch ×1.4 for every vowel, or formant shifts per formant;
- **phoneme overrides** (`phonemeOverrides`): exact values for single phonemes, such as a different F2 for `ɪ`;
- **an inflection scale**: how much livelier or flatter than the listener's own inflection setting.

Profiles appear in the Voice list of every TGSpeechBox host (NVDA, SAPI, Android, iOS, Linux) next to the built-in voices (Adam, Benjamin, Caleb, David, Robert). There are two ways to make and change one, and you will usually use both.

### From the sound: Speech settings > Save to Profile

1. Open **Settings > Speech settings...** and pick a **Voice**: a built-in voice to start from, or a profile to change.
2. Move the sliders under **Voicing parameters** (and Rate, Pitch, Inflection) and press Speak in the main window until it sounds right. Each voicing slider starts at the selected voice's own value, and 50 is the engine's default.
3. Choose **Save to Profile...** and give a **Profile name** and an **Inflection scale**.
   - Starting from a built-in voice, the editor makes a new profile with that voice's pitch and formant shape, the voicing sliders you moved, and the inflection scale.
   - Starting from a profile, keeping its name updates it; a new name makes a copy with your changes.
   - Saving under the name of a *different* profile that already exists asks first, and then replaces only its voice source and inflection scale; its class scales and phoneme overrides stay.
   - The suggested inflection scale keeps the inflection you hear now: after the save the Inflection slider goes back to 50 and the profile's scale carries the difference (1 = the listener's own inflection, 1.3 = a third livelier).
4. The profile is written to `phonemes.yaml` at once and selected in the Voice list.

Only the sliders you moved are written, so a profile keeps the values you didn't touch. The **Voice quality** sliders (creakiness, breathiness, jitter, shimmer, sharpness) are listener settings, as in NVDA's voice panel, and are not saved into a profile. A profile name can't contain a colon.

### From the inside: Editor > Edit voice profiles

**Editor > Edit voice profiles...** lists the profiles in `phonemes.yaml`, with **Add...**, **Edit...**, **Duplicate** and **Delete**. **Edit...** shows:

- **Profile name** and **Inflection scale** (from 0 to 3, 1 = as the listener set it).
- **Class Scales (multipliers)**: pick a **Class**, then a **Field** and a **Value**, and **Set** it. Fields are multipliers such as `voicePitch_mul`, `voiceAmplitude_mul`, `fricationAmplitude_mul`, `glottalOpenQuotient_mul`, and per-formant lists such as `cf_mul` written `[1.0, 1.1, 1.2, ...]` (F1 first). **Remove Selected** and **Remove Class** take values back out.
- **Phoneme Overrides (absolute values)**: **Add...** a phoneme by its IPA symbol, then set exact field values for it.

**Save & Close** writes `phonemes.yaml`. The profile's voice source is kept as it is; change it with Save to Profile. The fields and classes are described in [Tuning.md, Voice profiles](Tuning.md#voice-profiles-optional).

## Speech settings

**Settings > Speech settings...** controls how Speak and Save WAV sound. It doesn't change any pack file, except through Save to Profile.

- **Voice**: the built-in voices and every profile in `phonemes.yaml`.
- **Basic**: Rate, Pitch, Volume, Inflection, as in NVDA.
- **Frame parameters**: a multiplier for each synthesis parameter of every frame (pitch, formants, bandwidths, amplitudes). 50 leaves it unchanged, 100 doubles it, 0 turns it off. Useful for hearing what a parameter does.
- **Voicing parameters**: the voice source (the same settings a profile's voice source holds). These are the sliders Save to Profile writes.
- **Voice quality (FrameEx)**: creakiness, breathiness, jitter, shimmer, sharpness, added to every frame.

Each group has **Reset parameter** and **Reset all**. The settings are kept in `tgsbPhonemeEditor.ini` under `[speech]`. The pause after punctuation follows `pauseMode` there (`off`, `short` or `long`, as in NVDA).

## Allophone rules

**Editor > Edit allophone rules...** edits the current language's `allophoneRules:`, which change a phoneme by where it is: aspirate a stop at the start of a stressed syllable, shorten a vowel before a voiceless consonant, insert a glottal stop.

The list runs in order (for replace and insert rules the first match wins); **Move Up** and **Move Down** change the order. **Add...** and **Edit...** open the rule:

- **Match**: Name, Phonemes, Flags and Not flags, Token type, Position, Place, Stress, the phonemes or flags After and Before it, Before or After same phoneme.
- **Action**, with its own fields: **Replace** (with another phoneme, a duration, removing or scaling closure and aspiration), **Scale** (duration, fade, and field scales, one `fieldName: value` per line), **Shift** (field targets, `field: target=N, blend=M` or `field: delta=N`), **Insert** (a phoneme with its duration, fade and contexts).

The change is kept in the editor until you save the language (Ctrl+S). The rule engine is described in [Tuning.md, Allophone rules](Tuning.md#allophone-rules-yaml-driven-rule-engine).

## Special coarticulation rules

**Editor > Edit special coarticulation rules...** edits the language's `specialCoarticulation:` rules, which move a vowel's formant near certain consonants (for example F2 lower next to a velarized l). Each rule has a Name, Triggers (IPA), a Vowel filter, the Formant and Delta Hz, the Side (left, right or both), Cumulative, and scales for unstressed and phrase-final stressed vowels. Order them with Move Up and Move Down; save the language (Ctrl+S) to keep them. See [Tuning.md, Coarticulation](Tuning.md#coarticulation).

## Phonemizer settings

"Convert to IPA" and speaking text use a command-line phonemizer: by default `espeak-ng.exe` from the eSpeak directory. **Settings > Phonemizer settings...** changes it:

- **Template**: eSpeak NG (recommended), the Python `phonemize` package, or Custom.
- **Phonemizer exe**: leave blank to use eSpeak from the eSpeak directory.
- **Command type**: send text on standard input (recommended) or on the command line.
- **Args for STDIN** and **Args for CLI**, with the placeholders `{lang}` `{qlang}` `{text}` `{qtext}` `{espeakDir}` `{qespeakDir}` `{dataDir}` `{qdataDir}` `{pathArg}`. `{pathArg}` becomes `--path=...` when eSpeak's data folder is found.
- **Max chunk chars**: long text is split at sentence boundaries into pieces this long before phonemizing.

The same settings are in `tgsbPhonemeEditor.ini`:

```ini
[phonemizer]
; If empty, the tool uses espeak-ng.exe in [paths].espeakDir.
exe=
; Feed text on stdin (safer for long input); fall back to the command line.
preferStdin=1
maxChunkChars=420
; eSpeak defaults:
; argsStdin=-q {pathArg}--ipa=3 -b 1 -v {qlang} --stdin
; argsCli=-q {pathArg}--ipa=3 -b 1 -v {qlang} {qtext}
argsStdin=
argsCli=
```

TGSpeechBox works with any phonemizer that writes IPA; eSpeak NG is the default.

## Saving, files and what is kept

| Change | Written when | File |
|---|---|---|
| Phonemes, phoneme clones | Ctrl+P | `packs\phonemes.yaml` |
| Mappings, skip rules, classes, settings, allophone and coarticulation rules | Ctrl+S | `packs\lang\<language>.yaml` |
| Voice profiles (either way) | Save & Close, or Save to Profile | `packs\phonemes.yaml`, at once |
| Dictionary entries | Save in the dictionary editor | `packs\dict\<language>-<type>.tsv` |
| Speech, phonemizer and folder settings | as you change them | `tgsbPhonemeEditor.ini` |

- **Comments are kept** where you didn't change anything. A YAML save rewrites only the sections and phonemes you changed; the rest of the file, comments included, stays as it was. Comments *inside* a phoneme you changed are lost.
- A dictionary save keeps the comments at the top of the file (where the entries come from and under which licence) and rewrites the entries below them.
- **Reload** (F5, Shift+F5) reads the file again from disk and asks first if you have unsaved changes. Saving `phonemes.yaml` also reloads it, so changes you made in a text editor meanwhile show up.
- Use git (or a copy of the `packs` folder) so you can see and undo what changed.

Some things are edited in a text editor instead: a language's `preReplacements`, its number words (`numberExpansion`), its `phonemes:` overrides, and a profile's voice source values typed by hand. [Tuning.md](Tuning.md) describes all of them.

## Taking your work to NVDA, SAPI and the phone apps

The editor edits its own `packs` folder. To use your changes in a screen reader:

- **NVDA**: in NVDA's settings, the **TGSpeechBox** category:
  - **Import Voice Profile Sliders...** replaces the add-on's `phonemes.yaml` with the one you pick, here `packs\phonemes.yaml` from the editor's folder. That brings your profiles *and* any phoneme changes. The voice reloads at once.
  - **Import User Dictionary...** brings in a user dictionary TSV.
  - An add-on update replaces the add-on's packs. Export your profiles and user dictionary first, and import them again after the update.
- **SAPI**: copy the changed files into the `packs` folder of the TGSpeechBox SAPI installation.
- **Android and iOS**: the apps have their own dictionary and profile editors; a user dictionary TSV can be imported there.

## Sharing your changes

If a change makes a language better for everyone, send it! Open an issue or a pull request on [GitHub](https://github.com/tgeczy/TGSpeechBox), and read [AGENTS.md](AGENTS.md) first: work on languages you speak, describe what you changed and how to hear it, and cite your sources for how a language sounds. Voice profiles are welcome as ideas or files in an issue.

## Building from source

The editor is built with the rest of TGSpeechBox (Visual Studio 2022 and CMake). The shipped editor is 32-bit:

```bat
cmake -S . -B build-x86 -A Win32
cmake --build build-x86 --config MinSizeRel --target tgsbPhonemeEditor
```

The executable is `build-x86\tools\tgsbPhonemeEditorWin32\MinSizeRel\tgsbPhonemeEditor.exe`. Put it next to `speechPlayer.dll`, `nvspFrontend.dll` (from the same build) and a copy of `packs`, or set the folders from the menus as in [Getting started](#getting-started). The editor's dictionary file handling is tested in `tests/cpp/test_editor_dict_tsv.cpp`.
