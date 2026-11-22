package com.nana.samp

import android.app.AlertDialog
import android.graphics.Bitmap
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.nana.samp.core.AlynSettingsManager
import com.nana.samp.core.AudioStreamPlayer
import com.nana.samp.core.CrashReporter
import com.nana.samp.core.GameDataManager
import com.nana.samp.core.NativeBridge
import com.nana.samp.core.NativeClient
import com.nana.samp.core.SampLogoLoader
import com.nana.samp.core.TextureFormat
import com.nana.samp.ui.ChatAdapter
import com.nana.samp.ui.SampDialogPresenter
import com.nana.samp.ui.ScoreboardDialog
import com.nana.samp.ui.TextDrawOverlayView
import com.nvidia.devtech.NvUtil
import com.wardrumstudios.utils.WarMedia
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors

/**
 * Native GTA San Andreas Android 2.00 + AML 1.4.0 + SA-MP 0.3.7 / open.mp GameActivity.
 *
 * Extends WarMedia -> WarGamepad -> WarBase -> NvEventQueueActivity so libGTASA.so 2.00 renders
 * directly to the native OpenGL ES SurfaceView and retains 100% of original GTA SA rendering,
 * audio, map, vehicles, physics, and multi-touch controls while overlaying native SA-MP HUD,
 * chat, dialogs, scoreboard, TextDraws, class selection, and pause menu.
 */
class GameActivity : WarMedia(), NativeClient.Listener {

    private lateinit var nativeClient: NativeClient
    private lateinit var audioPlayer: AudioStreamPlayer
    private lateinit var dialogPresenter: SampDialogPresenter
    private lateinit var scoreboardDialog: ScoreboardDialog
    private lateinit var chatAdapter: ChatAdapter

    private lateinit var surfaceView: SurfaceView
    private lateinit var textDrawOverlay: TextDrawOverlayView
    private lateinit var inGameHudContainer: FrameLayout
    private lateinit var rvChat: RecyclerView
    private lateinit var chatInputBar: LinearLayout
    private lateinit var etChatInput: EditText
    private lateinit var tvHudTelemetry: TextView
    private lateinit var tvGameTextBanner: TextView
    private lateinit var classSelectBar: LinearLayout
    private lateinit var tvClassLabel: TextView
    private lateinit var pauseMenuOverlay: FrameLayout
    private lateinit var tvPauseMeta: TextView

    private lateinit var sampLoadingOverlay: LinearLayout
    private lateinit var ivLoadingLogo: ImageView
    private lateinit var pbSampLoading: ProgressBar
    private lateinit var tvLoadingPercent: TextView
    private lateinit var tvLoadingConsole: TextView

    private val uiHandler = Handler(Looper.getMainLooper())
    private val logoExecutor = Executors.newSingleThreadExecutor()
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    private var serverHost = "127.0.0.1"
    private var serverPort = 7777
    private var nickname = "NanaPlayer"
    private var password = ""
    private var showFps = true
    private var chatTimestamp = false

    private var loadingDismissed = false
    private var pollingActive = true
    private var textDrawSelecting = false
    private var textDrawHoverRgba = 0xFFFFD700.toInt()
    private var lastLocalPlayerId = -1
    private var lastServerName = ""
    private var lastFrameCount = 0
    private var lastFpsTimestampMs = System.currentTimeMillis()
    private var currentFps = 0
    private var loadedLogoBitmap: Bitmap? = null

    private val hideGameTextRunnable = Runnable {
        tvGameTextBanner.visibility = View.GONE
    }

    private val telemetryRunnable = object : Runnable {
        override fun run() {
            if (!pollingActive || isFinishing || isDestroyed) return
            if (nativeLibrariesLoaded) {
                val snap = nativeClient.getSnapshot()
                lastLocalPlayerId = snap.playerId
                if (snap.serverName.isNotBlank()) lastServerName = snap.serverName

                val now = System.currentTimeMillis()
                val elapsed = now - lastFpsTimestampMs
                if (elapsed >= 1000L) {
                    val deltaFrames = (eglSwapCount - lastFrameCount).coerceAtLeast(0)
                    currentFps = ((deltaFrames * 1000L) / elapsed).toInt()
                    lastFrameCount = eglSwapCount
                    lastFpsTimestampMs = now
                }

                val hudParts = buildList {
                    add(snap.status.ifBlank { "Connecting..." })
                    if (snap.players > 0) add("Players: ${snap.players}")
                    if (snap.ping > 0) add("${snap.ping}ms")
                    if (showFps && currentFps > 0) add("${currentFps} FPS")
                    if (snap.amlLoaded) add("AML:${snap.amlMods}")
                }
                tvHudTelemetry.text = hudParts.joinToString("  •  ")
                tvPauseMeta.text = "$serverHost:$serverPort • $nickname • ${snap.amlSummary}"

                if (!loadingDismissed) {
                    val pct = snap.loadingPercent.coerceIn(5, 100)
                    pbSampLoading.progress = pct
                    tvLoadingPercent.text = "$pct%"
                    tvLoadingConsole.text = buildString {
                        append("Server: $serverHost:$serverPort ($nickname)\n")
                        append("Status: ${snap.status}\n")
                        append("Engine state=${snap.gameState} • TexDB=${snap.texDbLoaded} • ${snap.amlSummary}")
                    }
                    if (snap.worldReady || (snap.gameState >= 9 && eglSwapCount > 4)) {
                        dismissLoadingOverlay()
                    }
                }
            }
            uiHandler.postDelayed(this, 250L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CrashReporter.install(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(R.layout.activity_game)
        enterImmersiveFullscreen()

        serverHost = intent.getStringExtra(EXTRA_HOST) ?: "127.0.0.1"
        serverPort = intent.getIntExtra(EXTRA_PORT, 7777)
        nickname = intent.getStringExtra(EXTRA_NICKNAME) ?: "NanaPlayer"
        password = intent.getStringExtra(EXTRA_PASSWORD) ?: ""
        showFps = intent.getBooleanExtra(EXTRA_SHOW_FPS, true)
        chatTimestamp = intent.getBooleanExtra(EXTRA_CHAT_TIMESTAMP, false)
        val amlEnabled = intent.getBooleanExtra(EXTRA_AML_ENABLED, true)
        val gpuRenderer = intent.getStringExtra(EXTRA_GPU_RENDERER) ?: ""
        val gpuExtensions = intent.getStringExtra(EXTRA_GPU_EXTENSIONS) ?: ""
        val texFmtCode = intent.getIntExtra(EXTRA_TEX_FORMAT_CODE, TextureFormat.ETC.nativeCode)

        val inspected = GameDataManager.inspect(this)
        val storageRootRaw = intent.getStringExtra(EXTRA_STORAGE_ROOT)?.takeIf { it.isNotBlank() }
            ?: inspected.storageRoot
        val storageRootDir = File(storageRootRaw)
        if (!storageRootDir.exists()) storageRootDir.mkdirs()

        // Ensure trailing slash for Rockstar libGTASA.so StorageRootBuffer
        val storageRootWithSlash = storageRootRaw.trimEnd('/') + "/"
        gameDataRoot = storageRootWithSlash
        mainObbPath = intent.getStringExtra(EXTRA_MAIN_OBB) ?: inspected.mainObbPath
        patchObbPath = intent.getStringExtra(EXTRA_PATCH_OBB) ?: inspected.patchObbPath

        // Bootstrap compact Alyn SA-MP overrides (SAMP/main.scm, gta.dat, SAMP.ide, etc.) if absent
        val copiedDefaults = AlynSettingsManager.ensureSampDefaultsInstalled(this, storageRootDir)
        if (copiedDefaults > 0) {
            Log.i(TAG, "Installed $copiedDefaults default SA-MP override files into $storageRootRaw")
        }

        // Bind NvUtil before loading libGTASA.so so NvUtil::GetParameter("STORAGE_ROOT") succeeds
        NvUtil.getInstance().setActivity(this)
        NvUtil.getInstance().setParameter("STORAGE_ROOT", storageRootWithSlash)
        NvUtil.getInstance().setParameter("STORAGE_ROOT_BASE", storageRootWithSlash)

        bindViews()

        nativeClient = NativeClient()
        audioPlayer = AudioStreamPlayer()
        dialogPresenter = SampDialogPresenter(this) { dlgId, btn, item, input ->
            if (nativeLibrariesLoaded) {
                nativeClient.nativeSendDialogResponse(dlgId, btn, item, input)
            }
            enterImmersiveFullscreen()
        }
        scoreboardDialog = ScoreboardDialog(
            activity = this,
            onClickPlayer = { pid ->
                if (nativeLibrariesLoaded) nativeClient.nativeClickPlayer(pid)
            },
            onRefresh = {
                if (nativeLibrariesLoaded) nativeClient.nativeRequestScoreboard()
            },
        )

        // Load SA-MP splash logo if present in unpacked texdb
        logoExecutor.execute {
            val bmp = runCatching { SampLogoLoader.loadLogo(storageRootRaw).bitmap }.getOrNull()
            if (bmp != null) {
                runOnUiThread {
                    if (!isFinishing && !isDestroyed) {
                        loadedLogoBitmap = bmp
                        ivLoadingLogo.setImageBitmap(bmp)
                        ivLoadingLogo.visibility = View.VISIBLE
                    } else {
                        bmp.recycle()
                    }
                }
            }
        }

        // Load GTA SA 2.00 + AML 1.4.0 + Nana SAMP native library stack
        val loadResult = NativeClient.loadGtaSaEngineStack(enableAml = amlEnabled)
        if (loadResult.isFailure) {
            val err = loadResult.exceptionOrNull()?.message ?: "Unknown native library load failure"
            tvLoadingConsole.text = "Native stack load failed:\n$err"
            Toast.makeText(this, "Failed to load GTA SA 2.00 engine: $err", Toast.LENGTH_LONG).show()
            return
        }
        nativeLibrariesLoaded = true

        val crashLogPath = File(getExternalFilesDir(null) ?: filesDir, "nana_crash.log").absolutePath
        nativeClient.nativeInstallCrashHandler(crashLogPath)
        nativeClient.nativeSetListener(this)

        runCatching { jniNvAPKInit(assets) }
            .onFailure { Log.w(TAG, "jniNvAPKInit warning", it) }

        val hooksOk = nativeClient.nativeInitGtaHooks(
            storageRootWithSlash,
            texFmtCode,
            gpuRenderer,
            gpuExtensions,
        )
        Log.i(TAG, "nativeInitGtaHooks=$hooksOk root=$storageRootWithSlash fmt=$texFmtCode")

        // Attach the real GTA SA 2.00 SurfaceView and forward native touch input directly to GTA SA
        attachGameSurfaceView(surfaceView)
        surfaceView.setOnTouchListener { _, event ->
            forwardGameTouchEvent(event)
            true
        }

        // Queue live SA-MP 0.3.7 / open.mp multiplayer connection
        nativeClient.nativeConnect(serverHost, serverPort, nickname, password)

        uiHandler.post(telemetryRunnable)
    }

    private fun bindViews() {
        surfaceView = findViewById(R.id.gameSurfaceView)
        textDrawOverlay = findViewById(R.id.textDrawOverlay)
        inGameHudContainer = findViewById(R.id.inGameHudContainer)
        rvChat = findViewById(R.id.rvChat)
        chatInputBar = findViewById(R.id.chatInputBar)
        etChatInput = findViewById(R.id.etChatInput)
        tvHudTelemetry = findViewById(R.id.tvHudTelemetry)
        tvGameTextBanner = findViewById(R.id.tvGameTextBanner)
        classSelectBar = findViewById(R.id.classSelectBar)
        tvClassLabel = findViewById(R.id.tvClassLabel)
        pauseMenuOverlay = findViewById(R.id.pauseMenuOverlay)
        tvPauseMeta = findViewById(R.id.tvPauseMeta)

        sampLoadingOverlay = findViewById(R.id.sampLoadingOverlay)
        ivLoadingLogo = findViewById(R.id.ivLoadingLogo)
        pbSampLoading = findViewById(R.id.pbSampLoading)
        tvLoadingPercent = findViewById(R.id.tvLoadingPercent)
        tvLoadingConsole = findViewById(R.id.tvLoadingConsole)

        chatAdapter = ChatAdapter()
        rvChat.layoutManager = LinearLayoutManager(this).apply { stackFromEnd = true }
        rvChat.adapter = chatAdapter

        textDrawOverlay.onClickTextDraw = { tdId ->
            if (nativeLibrariesLoaded) nativeClient.nativeClickTextDraw(tdId)
        }

        findViewById<Button>(R.id.btnToggleChat).setOnClickListener { toggleChatInput() }
        findViewById<Button>(R.id.btnSendChat).setOnClickListener { submitChatMessage() }
        etChatInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                submitChatMessage()
                true
            } else {
                false
            }
        }

        findViewById<Button>(R.id.btnScoreboard).setOnClickListener { openScoreboard() }
        findViewById<Button>(R.id.btnPauseMenu).setOnClickListener { togglePauseMenu(true) }

        findViewById<Button>(R.id.btnClassPrev).setOnClickListener {
            if (nativeLibrariesLoaded) nativeClient.nativeRequestClassDelta(-1)
        }
        findViewById<Button>(R.id.btnClassNext).setOnClickListener {
            if (nativeLibrariesLoaded) nativeClient.nativeRequestClassDelta(1)
        }
        findViewById<Button>(R.id.btnClassSpawn).setOnClickListener {
            if (nativeLibrariesLoaded) nativeClient.nativeRequestSpawn()
        }

        findViewById<Button>(R.id.btnPauseResume).setOnClickListener { togglePauseMenu(false) }
        findViewById<Button>(R.id.btnPauseScoreboard).setOnClickListener {
            togglePauseMenu(false)
            openScoreboard()
        }
        findViewById<Button>(R.id.btnPauseReconnect).setOnClickListener {
            togglePauseMenu(false)
            if (nativeLibrariesLoaded) {
                nativeClient.nativeReconnect()
                Toast.makeText(this, "Reconnecting to $serverHost:$serverPort...", Toast.LENGTH_SHORT).show()
            }
        }
        findViewById<Button>(R.id.btnPauseAmlInfo).setOnClickListener { showAmlDiagnosticsDialog() }
        findViewById<Button>(R.id.btnPauseDisconnect).setOnClickListener {
            if (nativeLibrariesLoaded) runCatching { nativeClient.nativeDisconnect() }
            finish()
        }
    }

    private fun togglePauseMenu(show: Boolean) {
        pauseMenuOverlay.visibility = if (show) View.VISIBLE else View.GONE
        if (!show) enterImmersiveFullscreen()
    }

    private fun openScoreboard() {
        if (!nativeLibrariesLoaded) return
        nativeClient.nativeRequestScoreboard()
        val entries = nativeClient.getScoreboard(lastLocalPlayerId)
        scoreboardDialog.show(lastServerName.ifBlank { "$serverHost:$serverPort" }, entries)
    }

    private fun showAmlDiagnosticsDialog() {
        if (!nativeLibrariesLoaded) return
        val diag = nativeClient.getAmlDiagnostics()
        val msg = buildString {
            append("libAML.so Loaded: ${if (diag.loaded) "YES" else "NO"}\n")
            append("AML Initialized: ${if (diag.started) "YES" else "NO"}\n")
            append("IAML Interface: ${if (diag.interfaceOk) "OK" else "Unavailable"}\n")
            append("Mods Folder: ${diag.modsDir}\n\n")
            if (diag.mods.isEmpty()) {
                append("Installed AML Mods: (none)\n")
            } else {
                append("Installed AML Mods (${diag.mods.size}):\n")
                diag.mods.forEach { append("  • $it\n") }
            }
            if (diag.warnings.isNotEmpty()) {
                append("\nCompatibility Warnings:\n")
                diag.warnings.forEach { append("  ! $it\n") }
            }
        }
        AlertDialog.Builder(this)
            .setTitle("AML 1.4.0 Diagnostics")
            .setMessage(msg)
            .setPositiveButton("Close", null)
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        when {
            chatInputBar.visibility == View.VISIBLE -> toggleChatInput()
            textDrawSelecting -> {
                textDrawSelecting = false
                if (nativeLibrariesLoaded) nativeClient.nativeCancelTextDrawSelect()
            }
            pauseMenuOverlay.visibility == View.VISIBLE -> togglePauseMenu(false)
            else -> togglePauseMenu(true)
        }
    }

    private fun dismissLoadingOverlay() {
        if (loadingDismissed) return
        loadingDismissed = true
        pbSampLoading.progress = 100
        tvLoadingPercent.text = "100%"
        sampLoadingOverlay.animate()
            .alpha(0f)
            .setDuration(300L)
            .withEndAction {
                if (!isFinishing && !isDestroyed) {
                    sampLoadingOverlay.visibility = View.GONE
                    sampLoadingOverlay.alpha = 1f
                }
            }
            .start()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) enterImmersiveFullscreen()
    }

    private fun enterImmersiveFullscreen() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.insetsController?.let { controller ->
                    controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                    controller.systemBarsBehavior =
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                }
            } else {
                @Suppress("DEPRECATION")
                window.decorView.systemUiVisibility = (
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                        View.SYSTEM_UI_FLAG_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    )
            }
        } catch (_: Throwable) {
        }
    }

    private fun toggleChatInput() {
        val show = chatInputBar.visibility != View.VISIBLE
        chatInputBar.visibility = if (show) View.VISIBLE else View.GONE
        val imm = getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager
        if (show) {
            etChatInput.requestFocus()
            imm?.showSoftInput(etChatInput, InputMethodManager.SHOW_IMPLICIT)
        } else {
            imm?.hideSoftInputFromWindow(etChatInput.windowToken, 0)
            enterImmersiveFullscreen()
        }
    }

    private fun submitChatMessage() {
        val text = etChatInput.text.toString().trim()
        etChatInput.setText("")
        chatInputBar.visibility = View.GONE
        val imm = getSystemService(INPUT_METHOD_SERVICE) as? InputMethodManager
        imm?.hideSoftInputFromWindow(etChatInput.windowToken, 0)
        enterImmersiveFullscreen()
        if (text.isNotEmpty() && nativeLibrariesLoaded) {
            runCatching { nativeClient.nativeSendChat(text) }
        }
    }

    // ---- NativeClient.Listener callbacks ----

    override fun onNativeStateChanged(state: Int, message: String) {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            tvHudTelemetry.text = message
        }
    }

    override fun onNativeChatMessage(rgbaColor: Int, message: String) {
        val formatted = if (chatTimestamp) "[${timeFmt.format(Date())}] $message" else message
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            chatAdapter.addMessage(rgbaColor, formatted)
            rvChat.scrollToPosition(maxOf(0, chatAdapter.itemCount - 1))
        }
    }

    override fun onNativeClassSelection(visible: Boolean, classId: Int, skinId: Int) {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            classSelectBar.visibility = if (visible) View.VISIBLE else View.GONE
            tvClassLabel.text = "Class #$classId (Skin $skinId)"
        }
    }

    override fun onNativeShowDialog(
        dialogId: Int,
        style: Int,
        title: String,
        info: String,
        button1: String,
        button2: String,
    ) {
        val model = NativeBridge.DialogModel(dialogId, style, title, info, button1, button2)
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            dialogPresenter.sync(model)
        }
    }

    override fun onNativeHideDialog() {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            dialogPresenter.dismiss()
        }
    }

    override fun onNativeScoreboardUpdated() {
        // Refreshed entries are read on demand when Scoreboard dialog is opened
    }

    override fun onNativeGameText(style: Int, timeMs: Int, text: String) {
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            uiHandler.removeCallbacks(hideGameTextRunnable)
            val clean = TextDrawOverlayView.stripTags(text.replace("~n~", "\n", ignoreCase = true))
            tvGameTextBanner.text = clean
            tvGameTextBanner.visibility = View.VISIBLE
            uiHandler.postDelayed(hideGameTextRunnable, timeMs.coerceIn(800, 10000).toLong())
        }
    }

    override fun onNativeAudioStream(active: Boolean, url: String) {
        runOnUiThread {
            audioPlayer.syncState(active, url)
        }
    }

    override fun onNativeTextDrawsChanged() {
        if (!nativeLibrariesLoaded) return
        val items = nativeClient.getTextDraws()
        runOnUiThread {
            if (isFinishing || isDestroyed) return@runOnUiThread
            textDrawOverlay.updateState(
                NativeBridge.TextDrawOverlayState(
                    items = items,
                    selecting = textDrawSelecting,
                    hoverRgba = textDrawHoverRgba,
                )
            )
        }
    }

    override fun onNativeTextDrawSelectChanged(enabled: Boolean, hoverColorRgba: Int) {
        textDrawSelecting = enabled
        textDrawHoverRgba = hoverColorRgba
        onNativeTextDrawsChanged()
    }

    override fun onDestroy() {
        pollingActive = false
        uiHandler.removeCallbacksAndMessages(null)
        dialogPresenter.dismiss()
        audioPlayer.stop()
        logoExecutor.shutdownNow()
        ivLoadingLogo.setImageDrawable(null)
        loadedLogoBitmap?.recycle()
        loadedLogoBitmap = null
        if (nativeLibrariesLoaded) {
            runCatching { nativeClient.nativeSetListener(null) }
            runCatching { nativeClient.nativeDisconnect() }
        }
        super.onDestroy()
    }

    companion object {
        private const val TAG = "NanaSAMP-GameActivity"
        const val EXTRA_HOST = "extra_host"
        const val EXTRA_PORT = "extra_port"
        const val EXTRA_NICKNAME = "extra_nickname"
        const val EXTRA_PASSWORD = "extra_password"
        const val EXTRA_STORAGE_ROOT = "extra_storage_root"
        const val EXTRA_MAIN_OBB = "extra_main_obb"
        const val EXTRA_PATCH_OBB = "extra_patch_obb"
        const val EXTRA_GPU_RENDERER = "extra_gpu_renderer"
        const val EXTRA_GPU_EXTENSIONS = "extra_gpu_extensions"
        const val EXTRA_TEX_FORMAT_CODE = "extra_tex_format_code"
        const val EXTRA_AML_ENABLED = "extra_aml_enabled"
        const val EXTRA_SHOW_FPS = "extra_show_fps"
        const val EXTRA_CHAT_TIMESTAMP = "extra_chat_timestamp"
    }
}
