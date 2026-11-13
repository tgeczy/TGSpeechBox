/*
TGSpeechBox — Speech settings of the Win32 phoneme editor.
Copyright 2025-2026 Tamas Geczy.
Licensed under the MIT License. See LICENSE for details.
*/

#include "speech_settings.h"

namespace tgsb_editor {

double inflectionScaleKeepingSound(double storedScale, int inflectionSlider) {
  double scale = storedScale * static_cast<double>(inflectionSlider) / static_cast<double>(kInflectionNeutral);
  if (scale > 3.0) scale = 3.0;
  return scale;
}

SpeechSettings speechSettingsAfterProfileSave(SpeechSettings& dialog,
                                              const SpeechSettings& applied,
                                              const std::string& savedVoice) {
  dialog.voiceName = savedVoice;
  dialog.inflection = kInflectionNeutral;

  SpeechSettings out = applied;
  out.voiceName = dialog.voiceName;
  out.inflection = dialog.inflection;
  out.voicingParams = dialog.voicingParams;
  return out;
}

} // namespace tgsb_editor
