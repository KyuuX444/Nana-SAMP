package com.nana.samp

import com.nana.samp.core.SampLogoLoader
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class SampLogoLoaderTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun findsLooseMyLogoInTexdbTxdPriority() {
        val root = tempFolder.newFolder("gta_root")
        val txdDir = File(root, "texdb/txd").apply { mkdirs() }
        val sampDir = File(root, "SAMP").apply { mkdirs() }
        val expected = File(txdDir, "mylogo.png").apply { writeBytes(byteArrayOf(1, 2, 3, 4)) }
        File(sampDir, "mylogo.png").writeBytes(byteArrayOf(9, 9, 9, 9))

        val found = SampLogoLoader.findLooseLogoFile(root)
        assertNotNull(found)
        assertEquals(expected.canonicalPath, found!!.canonicalPath)
    }

    @Test
    fun findsLooseMyLogoCaseInsensitiveInTxdOrSrc() {
        val root = tempFolder.newFolder("gta_root_ci")
        val srcDir = File(root, "texdb/TXD/src").apply { mkdirs() }
        val expected = File(srcDir, "MyLogo.PNG").apply { writeBytes(byteArrayOf(5, 6, 7)) }

        val found = SampLogoLoader.findLooseLogoFile(root)
        assertNotNull(found)
        assertEquals(expected.canonicalPath, found!!.canonicalPath)

        // Direct <root>/txd/mylogo.png fallback
        val root2 = tempFolder.newFolder("gta_root_txd")
        val directTxd = File(root2, "txd").apply { mkdirs() }
        val expected2 = File(directTxd, "mylogo.png").apply { writeBytes(byteArrayOf(7, 8)) }
        assertEquals(expected2.canonicalPath, SampLogoLoader.findLooseLogoFile(root2)!!.canonicalPath)
    }

    @Test
    fun returnsNullWhenNoLogoPresent() {
        val root = tempFolder.newFolder("empty_root")
        assertNull(SampLogoLoader.findLooseLogoFile(root))
    }

    @Test
    fun readsPackedMyLogoFromWarDrumTexDbUncAndEtc1() {
        val root = tempFolder.newFolder("texdb_packed")
        val txdDir = File(root, "texdb/txd").apply { mkdirs() }

        // Entry 0: dummy, Entry 1: "mylogo"
        File(txdDir, "txd.txt").writeText(
            """
            cat=txd
            "splash0" width=4 height=4 img=11111111
            "mylogo" width=4 height=4 img=22222222 alphamode=1
            """.trimIndent()
        )
        assertEquals(1, SampLogoLoader.findTexDbEntryIndex(File(txdDir, "txd.txt")))

        // Build synthetic txd.unc.dat with a 4x4 RGBA8888 red texture at offset 0
        val width = 4
        val height = 4
        val pixels = ByteArray(width * height * 4)
        for (i in 0 until width * height) {
            pixels[i * 4] = 0xE0.toByte()     // R
            pixels[i * 4 + 1] = 0x40.toByte() // G
            pixels[i * 4 + 2] = 0x20.toByte() // B
            pixels[i * 4 + 3] = 0xFF.toByte() // A
        }
        val datBuf = ByteBuffer.allocate(16 + pixels.size).order(ByteOrder.LITTLE_ENDIAN)
        datBuf.putShort(0)                 // flags
        datBuf.putShort(0x1908.toShort())  // GL_RGBA
        datBuf.putShort(width.toShort())
        datBuf.putShort(height.toShort())
        datBuf.putInt(pixels.size + 4)     // totalSize including compFlag
        datBuf.putInt(0)                   // compFlag = 0 (uncompressed)
        datBuf.put(pixels)
        val datFile = File(txdDir, "txd.unc.dat").apply { writeBytes(datBuf.array()) }

        // Build txd.unc.toc: [datSize][0xFFFFFFFF (entry 0)][0 (entry 1)]
        val tocBuf = ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN)
        tocBuf.putInt(datFile.length().toInt())
        tocBuf.putInt(-1)
        tocBuf.putInt(0)
        File(txdDir, "txd.unc.toc").writeBytes(tocBuf.array())

        val decoded = SampLogoLoader.readLogoFromTexDb(root)
        assertNotNull(decoded)
        assertEquals(4, decoded!!.width)
        assertEquals(4, decoded.height)
        assertEquals(0xFFE04020.toInt(), decoded.argbPixels[0])
        assertTrue(decoded.sourceLabel.contains("texdb/txd"))

        // Also test ETC1 (0x8D64) single 4x4 block decoding
        val etcRoot = tempFolder.newFolder("texdb_etc1")
        val etcTxdDir = File(etcRoot, "texdb/txd").apply { mkdirs() }
        File(etcTxdDir, "txd.txt").writeText("\"mylogo\" width=4 height=4\n")
        // Individual mode (diff=0, flip=0): R4a=15, R4b=15, G4a=8, G4b=8, B4a=0, B4b=0, table0=0, table1=0, all idx=0 (+2)
        val etcBlock = byteArrayOf(
            0xFF.toByte(), 0x88.toByte(), 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        )
        val etcDat = ByteBuffer.allocate(16 + etcBlock.size).order(ByteOrder.LITTLE_ENDIAN).apply {
            putShort(0)
            putShort(0x8D64.toShort()) // GL_ETC1_RGB8_OES
            putShort(4)
            putShort(4)
            putInt(etcBlock.size + 4)
            putInt(0)
            put(etcBlock)
        }.array()
        val etcDatFile = File(etcTxdDir, "txd.etc.dat").apply { writeBytes(etcDat) }
        val etcToc = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).apply {
            putInt(etcDatFile.length().toInt())
            putInt(0)
        }.array()
        File(etcTxdDir, "txd.etc.toc").writeBytes(etcToc)

        val etcDecoded = SampLogoLoader.readLogoFromTexDb(etcRoot)
        assertNotNull(etcDecoded)
        assertEquals(4, etcDecoded!!.width)
        assertEquals(4, etcDecoded.height)
        val firstPx = etcDecoded.argbPixels[0]
        val r = (firstPx ushr 16) and 0xFF
        val g = (firstPx ushr 8) and 0xFF
        assertTrue("Expected red channel near 255, got $r", r >= 250)
        assertTrue("Expected green channel near 138, got $g", g in 130..145)
    }

    @Test
    fun loadingProgressAdvancesMonotonicallyAcrossGtaStates() {
        val (p0, _) = SampLogoLoader.computeLoadingProgress(false, -1, 0, 0, 0, false)
        val (p1, _) = SampLogoLoader.computeLoadingProgress(true, 0, 0, 0, 1, false)
        val (p2, _) = SampLogoLoader.computeLoadingProgress(true, 5, 0, 3, 4, false)
        val (p3, _) = SampLogoLoader.computeLoadingProgress(true, 7, 0, 5, 6, false)
        val (p4, _) = SampLogoLoader.computeLoadingProgress(true, 8, 50, 6, 7, false)
        val (p5, _) = SampLogoLoader.computeLoadingProgress(true, 9, 100, 6, 8, false)
        val (p6, _) = SampLogoLoader.computeLoadingProgress(true, 9, 100, 6, 9, true)

        assertTrue(p0 < p1)
        assertTrue(p1 < p2)
        assertTrue(p2 < p3)
        assertTrue(p3 < p4)
        assertTrue(p4 < p5)
        assertTrue(p5 < p6)
        assertEquals(100, p6)
    }
}
