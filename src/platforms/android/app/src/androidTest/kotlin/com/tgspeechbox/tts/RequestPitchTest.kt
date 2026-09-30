package com.tgspeechbox.tts

import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * The pitch a caller asks for reaches the voice, so capitals can be heard.
 *
 * TalkBack marks a capital letter by raising the pitch of that one request
 * (and the system pitch slider works the same way).  Panthera Speech's service
 * never read SynthesisRequest.getPitch(), so capitals sounded like everything
 * else; this checks TGSpeechBox through the same platform TTS client, not the
 * native setter.  The median pitch of a sentence follows the requested ratio
 * (1.5 and 0.75), and a normal request after the raised one is back at the
 * normal pitch: the raise ended with its request.  (Not byte for byte: the
 * breath noise runs on from one request to the next, so no two renders are
 * identical.)
 *
 * Run: adb install -r the release APK and the androidTest APK, then
 *   adb shell am instrument -w -e class com.tgspeechbox.tts.RequestPitchTest \
 *     com.tgspeechbox.tts.test/androidx.test.runner.AndroidJUnitRunner
 */
class RequestPitchTest {
    @Test
    fun capitalsAndThePitchSliderReachTheVoice() {
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        val outDir = File(ctx.getExternalFilesDir(null), "request_pitch")
        outDir.mkdirs()

        val initLatch = CountDownLatch(1)
        var initStatus = -1
        val tts = TextToSpeech(ctx, { status -> initStatus = status; initLatch.countDown() }, "com.tgspeechbox.tts")
        check(initLatch.await(20, TimeUnit.SECONDS)) { "TTS init timeout" }
        check(initStatus == TextToSpeech.SUCCESS) { "TTS init failed: $initStatus" }
        tts.setLanguage(Locale.US)

        // Median pitch (Hz) of the voiced 30 ms frames: the lag of the
        // strongest normalized autocorrelation between 60 and 500 Hz.
        fun medianF0(pcm: ShortArray, sr: Int): Double {
            val win = sr * 30 / 1000
            val hop = sr * 10 / 1000
            val minLag = sr / 500
            val maxLag = sr / 60
            val found = ArrayList<Double>()
            var s = 0
            while (s + win + maxLag < pcm.size) {
                var energy = 0.0
                for (i in 0 until win) energy += pcm[s + i].toDouble() * pcm[s + i]
                if (Math.sqrt(energy / win) >= 500.0) {
                    var best = 0.0
                    var bestLag = 0
                    for (lag in minLag..maxLag) {
                        var num = 0.0
                        var den = 0.0
                        for (i in 0 until win) {
                            val a = pcm[s + i].toDouble()
                            val b = pcm[s + i + lag].toDouble()
                            num += a * b
                            den += b * b
                        }
                        val r = num / Math.sqrt(energy * den + 1e-9)
                        if (r > best) { best = r; bestLag = lag }
                    }
                    if (best > 0.7) found.add(sr.toDouble() / bestLag)
                }
                s += hop
            }
            check(found.size >= 5) { "too few voiced frames: ${found.size}" }
            found.sort()
            return found[found.size / 2]
        }

        var sampleRate = 22050

        fun render(pitch: Float, name: String): ShortArray {
            tts.setPitch(pitch)
            val out = File(outDir, "$name.wav")
            val latch = CountDownLatch(1)
            tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                override fun onStart(utteranceId: String?) {}
                override fun onDone(utteranceId: String?) { latch.countDown() }
                @Deprecated("legacy")
                override fun onError(utteranceId: String?) { latch.countDown() }
                override fun onError(utteranceId: String?, errorCode: Int) { latch.countDown() }
            })
            check(tts.synthesizeToFile("Hello there, this is a pitch test", Bundle(), out, name) == TextToSpeech.SUCCESS) { "refused at $pitch" }
            check(latch.await(25, TimeUnit.SECONDS)) { "timeout at $pitch" }
            val bytes = out.readBytes()
            sampleRate = (bytes[24].toInt() and 0xFF) or ((bytes[25].toInt() and 0xFF) shl 8) or
                ((bytes[26].toInt() and 0xFF) shl 16) or ((bytes[27].toInt() and 0xFF) shl 24)
            return ShortArray((bytes.size - 44) / 2) { i ->
                ((bytes[44 + 2 * i].toInt() and 0xFF) or (bytes[45 + 2 * i].toInt() shl 8)).toShort()
            }
        }

        val normal = render(1.0f, "normal")
        val raised = render(1.5f, "raised")
        val normalAgain = render(1.0f, "normal_again")
        val lowered = render(0.75f, "lowered")
        tts.shutdown()

        val f0 = medianF0(normal, sampleRate)
        val up = medianF0(raised, sampleRate) / f0
        val down = medianF0(lowered, sampleRate) / f0
        val again = medianF0(normalAgain, sampleRate) / f0
        val report = "normal $f0 Hz; raised x$up, lowered x$down, normal again x$again"
        File(outDir, "report.txt").writeText(report + "\n")
        assertTrue("a raised request is not raised: $report", up in 1.4..1.6)
        assertTrue("a lowered request is not lowered: $report", down in 0.68..0.82)
        assertEquals("the raised pitch outlived its request: $report", 1.0, again, 0.03)
    }
}
