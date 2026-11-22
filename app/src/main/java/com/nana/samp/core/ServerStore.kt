package com.nana.samp.core

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

data class SavedServer(
    val id: String,
    val label: String,
    val host: String,
    val port: Int,
    val password: String = "",
    val protocolVersion: Int = NativeBridge.PROTOCOL_037,
    // Live transient query fields
    var online: Boolean? = null,
    var hostname: String = "",
    var gamemode: String = "",
    var players: Int = 0,
    var maxPlayers: Int = 0,
    var pingMs: Int = 0,
    var hasPassword: Boolean = false,
    var version: String = "",
)

data class OptimizationSettings(
    val drawDistance: Float = 180f,
    val maxGpuCacheMb: Int = 96,
    val textureDownscaleShift: Int = 0,
    val targetFps: Int = 60,
)

class ServerStore(private val context: Context) {

    private val prefs = context.getSharedPreferences("nana_samp_prefs", Context.MODE_PRIVATE)

    var nickname: String
        get() = prefs.getString("nickname", "NanaPlayer") ?: "NanaPlayer"
        set(value) { prefs.edit().putString("nickname", sanitizeNickname(value)).apply() }

    var defaultProtocolVersion: Int
        get() = prefs.getInt("protocol_version", NativeBridge.PROTOCOL_037)
        set(value) { prefs.edit().putInt("protocol_version", value).apply() }

    var customGameDataPath: String
        get() = prefs.getString("game_data_path", "") ?: ""
        set(value) { prefs.edit().putString("game_data_path", value.trim()).apply() }

    fun getOptimizationSettings(): OptimizationSettings = OptimizationSettings(
        drawDistance = prefs.getFloat("opt_draw_distance", 180f).coerceIn(60f, 450f),
        maxGpuCacheMb = prefs.getInt("opt_gpu_cache_mb", 96).coerceIn(32, 512),
        textureDownscaleShift = prefs.getInt("opt_tex_downscale", 0).coerceIn(0, 2),
        targetFps = prefs.getInt("opt_target_fps", 60).coerceIn(20, 90),
    )

    fun saveOptimizationSettings(opt: OptimizationSettings) {
        prefs.edit()
            .putFloat("opt_draw_distance", opt.drawDistance)
            .putInt("opt_gpu_cache_mb", opt.maxGpuCacheMb)
            .putInt("opt_tex_downscale", opt.textureDownscaleShift)
            .putInt("opt_target_fps", opt.targetFps)
            .apply()
    }

    var amlEnabled: Boolean
        get() = prefs.getBoolean("aml_enabled", true)
        set(value) { prefs.edit().putBoolean("aml_enabled", value).apply() }

    var chatTimestamp: Boolean
        get() = prefs.getBoolean("chat_timestamp", false)
        set(value) { prefs.edit().putBoolean("chat_timestamp", value).apply() }

    var displayFps: Boolean
        get() = prefs.getBoolean("display_fps", true)
        set(value) { prefs.edit().putBoolean("display_fps", value).apply() }

    fun resolveGameDataDir(): File {
        val custom = customGameDataPath
        if (custom.isNotEmpty()) {
            val f = File(custom)
            if (f.exists() && f.isDirectory) return f
        }
        return File(GameDataManager.inspect(context).storageRoot)
    }

    private fun serversFile(): File {
        // Always store in app-private internal filesDir (100% writable on Android 6-16 Scoped Storage
        // and never overwritten when the user copies GTA SA data into external files/).
        return File(context.filesDir, "servers.json")
    }

    fun loadServers(): MutableList<SavedServer> {
        val f = serversFile()
        if (f.exists()) {
            runCatching { return parseServersJson(f.readText()) }
        }
        val prefJson = prefs.getString("servers_json_backup", null)
        if (!prefJson.isNullOrBlank()) {
            runCatching { return parseServersJson(prefJson) }
        }
        return mutableListOf()
    }

    fun saveServers(servers: List<SavedServer>) {
        val serialized = serializeServersJson(servers)
        prefs.edit().putString("servers_json_backup", serialized).apply()
        runCatching {
            val f = serversFile()
            f.parentFile?.mkdirs()
            f.writeText(serialized)
        }
    }

    companion object {
        fun sanitizeNickname(raw: String): String {
            val cleaned = raw.trim().filter { ch ->
                ch.isLetterOrDigit() || ch in listOf('_', '[', ']', '(', ')', '$', '@', '.', '=')
            }
            return cleaned.take(24).ifEmpty { "NanaPlayer" }
        }

        fun parseAddress(input: String): Pair<String, Int>? {
            val trimmed = input.trim()
            if (trimmed.isEmpty()) return null
            val idx = trimmed.lastIndexOf(':')
            return if (idx > 0 && idx < trimmed.length - 1) {
                val host = trimmed.substring(0, idx).trim()
                val port = trimmed.substring(idx + 1).trim().toIntOrNull() ?: return null
                if (host.isEmpty() || port !in 1..65535) null else Pair(host, port)
            } else {
                Pair(trimmed, 7777)
            }
        }

        fun parseServersJson(json: String): MutableList<SavedServer> {
            val arr = JSONArray(json)
            val list = mutableListOf<SavedServer>()
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val host = o.optString("host", "").trim()
                val port = o.optInt("port", 7777)
                if (host.isEmpty() || port !in 1..65535) continue
                list.add(
                    SavedServer(
                        id = o.optString("id", "$host:$port"),
                        label = o.optString("label", "$host:$port"),
                        host = host,
                        port = port,
                        password = o.optString("password", ""),
                        protocolVersion = o.optInt("protocol", NativeBridge.PROTOCOL_037),
                    )
                )
            }
            return list
        }

        fun serializeServersJson(servers: List<SavedServer>): String {
            val arr = JSONArray()
            for (s in servers) {
                val o = JSONObject()
                o.put("id", s.id)
                o.put("label", s.label)
                o.put("host", s.host)
                o.put("port", s.port)
                o.put("password", s.password)
                o.put("protocol", s.protocolVersion)
                arr.put(o)
            }
            return arr.toString(2)
        }
    }
}
