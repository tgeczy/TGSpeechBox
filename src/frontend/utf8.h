/*
TGSpeechBox — UTF-8 utility function declarations.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#ifndef TGSB_FRONTEND_UTF8_H
#define TGSB_FRONTEND_UTF8_H

#include <filesystem>
#include <string>
#include <string_view>

namespace nvsp_frontend {

// A file-system path from the UTF-8 text the API takes (nvspFrontend.h:
// packDirUtf8, overrideDirUtf8), and UTF-8 text back from a path.
//
// On Windows a narrow string handed to std::filesystem or std::ifstream is
// read in the system code page, so a pack folder with any letter outside
// ASCII in its path (a Windows account named Tamás or Győző holds the NVDA
// add-on under such a path) was "not found" and the synthesizer did not
// load.  Every path the frontend opens goes through these two
// (tests/test_pack_dir_unicode.py).  Bytes that are not UTF-8 are taken in
// the system code page, as all of them were before: a command-line tool
// handing over its argv.  On other platforms both are the identity.
std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& p);

// Best-effort UTF-8 -> UTF-32. Invalid sequences become U+FFFD.
std::u32string utf8ToU32(std::string_view s);

// UTF-32 -> UTF-8.
std::string u32ToUtf8(std::u32string_view s);

// Locale-independent single-codepoint lowercase for the alphabets the packs
// ship (ASCII, Latin-1 Supplement, Latin Extended-A, Greek, Cyrillic incl.
// Ukrainian/Extended). std::towlower is ASCII-only in the default "C"
// locale on MSVC, which is how capital letters silently missed the letter
// dictionaries (#122). Returns the input unchanged when it is not an
// uppercase letter in a covered block. Turkish dotted İ maps to plain i.
char32_t foldCodepointLower(char32_t c);

// True for a lowercase letter of those same alphabets: one that an uppercase
// letter folds to.  False for uppercase letters, digits, punctuation and
// scripts with no letter case.
bool isLowerCodepoint(char32_t c);

// True for whitespace/control and common punctuation (ASCII, Latin-1
// inverted marks/guillemets/middle dot/section, General Punctuation, CJK
// symbols, fullwidth forms). Used to find a lone letter wrapped in symbols
// ("r?", "¿r", "ó.") for letter-name lookup.
bool isPunctOrSpaceCodepoint(char32_t c);

// Lowercase ASCII and convert '_' -> '-' (for language tags).
std::string normalizeLangTag(std::string_view tag);

// Strip invisible/zero-width Unicode characters (ZWS, ZWNJ, ZWJ, BOM,
// soft hyphen, bidi controls, etc.).
std::string stripInvisible(std::string_view s);

// Canonical composition (NFC) for the Latin, Greek and Cyrillic scripts, with
// no platform library: a letter followed by combining marks ("e" + U+0301)
// becomes the one precomposed letter the dictionaries are written with.
// Text of other scripts, and compatibility forms (fullwidth letters,
// ligatures, superscripts), come back as they went in.
std::string composeCanonical(const std::string& s);

// The seven Latin ligatures of typeset text, U+FB00 to U+FB06 (ff, fi, fl,
// ffi, ffl, long-s t, st), as the letters of their compatibility
// decompositions (Unicode Character Database, UnicodeData.txt).  PDFs and
// e-books write "office" with one character for "ffi", and the phonemizer
// read that as "letter F B zero three" (tests/cpp/test_ligatures.cpp).
// Everything else comes back as it went in.
std::string foldLigatures(const std::string& s);

// Normalize UTF-8 string to NFKC form.
// Windows: NormalizeString (Vista+). Apple: CFStringNormalize.
// Other platforms (Linux, Android): foldLigatures, then composeCanonical,
// which composes letters.  The other compatibility forms (fullwidth letters,
// superscripts) are left alone there.
std::string normalizeNFKC(const std::string& s);

// Convenience: stripInvisible + normalizeNFKC in one call.
std::string normalizeText(const std::string& s);

} // namespace nvsp_frontend

#endif
