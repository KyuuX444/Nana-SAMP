package com.nana.samp.core

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Environment
import androidx.core.content.ContextCompat
import java.io.File

/**
 * Inspects and prepares legal GTA San Andreas 2.00 game data paths, unpacked SA-MP / Alyn
 * data packs (including Mali .etc, Adreno .dxt, PowerVR .pvr, and .unc texture databases),
 * and Rockstar OBB archives.
 */
data class GameDataStatus(
    val ready: Boolean,
    val storageRoot: String,
    val mainObbPath: String,
    val patchObbPath: String,
    val detectedFormats: Set<TextureFormat>,
    val isUnpacked: Boolean,
    val summary: String,
    val missingDetails: String
)

object GameDataManager {

    private val KNOWN_TEXDB_NAMES = listOf("gta3", "gta_int", "txd", "mobile", "menu", "player")

    fun needsLegacyStoragePermission(context: Context): Boolean {
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.S_V2) {
            return false
        }
        val readGranted = ContextCompat.checkSelfPermission(
            context, Manifest.permission.READ_EXTERNAL_STORAGE
        ) == PackageManager.PERMISSION_GRANTED
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            val writeGranted = ContextCompat.checkSelfPermission(
                context, Manifest.permission.WRITE_EXTERNAL_STORAGE
            ) == PackageManager.PERMISSION_GRANTED
            return !readGranted || !writeGranted
        }
        return !readGranted
    }

    fun requiredLegacyPermissions(): Array<String> {
        return if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            arrayOf(
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            )
        } else {
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
        }
    }

    /**
     * Checks whether a directory directly contains unpacked GTA SA 2.00 game data
     * (like Data_Sem_Mods_Alyn_-_Mali.zip extracted to files/: texdb/, audio/, data/, models/, anim/).
     */
    internal fun isUnpackedGameRoot(dir: File): Boolean {
        if (!dir.isDirectory || !dir.canRead()) return false
        val texdb = findChildIgnoreCase(dir, "texdb")
        if (texdb == null || !texdb.isDirectory) return false
        val hasAux = findChildIgnoreCase(dir, "data")?.isDirectory == true ||
            findChildIgnoreCase(dir, "models")?.isDirectory == true ||
            findChildIgnoreCase(dir, "audio")?.isDirectory == true ||
            findChildIgnoreCase(dir, "anim")?.isDirectory == true ||
            findChildIgnoreCase(texdb, "gta3.img")?.isFile == true ||
            findChildIgnoreCase(texdb, "gta3")?.isDirectory == true
        return hasAux
    }

    /**
     * Resolves the true unpacked GTA SA data root even if the user extracted a zip archive
     * with an extra nesting folder (e.g., files/files/, files/Data_Sem_Mods_Alyn_-_Mali/,
     * files/com.rockstargames.gtasa/files/).
     */
    fun resolveUnpackedRoot(baseDir: File): File? {
        if (!baseDir.isDirectory || !baseDir.canRead()) return null
        if (isUnpackedGameRoot(baseDir)) return baseDir

        // Common 1-level and 2-level nested extraction roots
        val directChildren = baseDir.listFiles()?.filter { it.isDirectory && it.canRead() } ?: return null
        for (child in directChildren) {
            if (isUnpackedGameRoot(child)) return child
        }
        for (child in directChildren) {
            val grandChildren = child.listFiles()?.filter { it.isDirectory && it.canRead() } ?: continue
            for (grand in grandChildren) {
                if (isUnpackedGameRoot(grand)) return grand
            }
        }
        return null
    }

    /**
     * Scans texdb/ inside an unpacked root to detect which GTA SA texture compression formats
     * (.etc for Mali, .dxt for Adreno, .pvr for PowerVR, .unc uncompressed) exist on disk.
     */
    fun detectInstalledTextureFormats(rootDir: File): Set<TextureFormat> {
        val texdbDir = findChildIgnoreCase(rootDir, "texdb") ?: return emptySet()
        if (!texdbDir.isDirectory) return emptySet()

        val found = LinkedHashSet<TextureFormat>()
        val subdirs = mutableListOf<File>()
        for (name in KNOWN_TEXDB_NAMES) {
            findChildIgnoreCase(texdbDir, name)?.takeIf { it.isDirectory }?.let { subdirs.add(it) }
        }
        // Also scan any additional custom texdb subdirectories (e.g. samp, cutscene)
        texdbDir.listFiles()?.filter { it.isDirectory }?.forEach { d ->
            if (subdirs.none { it.absolutePath == d.absolutePath }) subdirs.add(d)
        }

        for (sub in subdirs) {
            val files = sub.listFiles() ?: continue
            for (f in files) {
                if (!f.isFile) continue
                val n = f.name.lowercase()
                if (n.endsWith(".etc.tmb") || n.endsWith(".etc.toc") || n.endsWith(".etc.dat")) {
                    found.add(TextureFormat.ETC)
                } else if (n.endsWith(".dxt.tmb") || n.endsWith(".dxt.toc") || n.endsWith(".dxt.dat")) {
                    found.add(TextureFormat.DXT)
                } else if (n.endsWith(".pvr.tmb") || n.endsWith(".pvr.toc") || n.endsWith(".pvr.dat")) {
                    found.add(TextureFormat.PVR)
                } else if (n.endsWith(".unc.tmb") || n.endsWith(".unc.toc") || n.endsWith(".unc.dat")) {
                    found.add(TextureFormat.UNC)
                }
            }
        }
        return found
    }

    /**
     * Pure testable directory inspector for unpacked GTA SA 2.00 data packs.
     */
    fun inspectDirectories(
        primaryExternalFilesDir: File,
        fallbackSearchDirs: List<File> = emptyList(),
        obbSearchDirs: List<File> = emptyList()
    ): GameDataStatus {
        val normalizedPrimary = primaryExternalFilesDir.absolutePath.trimEnd('/')

        // 1. Search for unpacked GTA SA data (primary app files/ first, then optional fallbacks)
        var unpackedRoot = resolveUnpackedRoot(primaryExternalFilesDir)
        if (unpackedRoot == null) {
            for (alt in fallbackSearchDirs) {
                unpackedRoot = resolveUnpackedRoot(alt)
                if (unpackedRoot != null) break
            }
        }

        // 2. Search for official Rockstar OBB archives
        var mainObb = ""
        var patchObb = ""
        for (dir in obbSearchDirs) {
            if (!dir.isDirectory || !dir.canRead()) continue
            val files = dir.listFiles() ?: continue
            for (f in files) {
                val name = f.name.lowercase()
                if (name.startsWith("main.") && name.endsWith(".obb") && mainObb.isEmpty()) {
                    mainObb = f.absolutePath
                } else if (name.startsWith("patch.") && name.endsWith(".obb") && patchObb.isEmpty()) {
                    patchObb = f.absolutePath
                }
            }
            if (mainObb.isNotEmpty()) break
        }

        val hasUnpacked = unpackedRoot != null
        val hasObb = mainObb.isNotEmpty()
        val effectiveRoot = (unpackedRoot?.absolutePath ?: normalizedPrimary).trimEnd('/')
        val detectedFormats = if (unpackedRoot != null) detectInstalledTextureFormats(unpackedRoot) else emptySet()

        return if (hasUnpacked || hasObb) {
            val fmtTag = if (detectedFormats.isNotEmpty()) {
                detectedFormats.joinToString("/") { it.label }
            } else if (hasObb) {
                "OBB Multi-GPU"
            } else {
                "Auto"
            }
            val mode = if (hasUnpacked) "Unpacked GTA SA 2.00 ($fmtTag)" else "GTA SA OBB ($fmtTag)"
            GameDataStatus(
                ready = true,
                storageRoot = effectiveRoot,
                mainObbPath = mainObb,
                patchObbPath = patchObb,
                detectedFormats = detectedFormats,
                isUnpacked = hasUnpacked,
                summary = "Game data ready: $mode\nPath: $effectiveRoot",
                missingDetails = ""
            )
        } else {
            GameDataStatus(
                ready = false,
                storageRoot = normalizedPrimary,
                mainObbPath = "",
                patchObbPath = "",
                detectedFormats = emptySet(),
                isUnpacked = false,
                summary = "GTA SA 2.00 game data not found",
                missingDetails = "Extract GTA SA 2.00 data (e.g. Data_Sem_Mods_Alyn_-_Mali.zip → texdb/, audio/, data/, models/) into:\n$normalizedPrimary"
            )
        }
    }

    fun inspect(context: Context): GameDataStatus {
        val externalFilesDir = context.getExternalFilesDir(null) ?: context.filesDir
        if (!externalFilesDir.exists()) {
            externalFilesDir.mkdirs()
        }

        val fallbackDirs = mutableListOf<File>()
        val obbDirs = mutableListOf<File>()
        context.obbDir?.let { obbDirs.add(it) }

        val sharedRoot = runCatching { Environment.getExternalStorageDirectory() }.getOrNull()
        if (sharedRoot != null) {
            fallbackDirs.add(File(sharedRoot, "Android/data/ro.alyn_sampmobile.game/files"))
            fallbackDirs.add(File(sharedRoot, "Android/data/com.rockstargames.gtasa/files"))
            obbDirs.add(File(sharedRoot, "Android/obb/com.rockstargames.gtasa"))
        }

        return inspectDirectories(externalFilesDir, fallbackDirs, obbDirs)
    }

    private fun findChildIgnoreCase(parent: File, targetName: String): File? {
        val direct = File(parent, targetName)
        if (direct.exists()) return direct
        val list = parent.listFiles() ?: return null
        return list.firstOrNull { it.name.equals(targetName, ignoreCase = true) }
    }
}
