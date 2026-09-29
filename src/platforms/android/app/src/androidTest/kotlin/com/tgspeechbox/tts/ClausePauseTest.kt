package com.tgspeechbox.tts

import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * One clause splitter for every platform (#133), checked on the device.
 *
 * Dashes between words, parentheses around words, and a colon pause the way
 * every TGSpeechBox platform pauses there: the marked text renders exactly
 * as long as the same words written with commas (or a full stop for the
 * colon), and longer than the words with no mark at all.  Rendered through
 * TextToSpeech.synthesizeToFile, the path TalkBack's speech takes minus the
 * playback.
 *
 * Run: adb install -r the release APK and the androidTest APK, then
 *   adb shell am instrument -w -e class com.tgspeechbox.tts.ClausePauseTest \
 *     com.tgspeechbox.tts.test/androidx.test.runner.AndroidJUnitRunner
 */
class ClausePauseTest {
    private data class Case(val marked: String, val commas: String, val plain: String)

    @Test
    fun marksPauseLikeTheirPunctuation() {
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(ctx.getExternalFilesDir(null), "clause_pauses")
        outDir.mkdirs()

        val initLatch = CountDownLatch(1)
        var initStatus = -1
        val tts = TextToSpeech(ctx, { status -> initStatus = status; initLatch.countDown() }, "com.tgspeechbox.tts")
        check(initLatch.await(20, TimeUnit.SECONDS)) { "TTS init timeout" }
        check(initStatus == TextToSpeech.SUCCESS) { "TTS init failed: $initStatus" }
        tts.setLanguage(Locale.US)

        fun pcmBytes(text: String, name: String): Long {
            val out = File(outDir, "$name.wav")
            val latch = CountDownLatch(1)
            tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String?) {}
                override fun onDone(utteranceId: String?) { latch.countDown() }
                @Deprecated("legacy")
                override fun onError(utteranceId: String?) { latch.countDown() }
                override fun onError(utteranceId: String?, errorCode: Int) { latch.countDown() }
            })
            check(tts.synthesizeToFile(text, Bundle(), out, name) == TextToSpeech.SUCCESS) { "refused: $text" }
            check(latch.await(25, TimeUnit.SECONDS)) { "timeout: $text" }
            return out.length() - 44  // the WAV header
        }

        val cases = listOf(
            Case("wait — what now", "wait, what now", "wait what now"),
            Case("wait - what now", "wait, what now", "wait what now"),
            Case("The file (about two megabytes) is ready", "The file, about two megabytes, is ready",
                 "The file about two megabytes is ready"),
            Case("Note: this", "Note. this", "Note this"),
        )
        val report = StringBuilder()
        var ok = true
        cases.forEachIndexed { i, c ->
            val marked = pcmBytes(c.marked, "c${i}_marked")
            val commas = pcmBytes(c.commas, "c${i}_commas")
            val plain = pcmBytes(c.plain, "c${i}_plain")
            // 16-bit mono at the engine's rate; 5 ms and 20 ms at 22050 Hz.
            val same = Math.abs(marked - commas) <= 2 * 110
            val longer = marked - plain >= 2 * 441
            ok = ok && same && longer
            report.append("${c.marked}: marked $marked commas $commas plain $plain bytes -> ")
                .append(if (same && longer) "ok" else "FAIL").append('\n')
        }
        tts.shutdown()
        File(outDir, "report.txt").writeText(report.toString())
        assertTrue(report.toString(), ok)
    }
}
