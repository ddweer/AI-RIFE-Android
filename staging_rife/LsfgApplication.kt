package com.lsfg.android

import android.app.Application
import android.util.Log
import com.lsfg.android.session.CrashReporter
import com.lsfg.android.session.LsfgLog
import java.io.File

class LsfgApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        // Install the crash reporter as early as possible so even a crash during
        // the first JNI load attempt gets captured (NativeBridge static init
        // already loads the .so; the call below only configures the handler).
        CrashReporter.install(this)
        LsfgLog.init(this)
        extractRifeModel()
    }

    /**
     * The RIFE framegen pipeline loads its model from
     * `<filesDir>/spirv/rife/` — the same "cache" directory every native
     * entry point receives (`File(filesDir, "spirv")`). Ship the two model
     * files inside the APK as assets and extract them on first launch;
     * skipped when both already exist and are non-empty.
     */
    private fun extractRifeModel() {
        val names = listOf("flownet.param", "flownet.bin")
        val dir = File(File(filesDir, "spirv"), "rife")
        val allPresent = names.all { name ->
            val f = File(dir, name)
            f.isFile && f.length() > 0L
        }
        if (allPresent) return
        try {
            if (!dir.exists() && !dir.mkdirs()) {
                Log.w(TAG, "failed to create ${dir.absolutePath}")
                return
            }
            for (name in names) {
                val out = File(dir, name)
                if (out.isFile && out.length() > 0L) continue
                assets.open("rife/$name").use { input ->
                    out.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
            }
            Log.i(TAG, "RIFE model extracted to ${dir.absolutePath}")
        } catch (t: Throwable) {
            // Surface later as kErrMissingResource from initContext instead of
            // crashing app startup here.
            Log.w(TAG, "RIFE model extraction failed", t)
        }
    }

    private companion object {
        private const val TAG = "LsfgApplication"
    }
}
