/*
 * TGSpeechBox — reading a file picked for import.
 * Copyright 2025-2026 Tamas Geczy.
 * Licensed under the MIT License. See LICENSE for details.
 */

package com.tgspeechbox.tts

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream

/**
 * The text of a file the user picked to import: a language pack, phoneme
 * overrides or a dictionary.
 *
 * Every import reads through [read].  The file picker hands over whatever
 * was chosen, and read whole with no limit the wrong file (a video, say) ran
 * the app out of memory.  An OutOfMemoryError is not an Exception, so the
 * imports' own error handling did not catch it and the process died, and the
 * speech service a screen reader talks through lives in that process.
 *
 * Plain Kotlin with nothing of Android in it, so that it is tested on the
 * JVM (ImportTextTest).  The iOS app has the same limit (tgsbImportMaxBytes
 * in TgsbEngine.swift); tests/test_phone_import_size.py holds the two equal.
 */
object ImportText {
    /**
     * The largest file an import reads.  The largest text the packs hold is
     * the US English stress dictionary at 1.7 MB, and an "Export all" of it
     * has to import again; nothing made with the editors comes near this.
     */
    const val MAX_BYTES = 8 * 1024 * 1024

    /** The file is larger than [limit] bytes.  An ordinary read error, with a sentence the import screens show. */
    class TooLarge(val limit: Int) : IOException("the file is larger than ${limit / (1024 * 1024)} MB")

    /**
     * [input] as UTF-8 text, when it is at most [limit] bytes.  Beyond that
     * it throws [TooLarge], having taken no more than one byte past the
     * limit from the stream.
     */
    @Throws(IOException::class)
    fun read(input: InputStream, limit: Int = MAX_BYTES): String {
        val out = ByteArrayOutputStream()
        val block = ByteArray(64 * 1024)
        var total = 0
        while (true) {
            // Never ask for more than one byte past the limit: that one byte
            // is how "exactly the limit" is told from "more than it".
            val n = input.read(block, 0, minOf(block.size, limit + 1 - total))
            if (n < 0) break
            total += n
            if (total > limit) throw TooLarge(limit)
            out.write(block, 0, n)
        }
        return String(out.toByteArray(), Charsets.UTF_8)
    }
}
