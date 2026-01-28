/*
NV Speech Player - Frontend (IPA -> Frames)

Design goals:
- Keep speechPlayer.dll DSP-only.
- Make language behaviour data-driven via human-editable YAML packs.
- Provide a stable C ABI so NVDA / other callers can use this from any language.

This frontend does NOT call into speechPlayer.dll.
Instead it emits frames via a callback.

Frame layout matches the existing speechPlayer Frame struct order (see speechPlayer.py).
*/

#ifndef NVSP_FRONTEND_H
#define NVSP_FRONTEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef NVSP_FRONTEND_EXPORTS
    #define NVSP_FRONTEND_API __declspec(dllexport)
  #else
    #define NVSP_FRONTEND_API __declspec(dllimport)
  #endif
#else
  #define NVSP_FRONTEND_API
#endif

#define NVSP_FRONTEND_ABI_VERSION 1

typedef void* nvspFrontend_handle_t;

/*
  Frame struct. Field order MUST stay in sync with speechPlayer.dll.
  This is intentionally a plain-old-data struct for ABI stability.
*/
typedef struct nvspFrontend_Frame {
  double voicePitch;
  double vibratoPitchOffset;
  double vibratoSpeed;
  double voiceTurbulenceAmplitude;
  double glottalOpenQuotient;
  double voiceAmplitude;
  double aspirationAmplitude;
  double cf1, cf2, cf3, cf4, cf5, cf6, cfN0, cfNP;
  double cb1, cb2, cb3, cb4, cb5, cb6, cbN0, cbNP;
  double caNP;
  double fricationAmplitude;
  double pf1, pf2, pf3, pf4, pf5, pf6;
  double pb1, pb2, pb3, pb4, pb5, pb6;
  double pa1, pa2, pa3, pa4, pa5, pa6;
  double parallelBypass;
  double preFormantGain;
  double outputGain;
  double endVoicePitch;
} nvspFrontend_Frame;

/*
  Callback invoked for each frame.
  - frameOrNull: NULL means "silence" for the given duration.
  - durationMs and fadeMs are in milliseconds (same units as the Python side today).
  - userIndex is passed through, so callers can map audio back to text positions.
*/
typedef void (*nvspFrontend_FrameCallback)(
  void* userData,
  const nvspFrontend_Frame* frameOrNull,
  double durationMs,
  double fadeMs,
  int userIndex
);

/* Create/destroy. packDir should contain:
   - packs/phonemes.yaml
   - packs/lang/default.yaml
   - packs/lang/<lang>.yaml (optional)

   You can point packDir to the directory that contains the "packs" folder.
*/
NVSP_FRONTEND_API nvspFrontend_handle_t nvspFrontend_create(const char* packDirUtf8);
NVSP_FRONTEND_API void nvspFrontend_destroy(nvspFrontend_handle_t handle);

/*
  Set the language (BCP-47-ish: en, en-us, hu, pl, bg, ...).
  Loads and merges:
    default.yaml, <base>.yaml, <base-region>.yaml, ... up to the most specific tag.

  Returns 1 on success, 0 on failure.
*/
NVSP_FRONTEND_API int nvspFrontend_setLanguage(nvspFrontend_handle_t handle, const char* langTagUtf8);

/*
  Convert IPA text into frames.

  Inputs:
  - ipaUtf8: IPA string (ideally already normalized to your phoneme table)
  - speed: same meaning as ipa_convert.py today (used as divisor in timing)
  - basePitch: base pitch in Hz-ish terms used by speechPlayer
  - inflection: 0..1-ish pitch range scaling (same meaning as ipa_convert.py)
  - clauseTypeUtf8: one of "." "," "?" "!" (NULL treated as ".")
  - userIndexBase: the index passed to callback; you can call multiple times with different bases

  Output:
  - cb is called once per frame.

  Returns 1 on success, 0 on failure.
*/
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
);

/*
  Set the voice profile to use for parameter transformation.
  
  Voice profiles allow producing different voice qualities (e.g., female voice)
  by applying formant scaling and other modifications to phoneme parameters,
  without maintaining separate phoneme tables.
  
  - profileNameUtf8: Name of the profile to use (e.g., "female").
    Pass NULL or "" to disable voice profiles (use base phoneme parameters).
  - The profile must be defined in the pack's phonemes.yaml under "voiceProfiles:".
  - Unknown profile names are silently ignored (no effect).
  
  The voice profile setting persists until changed or the handle is destroyed.
  It can also be set per-language in the lang.yaml file via "voiceProfileName:".
  
  Returns 1 on success, 0 on failure (e.g., invalid handle).
*/
NVSP_FRONTEND_API int nvspFrontend_setVoiceProfile(nvspFrontend_handle_t handle, const char* profileNameUtf8);

/*
  Get the currently active voice profile name.
  Returns a pointer to an empty string if no profile is active.
  The returned pointer is owned by the handle and valid until the next API call.
*/
NVSP_FRONTEND_API const char* nvspFrontend_getVoiceProfile(nvspFrontend_handle_t handle);

/*
  Get non-fatal warnings from pack loading (e.g., voice profile parse errors).
  Returns an empty string if no warnings.
  Useful for debugging "why does my voice profile do nothing?" issues.
  The returned pointer is owned by the handle and valid until the next API call.
*/
NVSP_FRONTEND_API const char* nvspFrontend_getPackWarnings(nvspFrontend_handle_t handle);

/*
  If a function returns failure, call this to get a human-readable message.
  The returned pointer is owned by the frontend handle and remains valid until the next call.
*/
NVSP_FRONTEND_API const char* nvspFrontend_getLastError(nvspFrontend_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif
