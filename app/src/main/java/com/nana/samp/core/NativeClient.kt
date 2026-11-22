package com.nana.samp.core

import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

data class ClientSnapshot(
    val state: Int = 0,
    val status: String = "Disconnected",
    val serverName: String = "",
    val playerId: Int = -1,
    val ping: Int = 0,
    val players: Int = 0,
    val score: Int = 0,
    val money: Int = 0,
    val inClassSelect: Boolean = false,
    val classId: Int = 0,
    val worldReady: Boolean = false,
    val gameState: Int = -1,
    val loadingPercent: Int = 0,
    val texDbLoaded: Int = 0,
    val ticks: Int = 0,
    val amlLoaded: Boolean = false,
    val amlMods: Int = 0,
    val amlSummary: String = "AML 1.4.0 Standby",
    val health: Float = 100f,
    val armour: Float = 0f,
    val x: Float = 0f,
    val y: Float = 0f,
    val z: Float = 0f,
)

data class ServerQueryResult(
    val reachable: Boolean = false,
    val passworded: Boolean = false,
    val players: Int = 0,
    val maxPlayers: Int = 0,
    val pingMs: Int = 0,
    val hostname: String = "",
    val gamemode: String = "",
    val language: String = "",
)

data class AmlDiagnostics(
    val loaded: Boolean = false,
    val started: Boolean = false,
    val interfaceOk: Boolean = false,
    val modsDir: String = "",
    val mods: List<String> = emptyList(),
    val warnings: List<String> = emptyList(),
)

/**
 * JNI bridge to libnana_samp.so + verified GTA SA Android 2.00 & AML 1.4.0 native library loader.
 */
class NativeClient {

    interface Listener {
        fun onNativeStateChanged(state: Int, message: String)
        fun onNativeChatMessage(rgbaColor: Int, message: String)
        fun onNativeClassSelection(visible: Boolean, classId: Int, skinId: Int)
        fun onNativeShowDialog(dialogId: Int, style: Int, title: String, info: String, button1: String, button2: String)
        fun onNativeHideDialog()
        fun onNativeScoreboardUpdated()
        fun onNativeGameText(style: Int, timeMs: Int, text: String)
        fun onNativeAudioStream(active: Boolean, url: String)
        fun onNativeTextDrawsChanged()
        fun onNativeTextDrawSelectChanged(enabled: Boolean, hoverColorRgba: Int)
    }

    external fun nativeInstallCrashHandler(logPath: String)
    external fun nativeSetListener(listener: Listener?)
    external fun nativeInitGtaHooks(
        storageRoot: String,
        preferredTexFormat: Int,
        gpuRenderer: String,
        gpuExtensions: String,
    ): Boolean

    external fun nativeConnect(host: String, port: Int, nickname: String, password: String)
    external fun nativeDisconnect()
    external fun nativeReconnect()
    external fun nativeSendChat(text: String)
    external fun nativeRequestClassDelta(delta: Int)
    external fun nativeRequestSpawn()
    external fun nativeSendDialogResponse(dialogId: Int, button: Int, listItem: Int, inputText: String)
    external fun nativeRequestScoreboard()
    external fun nativeClickPlayer(playerId: Int)
    external fun nativeClickTextDraw(textDrawId: Int)
    external fun nativeCancelTextDrawSelect()

    external fun nativeGetSnapshotJson(): String
    external fun nativeGetScoreboardJson(): String
    external fun nativeGetTextDrawsJson(): String
    external fun nativeGetAmlStatusJson(): String
    external fun nativeQueryServerInfoJson(host: String, port: Int, timeoutMs: Int): String

    fun getSnapshot(): ClientSnapshot {
        return runCatching {
            val o = JSONObject(nativeGetSnapshotJson())
            ClientSnapshot(
                state = o.optInt("state", 0),
                status = o.optString("status", "Disconnected"),
                serverName = o.optString("serverName", ""),
                playerId = o.optInt("playerId", -1),
                ping = o.optInt("ping", 0),
                players = o.optInt("players", 0),
                score = o.optInt("score", 0),
                money = o.optInt("money", 0),
                inClassSelect = o.optBoolean("inClassSelect", false),
                classId = o.optInt("classId", 0),
                worldReady = o.optBoolean("worldReady", false),
                gameState = o.optInt("gameState", -1),
                loadingPercent = o.optInt("loadingPercent", 0),
                texDbLoaded = o.optInt("texDbLoaded", 0),
                ticks = o.optInt("ticks", 0),
                amlLoaded = o.optBoolean("amlLoaded", false),
                amlMods = o.optInt("amlMods", 0),
                amlSummary = o.optString("amlSummary", "AML 1.4.0"),
                health = o.optDouble("health", 100.0).toFloat(),
                armour = o.optDouble("armour", 0.0).toFloat(),
                x = o.optDouble("x", 0.0).toFloat(),
                y = o.optDouble("y", 0.0).toFloat(),
                z = o.optDouble("z", 0.0).toFloat(),
            )
        }.getOrDefault(ClientSnapshot())
    }

    fun getScoreboard(localPlayerId: Int): List<NativeBridge.ScoreboardItem> {
        return runCatching {
            val arr = JSONArray(nativeGetScoreboardJson())
            buildList(arr.length()) {
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    val pid = o.optInt("id", -1)
                    add(
                        NativeBridge.ScoreboardItem(
                            id = pid,
                            name = o.optString("name", "Player"),
                            score = o.optInt("score", 0),
                            ping = o.optInt("ping", 0),
                            rgba = o.optLong("color", 0xFFFFFFFFL).toInt(),
                            isLocal = (pid == localPlayerId),
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    fun getTextDraws(): List<NativeBridge.TextDrawModel> {
        return runCatching {
            val arr = JSONArray(nativeGetTextDrawsJson())
            buildList(arr.length()) {
                for (i in 0 until arr.length()) {
                    val o = arr.getJSONObject(i)
                    add(
                        NativeBridge.TextDrawModel(
                            id = o.optInt("id", 0),
                            flags = o.optInt("flags", 0),
                            letterWidth = o.optDouble("letterW", 0.4).toFloat(),
                            letterHeight = o.optDouble("letterH", 1.2).toFloat(),
                            letterColorAbgr = o.optLong("letterColor", 0xFFFFFFFFL).toInt(),
                            lineWidth = o.optDouble("lineW", 640.0).toFloat(),
                            lineHeight = o.optDouble("lineH", 0.0).toFloat(),
                            boxColorAbgr = o.optLong("boxColor", 0x80000000L).toInt(),
                            shadow = o.optInt("shadow", 0),
                            outline = o.optInt("outline", 0),
                            bgColorAbgr = o.optLong("bgColor", 0xFF000000L).toInt(),
                            style = o.optInt("style", 1),
                            selectable = o.optBoolean("selectable", false),
                            x = o.optDouble("x", 0.0).toFloat(),
                            y = o.optDouble("y", 0.0).toFloat(),
                            modelId = o.optInt("modelId", 0),
                            text = o.optString("text", ""),
                        )
                    )
                }
            }
        }.getOrDefault(emptyList())
    }

    fun getAmlDiagnostics(): AmlDiagnostics {
        return runCatching {
            val o = JSONObject(nativeGetAmlStatusJson())
            val mods = mutableListOf<String>()
            val modsArr = o.optJSONArray("mods")
            if (modsArr != null) {
                for (i in 0 until modsArr.length()) mods.add(modsArr.optString(i))
            }
            val warns = mutableListOf<String>()
            val warnArr = o.optJSONArray("warnings")
            if (warnArr != null) {
                for (i in 0 until warnArr.length()) warns.add(warnArr.optString(i))
            }
            AmlDiagnostics(
                loaded = o.optBoolean("loaded", false),
                started = o.optBoolean("started", false),
                interfaceOk = o.optBoolean("interfaceOk", false),
                modsDir = o.optString("modsDir", ""),
                mods = mods,
                warnings = warns,
            )
        }.getOrDefault(AmlDiagnostics())
    }

    fun queryServer(host: String, port: Int, timeoutMs: Int = 1800): ServerQueryResult {
        return runCatching {
            val o = JSONObject(nativeQueryServerInfoJson(host, port, timeoutMs))
            ServerQueryResult(
                reachable = o.optBoolean("reachable", false),
                passworded = o.optBoolean("passworded", false),
                players = o.optInt("players", 0),
                maxPlayers = o.optInt("maxPlayers", 0),
                pingMs = o.optInt("pingMs", 0),
                hostname = o.optString("hostname", ""),
                gamemode = o.optString("gamemode", ""),
                language = o.optString("language", ""),
            )
        }.getOrDefault(ServerQueryResult())
    }

    companion object {
        private const val TAG = "NanaSAMP-Native"

        @Volatile
        private var engineStackLoaded = false

        @Volatile
        private var sampCoreLoaded = false

        fun ensureSampCoreLoaded(): Boolean {
            if (sampCoreLoaded) return true
            return try {
                System.loadLibrary("nana_samp")
                sampCoreLoaded = true
                true
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to load libnana_samp.so", t)
                false
            }
        }

        /**
         * Loads the verified GTA SA Android 2.00 + AML 1.4.0 native stack in the exact order
         * discovered from com.rockstargames.gtasa.GTASA:
         *   ImmEmulatorJ -> SCAnd -> Fastman92pluginLoader -> GTASA -> threadfix -> AML -> nana_samp
         */
        @Synchronized
        fun loadGtaSaEngineStack(enableAml: Boolean = true): Result<Unit> {
            if (engineStackLoaded) return Result.success(Unit)
            return try {
                runCatching { System.loadLibrary("ImmEmulatorJ") }
                runCatching { System.loadLibrary("TouchSenseSDK") }
                System.loadLibrary("SCAnd")
                System.loadLibrary("Fastman92pluginLoader")
                System.loadLibrary("GTASA")
                runCatching { System.loadLibrary("threadfix") }
                    .onFailure { Log.w(TAG, "libthreadfix.so optional load skipped", it) }
                if (enableAml) {
                    runCatching { System.loadLibrary("AML") }
                        .onSuccess { Log.i(TAG, "AML 1.4.0 (libAML.so) loaded cleanly") }
                        .onFailure { Log.w(TAG, "libAML.so load failed (continuing without AML)", it) }
                }
                System.loadLibrary("nana_samp")
                sampCoreLoaded = true
                engineStackLoaded = true
                Result.success(Unit)
            } catch (t: Throwable) {
                Log.e(TAG, "Failed loading GTA SA 2.00 native stack", t)
                Result.failure(t)
            }
        }
    }
}
