package com.tgspeechbox.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Stored pack overrides lose the timing settings nobody ever heard (#172).
 *
 * "Import pack" stores every setting of the imported file as an override,
 * the lines the shipped pack itself had among them: en-us shipped
 * postStopAspirationDurationMs 35 from 3.01 on, es tiedFromVowelDurationMs
 * 30, da 45 and pt-br 15 in the release candidates.  Until 3.10 release
 * candidate 4 the frontend read none of the sixteen phoneme timing
 * settings, so those stored values did nothing.  Now it reads them, and an
 * English listener who once imported a pack would get a 35 ms puff after
 * p, t and k where 20 ms had always played.
 *
 * Run: gradlew :app:testReleaseUnitTest
 */
class PackOverrideMigrationTest {
    private val keys = PackOverrideMigration.TIMING_KEYS_UNREAD_BEFORE_SCHEMA_1

    @Test
    fun theSixteenTimingSettingsOfPackCpp() {
        // The getNum block under "Phoneme timing" in src/frontend/pack.cpp.
        assertEquals(
            setOf(
                "defaultVowelDurationMs", "defaultFadeMs", "postStopAspirationDurationMs", "stopDurationMs",
                "affricateDurationMs", "voicelessFricativeDurationMs", "voicedConsonantDurationMs", "tapDurationMs",
                "trillFallbackDurationMs", "tiedVowelDurationMs", "tiedFromVowelDurationMs", "tiedFromVowelFadeMs",
                "vowelBeforeLiquidDurationMs", "vowelBeforeNasalDurationMs", "fadeAfterLiquidMs", "liquidFadeMs",
            ),
            keys,
        )
    }

    @Test
    fun anImportedEnglishPackLosesTheUnheardLineAndKeepsTheRest() {
        val stored = mapOf(
            "postStopAspirationDurationMs" to "35",
            "primaryStressDiv" to "1.4",
            "stopClosureMode" to "vowel-and-cluster",
            "legacyPitchMode" to "fujisaki_style",
        )
        assertEquals(
            mapOf(
                "primaryStressDiv" to "1.4",
                "stopClosureMode" to "vowel-and-cluster",
                "legacyPitchMode" to "fujisaki_style",
            ),
            PackOverrideMigration.withoutUnheardTimingKeys(stored),
        )
    }

    @Test
    fun everyOneOfTheSixteenGoesWhateverItsValue() {
        val stored = keys.associateWith { "12.5" } + ("voiceProfileName" to "Beth")
        assertEquals(mapOf("voiceProfileName" to "Beth"), PackOverrideMigration.withoutUnheardTimingKeys(stored))
    }

    @Test
    fun overridesWithoutThemAreTheSameMap() {
        val stored = mapOf("primaryStressDiv" to "1.4", "speechRateScale" to "1.0")
        assertEquals(stored, PackOverrideMigration.withoutUnheardTimingKeys(stored))
        assertTrue(PackOverrideMigration.withoutUnheardTimingKeys(emptyMap()).isEmpty())
    }

    @Test
    fun aSecondPassChangesNothing() {
        val once = PackOverrideMigration.withoutUnheardTimingKeys(
            mapOf("tiedFromVowelDurationMs" to "30", "primaryStressDiv" to "1.4"))
        assertEquals(once, PackOverrideMigration.withoutUnheardTimingKeys(once))
    }

    @Test
    fun onlyTheLanguageEntriesAreOverrideMaps() {
        assertTrue(PackOverrideMigration.isLanguageOverridesKey("pack_overrides_en-us"))
        assertTrue(PackOverrideMigration.isLanguageOverridesKey("pack_overrides_pt-br"))
        assertFalse(PackOverrideMigration.isLanguageOverridesKey("pack_overrides_version"))
        assertFalse(PackOverrideMigration.isLanguageOverridesKey("pack_overrides_schema"))
        assertFalse(PackOverrideMigration.isLanguageOverridesKey("phoneme_overrides"))
        assertFalse(PackOverrideMigration.isLanguageOverridesKey("dict_overrides_en-us"))
    }
}
