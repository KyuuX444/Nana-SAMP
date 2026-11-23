package com.nana.samp.core

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Pure pixel buffer returned by from-scratch GTA SA Mobile TexDB & RenderWare TXD readers
 * so decoding logic can be unit-tested on the JVM without Android Bitmap dependencies.
 */
data class DecodedRgbaImage(
    val width: Int,
    val height: Int,
    val argbPixels: IntArray,
    val sourceLabel: String
)

data class SampLogoResult(
    val bitmap: Bitmap?,
    val sourceLabel: String,
    val isCustom: Boolean
)

/**
 * Loads `mylogo.png` (or packed `mylogo` texture) from the user's GTA SA / SA-MP data directory
 * for the SA-MP PC-style loading screen.
 *
 * Resolution priority:
 *  1. Loose image file (`mylogo.png`, `mylogo.jpg`, `mylogo.webp`) inside `texdb/txd/`,
 *     `texdb/txd/src/`, `txd/`, `SAMP/`, `texdb/samp/`, `texdb/`, `<storageRoot>/`, or parent dir.
 *  2. Packed GTA SA Mobile War Drum TextureDatabase archive (`texdb/txd/`, `texdb/samp/`,
 *     `texdb/mobile/`, `texdb/menu/`) via `<db>.txt` + `<db>.{unc,etc,dxt}.{toc,dat}`
 *     supporting RGBA8888, RGB888, RGB565, RGBA4444, RGBA5551, ETC1 (0x8D64), DXT1, and DXT5.
 *  3. Classic RenderWare `.txd` archive (`texdb/txd.txd`, `models/txd.txd`, `txd/txd.txd`, `SAMP/samp.txd`).
 */
object SampLogoLoader {

    private val LOGO_BASE_NAMES = listOf("mylogo")
    private val LOOSE_FILE_NAMES = listOf("mylogo.png", "mylogo.webp", "mylogo.jpg", "mylogo.jpeg")
    private val TEXDB_CANDIDATES = listOf("txd", "samp", "mobile", "menu")
    private val FORMAT_ORDER = listOf("unc", "etc", "dxt")

    private val ETC1_MODIFIER_TABLES = arrayOf(
        intArrayOf(2, 8, -2, -8),
        intArrayOf(5, 17, -5, -17),
        intArrayOf(9, 29, -9, -29),
        intArrayOf(13, 42, -13, -42),
        intArrayOf(18, 60, -18, -60),
        intArrayOf(24, 80, -24, -80),
        intArrayOf(33, 106, -33, -106),
        intArrayOf(47, 183, -47, -183)
    )

    /**
     * Loads `mylogo.png` / `mylogo` into an Android Bitmap. Safe to call on a background thread.
     */
    fun loadLogo(storageRoot: String): SampLogoResult {
        if (storageRoot.isBlank()) {
            return SampLogoResult(null, "default", false)
        }
        val rootDir = File(storageRoot)

        // 1. Loose mylogo.png in txd / texdb/txd / SAMP / etc.
        val loose = findLooseLogoFile(rootDir)
        if (loose != null) {
            val bmp = runCatching { decodeSampledBitmap(loose, 1024, 512) }.getOrNull()
            if (bmp != null) {
                val rel = runCatching { loose.relativeTo(rootDir).path }.getOrElse { loose.name }
                return SampLogoResult(bmp, rel, true)
            }
        }

        // 2. Packed War Drum texdb archive (texdb/txd/txd.txt + .toc + .dat)
        val texDbImg = readLogoFromTexDb(rootDir)
        if (texDbImg != null) {
            val bmp = runCatching {
                Bitmap.createBitmap(
                    texDbImg.argbPixels,
                    texDbImg.width,
                    texDbImg.height,
                    Bitmap.Config.ARGB_8888
                )
            }.getOrNull()
            if (bmp != null) {
                return SampLogoResult(bmp, texDbImg.sourceLabel, true)
            }
        }

        // 3. Classic RenderWare .txd archive
        val rwImg = readLogoFromRenderWareTxd(rootDir)
        if (rwImg != null) {
            val bmp = runCatching {
                Bitmap.createBitmap(
                    rwImg.argbPixels,
                    rwImg.width,
                    rwImg.height,
                    Bitmap.Config.ARGB_8888
                )
            }.getOrNull()
            if (bmp != null) {
                return SampLogoResult(bmp, rwImg.sourceLabel, true)
            }
        }

        return SampLogoResult(null, "default (mylogo.png not found in txd)", false)
    }

    /**
     * Searches standard GTA SA / SA-MP Mobile paths (case-insensitively) for a loose `mylogo.png`.
     */
    fun findLooseLogoFile(rootDir: File): File? {
        if (!rootDir.isDirectory || !rootDir.canRead()) return null

        val priorityDirs = mutableListOf<File>()
        resolvePathIgnoreCase(rootDir, "texdb", "txd")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "texdb", "txd", "src")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "txd")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "SAMP")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "texdb", "samp")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "texdb", "samp", "src")?.let { priorityDirs.add(it) }
        resolvePathIgnoreCase(rootDir, "texdb")?.let { priorityDirs.add(it) }
        priorityDirs.add(rootDir)

        rootDir.parentFile?.takeIf { it.isDirectory && it.canRead() }?.let { parent ->
            resolvePathIgnoreCase(parent, "txd")?.let { priorityDirs.add(it) }
            resolvePathIgnoreCase(parent, "SAMP")?.let { priorityDirs.add(it) }
        }

        for (dir in priorityDirs) {
            for (fname in LOOSE_FILE_NAMES) {
                findChildFileIgnoreCase(dir, fname)?.let { return it }
            }
        }

        // Fallback bounded recursive search inside txd/, texdb/, SAMP/ (max depth 3)
        val scanRoots = listOfNotNull(
            resolvePathIgnoreCase(rootDir, "txd"),
            resolvePathIgnoreCase(rootDir, "texdb"),
            resolvePathIgnoreCase(rootDir, "SAMP")
        )
        for (sr in scanRoots) {
            findRecursiveIgnoreCase(sr, LOOSE_FILE_NAMES, maxDepth = 3)?.let { return it }
        }
        return null
    }

    /**
     * Parses GTA SA Mobile `<db>.txt` to find the 0-based texture entry index for `mylogo`.
     */
    fun findTexDbEntryIndex(txtFile: File, targetNames: List<String> = LOGO_BASE_NAMES): Int {
        if (!txtFile.isFile || !txtFile.canRead()) return -1
        val normalizedTargets = targetNames.map { it.lowercase() }.toSet()
        var entryIndex = 0
        txtFile.useLines { lines ->
            for (rawLine in lines) {
                val line = rawLine.trim()
                if (line.isEmpty() || line.startsWith("#") || line.startsWith("//")) continue
                // GTA SA Mobile texdb/*.txt texture lines start with `"<texname>" ...`
                if (!line.startsWith("\"")) continue
                val endQuote = line.indexOf('"', startIndex = 1)
                if (endQuote <= 1) continue
                val rawName = line.substring(1, endQuote).trim().lowercase()
                val stripped = rawName.removeSuffix(".png").removeSuffix(".tga").removeSuffix(".bmp")
                if (rawName in normalizedTargets || stripped in normalizedTargets) {
                    return entryIndex
                }
                entryIndex++
            }
        }
        return -1
    }

    /**
     * Reads `mylogo` directly from GTA SA Mobile `texdb/<db>/<db>.{txt,toc,dat}` archives.
     */
    fun readLogoFromTexDb(rootDir: File): DecodedRgbaImage? {
        val texdbDir = resolvePathIgnoreCase(rootDir, "texdb") ?: return null
        val subDirs = mutableListOf<Pair<String, File>>()
        for (db in TEXDB_CANDIDATES) {
            findChildDirIgnoreCase(texdbDir, db)?.let { subDirs.add(db to it) }
        }
        // Also scan any extra custom texdb folders
        texdbDir.listFiles()?.filter { it.isDirectory }?.forEach { d ->
            if (subDirs.none { it.second.absolutePath == d.absolutePath }) {
                subDirs.add(d.name to d)
            }
        }

        for ((dbName, subDir) in subDirs) {
            val txtFile = findChildFileIgnoreCase(subDir, "$dbName.txt")
                ?: subDir.listFiles()?.firstOrNull { it.isFile && it.name.endsWith(".txt", ignoreCase = true) }
                ?: continue
            val entryIdx = findTexDbEntryIndex(txtFile)
            if (entryIdx < 0) continue

            val baseName = txtFile.nameWithoutExtension
            for (fmt in FORMAT_ORDER) {
                val tocFile = findChildFileIgnoreCase(subDir, "$baseName.$fmt.toc") ?: continue
                val datFile = findChildFileIgnoreCase(subDir, "$baseName.$fmt.dat") ?: continue
                val decoded = readTexDbEntry(tocFile, datFile, entryIdx, "texdb/$dbName ($fmt)")
                if (decoded != null) return decoded
            }
        }
        return null
    }

    /**
     * Reads a single War Drum TextureListingContainer entry from `.toc` + `.dat`.
     * Verified layout against libGTASA.so 2.00 ARM32 (`TextureDatabase::LoadDataFile` & `LoadDataOffsets`):
     *   .toc: [uint32 datFileSize][uint32 offset_0][uint32 offset_1]...
     *   .dat @ offset:
     *     +0x00 uint16 flags
     *     +0x02 uint16 glFormat (0x8D64=ETC1, 0x83F0/1=DXT1, 0x83F3=DXT5, 0x1908=RGBA, etc.)
     *     +0x04 uint16 width
     *     +0x06 uint16 height (bits 0..14; bit 15 = mip flag)
     *     +0x08 uint32 payloadSizeWithCompFlag
     *     +0x0C uint32 compFlag (0 = raw pixel/block data immediately at +0x10)
     *     +0x10 byte[] mip0 + subsequent mips
     */
    fun readTexDbEntry(tocFile: File, datFile: File, entryIndex: Int, sourceLabel: String): DecodedRgbaImage? {
        if (!tocFile.isFile || !datFile.isFile || entryIndex < 0) return null
        val tocPos = 4L + entryIndex.toLong() * 4L
        if (tocPos + 4L > tocFile.length()) return null

        val dataOffset = RandomAccessFile(tocFile, "r").use { raf ->
            val hdr = ByteArray(4)
            raf.readFully(hdr)
            val expectedDatSize = readLeUInt32(hdr, 0)
            // Tolerate small mismatches if modded, but require offset inside datFile
            raf.seek(tocPos)
            val offBuf = ByteArray(4)
            raf.readFully(offBuf)
            val off = readLeUInt32(offBuf, 0)
            if (off == 0xFFFFFFFFL || (expectedDatSize > 0 && off >= datFile.length())) -1L else off
        }
        if (dataOffset < 0 || dataOffset + 16L > datFile.length()) return null

        return RandomAccessFile(datFile, "r").use { raf ->
            raf.seek(dataOffset)
            val header = ByteArray(16)
            raf.readFully(header)

            val glFormat = readLeUInt16(header, 2)
            val width = readLeUInt16(header, 4)
            val height = readLeUInt16(header, 6) and 0x7FFF
            val totalSize = readLeUInt32(header, 8).toInt()
            val compFlag = readLeUInt32(header, 12).toInt()

            if (width !in 4..2048 || height !in 4..2048 || compFlag != 0) {
                return@use null
            }

            val maxReadable = (datFile.length() - (dataOffset + 16L)).coerceAtMost(16L * 1024L * 1024L).toInt()
            val payloadSize = if (totalSize > 4) (totalSize - 4).coerceAtMost(maxReadable) else maxReadable
            if (payloadSize <= 0) return@use null

            val payload = ByteArray(payloadSize)
            raf.readFully(payload)

            val argb = decodeGlTexturePayload(glFormat, width, height, payload) ?: return@use null
            DecodedRgbaImage(width, height, argb, sourceLabel)
        }
    }

    /**
     * Reads `mylogo` from a classic RenderWare `.txd` archive if present.
     */
    fun readLogoFromRenderWareTxd(rootDir: File): DecodedRgbaImage? {
        val candidates = listOfNotNull(
            resolvePathIgnoreCase(rootDir, "texdb", "txd.txd"),
            resolvePathIgnoreCase(rootDir, "models", "txd.txd"),
            resolvePathIgnoreCase(rootDir, "txd", "txd.txd"),
            resolvePathIgnoreCase(rootDir, "SAMP", "samp.txd"),
            resolvePathIgnoreCase(rootDir, "texdb", "samp.txd")
        ).filter { it.isFile && it.canRead() && it.length() in 128L..(64L * 1024L * 1024L) }

        for (txdFile in candidates) {
            val bytes = runCatching { txdFile.readBytes() }.getOrNull() ?: continue
            val decoded = parseRenderWareTxdBytes(bytes, txdFile.name)
            if (decoded != null) return decoded
        }
        return null
    }

    internal fun parseRenderWareTxdBytes(bytes: ByteArray, sourceName: String): DecodedRgbaImage? {
        if (bytes.size < 24) return null
        val rootType = readLeUInt32(bytes, 0).toInt()
        if (rootType != 0x16) return null // RW_TEXDICTIONARY

        var pos = 12
        val end = bytes.size
        while (pos + 12 <= end) {
            val chunkType = readLeUInt32(bytes, pos).toInt()
            val chunkSize = readLeUInt32(bytes, pos + 4).toInt()
            val chunkStart = pos + 12
            val chunkEnd = chunkStart + chunkSize
            if (chunkSize < 0 || chunkEnd > end) break

            if (chunkType == 0x15 && chunkStart + 12 <= chunkEnd) { // RW_TEXTURENATIVE
                val innerType = readLeUInt32(bytes, chunkStart).toInt()
                val innerSize = readLeUInt32(bytes, chunkStart + 4).toInt()
                val sOff = chunkStart + 12
                if (innerType == 0x01 && innerSize >= 92 && sOff + 92 <= chunkEnd) {
                    val nameBytes = bytes.copyOfRange(sOff + 8, sOff + 40)
                    val zeroIdx = nameBytes.indexOf(0)
                    val texName = String(nameBytes, 0, if (zeroIdx >= 0) zeroIdx else nameBytes.size)
                        .trim().lowercase().removeSuffix(".png")
                    if (texName in LOGO_BASE_NAMES) {
                        val rasterFormat = readLeUInt32(bytes, sOff + 72).toInt()
                        val d3dFourCc = readLeUInt32(bytes, sOff + 76).toInt()
                        val width = readLeUInt16(bytes, sOff + 80)
                        val height = readLeUInt16(bytes, sOff + 82)
                        val bpp = bytes[sOff + 84].toInt() and 0xFF
                        val mipSizeOff = sOff + 88
                        if (width in 4..2048 && height in 4..2048 && mipSizeOff + 4 <= chunkEnd) {
                            val mipSize = readLeUInt32(bytes, mipSizeOff).toInt()
                            val dataStart = mipSizeOff + 4
                            if (mipSize > 0 && dataStart + mipSize <= chunkEnd) {
                                val data = bytes.copyOfRange(dataStart, dataStart + mipSize)
                                val argb = when {
                                    d3dFourCc == 0x31545844 -> decodeDxt1(width, height, data) // 'DXT1'
                                    d3dFourCc == 0x35545844 -> decodeDxt5(width, height, data) // 'DXT5'
                                    bpp == 32 && data.size >= width * height * 4 -> {
                                        val isBgra = (rasterFormat and 0x0F00) == 0x0500 || (rasterFormat and 0x0F00) == 0x0600
                                        decodeRaw32(width, height, data, bgra = isBgra)
                                    }
                                    else -> null
                                }
                                if (argb != null) {
                                    return DecodedRgbaImage(width, height, argb, "$sourceName ($texName)")
                                }
                            }
                        }
                    }
                }
            }
            pos = chunkEnd
        }
        return null
    }

    internal fun decodeGlTexturePayload(glFormat: Int, width: Int, height: Int, payload: ByteArray): IntArray? {
        return when (glFormat) {
            0x8D64 -> decodeEtc1(width, height, payload)                 // GL_ETC1_RGB8_OES
            0x83F0, 0x83F1 -> decodeDxt1(width, height, payload)         // GL_COMPRESSED_RGB/RGBA_S3TC_DXT1_EXT
            0x83F3 -> decodeDxt5(width, height, payload)                 // GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
            0x1908, 0x1401, 0x0000 -> {                                  // GL_RGBA8888 (or default uncompressed)
                if (payload.size >= width * height * 4) decodeRaw32(width, height, payload, bgra = false)
                else if (payload.size >= width * height * 3) decodeRgb24(width, height, payload)
                else if (payload.size >= width * height * 2) decodeRgb565(width, height, payload)
                else null
            }
            0x1907 -> {                                                  // GL_RGB888
                if (payload.size >= width * height * 3) decodeRgb24(width, height, payload)
                else if (payload.size >= width * height * 2) decodeRgb565(width, height, payload)
                else null
            }
            0x8363 -> if (payload.size >= width * height * 2) decodeRgb565(width, height, payload) else null
            0x8033 -> if (payload.size >= width * height * 2) decodeRgba4444(width, height, payload) else null
            0x8034 -> if (payload.size >= width * height * 2) decodeRgba5551(width, height, payload) else null
            else -> {
                // Best-effort heuristic by byte size
                when {
                    payload.size >= width * height * 4 -> decodeRaw32(width, height, payload, bgra = false)
                    payload.size >= width * height * 2 -> decodeRgb565(width, height, payload)
                    else -> null
                }
            }
        }
    }

    internal fun decodeRaw32(width: Int, height: Int, data: ByteArray, bgra: Boolean): IntArray {
        val out = IntArray(width * height)
        var s = 0
        for (i in out.indices) {
            val c0 = data[s].toInt() and 0xFF
            val c1 = data[s + 1].toInt() and 0xFF
            val c2 = data[s + 2].toInt() and 0xFF
            val a = data[s + 3].toInt() and 0xFF
            s += 4
            out[i] = if (bgra) {
                (a shl 24) or (c2 shl 16) or (c1 shl 8) or c0
            } else {
                (a shl 24) or (c0 shl 16) or (c1 shl 8) or c2
            }
        }
        return out
    }

    internal fun decodeRgb24(width: Int, height: Int, data: ByteArray): IntArray {
        val out = IntArray(width * height)
        var s = 0
        for (i in out.indices) {
            val r = data[s].toInt() and 0xFF
            val g = data[s + 1].toInt() and 0xFF
            val b = data[s + 2].toInt() and 0xFF
            s += 3
            out[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
        }
        return out
    }

    internal fun decodeRgb565(width: Int, height: Int, data: ByteArray): IntArray {
        val out = IntArray(width * height)
        var s = 0
        for (i in out.indices) {
            val v = (data[s].toInt() and 0xFF) or ((data[s + 1].toInt() and 0xFF) shl 8)
            s += 2
            val r = ((v ushr 11) and 0x1F) * 255 / 31
            val g = ((v ushr 5) and 0x3F) * 255 / 63
            val b = (v and 0x1F) * 255 / 31
            out[i] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
        }
        return out
    }

    internal fun decodeRgba4444(width: Int, height: Int, data: ByteArray): IntArray {
        val out = IntArray(width * height)
        var s = 0
        for (i in out.indices) {
            val v = (data[s].toInt() and 0xFF) or ((data[s + 1].toInt() and 0xFF) shl 8)
            s += 2
            val r = ((v ushr 12) and 0xF) * 17
            val g = ((v ushr 8) and 0xF) * 17
            val b = ((v ushr 4) and 0xF) * 17
            val a = (v and 0xF) * 17
            out[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        return out
    }

    internal fun decodeRgba5551(width: Int, height: Int, data: ByteArray): IntArray {
        val out = IntArray(width * height)
        var s = 0
        for (i in out.indices) {
            val v = (data[s].toInt() and 0xFF) or ((data[s + 1].toInt() and 0xFF) shl 8)
            s += 2
            val r = ((v ushr 11) and 0x1F) * 255 / 31
            val g = ((v ushr 6) and 0x1F) * 255 / 31
            val b = ((v ushr 1) and 0x1F) * 255 / 31
            val a = if ((v and 1) != 0) 255 else 0
            out[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        return out
    }

    /**
     * Standard Khronos ETC1 RGB8 4x4 block decoder (used by Mali GTA SA `.etc.dat` archives).
     */
    internal fun decodeEtc1(width: Int, height: Int, data: ByteArray): IntArray? {
        val bxCount = (width + 3) / 4
        val byCount = (height + 3) / 4
        if (data.size < bxCount * byCount * 8) return null
        val out = IntArray(width * height)

        var off = 0
        for (by in 0 until byCount) {
            for (bx in 0 until bxCount) {
                val high = ((data[off].toLong() and 0xFF) shl 24) or
                    ((data[off + 1].toLong() and 0xFF) shl 16) or
                    ((data[off + 2].toLong() and 0xFF) shl 8) or
                    (data[off + 3].toLong() and 0xFF)
                val low = ((data[off + 4].toLong() and 0xFF) shl 24) or
                    ((data[off + 5].toLong() and 0xFF) shl 16) or
                    ((data[off + 6].toLong() and 0xFF) shl 8) or
                    (data[off + 7].toLong() and 0xFF)
                off += 8

                val diff = ((high ushr 1) and 1L) != 0L
                val flip = (high and 1L) != 0L
                val table0 = ETC1_MODIFIER_TABLES[((high ushr 5) and 7L).toInt()]
                val table1 = ETC1_MODIFIER_TABLES[((high ushr 2) and 7L).toInt()]

                val r0: Int
                val g0: Int
                val b0: Int
                val r1: Int
                val g1: Int
                val b1: Int
                if (diff) {
                    val r5 = ((high ushr 27) and 0x1FL).toInt()
                    val g5 = ((high ushr 19) and 0x1FL).toInt()
                    val b5 = ((high ushr 11) and 0x1FL).toInt()
                    val dr = signExtend3(((high ushr 24) and 0x7L).toInt())
                    val dg = signExtend3(((high ushr 16) and 0x7L).toInt())
                    val db = signExtend3(((high ushr 8) and 0x7L).toInt())
                    r0 = (r5 shl 3) or (r5 ushr 2)
                    g0 = (g5 shl 3) or (g5 ushr 2)
                    b0 = (b5 shl 3) or (b5 ushr 2)
                    val r5b = (r5 + dr).coerceIn(0, 31)
                    val g5b = (g5 + dg).coerceIn(0, 31)
                    val b5b = (b5 + db).coerceIn(0, 31)
                    r1 = (r5b shl 3) or (r5b ushr 2)
                    g1 = (g5b shl 3) or (g5b ushr 2)
                    b1 = (b5b shl 3) or (b5b ushr 2)
                } else {
                    val r4a = ((high ushr 28) and 0xFL).toInt()
                    val r4b = ((high ushr 24) and 0xFL).toInt()
                    val g4a = ((high ushr 20) and 0xFL).toInt()
                    val g4b = ((high ushr 16) and 0xFL).toInt()
                    val b4a = ((high ushr 12) and 0xFL).toInt()
                    val b4b = ((high ushr 8) and 0xFL).toInt()
                    r0 = r4a * 17
                    g0 = g4a * 17
                    b0 = b4a * 17
                    r1 = r4b * 17
                    g1 = g4b * 17
                    b1 = b4b * 17
                }

                for (px in 0 until 4) {
                    val x = bx * 4 + px
                    if (x >= width) continue
                    for (py in 0 until 4) {
                        val y = by * 4 + py
                        if (y >= height) continue
                        val bitIdx = px * 4 + py
                        val lsb = ((low ushr bitIdx) and 1L).toInt()
                        val msb = ((low ushr (bitIdx + 16)) and 1L).toInt()
                        val modIdx = (msb shl 1) or lsb
                        val secondHalf = if (flip) py >= 2 else px >= 2
                        val mod = if (secondHalf) table1[modIdx] else table0[modIdx]
                        val r = ((if (secondHalf) r1 else r0) + mod).coerceIn(0, 255)
                        val g = ((if (secondHalf) g1 else g0) + mod).coerceIn(0, 255)
                        val b = ((if (secondHalf) b1 else b0) + mod).coerceIn(0, 255)
                        out[y * width + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
                    }
                }
            }
        }
        return out
    }

    /**
     * Standard S3TC DXT1 4x4 block decoder (used by Adreno GTA SA `.dxt.dat` archives).
     */
    internal fun decodeDxt1(width: Int, height: Int, data: ByteArray): IntArray? {
        val bxCount = (width + 3) / 4
        val byCount = (height + 3) / 4
        if (data.size < bxCount * byCount * 8) return null
        val out = IntArray(width * height)
        val palette = IntArray(4)

        var off = 0
        for (by in 0 until byCount) {
            for (bx in 0 until bxCount) {
                val c0 = readLeUInt16(data, off)
                val c1 = readLeUInt16(data, off + 2)
                val bits = readLeUInt32(data, off + 4)
                off += 8
                buildDxtColorPalette(c0, c1, allowTransparent = true, palette)
                for (py in 0 until 4) {
                    val y = by * 4 + py
                    if (y >= height) continue
                    for (px in 0 until 4) {
                        val x = bx * 4 + px
                        if (x >= width) continue
                        val code = ((bits ushr ((py * 4 + px) * 2)) and 3L).toInt()
                        out[y * width + x] = palette[code]
                    }
                }
            }
        }
        return out
    }

    /**
     * Standard S3TC DXT5 4x4 block decoder (interpolated alpha + DXT color block).
     */
    internal fun decodeDxt5(width: Int, height: Int, data: ByteArray): IntArray? {
        val bxCount = (width + 3) / 4
        val byCount = (height + 3) / 4
        if (data.size < bxCount * byCount * 16) return null
        val out = IntArray(width * height)
        val palette = IntArray(4)
        val alphaTable = IntArray(8)

        var off = 0
        for (by in 0 until byCount) {
            for (bx in 0 until bxCount) {
                val a0 = data[off].toInt() and 0xFF
                val a1 = data[off + 1].toInt() and 0xFF
                alphaTable[0] = a0
                alphaTable[1] = a1
                if (a0 > a1) {
                    for (k in 1..6) alphaTable[k + 1] = ((7 - k) * a0 + k * a1) / 7
                } else {
                    for (k in 1..4) alphaTable[k + 1] = ((5 - k) * a0 + k * a1) / 5
                    alphaTable[6] = 0
                    alphaTable[7] = 255
                }
                var alphaBits = 0L
                for (i in 0 until 6) {
                    alphaBits = alphaBits or ((data[off + 2 + i].toLong() and 0xFF) shl (i * 8))
                }

                val c0 = readLeUInt16(data, off + 8)
                val c1 = readLeUInt16(data, off + 10)
                val colorBits = readLeUInt32(data, off + 12)
                off += 16

                buildDxtColorPalette(c0, c1, allowTransparent = false, palette)
                for (py in 0 until 4) {
                    val y = by * 4 + py
                    if (y >= height) continue
                    for (px in 0 until 4) {
                        val x = bx * 4 + px
                        if (x >= width) continue
                        val idx = py * 4 + px
                        val aCode = ((alphaBits ushr (idx * 3)) and 7L).toInt()
                        val cCode = ((colorBits ushr (idx * 2)) and 3L).toInt()
                        val rgb = palette[cCode] and 0x00FFFFFF
                        out[y * width + x] = (alphaTable[aCode] shl 24) or rgb
                    }
                }
            }
        }
        return out
    }

    /**
     * Maps live GTA SA 2.00 boot telemetry to a clean 0..100 progress percentage and stage caption
     * for the SA-MP PC-style loading screen.
     */
    fun computeLoadingProgress(
        eglReady: Boolean,
        gameState: Int,
        nativeLoadingPercent: Int,
        texDbLoadedCount: Int,
        doGameStateTicks: Int,
        worldReady: Boolean
    ): Pair<Int, String> {
        if (worldReady) return 100 to "San Andreas Ready — Connecting to Server..."
        if (!eglReady && gameState < 0) {
            return 8 to "Initializing OpenGL ES 2.0 & Engine..."
        }
        val texBonus = texDbLoadedCount.coerceIn(0, 6) * 2
        return when {
            gameState < 0 -> (14 + (if (doGameStateTicks > 0) 4 else 0)) to "Starting GTA San Andreas 2.00..."
            gameState in 0..4 -> (20 + gameState * 2) to "Initializing Engine Subsystems..."
            gameState == 5 -> (30 + texBonus).coerceAtMost(46) to "Loading Texture Databases ($texDbLoadedCount)..."
            gameState == 6 -> 48 to "Preparing Multiplayer Session..."
            gameState == 7 -> 52 to "Launching San Andreas World..."
            gameState == 8 -> {
                val scaled = 54 + (nativeLoadingPercent.coerceIn(0, 100) * 40) / 100
                scaled.coerceIn(54, 94) to "Loading San Andreas World (${nativeLoadingPercent.coerceIn(0, 100)}%)..."
            }
            gameState >= 9 -> 96 to "Spawning Multiplayer World..."
            else -> 15 to "Booting GTA San Andreas 2.00..."
        }
    }

    private fun buildDxtColorPalette(c0: Int, c1: Int, allowTransparent: Boolean, out: IntArray) {
        val r0 = ((c0 ushr 11) and 0x1F) * 255 / 31
        val g0 = ((c0 ushr 5) and 0x3F) * 255 / 63
        val b0 = (c0 and 0x1F) * 255 / 31
        val r1 = ((c1 ushr 11) and 0x1F) * 255 / 31
        val g1 = ((c1 ushr 5) and 0x3F) * 255 / 63
        val b1 = (c1 and 0x1F) * 255 / 31
        out[0] = (0xFF shl 24) or (r0 shl 16) or (g0 shl 8) or b0
        out[1] = (0xFF shl 24) or (r1 shl 16) or (g1 shl 8) or b1
        if (c0 > c1 || !allowTransparent) {
            out[2] = (0xFF shl 24) or (((2 * r0 + r1) / 3) shl 16) or (((2 * g0 + g1) / 3) shl 8) or ((2 * b0 + b1) / 3)
            out[3] = (0xFF shl 24) or (((r0 + 2 * r1) / 3) shl 16) or (((g0 + 2 * g1) / 3) shl 8) or ((b0 + 2 * b1) / 3)
        } else {
            out[2] = (0xFF shl 24) or (((r0 + r1) / 2) shl 16) or (((g0 + g1) / 2) shl 8) or ((b0 + b1) / 2)
            out[3] = 0x00000000
        }
    }

    private fun signExtend3(v: Int): Int = if ((v and 4) != 0) v - 8 else v

    private fun readLeUInt16(buf: ByteArray, offset: Int): Int =
        (buf[offset].toInt() and 0xFF) or ((buf[offset + 1].toInt() and 0xFF) shl 8)

    private fun readLeUInt32(buf: ByteArray, offset: Int): Long =
        ByteBuffer.wrap(buf, offset, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFFFFFFL

    private fun decodeSampledBitmap(file: File, maxW: Int, maxH: Int): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.absolutePath, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1
        while ((bounds.outWidth / sample) > maxW || (bounds.outHeight / sample) > maxH) {
            sample *= 2
        }
        val opts = BitmapFactory.Options().apply {
            inSampleSize = sample
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        return BitmapFactory.decodeFile(file.absolutePath, opts)
    }

    private fun resolvePathIgnoreCase(root: File, vararg segments: String): File? {
        var cur: File = root
        for (seg in segments) {
            val next = findChildAnyIgnoreCase(cur, seg) ?: return null
            cur = next
        }
        return cur
    }

    private fun findChildAnyIgnoreCase(parent: File, name: String): File? {
        val direct = File(parent, name)
        if (direct.exists()) return direct
        return parent.listFiles()?.firstOrNull { it.name.equals(name, ignoreCase = true) }
    }

    private fun findChildDirIgnoreCase(parent: File, name: String): File? {
        return findChildAnyIgnoreCase(parent, name)?.takeIf { it.isDirectory && it.canRead() }
    }

    private fun findChildFileIgnoreCase(parent: File, name: String): File? {
        return findChildAnyIgnoreCase(parent, name)?.takeIf { it.isFile && it.canRead() }
    }

    private fun findRecursiveIgnoreCase(dir: File, names: List<String>, maxDepth: Int): File? {
        if (maxDepth < 0 || !dir.isDirectory || !dir.canRead()) return null
        val children = dir.listFiles() ?: return null
        for (f in children) {
            if (f.isFile && f.canRead() && names.any { f.name.equals(it, ignoreCase = true) }) {
                return f
            }
        }
        if (maxDepth == 0) return null
        for (sub in children) {
            if (sub.isDirectory && sub.canRead()) {
                findRecursiveIgnoreCase(sub, names, maxDepth - 1)?.let { return it }
            }
        }
        return null
    }
}
