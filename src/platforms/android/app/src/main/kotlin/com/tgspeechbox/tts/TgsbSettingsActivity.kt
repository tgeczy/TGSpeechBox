/*
 * TgsbSettingsActivity — Voice preset + supported languages for TGSpeechBox TTS.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.app.Activity
import android.os.Bundle
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Toast
import android.view.View
import android.widget.TextView

class TgsbSettingsActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = TgsbStorage.prefs(this)
        val currentPreset = prefs.getString(
            TgsbTtsService.PREF_VOICE_PRESET,
            TgsbTtsService.DEFAULT_PRESET
        ) ?: TgsbTtsService.DEFAULT_PRESET

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val pad = (16 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }

        // ---- Voice preset section ----

        val voiceTitle = TextView(this).apply {
            text = "Voice"
            textSize = 20f
            val bot = (16 * resources.displayMetrics.density).toInt()
            setPadding(0, 0, 0, bot)
        }
        layout.addView(voiceTitle)

        val radioGroup = RadioGroup(this)

        for (vd in TgsbTtsService.VOICES) {
            val rb = RadioButton(this).apply {
                id = View.generateViewId()
                text = vd.label
                tag = vd.id
                textSize = 18f
                val vert = (8 * resources.displayMetrics.density).toInt()
                setPadding(paddingLeft, vert, paddingRight, vert)
                isChecked = (vd.id == currentPreset)
            }
            radioGroup.addView(rb)
        }

        radioGroup.setOnCheckedChangeListener { group, checkedId ->
            val rb = group.findViewById<RadioButton>(checkedId)
            val presetId = rb?.tag as? String ?: return@setOnCheckedChangeListener
            prefs.edit()
                .putString(TgsbTtsService.PREF_VOICE_PRESET, presetId)
                .apply()
        }

        layout.addView(radioGroup)

        // ---- Supported languages section ----

        val langTitle = TextView(this).apply {
            text = "Supported Languages"
            textSize = 20f
            val top = (24 * resources.displayMetrics.density).toInt()
            val bot = (8 * resources.displayMetrics.density).toInt()
            setPadding(0, top, 0, bot)
        }
        layout.addView(langTitle)

        val langHint = TextView(this).apply {
            text = "Only checked languages are reported to Android. " +
                   "Uncheck languages you don't use to prevent unwanted auto-switching."
            textSize = 14f
            val bot = (12 * resources.displayMetrics.density).toInt()
            setPadding(0, 0, 0, bot)
        }
        layout.addView(langHint)

        // Build unique language entries (deduplicated by displayLocale)
        data class LangEntry(val localeKey: String, val displayName: String)
        val entries = mutableListOf<LangEntry>()
        val seen = mutableSetOf<String>()
        for (ld in TgsbTtsService.LANGUAGES) {
            val key = ld.displayLocale.toString()
            if (key in seen) continue
            seen.add(key)
            entries.add(LangEntry(key, ld.displayLocale.getDisplayName()))
        }
        entries.sortBy { it.displayName }

        // Current enabled set (null = all enabled)
        val enabledKeys = TgsbTtsService.getEnabledLocaleKeys(prefs)
        val currentEnabled = enabledKeys?.toMutableSet()
            ?: entries.map { it.localeKey }.toMutableSet()

        val checkboxes = mutableListOf<CheckBox>()

        for (entry in entries) {
            val cb = CheckBox(this).apply {
                text = entry.displayName
                tag = entry.localeKey
                textSize = 16f
                isChecked = entry.localeKey in currentEnabled
                val vert = (4 * resources.displayMetrics.density).toInt()
                setPadding(paddingLeft, vert, paddingRight, vert)
            }
            checkboxes.add(cb)

            cb.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    currentEnabled.add(entry.localeKey)
                } else {
                    // Must keep at least one language
                    if (currentEnabled.size <= 1) {
                        cb.isChecked = true
                        Toast.makeText(
                            this, "At least one language must be enabled.",
                            Toast.LENGTH_SHORT
                        ).show()
                        return@setOnCheckedChangeListener
                    }
                    currentEnabled.remove(entry.localeKey)
                }
                // Save. If all are enabled, clear the pref (null = all).
                if (currentEnabled.size >= entries.size) {
                    prefs.edit()
                        .remove(TgsbTtsService.PREF_SUPPORTED_LANGUAGES)
                        .apply()
                } else {
                    prefs.edit()
                        .putStringSet(
                            TgsbTtsService.PREF_SUPPORTED_LANGUAGES,
                            currentEnabled.toSet()
                        )
                        .apply()
                }
            }

            layout.addView(cb)
        }

        // Wrap in a ScrollView for phones with small screens
        val scrollView = ScrollView(this)
        scrollView.addView(layout)
        setContentView(scrollView)
    }
}
