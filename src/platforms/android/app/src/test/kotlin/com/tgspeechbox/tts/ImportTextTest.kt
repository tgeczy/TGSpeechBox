package com.tgspeechbox.tts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.InputStream

/**
 * A file picked for import is read whole only when it is small.
 *
 * The three imports (a language pack, phoneme overrides, a dictionary) read
 * whatever file was picked into memory with no limit.  The wrong file, a
 * video say, ran the app out of memory; an OutOfMemoryError is not an
 * Exception, so nothing caught it and the process died.  The speech service
 * the screen reader talks through lives in that process.  Found by checking
 * this repository against the audits of the maintainer's other speech
 * projects (their imports had the same fault).
 *
 * ImportText.read stops at a limit and has by then taken no more than one
 * byte past it from the stream.
 *
 * Run: gradlew :app:testReleaseUnitTest
 */
class ImportTextTest {

    /** A stream that never ends, and counts what was taken from it. */
    private class Endless : InputStream() {
        var taken = 0L
        override fun read(): Int { taken++; return 'x'.code }
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            b.fill('x'.code.toByte(), off, off + len)
            taken += len
            return len
        }
    }

    private fun stream(text: String) = ByteArrayInputStream(text.toByteArray(Charsets.UTF_8))

    @Test
    fun aSmallFileIsItsText() {
        val text = "postStopAspirationDurationMs: 20\n# árvíztűrő tükörfúrógép 你好\n"
        assertEquals(text, ImportText.read(stream(text)))
        assertEquals("", ImportText.read(stream("")))
    }

    @Test
    fun aFileOfExactlyTheLimitIsRead() {
        val text = "a".repeat(1000)
        assertEquals(text, ImportText.read(stream(text), limit = 1000))
    }

    @Test
    fun oneByteMoreIsRefused() {
        try {
            ImportText.read(stream("a".repeat(1001)), limit = 1000)
            fail("a file over the limit was read")
        } catch (e: ImportText.TooLarge) {
            assertEquals(1000, e.limit)
        }
    }

    @Test
    fun aFileWithoutEndIsGivenUpAtTheLimitAndNoFurther() {
        val endless = Endless()
        try {
            ImportText.read(endless, limit = 100_000)
            fail("a stream without end was read")
        } catch (e: ImportText.TooLarge) {
            // What makes the limit worth having: the rest was never taken.
            assertEquals(100_001L, endless.taken)
        }
    }

    @Test
    fun theRefusalIsAnOrdinaryReadErrorWithASentence() {
        // The import screens catch Exception and show its message.
        val e: Exception = ImportText.TooLarge(8 * 1024 * 1024)
        assertTrue(e is java.io.IOException)
        assertEquals("the file is larger than 8 MB", e.message)
    }

    @Test
    fun theLimitLeavesRoomForTheLargestFileTheAppItselfExports() {
        // The US English stress dictionary, the largest text the packs hold,
        // is 1.7 MB; "Export all" of a dictionary must import again.
        assertTrue(ImportText.MAX_BYTES >= 4 * 1024 * 1024)
        // And it is a limit: a phone has it to spare many times over.
        assertTrue(ImportText.MAX_BYTES <= 32 * 1024 * 1024)
    }

    @Test
    fun aStreamThatHandsOutAByteAtATimeIsReadWhole() {
        val text = "one\ttwo\n".repeat(50)
        val slow = object : InputStream() {
            private val bytes = text.toByteArray(Charsets.UTF_8)
            private var at = 0
            override fun read(): Int = if (at < bytes.size) bytes[at++].toInt() and 0xFF else -1
            override fun read(b: ByteArray, off: Int, len: Int): Int {
                if (at >= bytes.size) return -1
                b[off] = bytes[at++]
                return 1
            }
        }
        assertEquals(text, ImportText.read(slow))
    }
}
