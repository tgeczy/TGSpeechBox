/*
 * TgsbStorage — where the engine's data and settings live, so that a screen
 * reader can speak with this engine on the lock screen after a reboot
 * (Direct Boot).
 *
 * Android keeps an app's data in two halves. The credential-encrypted half --
 * the ordinary files dir, the shared preferences -- does not exist until the
 * person has unlocked the phone once after a reboot, and reading it before
 * then throws. The device-protected half is there from boot. The package
 * manager hides TTS engines that have not said they can run before unlock,
 * which is why TalkBack fell back to Google's engine on the lock screen: our
 * data lived in the wrong half and the service never said it could run there.
 *
 * Now the service says so (android:directBootAware in the manifest), the
 * extracted espeak-ng-data and tgsb packs live in protected storage, and so
 * do the settings, carried over once from where they used to live. The other
 * half is touched only after UserManager says the user is unlocked, because
 * an engine that throws on the lock screen is worse than one that is quiet
 * there.
 *
 * Ported from Tomi's Panthera-speech implementation (panthera-speech
 * bbe6e37 + 72c3a81). TGSpeechBox is the simpler case: its data comes out of
 * the APK, so there is no user-visible inbox to move. The data is extracted
 * straight into the protected half (TgsbAssets), and the copies an older
 * version left in the other half are removed once the user is unlocked.
 *
 * License: GPL-3.0
 */

package com.tgspeechbox.tts

import android.content.Context
import android.content.SharedPreferences
import android.os.UserManager
import android.util.Log
import java.io.File

object TgsbStorage {

    private const val TAG = "TgsbStorage"

    @Volatile private var knownUnlocked = false

    /**
     * Whether the person has unlocked the phone since it booted. Before that,
     * only protected storage may be touched.
     *
     * Asked on every call until the answer is yes. A process never survives
     * the reboot that would lock the user again, so a yes is remembered for
     * the life of the process and the system is not asked again.
     */
    fun unlocked(ctx: Context): Boolean {
        if (knownUnlocked) return true
        val now = try {
            ctx.getSystemService(UserManager::class.java)?.isUserUnlocked ?: true
        } catch (e: Exception) {
            true
        }
        if (now) knownUnlocked = true
        return now
    }

    /** A context whose files and preferences live in device-protected storage. */
    fun protectedContext(ctx: Context): Context =
        if (ctx.isDeviceProtectedStorage) ctx else ctx.createDeviceProtectedStorageContext()

    /**
     * Where espeak-ng-data and tgsb/ are extracted to and read from. Exists
     * from boot, so the service can open the native engine on the lock screen.
     */
    fun dataDir(ctx: Context): File = protectedContext(ctx).filesDir

    /**
     * The credential-encrypted files dir an older version extracted into, or
     * null when it cannot be looked at: before unlock, or when [ctx] is itself
     * a protected context (the other half cannot be reached from there).
     */
    fun legacyDataDir(ctx: Context): File? =
        if (ctx.isDeviceProtectedStorage || !unlocked(ctx)) null else ctx.filesDir

    @Volatile private var prefsMoved = false

    /**
     * The settings, in device-protected storage so the service can read them
     * before unlock. The first call in a process after unlock carries them
     * over from where they used to live -- a no-op once nothing is left there
     * -- so nobody's voice preset, language or sliders are lost to the move.
     * The other half cannot even be asked before unlock: it throws.
     *
     * Callers that hold on to the returned instance across an unlock must
     * fetch it again afterwards: the move evicts the cached instance, and the
     * one obtained before the move keeps its pre-move (empty) contents.
     */
    fun prefs(ctx: Context): SharedPreferences {
        val protected = protectedContext(ctx)
        if (!prefsMoved && !ctx.isDeviceProtectedStorage && unlocked(ctx)) {
            try {
                protected.moveSharedPreferencesFrom(ctx, TgsbTtsService.PREFS_NAME)
            } catch (e: Exception) {
                Log.w(TAG, "settings could not be moved into protected storage", e)
            }
            prefsMoved = true
        }
        return protected.getSharedPreferences(TgsbTtsService.PREFS_NAME, Context.MODE_PRIVATE)
    }
}
