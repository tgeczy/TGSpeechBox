#include "nvspFrontend.h"

#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "ipa_engine.h"
#include "pack.h"

namespace nvsp_frontend {

struct Handle {
  std::string packDir;
  PackSet pack;
  bool packLoaded = false;
  // True once we have emitted at least one chunk of speech on this handle.
  // Used to optionally insert a tiny silence between consecutive queueIPA calls.
  bool streamHasSpeech = false;
  // True if the last emitted *real phoneme* in the previous chunk was vowel-like
  // (vowel or semivowel). Used to avoid inserting boundary pauses inside
  // vowel-to-vowel transitions (e.g. diphthongs split across chunks).
  bool lastEndsVowelLike = false;
  std::string langTag;
  std::string lastError;
  std::mutex mu;
};

static Handle* asHandle(nvspFrontend_handle_t h) {
  return reinterpret_cast<Handle*>(h);
}

static void setError(Handle* h, const std::string& msg) {
  if (!h) return;
  h->lastError = msg;
}

} // namespace nvsp_frontend

extern "C" {

NVSP_FRONTEND_API nvspFrontend_handle_t nvspFrontend_create(const char* packDirUtf8) {
  using namespace nvsp_frontend;
  try {
    auto* h = new Handle();
    h->packDir = packDirUtf8 ? std::string(packDirUtf8) : std::string();
    h->lastError.clear();
    return reinterpret_cast<nvspFrontend_handle_t>(h);
  } catch (...) {
    return nullptr;
  }
}

NVSP_FRONTEND_API void nvspFrontend_destroy(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  delete h;
}

NVSP_FRONTEND_API int nvspFrontend_setLanguage(nvspFrontend_handle_t handle, const char* langTagUtf8) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);

  h->lastError.clear();
  const std::string lang = langTagUtf8 ? std::string(langTagUtf8) : std::string();

  PackSet pack;
  std::string err;
  if (!loadPackSet(h->packDir, lang, pack, err)) {
    setError(h, err.empty() ? "Failed to load pack set" : err);
    return 0;
  }

  h->pack = std::move(pack);
  h->packLoaded = true;
  // Treat language change as the start of a new stream, so we don't
  // insert a segment boundary gap before the first chunk in the new language.
  h->streamHasSpeech = false;
  h->lastEndsVowelLike = false;
  h->langTag = normalizeLangTag(lang);
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_queueIPA(
  nvspFrontend_handle_t handle,
  const char* ipaUtf8,
  double speed,
  double basePitch,
  double inflection,
  const char* clauseTypeUtf8,
  int userIndexBase,
  nvspFrontend_FrameCallback cb,
  void* userData
) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  h->lastError.clear();

  if (!h->packLoaded) {
    // Default to "default" language if the caller didn't call setLanguage.
    PackSet pack;
    std::string err;
    if (!loadPackSet(h->packDir, "default", pack, err)) {
      setError(h, err.empty() ? "No language loaded and default load failed" : err);
      return 0;
    }
    h->pack = std::move(pack);
    h->packLoaded = true;
    h->langTag = "default";
  }

  if (!ipaUtf8) ipaUtf8 = "";

  char clauseType = '.';
  if (clauseTypeUtf8 && clauseTypeUtf8[0]) {
    clauseType = clauseTypeUtf8[0];
  }

  std::vector<Token> tokens;
  std::string err;
  if (!convertIpaToTokens(h->pack, ipaUtf8, speed, basePitch, inflection, clauseType, tokens, err)) {
    setError(h, err.empty() ? "IPA conversion failed" : err);
    return 0;
  }

  // Determine whether this chunk starts/ends with a vowel-like phoneme.
  // We ignore silence/preStopGap tokens for this purpose.
  const Token* firstReal = nullptr;
  const Token* lastReal = nullptr;
  for (const Token& t : tokens) {
    if (!t.def || t.silence) continue;
    if (!firstReal) firstReal = &t;
    lastReal = &t;
  }

  auto isVowelLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsVowel) || (f & kIsSemivowel);
  };

  auto isLiquidLike = [](const Token& t) -> bool {
    if (!t.def) return false;
    const std::uint32_t f = t.def->flags;
    return (f & kIsLiquid) || (f & kIsTap) || (f & kIsTrill);
  };

  const bool startsVowelLike = firstReal && isVowelLike(*firstReal);
  const bool startsLiquidLike = firstReal && isLiquidLike(*firstReal);
  const bool endsVowelLike = lastReal && isVowelLike(*lastReal);
  const bool hasRealPhoneme = (firstReal != nullptr);

  // Optional: insert a short silence between consecutive queueIPA calls.
  // This helps when callers stitch UI speech from multiple chunks.
  //
  // However, a boundary pause can create an audible "hole" in vowel-to-vowel
  // transitions (e.g. when a diphthong is split across chunks). To keep
  // diphthongs smooth while preserving consonant clarity, we suppress the
  // boundary gap when the previous chunk ended with a vowel/semivowel and
  // the next chunk starts with a vowel/semivowel.
  if (cb && h->streamHasSpeech && hasRealPhoneme) {
    const double gapMs = h->pack.lang.segmentBoundaryGapMs;
    const double fadeMs = h->pack.lang.segmentBoundaryFadeMs;
    if (gapMs > 0.0 || fadeMs > 0.0) {
      bool skip = false;
      if (h->pack.lang.segmentBoundarySkipVowelToVowel &&
          h->lastEndsVowelLike && startsVowelLike) {
        skip = true;
      }
      if (!skip && h->pack.lang.segmentBoundarySkipVowelToLiquid &&
          h->lastEndsVowelLike && startsLiquidLike) {
        skip = true;
      }
      if (!skip) {
        const double spd = (speed > 0.0) ? speed : 1.0;
        cb(userData, nullptr, gapMs / spd, fadeMs / spd, userIndexBase);
      }
    }
  }

  emitFrames(h->pack, tokens, userIndexBase, cb, userData);
  if (hasRealPhoneme) {
    h->streamHasSpeech = true;
    h->lastEndsVowelLike = endsVowelLike;
  }
  return 1;
}

NVSP_FRONTEND_API int nvspFrontend_setVoiceProfile(nvspFrontend_handle_t handle, const char* profileNameUtf8) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return 0;

  std::lock_guard<std::mutex> lock(h->mu);
  h->lastError.clear();

  // Set the voice profile name in the language pack settings.
  // This will be used during the next queueIPA call.
  h->pack.lang.voiceProfileName = profileNameUtf8 ? std::string(profileNameUtf8) : std::string();
  return 1;
}

NVSP_FRONTEND_API const char* nvspFrontend_getVoiceProfile(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "";

  std::lock_guard<std::mutex> lock(h->mu);
  return h->pack.lang.voiceProfileName.c_str();
}

NVSP_FRONTEND_API const char* nvspFrontend_getPackWarnings(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "";

  std::lock_guard<std::mutex> lock(h->mu);
  return h->pack.loadWarnings.c_str();
}

NVSP_FRONTEND_API const char* nvspFrontend_getLastError(nvspFrontend_handle_t handle) {
  using namespace nvsp_frontend;
  Handle* h = asHandle(handle);
  if (!h) return "invalid handle";
  std::lock_guard<std::mutex> lock(h->mu);
  return h->lastError.c_str();
}

} // extern "C"
