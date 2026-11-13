package com.tgspeechbox.tts

import android.content.SharedPreferences
import android.util.Log

/**
 * One-time clean-up of stored language pack overrides (#172).
 *
 * "Import pack" stores every setting of the imported file as an override
 * ("pack_overrides_<language>"), the lines the shipped pack itself had among
 * them, and they are applied again after every language load.  Until 3.10
 * release candidate 4 the frontend read none of the sixteen phoneme timing
 * settings below (src/frontend/pack.cpp), so a stored value for one of them
 * did nothing: nobody ever heard it, and nobody could have tuned it.  Now
 * they are read.  en-us shipped postStopAspirationDurationMs 35 from 3.01
 * on, so an English listener who once imported a pack (their own backup, or
 * a friend's) would get a 35 ms puff after p, t and k where 20 ms had always
 * played; Spanish, Danish and Brazilian Portuguese carried such a line too.
 *
 * So every stored override for those sixteen settings is removed once, and
 * the language sounds as it did.  A value set after that is the listener's
 * own choice and is kept.  An old pack file imported after this still brings
 * its line along: a file does not say which release wrote it.
 */
object PackOverrideMigration {
    private const val TAG = "TgsbOverrideMigration"

    const val SCHEMA_KEY = "pack_overrides_schema"
    const val SCHEMA = 1
    private const val PREFIX = "pack_overrides_"
    private const val VERSION_KEY = "pack_overrides_version"

    /** The phoneme timing settings the frontend did not read before schema 1. */
    val TIMING_KEYS_UNREAD_BEFORE_SCHEMA_1: Set<String> = setOf(
        "defaultVowelDurationMs",
        "defaultFadeMs",
        "postStopAspirationDurationMs",
        "stopDurationMs",
        "affricateDurationMs",
        "voicelessFricativeDurationMs",
        "voicedConsonantDurationMs",
        "tapDurationMs",
        "trillFallbackDurationMs",
        "tiedVowelDurationMs",
        "tiedFromVowelDurationMs",
        "tiedFromVowelFadeMs",
        "vowelBeforeLiquidDurationMs",
        "vowelBeforeNasalDurationMs",
        "fadeAfterLiquidMs",
        "liquidFadeMs",
    )

    /** A language's overrides without the settings nobody ever heard. */
    fun withoutUnheardTimingKeys(overrides: Map<String, String>): Map<String, String> =
        overrides.filterKeys { it !in TIMING_KEYS_UNREAD_BEFORE_SCHEMA_1 }

    /** True for "pack_overrides_<language>", not for the counters beside them. */
    fun isLanguageOverridesKey(name: String): Boolean =
        name.startsWith(PREFIX) && name != VERSION_KEY && name != SCHEMA_KEY

    /**
     * Runs the clean-up when these preferences have not had it.  Cheap to
     * call again: one integer is read.  Preferences that are replaced later
     * (the move into device-protected storage after the first unlock) do not
     * carry the mark and get their own run.
     */
    fun run(prefs: SharedPreferences) {
        try {
            if (prefs.getInt(SCHEMA_KEY, 0) >= SCHEMA) return
            val edit = prefs.edit()
            var changed = false
            for ((name, value) in prefs.all) {
                if (!isLanguageOverridesKey(name) || value !is String) continue
                val stored = try {
                    val obj = org.json.JSONObject(value)
                    obj.keys().asSequence().associateWith { obj.getString(it) }
                } catch (e: Exception) {
                    continue  // not ours to repair; it is ignored when applied, too
                }
                val kept = withoutUnheardTimingKeys(stored)
                if (kept.size == stored.size) continue
                if (kept.isEmpty()) {
                    edit.remove(name)
                } else {
                    val obj = org.json.JSONObject()
                    for ((k, v) in kept) obj.put(k, v)
                    edit.putString(name, obj.toString())
                }
                changed = true
            }
            if (changed) {
                // The service reloads the pack when this counter moves.
                edit.putInt(VERSION_KEY, prefs.getInt(VERSION_KEY, 0) + 1)
            }
            edit.putInt(SCHEMA_KEY, SCHEMA).apply()
        } catch (e: Exception) {
            Log.w(TAG, "stored pack overrides could not be cleaned up", e)
        }
    }
}
