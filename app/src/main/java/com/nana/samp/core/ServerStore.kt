package com.nana.samp.core

import android.content.Context
import android.os.Environment
import android.util.Log
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
        get() {
            val cached = prefs.getString("nickname", null)
            if (!cached.isNullOrBlank() && cached != "NanaPlayer") {
                return cached
            }
            val restored = restoreNicknameFromExternal()
            if (restored.isNotBlank() && restored != "NanaPlayer") {
                prefs.edit().putString("nickname", restored).apply()
                return restored
            }
            return cached ?: "NanaPlayer"
        }
        set(value) {
            val sanitized = sanitizeNickname(value)
            prefs.edit().putString("nickname", sanitized).apply()
            persistNicknameOutside(sanitized)
        }

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

    /**
     * Resolves all candidate servers.json locations in priority order:
     * 1. /sdcard/NanaSAMP/servers.json (public external folder, survives APK uninstall)
     * 2. /sdcard/Documents/NanaSAMP/servers.json (public documents folder)
     * 3. <storageRoot>/SAMP/servers.json (GTA SA SAMP data directory)
     * 4. <storageRoot>/servers.json (GTA SA data root)
     * 5. <externalFilesDir>/servers.json (/sdcard/Android/data/com.nana.samp/files/servers.json)
     * 6. <internalFilesDir>/servers.json (/data/data/com.nana.samp/files/servers.json)
     */
    fun getServersCandidateFiles(): List<File> {
        val list = mutableListOf<File>()
        runCatching {
            val ext = Environment.getExternalStorageDirectory()
            if (ext != null) list.add(File(ext, "NanaSAMP/servers.json"))
        }
        runCatching {
            val doc = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
            if (doc != null) list.add(File(doc, "NanaSAMP/servers.json"))
        }
        runCatching {
            val gDir = resolveGameDataDir()
            list.add(File(gDir, "SAMP/servers.json"))
            list.add(File(gDir, "servers.json"))
        }
        runCatching {
            context.getExternalFilesDir(null)?.let { list.add(File(it, "servers.json")) }
        }
        list.add(File(context.filesDir, "servers.json"))
        return list
    }

    private fun restoreNicknameFromExternal(): String {
        for (f in getServersCandidateFiles()) {
            if (f.isFile && f.canRead()) {
                val nick = runCatching {
                    val root = JSONObject(f.readText())
                    root.optString("nickname", root.optString("username", "")).trim()
                }.getOrNull()
                if (!nick.isNullOrBlank() && nick != "NanaPlayer") return sanitizeNickname(nick)
            }
        }
        runCatching {
            val alyn = AlynSettingsManager.readFromStorage(resolveGameDataDir())
            if (alyn != null && alyn.nickname.isNotBlank() && alyn.nickname != "NanaPlayer") {
                return sanitizeNickname(alyn.nickname)
            }
        }
        runCatching {
            val ext = Environment.getExternalStorageDirectory()
            if (ext != null) {
                val uf = File(ext, "NanaSAMP/user.json")
                if (uf.isFile && uf.canRead()) {
                    val o = JSONObject(uf.readText())
                    val nick = o.optString("nickname", o.optString("username", "")).trim()
                    if (nick.isNotBlank() && nick != "NanaPlayer") return sanitizeNickname(nick)
                }
            }
        }
        return ""
    }

    private fun persistNicknameOutside(nick: String) {
        val sanitized = sanitizeNickname(nick)
        runCatching {
            val ext = Environment.getExternalStorageDirectory()
            if (ext != null) {
                val uf = File(ext, "NanaSAMP/user.json")
                uf.parentFile?.mkdirs()
                uf.writeText(JSONObject().put("nickname", sanitized).put("username", sanitized).toString(2))
            }
        }
        runCatching {
            val gDir = resolveGameDataDir()
            val existing = AlynSettingsManager.readFromStorage(gDir) ?: AlynClientSettings()
            AlynSettingsManager.writeToStorage(gDir, existing.copy(nickname = sanitized))
        }
        runCatching {
            val currentServers = loadServers()
            if (currentServers.isNotEmpty()) {
                saveServers(currentServers)
            }
        }
    }

    fun loadServers(): MutableList<SavedServer> {
        for (f in getServersCandidateFiles()) {
            if (f.isFile && f.canRead()) {
                val parsed = runCatching {
                    val content = f.readText()
                    val pair = parseServersAndNickname(content)
                    if (pair.second.isNotBlank() && pair.second != "NanaPlayer") {
                        if (prefs.getString("nickname", "NanaPlayer") == "NanaPlayer") {
                            prefs.edit().putString("nickname", pair.second).apply()
                        }
                    }
                    pair.first
                }.getOrNull()
                if (parsed != null && parsed.isNotEmpty()) {
                    return parsed
                }
            }
        }
        val prefJson = prefs.getString("servers_json_backup", null)
        if (!prefJson.isNullOrBlank()) {
            val parsed = runCatching { parseServersAndNickname(prefJson).first }.getOrNull()
            if (parsed != null && parsed.isNotEmpty()) return parsed
        }
        return mutableListOf()
    }

    fun saveServers(servers: List<SavedServer>) {
        val serialized = serializeServersJson(servers, nickname)
        prefs.edit().putString("servers_json_backup", serialized).apply()

        for (f in getServersCandidateFiles()) {
            runCatching {
                f.parentFile?.mkdirs()
                f.writeText(serialized)
            }.onFailure {
                Log.d("NanaSAMP-Store", "Could not write servers.json to ${f.absolutePath}: ${it.message}")
            }
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

        fun parseServersAndNickname(json: String): Pair<MutableList<SavedServer>, String> {
            val trimmed = json.trim()
            if (trimmed.startsWith("{")) {
                val root = JSONObject(trimmed)
                val nick = root.optString("nickname", root.optString("username", "")).trim()
                val arr = root.optJSONArray("servers") ?: JSONArray()
                return Pair(parseServersFromArray(arr), nick)
            }
            if (trimmed.startsWith("[")) {
                return Pair(parseServersFromArray(JSONArray(trimmed)), "")
            }
            return Pair(mutableListOf(), "")
        }

        fun parseServersJson(json: String): MutableList<SavedServer> =
            parseServersAndNickname(json).first

        fun parseServersFromArray(arr: JSONArray): MutableList<SavedServer> {
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

        fun serializeServersJson(servers: List<SavedServer>, nickname: String = ""): String {
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
            val root = JSONObject()
            if (nickname.isNotBlank()) {
                val clean = sanitizeNickname(nickname)
                root.put("nickname", clean)
                root.put("username", clean)
            }
            root.put("servers", arr)
            return root.toString(2)
        }
    }
}
