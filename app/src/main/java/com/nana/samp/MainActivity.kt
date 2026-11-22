package com.nana.samp

import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.nana.samp.core.AlynClientSettings
import com.nana.samp.core.AlynSettingsManager
import com.nana.samp.core.CrashReporter
import com.nana.samp.core.GameDataManager
import com.nana.samp.core.GameDataStatus
import com.nana.samp.core.GpuDetector
import com.nana.samp.core.GpuInfo
import com.nana.samp.core.NativeBridge
import com.nana.samp.core.NativeClient
import com.nana.samp.core.SavedServer
import com.nana.samp.core.ServerStore
import com.nana.samp.ui.ServerListAdapter
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private lateinit var store: ServerStore
    private lateinit var nativeClient: NativeClient
    private lateinit var adapter: ServerListAdapter

    private val servers = mutableListOf<SavedServer>()
    private val bgExecutor = Executors.newCachedThreadPool()

    private lateinit var cardCrashBanner: LinearLayout
    private lateinit var tvCrashSummary: TextView
    private lateinit var tvDataStatus: TextView
    private lateinit var tvGpuAndAmlInfo: TextView
    private lateinit var etNickname: EditText
    private lateinit var etDirectAddress: EditText
    private lateinit var tvEmptyServers: TextView
    private lateinit var rvServers: RecyclerView

    private var cachedGpu: GpuInfo? = null
    private var cachedDataStatus: GameDataStatus? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CrashReporter.install(this)
        setContentView(R.layout.activity_main)

        store = ServerStore(this)
        nativeClient = NativeClient()
        NativeClient.ensureSampCoreLoaded()

        cardCrashBanner = findViewById(R.id.cardCrashBanner)
        tvCrashSummary = findViewById(R.id.tvCrashSummary)
        tvDataStatus = findViewById(R.id.tvDataStatus)
        tvGpuAndAmlInfo = findViewById(R.id.tvGpuAndAmlInfo)
        etNickname = findViewById(R.id.etNickname)
        etDirectAddress = findViewById(R.id.etDirectAddress)
        tvEmptyServers = findViewById(R.id.tvEmptyServers)
        rvServers = findViewById(R.id.rvServers)

        etNickname.setText(store.nickname)

        servers.clear()
        servers.addAll(store.loadServers())

        adapter = ServerListAdapter(
            servers = servers,
            onConnect = { srv -> launchGame(srv.host, srv.port, srv.password) },
            onDelete = { srv ->
                val idx = servers.indexOfFirst { it.id == srv.id }
                if (idx >= 0) {
                    servers.removeAt(idx)
                    store.saveServers(servers)
                    adapter.notifyItemRemoved(idx)
                    updateEmptyState()
                }
            },
        )
        rvServers.layoutManager = LinearLayoutManager(this)
        rvServers.adapter = adapter
        updateEmptyState()

        findViewById<Button>(R.id.btnClearCrash).setOnClickListener {
            CrashReporter.clearCrashLogs(this)
            cardCrashBanner.visibility = View.GONE
        }

        findViewById<Button>(R.id.btnDirectConnect).setOnClickListener {
            val addr = ServerStore.parseAddress(etDirectAddress.text.toString())
            if (addr == null) {
                Toast.makeText(this, "Enter valid IP:PORT (e.g. 127.0.0.1:7777)", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            launchGame(addr.first, addr.second, "")
        }

        findViewById<Button>(R.id.btnAddServer).setOnClickListener { showAddServerDialog() }
        findViewById<Button>(R.id.btnRefreshServers).setOnClickListener { queryAllServers() }
        findViewById<Button>(R.id.btnSettings).setOnClickListener { showSettingsDialog() }

        if (GameDataManager.needsLegacyStoragePermission(this)) {
            ActivityCompat.requestPermissions(this, GameDataManager.requiredLegacyPermissions(), REQ_STORAGE)
        }

        refreshEnvironmentStatus()
        queryAllServers()
    }

    override fun onResume() {
        super.onResume()
        val crash = CrashReporter.readLastCrashSummary(this)
        if (!crash.isNullOrBlank()) {
            tvCrashSummary.text = crash
            cardCrashBanner.visibility = View.VISIBLE
        } else {
            cardCrashBanner.visibility = View.GONE
        }
        refreshEnvironmentStatus()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_STORAGE && grantResults.any { it == PackageManager.PERMISSION_GRANTED }) {
            refreshEnvironmentStatus()
        }
    }

    private fun refreshEnvironmentStatus() {
        bgExecutor.execute {
            val gpu = GpuDetector.detect()
            val status = GameDataManager.inspect(this)
            cachedGpu = gpu
            cachedDataStatus = status

            val storageDir = File(status.storageRoot)
            val alynCfg = AlynSettingsManager.readFromStorage(storageDir)
            val modsDir = File(storageDir.parentFile ?: storageDir, "mods")
            val modCount = modsDir.listFiles { f -> f.isFile && f.name.endsWith(".so", ignoreCase = true) }?.size ?: 0
            val amlState = if (store.amlEnabled) "AML 1.4.0 ON ($modCount mods)" else "AML 1.4.0 OFF"
            val effectiveFmt = GpuDetector.resolveEffectiveFormat(
                gpu,
                status.detectedFormats,
                status.mainObbPath.isNotEmpty(),
            )

            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                tvDataStatus.text = if (status.ready) {
                    status.summary
                } else {
                    "${status.summary}\n${status.missingDetails}"
                }
                tvGpuAndAmlInfo.text = "GPU: ${gpu.shortLabel()} • Format: ${effectiveFmt.label} • $amlState"

                // Pre-fill Direct Connect / Nickname from Alyn SAMP/settings.json if empty
                if (alynCfg != null) {
                    if (etDirectAddress.text.isNullOrBlank() && alynCfg.host.isNotBlank()) {
                        etDirectAddress.setText("${alynCfg.host}:${alynCfg.port}")
                    }
                    if (store.nickname == "NanaPlayer" && alynCfg.nickname.isNotBlank()) {
                        store.nickname = alynCfg.nickname
                        etNickname.setText(alynCfg.nickname)
                    }
                }
            }
        }
    }

    private fun showAddServerDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_server_input, null)
        val etLabel = view.findViewById<EditText>(R.id.etDialogLabel)
        val etAddr = view.findViewById<EditText>(R.id.etDialogAddress)
        val etPass = view.findViewById<EditText>(R.id.etDialogPassword)

        AlertDialog.Builder(this)
            .setTitle("Add SA-MP Server")
            .setView(view)
            .setPositiveButton("Add") { _, _ ->
                val parsed = ServerStore.parseAddress(etAddr.text.toString())
                if (parsed == null) {
                    Toast.makeText(this, "Invalid IP:Port address", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                val (host, port) = parsed
                val label = etLabel.text.toString().trim().ifEmpty { "$host:$port" }
                val srv = SavedServer(
                    id = UUID.randomUUID().toString(),
                    label = label,
                    host = host,
                    port = port,
                    password = etPass.text.toString(),
                    protocolVersion = NativeBridge.PROTOCOL_037,
                )
                servers.add(0, srv)
                store.saveServers(servers)
                adapter.notifyItemInserted(0)
                updateEmptyState()
                querySingleServer(srv)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showSettingsDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_settings, null)
        val cbAml = view.findViewById<CheckBox>(R.id.cbEnableAml)
        val cbFps = view.findViewById<CheckBox>(R.id.cbDisplayFps)
        val cbTs = view.findViewById<CheckBox>(R.id.cbChatTimestamp)
        val etCustom = view.findViewById<EditText>(R.id.etCustomDataPath)

        cbAml.isChecked = store.amlEnabled
        cbFps.isChecked = store.displayFps
        cbTs.isChecked = store.chatTimestamp
        etCustom.setText(store.customGameDataPath)

        AlertDialog.Builder(this)
            .setTitle("Nana SAMP Settings")
            .setView(view)
            .setPositiveButton("Save") { _, _ ->
                store.amlEnabled = cbAml.isChecked
                store.displayFps = cbFps.isChecked
                store.chatTimestamp = cbTs.isChecked
                store.customGameDataPath = etCustom.text.toString().trim()
                syncAlynSettingsFile("", 7777, "")
                refreshEnvironmentStatus()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun queryAllServers() {
        val snapshot = servers.toList()
        for (srv in snapshot) {
            querySingleServer(srv)
        }
    }

    private fun querySingleServer(srv: SavedServer) {
        if (!NativeClient.ensureSampCoreLoaded()) return
        srv.online = null
        val pos = servers.indexOfFirst { it.id == srv.id }
        if (pos >= 0) adapter.notifyItemChanged(pos)

        bgExecutor.execute {
            val res = nativeClient.queryServer(srv.host, srv.port, 1800)
            runOnUiThread {
                if (isFinishing || isDestroyed) return@runOnUiThread
                val idx = servers.indexOfFirst { it.id == srv.id }
                if (idx < 0) return@runOnUiThread
                val target = servers[idx]
                target.online = res.reachable
                if (res.reachable) {
                    target.hostname = res.hostname
                    target.gamemode = res.gamemode
                    target.players = res.players
                    target.maxPlayers = res.maxPlayers
                    target.pingMs = res.pingMs
                    target.hasPassword = res.passworded
                }
                adapter.notifyItemChanged(idx)
            }
        }
    }

    private fun launchGame(host: String, port: Int, password: String) {
        val nick = ServerStore.sanitizeNickname(etNickname.text.toString())
        store.nickname = nick
        etNickname.setText(nick)
        syncAlynSettingsFile(host, port, password)

        val status = cachedDataStatus ?: GameDataManager.inspect(this)
        if (!status.ready) {
            AlertDialog.Builder(this)
                .setTitle("GTA SA 2.00 Game Data Required")
                .setMessage(
                    "${status.missingDetails}\n\n" +
                        "Continue launching anyway? (GTA SA 2.00 requires texdb/data/audio assets to render the map.)"
                )
                .setPositiveButton("Launch Anyway") { _, _ -> startGameActivity(host, port, nick, password) }
                .setNegativeButton("Cancel", null)
                .show()
            return
        }
        startGameActivity(host, port, nick, password)
    }

    private fun startGameActivity(host: String, port: Int, nickname: String, password: String) {
        val gpu = cachedGpu ?: GpuDetector.classify("", "")
        val status = cachedDataStatus ?: GameDataManager.inspect(this)
        val effFmt = GpuDetector.resolveEffectiveFormat(
            gpu,
            status.detectedFormats,
            status.mainObbPath.isNotEmpty(),
        )
        val intent = Intent(this, GameActivity::class.java).apply {
            putExtra(GameActivity.EXTRA_HOST, host)
            putExtra(GameActivity.EXTRA_PORT, port)
            putExtra(GameActivity.EXTRA_NICKNAME, nickname)
            putExtra(GameActivity.EXTRA_PASSWORD, password)
            putExtra(GameActivity.EXTRA_STORAGE_ROOT, status.storageRoot)
            putExtra(GameActivity.EXTRA_MAIN_OBB, status.mainObbPath)
            putExtra(GameActivity.EXTRA_PATCH_OBB, status.patchObbPath)
            putExtra(GameActivity.EXTRA_GPU_RENDERER, gpu.renderer)
            putExtra(GameActivity.EXTRA_GPU_EXTENSIONS, gpu.extensions)
            putExtra(GameActivity.EXTRA_TEX_FORMAT_CODE, effFmt.nativeCode)
            putExtra(GameActivity.EXTRA_AML_ENABLED, store.amlEnabled)
            putExtra(GameActivity.EXTRA_SHOW_FPS, store.displayFps)
            putExtra(GameActivity.EXTRA_CHAT_TIMESTAMP, store.chatTimestamp)
        }
        startActivity(intent)
    }

    private fun syncAlynSettingsFile(host: String, port: Int, password: String) {
        bgExecutor.execute {
            val dir = store.resolveGameDataDir()
            val cfg = AlynClientSettings(
                host = host,
                port = port,
                password = password,
                nickname = store.nickname,
                amlEnabled = store.amlEnabled,
                displayFps = store.displayFps,
                timestamp = store.chatTimestamp,
            )
            AlynSettingsManager.writeToStorage(dir, cfg)
        }
    }

    private fun updateEmptyState() {
        tvEmptyServers.visibility = if (servers.isEmpty()) View.VISIBLE else View.GONE
    }

    override fun onDestroy() {
        bgExecutor.shutdownNow()
        super.onDestroy()
    }

    companion object {
        private const val REQ_STORAGE = 1001
    }
}
