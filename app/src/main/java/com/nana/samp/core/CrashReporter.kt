package com.nana.samp.core

import android.content.Context
import android.util.Log
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Captures uncaught JVM exceptions and reads native crash logs from app-scoped storage so
 * users and developers get actionable diagnostics instead of silent force closes.
 */
object CrashReporter {
    private const val TAG = "NanaSAMP-Crash"
    private const val JAVA_CRASH_FILE = "nana_java_crash.log"
    private const val NATIVE_CRASH_FILE = "nana_crash.log"

    @Volatile
    private var installed = false

    fun install(context: Context) {
        if (installed) return
        installed = true
        val logDir = context.getExternalFilesDir(null) ?: context.filesDir
        val previous = Thread.getDefaultUncaughtExceptionHandler()

        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            try {
                val sw = StringWriter()
                throwable.printStackTrace(PrintWriter(sw))
                val stamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
                val report = "[$stamp] Uncaught exception on thread '${thread.name}':\n$sw\n"
                Log.e(TAG, report)
                if (!logDir.exists()) logDir.mkdirs()
                File(logDir, JAVA_CRASH_FILE).writeText(report)
            } catch (_: Throwable) {
            }
            previous?.uncaughtException(thread, throwable)
        }
    }

    fun readLastCrashSummary(context: Context): String? {
        val dirs = listOfNotNull(context.getExternalFilesDir(null), context.filesDir)
        for (dir in dirs) {
            val javaFile = File(dir, JAVA_CRASH_FILE)
            if (javaFile.isFile && javaFile.length() > 0) {
                val lines = runCatching { javaFile.readLines().take(6) }.getOrDefault(emptyList())
                if (lines.isNotEmpty()) {
                    return "Last Java crash (${javaFile.name}):\n" + lines.joinToString("\n")
                }
            }
            val nativeFile = File(dir, NATIVE_CRASH_FILE)
            if (nativeFile.isFile && nativeFile.length() > 0) {
                val lines = runCatching { nativeFile.readLines().takeLast(12) }.getOrDefault(emptyList())
                if (lines.isNotEmpty()) {
                    return "Last native crash (${nativeFile.name}):\n" + lines.joinToString("\n")
                }
            }
        }
        return null
    }

    fun clearCrashLogs(context: Context) {
        val dirs = listOfNotNull(context.getExternalFilesDir(null), context.filesDir)
        for (dir in dirs) {
            runCatching { File(dir, JAVA_CRASH_FILE).delete() }
            runCatching { File(dir, NATIVE_CRASH_FILE).delete() }
        }
    }
}
