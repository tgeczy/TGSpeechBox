/*
 * CheckVoiceData — Invisible activity that tells Android Settings
 * which languages this TTS engine supports.
 *
 * Android's TTS settings sends ACTION_CHECK_TTS_DATA and expects
 * EXTRA_AVAILABLE_VOICES back as locale strings like "eng-USA", "fra".
 * Without this, the language dropdown stays blank.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

class CheckVoiceData : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = TgsbStorage.prefs(this)
        val enabledKeys = TgsbTtsService.getEnabledLocaleKeys(prefs)

        val available = ArrayList<String>()
        val seen = mutableSetOf<String>()

        for (ld in TgsbTtsService.LANGUAGES) {
            if (!TgsbTtsService.isLangEnabled(ld, enabledKeys)) continue
            val tag = if (ld.iso3Country.isNotEmpty()) {
                "${ld.iso3Lang}-${ld.iso3Country}"
            } else {
                ld.iso3Lang
            }
            if (tag !in seen) {
                seen.add(tag)
                available.add(tag)
            }
        }

        val returnData = Intent().apply {
            putStringArrayListExtra(
                TextToSpeech.Engine.EXTRA_AVAILABLE_VOICES, available
            )
        }

        setResult(TextToSpeech.Engine.CHECK_VOICE_DATA_PASS, returnData)
        finish()
    }
}
