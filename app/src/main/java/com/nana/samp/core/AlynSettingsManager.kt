package com.nana.samp.core

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File

data class AlynClientSettings(
    val host: String = "",
    val port: Int = 7777,
    val password: String = "",
    val nickname: String = "NanaPlayer",
    val amlEnabled: Boolean = true,
    val displayFps: Boolean = true,
    val timestamp: Boolean = false,
    val fpsLimit: Int = 60,
    val chatLines: Int = 8,
)

/**
 * Full compatibility layer for Data_Sem_Mods_Alyn_-_Mali.zip's SAMP/settings.json schema
 * and automatic bootstrapping of compact SA-MP override defaults from assets/samp_defaults/.
 */
object AlynSettingsManager {
    private const val TAG = "NanaSAMP-AlynCfg"

    fun parseSettingsJson(rawJson: String): AlynClientSettings {
        val root = JSONObject(rawJson)
        val client = root.optJSONObject("client") ?: JSONObject()
        val server = client.optJSONObject("server") ?: JSONObject()
        val settings = client.optJSONObject("settings") ?: JSONObject()
        return AlynClientSettings(
            host = server.optString("host", "").trim(),
            port = server.optInt("port", 7777).coerceIn(1, 65535),
            password = server.optString("password", ""),
            nickname = ServerStore.sanitizeNickname(settings.optString("nick_name", "NanaPlayer")),
            amlEnabled = settings.optBoolean("cleo_aml", true),
            displayFps = settings.optBoolean("display_fps", true),
            timestamp = settings.optBoolean("timestamp", false),
            fpsLimit = settings.optInt("fps_limit", 60).coerceIn(20, 90),
            chatLines = settings.optInt("chat_strings", 8).coerceIn(4, 20),
        )
    }

    fun serializeSettingsJson(cfg: AlynClientSettings): String {
        val server = JSONObject().apply {
            put("host", cfg.host)
            put("port", cfg.port)
            put("password", cfg.password)
        }
        val settings = JSONObject().apply {
            put("nick_name", ServerStore.sanitizeNickname(cfg.nickname))
            put("new_interface", true)
            put("timestamp", cfg.timestamp)
            put("fast_connect", false)
            put("voice_chat", false)
            put("display_fps", cfg.displayFps)
            put("cleo_aml", cfg.amlEnabled)
            put("modify_files", true)
            put("fps_limit", cfg.fpsLimit)
            put("chat_strings", cfg.chatLines)
            put("chat_posx", 100)
            put("chat_posy", 10)
            put("chat_sizex", 400)
            put("chat_sizey", 150)
        }
        val client = JSONObject().apply {
            put("server", server)
            put("settings", settings)
        }
        return JSONObject().put("client", client).toString(2)
    }

    fun readFromStorage(storageRoot: File): AlynClientSettings? {
        val f = File(storageRoot, "SAMP/settings.json")
        if (!f.isFile || !f.canRead()) return null
        return runCatching { parseSettingsJson(f.readText()) }.getOrNull()
    }

    fun writeToStorage(storageRoot: File, cfg: AlynClientSettings) {
        runCatching {
            val f = File(storageRoot, "SAMP/settings.json")
            f.parentFile?.mkdirs()
            f.writeText(serializeSettingsJson(cfg))
        }.onFailure {
            Log.w(TAG, "Unable to persist SAMP/settings.json", it)
        }
    }

    /**
     * Copies missing compact SA-MP override files (SAMP/main.scm, SAMP/script.img, SAMP/gta.dat,
     * SAMP/SAMP.ide, etc.) from APK assets/samp_defaults/ into storageRoot without overwriting
     * any files already provided by the user's Alyn/GTA SA data pack.
     */
    fun ensureSampDefaultsInstalled(context: Context, storageRoot: File): Int {
        var copied = 0
        try {
            copied += copyAssetTreeIfMissing(context, "samp_defaults", storageRoot)
        } catch (t: Throwable) {
            Log.w(TAG, "Failed bootstrapping samp_defaults", t)
        }
        return copied
    }

    private fun copyAssetTreeIfMissing(context: Context, assetPath: String, destDir: File): Int {
        val list = context.assets.list(assetPath) ?: return 0
        if (list.isEmpty()) return 0
        var copied = 0
        for (entry in list) {
            val childAsset = "$assetPath/$entry"
            val subList = context.assets.list(childAsset)
            val target = File(destDir, entry)
            if (subList != null && subList.isNotEmpty()) {
                target.mkdirs()
                copied += copyAssetTreeIfMissing(context, childAsset, target)
            } else if (!target.exists()) {
                target.parentFile?.mkdirs()
                context.assets.open(childAsset).use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                }
                copied++
            }
        }
        return copied
    }
}
