# -*- coding: utf-8 -*-
"""What "Save Voice Profile Sliders to YAML..." writes into phonemes.yaml (#195).

The button saves the voice that is heard.  On a profile the voice sliders are
offsets on the profile's stored values (voicing_tone.py, _applyVoicingTone),
so what is written is the composed value of each slider that was moved, and
those sliders then return to rest.  On a built-in voice the sliders are the
values, and saving makes a profile of that name: the whole voice source, and
the voice's own shape (the frame multipliers in constants.py) as class
scales, the way the phoneme editor's Save to Profile does it
(tools/tgsbPhonemeEditorWin32/profile_tone.cpp).

The frontend's nvspFrontend_saveVoiceProfileSliders is not used for this: it
writes thirteen keys over the block whatever was moved, the listener's voice
quality sliders among them, and it knows neither head size, chorus nor class
scales.

Nothing here needs NVDA: the choosing and the edit of the file are tested in
tests/test_nvda_profile_save.py, the button itself on the real driver in
tests/nvda/test_driver_profile_save.py.
"""
import os
import re


#: Why a save is refused, by code, in English for a log.  The listener is
#: shown the settings panel's sentence for the code, in NVDA's language:
#: there is no _() here, where nothing needs NVDA.
REFUSALS = {
    "toneUnreadable": "cannot read the profile's voicingTone: %s",
    "toneNotEditable": "cannot edit the profile's voicingTone as it is written: %s",
    "profilesNotABlock": "voiceProfiles is not written as a block",
    "profileOnOneLine": "profile %s is written on one line; edit it in the phoneme editor",
    "badName": "a profile cannot be named %r",
}


class ProfileSaveError(Exception):
    """phonemes.yaml is not written in a way this module can edit safely.

    `code` is a key of REFUSALS; `detail` is the line or the name it is about.
    """

    def __init__(self, code, detail=""):
        text = REFUSALS[code]
        Exception.__init__(self, text % detail if "%" in text else text)
        self.code, self.detail = code, detail


#: A profile's voice source: the keys of its voicingTone block, in the order
#: of speechPlayer.VoicingTone.
TONE_KEYS = (
    "voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
    "highShelfGainDb", "highShelfFcHz", "highShelfQ",
    "voicedTiltDbPerOct", "noiseGlottalModDepth",
    "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
    "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
    "nasalBwScale", "f4FreqScale", "nasalGainScale",
    "chorusDepth", "chorusDetuneHz",
)

#: What the frontend plays for a key a voicingTone block leaves out
#: (nvspFrontend_getVoicingTone).  The high shelf is 5.5 dB there and 4 dB
#: for a built-in voice, so a profile made from one always states it.
TONE_BLOCK_DEFAULTS = dict(zip(TONE_KEYS, (
    0.91, 0.92, 0.35, 5.5, 2000.0, 0.7,
    0.0, 0.0, 0.0, 0.0,
    2.0, 0.0, 1.0, 0.0,
    1.0, 1.0, 1.0,
    0.0, 2.0,
)))

#: The voice sliders _applyVoicingTone composes with a profile:
#: (driver setting, voicingTone key, rest position, the driver's per-voice store).
SLIDERS = (
    ("voiceTilt", "voicedTiltDbPerOct", 50, "_perVoiceTilt"),
    ("noiseGlottalMod", "noiseGlottalModDepth", 0, "_perVoiceNoiseGlottalMod"),
    ("pitchSyncF1", "pitchSyncF1DeltaHz", 50, "_perVoicePitchSyncF1"),
    ("pitchSyncB1", "pitchSyncB1DeltaHz", 50, "_perVoicePitchSyncB1"),
    ("speedQuotient", "speedQuotient", 50, "_perVoiceSpeedQuotient"),
    ("aspirationTilt", "aspirationTiltDbPerOct", 50, "_perVoiceAspirationTilt"),
    ("cascadeBwScale", "cascadeBwScale", 50, "_perVoiceCascadeBwScale"),
    ("voiceTremor", "tremorDepth", 0, "_perVoiceVoiceTremor"),
    ("headSize", "f4FreqScale", 50, "_perVoiceHeadSize"),
    ("chorusDepth", "chorusDepth", 0, "_perVoiceChorusDepth"),
    ("chorusDetune", "chorusDetuneHz", 33, "_perVoiceChorusDetune"),
)

#: The two classes every phoneme is in one of.  The frontend applies each
#: matching class in turn, so a whole voice's shape goes on these and on none
#: of the narrower ones.
ROOT_CLASSES = ("vowel", "consonant")

#: Keys of a built-in voice's preset that belong to its voice source, which
#: is saved from what is heard and not from the preset.
PRESET_VOICE_SOURCE_KEYS = ("voicedTiltDbPerOct", "headSize")

# The fields a profile's class scales have (voice_profile.cpp).
_SCALE_FORMANTS = re.compile(r"^(cf|pf|cb|pb|pa)([1-6])$")
_SCALE_FIELDS = (
    "voicePitch", "endVoicePitch", "vibratoPitchOffset", "vibratoSpeed",
    "voiceTurbulenceAmplitude", "glottalOpenQuotient", "voiceAmplitude",
    "aspirationAmplitude", "fricationAmplitude", "preFormantGain", "outputGain",
)


def formatValue(value):
    """A number as phonemes.yaml holds it: at most six decimals, no padding."""
    text = ("%.6f" % float(value)).rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def toneChanges(heard, sliders, hasStoredTone):
    """-> (keys to write with their values, keys to take out).

    `heard` is the voice source the player was given, `sliders` the driver's
    slider positions by setting name.

    A profile with a voicingTone block of its own (`hasStoredTone`): only the
    sliders that are off their rest position, so every value the listener did
    not touch stays as it is written.  Otherwise (a built-in voice, or a
    profile without a block, where the sliders are the values): the whole
    voice source, each key written when it differs from what a block plays
    without it and taken out when it does not.
    """
    toSet, toRemove = {}, []
    if hasStoredTone:
        for setting, key, rest, _store in SLIDERS:
            if int(sliders.get(setting, rest)) != rest:
                toSet[key] = float(heard[key])
        return toSet, toRemove
    for key in TONE_KEYS:
        value = float(heard[key])
        # Chorus detune rests at 33, which the slider's map makes 1.985 Hz;
        # at rest it stands for the engine's own 2 Hz, as in the phoneme editor.
        if key == "chorusDetuneHz" and int(sliders.get("chorusDetune", 33)) == 33:
            value = TONE_BLOCK_DEFAULTS[key]
        if abs(value - TONE_BLOCK_DEFAULTS[key]) < 1e-9:
            toRemove.append(key)
        else:
            toSet[key] = value
    return toSet, toRemove


def presetClassScales(preset):
    """-> (class scales, preset keys with no class-scale form).

    A built-in voice's preset multiplies or replaces fields of every frame
    (profile_utils.applyVoiceToFrame).  A multiplier of a field the class
    scales have is carried as it is, and an absolute 0 is the multiplier 0.
    Any other absolute value, and a multiplier of a field the class scales do
    not have, cannot be carried; those keys are returned so the listener can
    be told.
    """
    scales, notCarried = {}, []
    for key, value in preset.items():
        if key in PRESET_VOICE_SOURCE_KEYS:
            continue
        isMul = key.endswith("_mul")
        field = key[:-4] if isMul else key
        formant = _SCALE_FORMANTS.match(field)
        if isMul and formant:
            scales.setdefault(formant.group(1) + "_mul", [1.0] * 6)[int(formant.group(2)) - 1] = float(value)
        elif isMul and field in _SCALE_FIELDS:
            scales[key] = float(value)
        elif not isMul and field in _SCALE_FIELDS and float(value) == 0.0:
            scales[field + "_mul"] = 0.0
        else:
            notCarried.append(key)
    return scales, notCarried


def isWhisper(preset):
    """-> True for a built-in voice that whispers: no voicing, and an
    aspiration of its own, on every frame (Caleb).

    A profile made from one is not that voice, and the listener is told so
    before it is made.  The aspiration is an absolute value, which class
    scales cannot hold, so the profile is quieter; and class scales act on
    phonemes, so the frames the frontend puts in without a phoneme definition
    keep their voicing (the review of #195 traced them to those).  Measured
    in tests/nvda/test_driver_profile_save.py: RMS 980 against 588 on one
    sentence at rate 1.0, and four voiced frames where Caleb has none.
    """
    silent = float(preset.get("voiceAmplitude", preset.get("voiceAmplitude_mul", 1.0))) == 0.0
    return silent and float(preset.get("aspirationAmplitude", 0.0)) > 0.0


# ---------------------------------------------------------------------------
# The edit of phonemes.yaml
#
# Line by line, as the frontend and the phoneme editor do it: the file is
# large, hand-commented, and nothing outside the profile's own lines may
# change.  Each line keeps its own ending.

def _body(line):
    return line[:-1] if line.endswith("\r") else line


def _indent(line):
    return len(line) - len(line.lstrip(" "))


def _blank(line):
    text = _body(line).strip()
    return not text or text.startswith("#")


def _splitKey(line):
    """-> (key, the rest after the colon without a trailing comment), or (None, None)."""
    text = _body(line).strip()
    if ":" not in text or text.startswith("#"):
        return None, None
    key, rest = text.split(":", 1)
    return key.strip().strip("'\""), rest.split(" #", 1)[0].strip()


def _blockEnd(lines, start, indent, limit):
    """The first line after `start` that is not deeper than `indent`."""
    for i in range(start + 1, limit):
        if not _blank(lines[i]) and _indent(lines[i]) <= indent:
            return i
    return limit


def _lastContent(lines, start, end):
    """The index after the last line with content in [start, end)."""
    last = start
    for i in range(start, end):
        if not _blank(lines[i]):
            last = i + 1
    return last


def _scaleLines(scales, indent):
    """One class's scales: the plain multipliers, then the per-formant lists,
    as the phoneme editor orders them."""
    pad = " " * indent
    out = [pad + key + ": " + formatValue(value)
           for key, value in scales.items() if not isinstance(value, (list, tuple))]
    out += [pad + key + ": [" + ", ".join(formatValue(v) for v in value) + "]"
            for key, value in scales.items() if isinstance(value, (list, tuple))]
    return out


def _editTone(lines, at, end, fieldIndent, toneSet, toneRemove, cr):
    """Change the voicingTone of the profile whose fields are lines[at:end]."""
    toneAt = None
    for i in range(at, end):
        if not _blank(lines[i]) and _indent(lines[i]) == fieldIndent and _splitKey(lines[i])[0] == "voicingTone":
            toneAt = i
            break
    pad = " " * fieldIndent
    if toneAt is None:
        if toneSet:
            lines[at:at] = [pad + "voicingTone:" + cr] + [
                pad + "  " + key + ": " + formatValue(value) + cr for key, value in toneSet.items()]
        return
    rest = _splitKey(lines[toneAt])[1]
    if rest.startswith("{") and rest.endswith("}") and not re.search(r"[{}\[\]]", rest[1:-1]):
        # The phoneme editor's form, all on one line.
        values = {}
        for item in rest[1:-1].split(","):
            if not item.strip():
                continue
            if ":" not in item:
                raise ProfileSaveError("toneUnreadable", _body(lines[toneAt]).strip())
            key, value = item.split(":", 1)
            values[key.strip()] = value.strip()
        for key in toneRemove:
            values.pop(key, None)
        for key, value in toneSet.items():
            values[key] = formatValue(value)
        body = _body(lines[toneAt])
        tail = body.split(":", 1)[1]
        comment = tail[len(tail.split(" #", 1)[0].rstrip()):]
        lines[toneAt] = (pad + "voicingTone: {" + ", ".join(k + ": " + v for k, v in values.items())
                         + "}" + comment + lines[toneAt][len(body):])
        return
    if rest:
        raise ProfileSaveError("toneNotEditable", _body(lines[toneAt]).strip())
    # A block: one key a line under "voicingTone:".
    toneEnd = _blockEnd(lines, toneAt, fieldIndent, end)
    keyIndent = next((_indent(lines[i]) for i in range(toneAt + 1, toneEnd) if not _blank(lines[i])),
                     fieldIndent + 2)
    pending = dict(toneSet)
    kept = 0
    i = toneAt + 1
    while i < toneEnd:
        key = None if _blank(lines[i]) or _indent(lines[i]) != keyIndent else _splitKey(lines[i])[0]
        if key in toneRemove and key not in toneSet:
            del lines[i]
            toneEnd -= 1
            continue
        if key in pending:
            body = _body(lines[i])
            head, tail = body.split(":", 1)
            comment = tail[len(tail.split(" #", 1)[0].rstrip()):]  # "   # why", kept
            lines[i] = head + ": " + formatValue(pending.pop(key)) + comment + lines[i][len(body):]
        if key is not None:
            kept += 1
        i += 1
    if pending:
        at = _lastContent(lines, toneAt + 1, toneEnd)
        lines[at:at] = [" " * keyIndent + key + ": " + formatValue(value) + cr for key, value in pending.items()]
    elif not kept:
        # "voicingTone:" with nothing under it is no voice source to the
        # frontend; an empty map is one, with every value at its default.
        body = _body(lines[toneAt])
        lines[toneAt] = body.rstrip() + " {}" + lines[toneAt][len(body):]


def saveProfile(path, name, toneSet, toneRemove=(), classScales=None):
    """Write `toneSet` into profile `name`'s voicingTone in the phonemes.yaml
    at `path`, and take the keys in `toneRemove` out of it.

    A profile that is not there yet is added after the last one, with
    `classScales` ({class: {field: value or six values}}); an existing
    profile keeps the class scales and phoneme overrides it has.

    -> True when the profile was added, False when it was there.
    Raises ProfileSaveError, with the file untouched, when the profiles are
    written in a way that cannot be edited line by line.
    """
    with open(path, "rb") as f:
        raw = f.read()
    lines = raw.decode("utf-8").split("\n")
    cr = "\r" if any(line.endswith("\r") for line in lines) else ""
    toneRemove = tuple(toneRemove)

    # The voiceProfiles: section, a root key, and where it ends.
    section = None
    for i, line in enumerate(lines):
        if _body(line).startswith("voiceProfiles:"):
            if _splitKey(line)[1]:
                raise ProfileSaveError("profilesNotABlock")
            section = i
            break
    if section is None:
        sectionEnd = len(lines)
    else:
        sectionEnd = _blockEnd(lines, section, 0, len(lines))
    profileIndent = 2
    found = None
    if section is not None:
        profileIndent = next((_indent(lines[i]) for i in range(section + 1, sectionEnd) if not _blank(lines[i])), 2)
        for i in range(section + 1, sectionEnd):
            if _blank(lines[i]) or _indent(lines[i]) != profileIndent:
                continue
            key, rest = _splitKey(lines[i])
            if key == name:
                if rest:
                    raise ProfileSaveError("profileOnOneLine", name)
                found = i
                break

    created = found is None
    if not created:
        end = _blockEnd(lines, found, profileIndent, sectionEnd)
        fieldIndent = next((_indent(lines[i]) for i in range(found + 1, end) if not _blank(lines[i])),
                           profileIndent + 2)
        if toneSet or toneRemove:
            _editTone(lines, found + 1, end, fieldIndent, toneSet, toneRemove, cr)
    else:
        if not name or name != name.strip() or re.search(r"[:#\r\n]", name):
            raise ProfileSaveError("badName", name)
        pad = " " * profileIndent
        block = [pad + name + ":"]
        if toneSet:
            block.append(pad + "  voicingTone:")
            block += [pad + "    " + key + ": " + formatValue(value) for key, value in toneSet.items()]
        if classScales:
            block.append(pad + "  classScales:")
            for cls, scales in classScales.items():
                block.append(pad + "    " + cls + ":")
                block += _scaleLines(scales, profileIndent + 6)
        if section is None:
            block = ["", "voiceProfiles:"] + block
            at = _lastContent(lines, 0, len(lines))
        else:
            at = _lastContent(lines, section + 1, sectionEnd)
        if at >= len(lines):
            # The file ends inside the last line: that line gets an ending,
            # and the block's last line becomes the one without.
            lines[-1] += cr
            lines += [line + cr for line in block[:-1]] + [block[-1]]
        else:
            lines[at:at] = [line + cr for line in block]

    out = "\n".join(lines).encode("utf-8")
    if out != raw:
        writeFile(path, out)
    return created


def writeFile(path, raw):
    """Make `raw` the content of the file at `path`, whole or not at all.

    The save, and its undo when the frontend cannot load what was saved.
    """
    tmp = path + ".tmp"
    try:
        with open(tmp, "wb") as f:
            f.write(raw)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
