/*
 * TgsbAssets — shared one-time extraction of espeak-ng-data and tgsb packs
 * from the APK into device-protected storage (TgsbStorage.dataDir).
 *
 * Both entry points need the same files on disk: TgsbTtsService (TalkBack) and
 * TgsbSpeakEngine (the standalone Speak UI). They previously each carried their
 * own copy of this logic gated on its own ASSET_VERSION constant, and the two
 * constants drifted apart (29 vs 28). Since each one deletes every .assets_*
 * marker before extracting, neither ever saw its own marker after the other had
 * run, so ~26 MB was re-extracted on nearly every switch between the app and
 * TalkBack — and the app could delete the data directory while the service still
 * had the native engine open against it.
 *
 * One constant, one implementation. Bump ASSET_VERSION whenever anything under
 * assets/tgsb or assets/espeak-ng-data changes.
 *
 * Since the Direct Boot port the target is the device-protected files dir, which
 * exists before the phone is unlocked, so the service can extract and open the
 * engine on the lock screen. The credential-encrypted files dir an older version
 * extracted into is emptied once the user is unlocked (~26 MB of dead weight).
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.content.Context
import android.content.res.AssetManager
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.IOException

object TgsbAssets {

    private const val TAG = "TgsbAssets"

    /**
     * Bump on every release that changes shipped packs or espeak data. Devices
     * carrying an older marker re-extract once, then skip until the next bump.
     */
    private const val ASSET_VERSION = 32

    /**
     * Extract the bundled data into [TgsbStorage.dataDir] unless this version has
     * already been extracted. Safe to call from both the service and the app;
     * synchronized so a second caller waits rather than deleting the tree out
     * from under the first.
     */
    @Synchronized
    fun ensureExtracted(context: Context) {
        val dataDir = TgsbStorage.dataDir(context)
        val marker = File(dataDir, ".assets_v$ASSET_VERSION")
        if (!marker.exists()) {
            dataDir.listFiles()
                ?.filter { it.name.startsWith(".assets_") }
                ?.forEach { it.delete() }
            File(dataDir, "espeak-ng-data").deleteRecursively()
            File(dataDir, "tgsb").deleteRecursively()

            Log.i(TAG, "Extracting assets v$ASSET_VERSION to ${dataDir.absolutePath}")
            val assetMgr = context.assets
            copyAssetsDir(assetMgr, "espeak-ng-data", File(dataDir, "espeak-ng-data"))
            copyAssetsDir(assetMgr, "tgsb", File(dataDir, "tgsb"))
            marker.createNewFile()
        }
        removeLegacyCopies(context)
    }

    /**
     * Versions before the Direct Boot port extracted into the credential-encrypted
     * files dir. Nothing reads from there any more, so those copies are removed
     * once -- off the caller's thread, and only when the user is unlocked, because
     * before that the folder cannot be seen. Skipped silently until then; a later
     * launch gets it.
     */
    private fun removeLegacyCopies(context: Context) {
        val legacy = TgsbStorage.legacyDataDir(context) ?: return
        val stale = ArrayList<File>()
        stale.add(File(legacy, "espeak-ng-data"))
        stale.add(File(legacy, "tgsb"))
        legacy.listFiles()?.filter { it.name.startsWith(".assets_") }?.let { stale.addAll(it) }
        val present = stale.filter { it.exists() }
        if (present.isEmpty()) return
        Thread({
            for (f in present) {
                try {
                    f.deleteRecursively()
                } catch (e: Exception) {
                    Log.w(TAG, "Could not remove legacy copy ${f.name}: ${e.message}")
                }
            }
            Log.i(TAG, "Removed ${present.size} legacy copies from ${legacy.absolutePath}")
        }, "tgsb-legacy-cleanup").start()
    }

    private fun copyAssetsDir(assetMgr: AssetManager, assetPath: String, targetDir: File) {
        val entries = assetMgr.list(assetPath) ?: return

        if (entries.isEmpty()) {
            copyAssetFile(assetMgr, assetPath, targetDir)
            return
        }

        targetDir.mkdirs()
        for (entry in entries) {
            val childAsset = "$assetPath/$entry"
            val childTarget = File(targetDir, entry)
            val subEntries = assetMgr.list(childAsset)
            if (subEntries != null && subEntries.isNotEmpty()) {
                copyAssetsDir(assetMgr, childAsset, childTarget)
            } else {
                copyAssetFile(assetMgr, childAsset, childTarget)
            }
        }
    }

    private fun copyAssetFile(assetMgr: AssetManager, assetPath: String, target: File) {
        try {
            target.parentFile?.mkdirs()
            assetMgr.open(assetPath).use { input ->
                FileOutputStream(target).use { output ->
                    input.copyTo(output)
                }
            }
        } catch (e: IOException) {
            Log.e(TAG, "Failed to copy asset $assetPath: ${e.message}")
        }
    }
}
