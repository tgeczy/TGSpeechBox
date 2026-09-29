# -*- coding: utf-8 -*-
"""eSpeak bridge — voice selection and IPA conversion.

Uses espeak_direct for all DLL access — completely independent of
NVDA's synthDrivers._espeak module.  No WavePlayer, no background
thread, no shared global state.
"""

from __future__ import annotations

import ctypes

from logHandler import log

from . import espeak_direct
from .text_utils import splitByScript

# ctypes c_char_p truncates at the first NUL byte, so our parser only sees
# the PRIMARY language tag in eSpeak's packed language list.  Secondary tags
# (e.g. "es-mx" inside the es-419 voice) are invisible.
# Map our pack tags to eSpeak's canonical primary tags here.
_ESPEAK_PRIMARY_TAG = {
    "en-ca": "en-us",
    "es-mx": "es-419",
}


def espeakSetVoiceDirect(langTag: str) -> bool:
    """Set eSpeak voice with accurate language matching.

    Walks espeak_ListVoices to find the best match for *langTag*,
    then calls espeak_SetVoiceByName synchronously.

    Returns True if the voice was set successfully.
    """
    voiceList = espeak_direct.getVoiceList()
    if not voiceList:
        return False

    tag = langTag.lower().replace("_", "-")
    # ctypes c_char_p truncates at the first NUL, so we only see each voice's
    # primary language tag.  Map our pack tags to eSpeak's primary tags.
    tag = _ESPEAK_PRIMARY_TAG.get(tag, tag)
    base = tag.split("-")[0] if "-" in tag else ""

    exactId = None
    exactPri = 99
    baseId = None
    basePri = 99

    for voice in voiceList:
        try:
            rawLangs = voice.languages
            vid = voice.identifier
        except AttributeError:
            continue
        if not rawLangs or not vid:
            continue

        # Parse packed languages: [priority_byte][nul-terminated-tag] repeated.
        data = rawLangs
        i = 0
        while i < len(data):
            pri = data[i]
            ltag = data[i + 1:]
            nul = ltag.find(b"\x00")
            if nul >= 0:
                ltag = ltag[:nul]
            ltagStr = ltag.decode("utf-8", errors="ignore").lower()

            if ltagStr == tag:
                if pri < exactPri:
                    exactPri = pri
                    exactId = vid
            elif base and ltagStr == base:
                if pri < basePri:
                    basePri = pri
                    baseId = vid

            # Advance past priority byte + tag + NUL
            i += 1 + len(ltag) + 1
            if nul < 0:
                break

    chosenId = exactId or baseId
    if not chosenId:
        return False

    # Decode bytes identifier to str for the API call.
    try:
        name = chosenId.decode("utf-8", errors="replace") if isinstance(chosenId, bytes) else chosenId
    except Exception:
        name = chosenId

    if espeak_direct.setVoiceByName(name):
        log.debug("TGSpeechBox: eSpeak voice set directly: %r -> %r", tag, name)
        return True

    log.debug("TGSpeechBox: setVoiceByName failed for %r", name)
    return False


def espeakTextToIPA(text: str, espeakDLL, phonemeMode: int) -> str:
    """Convert *text* to IPA using espeak_TextToPhonemes.

    Parameters
    ----------
    text : str
        The text to convert.
    espeakDLL :
        The ctypes-loaded eSpeak DLL handle.
    phonemeMode : int
        Phoneme mode flags for ``espeak_TextToPhonemes``.

    Returns an empty string on failure.
    """
    if not text:
        return ""
    if not espeakDLL:
        return ""

    textToPhonemes = getattr(espeakDLL, "espeak_TextToPhonemes", None)
    if not textToPhonemes:
        return ""

    textBuf = ctypes.create_unicode_buffer(text)
    textPtr = ctypes.c_void_p(ctypes.addressof(textBuf))
    chunks = []
    lastPtr = None
    while textPtr and textPtr.value:
        if lastPtr == textPtr.value:
            break
        lastPtr = textPtr.value
        try:
            phonemeBuf = textToPhonemes(
                ctypes.byref(textPtr),
                espeak_direct.espeakCHARS_WCHAR,
                phonemeMode,
            )
        except OSError:
            # Let OSError propagate so callers can disable eSpeak.
            raise
        if phonemeBuf:
            chunks.append(ctypes.string_at(phonemeBuf))
        else:
            break
    # eSpeak returns one clause per call, and a clause can end inside the
    # text we pass (at a dash, at Spanish ¿ and ¡).  Its output has no
    # leading or trailing space, so the clauses are joined with one; with
    # nothing between them, "wait — what" reached the frontend as one word.
    ipaBytes = b" ".join(chunks)
    try:
        return ipaBytes.decode("utf8", errors="ignore").strip()
    except Exception:
        return ""


def espeakTextToIPA_scriptAware(
    text: str,
    espeakDLL,
    phonemeMode: int,
    espeakLang: str,
    latinFallback: str,
) -> str:
    """Convert text to IPA, switching eSpeak language for foreign-script runs.

    When the active language uses a non-Latin script (Russian, Bulgarian,
    Greek, etc.) and the text contains Latin-script words, eSpeak would
    produce garbage IPA for those words.  This function detects script
    boundaries, temporarily switches eSpeak to the Latin fallback language,
    and reassembles the IPA.
    """
    if not text:
        return ""

    segments = splitByScript(text, espeakLang, latinFallback)

    # Fast path: no script switching needed (single segment, base lang).
    if len(segments) == 1 and segments[0][1] is None:
        return espeakTextToIPA(text, espeakDLL, phonemeMode)

    ipaChunks = []
    currentLang = espeakLang

    for segText, langOverride in segments:
        if not segText or not segText.strip():
            continue

        # Switch eSpeak language if needed.
        targetLang = langOverride or espeakLang
        if targetLang != currentLang:
            if espeakSetVoiceDirect(targetLang):
                currentLang = targetLang
            elif espeak_direct.setVoiceByLanguage(targetLang):
                currentLang = targetLang
            # else: fall through with current language

        ipa = espeakTextToIPA(segText, espeakDLL, phonemeMode)
        if ipa:
            ipaChunks.append(ipa)

    # Restore base language if we switched away.
    if currentLang != espeakLang:
        if not espeakSetVoiceDirect(espeakLang):
            espeak_direct.setVoiceByLanguage(espeakLang)

    return " ".join(ipaChunks)
