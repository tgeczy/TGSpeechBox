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

#define NVSP_FRONTEND_ABI_VERSION 2

typedef void* nvspFrontend_handle_t;

/*
  Frame struct. Field order MUST stay in sync with speechPlayer.dll.
  This is intentionally a plain-old-data struct for ABI stability.
  
  This struct contains the core 47 parameters that have been stable since v1.
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
  Extended frame parameters (ABI v2+).
  
  These voice quality parameters are kept separate from nvspFrontend_Frame
  to maintain backward compatibility. They match speechPlayer_frameEx_t in
  the DSP DLL.
  
  All fields are in range [0.0, 1.0] except sharpness which is a multiplier
  (typically 0.5-2.0, where 1.0 is neutral).
*/
typedef struct nvspFrontend_FrameEx {
  double creakiness;      /* laryngealization / creaky voice (e.g. Danish stød) */
  double breathiness;     /* breath noise mixed into voicing */
  double jitter;          /* pitch period variation (irregular F0) */
  double shimmer;         /* amplitude variation (irregular loudness) */
  double sharpness;       /* glottal closure sharpness multiplier (1.0 = neutral) */
} nvspFrontend_FrameEx;

/* Number of fields in FrameEx struct (for size validation) */
#define NVSP_FRONTEND_FRAMEEX_NUM_PARAMS 5

/*
  Callback invoked for each frame (legacy, ABI v1).
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

/*
  Extended callback invoked for each frame (ABI v2+).
  - frameOrNull: NULL means "silence" for the given duration.
  - frameExOrNull: Extended parameters, or NULL if not applicable.
  - durationMs and fadeMs are in milliseconds.
  - userIndex is passed through for text position mapping.
*/
typedef void (*nvspFrontend_FrameExCallback)(
  void* userData,
  const nvspFrontend_Frame* frameOrNull,
  const nvspFrontend_FrameEx* frameExOrNull,
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

/*
  Get the ABI version of the loaded DLL.
  Callers can use this to check for feature availability.
*/
NVSP_FRONTEND_API int nvspFrontend_getABIVersion(void);

/* ============================================================================
 * Extended Frame API (ABI v2+)
 * ============================================================================
 *
 * These functions provide per-frame voice quality control via FrameEx parameters.
 * The frontend mixes user-level defaults with per-phoneme values from YAML.
 */

/*
  Set user-level FrameEx defaults.
  
  These values are added to per-phoneme FrameEx values (from YAML) when emitting
  frames. The result is clamped to valid ranges.
  
  This is typically called when the user adjusts voice quality sliders.
  The defaults persist until changed or the handle is destroyed.
  
  Parameters:
  - creakiness:  0.0-1.0, added to phoneme creakiness
  - breathiness: 0.0-1.0, added to phoneme breathiness  
  - jitter:      0.0-1.0, added to phoneme jitter
  - shimmer:     0.0-1.0, added to phoneme shimmer
  - sharpness:   multiplier (0.5-2.0 typical), multiplied with phoneme sharpness
*/
NVSP_FRONTEND_API void nvspFrontend_setFrameExDefaults(
  nvspFrontend_handle_t handle,
  double creakiness,
  double breathiness,
  double jitter,
  double shimmer,
  double sharpness
);

/*
  Get the current FrameEx defaults.
  
  Writes the current user-level defaults to the provided struct.
  Returns 1 on success, 0 on failure (e.g., invalid handle or NULL pointer).
*/
NVSP_FRONTEND_API int nvspFrontend_getFrameExDefaults(
  nvspFrontend_handle_t handle,
  nvspFrontend_FrameEx* outDefaults
);

/*
  Convert IPA text into frames with extended parameters (ABI v2+).
  
  This is the extended version of nvspFrontend_queueIPA that uses the
  FrameExCallback to emit both Frame and FrameEx data.
  
  The FrameEx values in the callback are the result of mixing:
  - Per-phoneme values from YAML (if defined)
  - User-level defaults set via nvspFrontend_setFrameExDefaults()
  
  For silence frames, frameExOrNull will be NULL.
  
  Returns 1 on success, 0 on failure.
*/
NVSP_FRONTEND_API int nvspFrontend_queueIPA_Ex(
  nvspFrontend_handle_t handle,
  const char* ipaUtf8,
  double speed,
  double basePitch,
  double inflection,
  const char* clauseTypeUtf8,
  int userIndexBase,
  nvspFrontend_FrameExCallback cb,
  void* userData
);

#ifdef __cplusplus
}
#endif

#endif
