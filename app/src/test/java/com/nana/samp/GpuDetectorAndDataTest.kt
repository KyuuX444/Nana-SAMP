package com.nana.samp

import com.nana.samp.core.GameDataManager
import com.nana.samp.core.GpuDetector
import com.nana.samp.core.GpuVendor
import com.nana.samp.core.TextureFormat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class GpuDetectorAndDataTest {

    @get:Rule
    val tempFolder = TemporaryFolder()

    @Test
    fun classifyAdrenoMapsToDxt() {
        val info = GpuDetector.classify(
            "Adreno (TM) 650",
            "GL_AMD_compressed_ATC_texture GL_OES_compressed_ETC1_RGB8_texture"
        )
        assertEquals(GpuVendor.ADRENO, info.vendor)
        assertEquals(TextureFormat.DXT, info.preferredFormat)
    }

    @Test
    fun classifyMaliAndImmortalisMapsToEtc() {
        val mali = GpuDetector.classify(
            "Mali-G68 MC4",
            "GL_OES_compressed_ETC1_RGB8_texture GL_EXT_texture_compression_s3tc"
        )
        assertEquals(GpuVendor.MALI, mali.vendor)
        assertEquals(TextureFormat.ETC, mali.preferredFormat)

        val immortalis = GpuDetector.classify("Immortalis-G715", "GL_OES_compressed_ETC1_RGB8_texture")
        assertEquals(GpuVendor.MALI, immortalis.vendor)
        assertEquals(TextureFormat.ETC, immortalis.preferredFormat)
    }

    @Test
    fun classifyPowerVrMapsToPvr() {
        val pvr = GpuDetector.classify(
            "PowerVR Rogue GE8320",
            "GL_IMG_texture_compression_pvrtc GL_OES_compressed_ETC1_RGB8_texture"
        )
        assertEquals(GpuVendor.POWERVR, pvr.vendor)
        assertEquals(TextureFormat.PVR, pvr.preferredFormat)
    }

    @Test
    fun resolveEffectiveFormatSupportsMaliPackCrossGpu() {
        val adreno = GpuDetector.classify("Adreno (TM) 730", "GL_AMD_compressed_ATC_texture")
        val mali = GpuDetector.classify("Mali-G57 MC2", "GL_OES_compressed_ETC1_RGB8_texture")
        val powervr = GpuDetector.classify("PowerVR Rogue GE8320", "GL_IMG_texture_compression_pvrtc")

        val maliOnlyPack = setOf(TextureFormat.ETC)
        assertEquals(TextureFormat.ETC, GpuDetector.resolveEffectiveFormat(mali, maliOnlyPack, hasObb = false))
        assertEquals(TextureFormat.ETC, GpuDetector.resolveEffectiveFormat(adreno, maliOnlyPack, hasObb = false))
        assertEquals(TextureFormat.ETC, GpuDetector.resolveEffectiveFormat(powervr, maliOnlyPack, hasObb = false))

        val adrenoOnlyPack = setOf(TextureFormat.DXT)
        assertEquals(TextureFormat.DXT, GpuDetector.resolveEffectiveFormat(adreno, adrenoOnlyPack, hasObb = false))

        val pvrOnlyPack = setOf(TextureFormat.PVR)
        assertEquals(TextureFormat.PVR, GpuDetector.resolveEffectiveFormat(powervr, pvrOnlyPack, hasObb = false))

        // OBB multi-GPU pack picks each GPU's native format
        assertEquals(TextureFormat.DXT, GpuDetector.resolveEffectiveFormat(adreno, emptySet(), hasObb = true))
        assertEquals(TextureFormat.ETC, GpuDetector.resolveEffectiveFormat(mali, emptySet(), hasObb = true))
        assertEquals(TextureFormat.PVR, GpuDetector.resolveEffectiveFormat(powervr, emptySet(), hasObb = true))
    }

    @Test
    fun detectsDirectAndNestedAlynMaliDataPack() {
        val appFiles = tempFolder.newFolder("app_files")
        // Simulate accidental nested extraction: files/Data_Sem_Mods_Alyn_-_Mali/files/texdb/gta3/gta3.etc.{tmb,toc,dat}
        val nestedRoot = File(appFiles, "Data_Sem_Mods_Alyn_-_Mali/files")
        val gta3Dir = File(nestedRoot, "texdb/gta3")
        gta3Dir.mkdirs()
        File(nestedRoot, "data").mkdirs()
        File(nestedRoot, "audio").mkdirs()
        File(gta3Dir, "gta3.etc.tmb").writeBytes(byteArrayOf(1, 2, 3))
        File(gta3Dir, "gta3.etc.toc").writeBytes(byteArrayOf(4, 5, 6))
        File(gta3Dir, "gta3.etc.dat").writeBytes(byteArrayOf(7, 8, 9))
        File(nestedRoot, "texdb/gta3.img").writeBytes(byteArrayOf(0))

        val status = GameDataManager.inspectDirectories(appFiles)
        assertTrue(status.ready)
        assertTrue(status.isUnpacked)
        assertEquals(nestedRoot.absolutePath.trimEnd('/'), status.storageRoot)
        assertEquals(setOf(TextureFormat.ETC), status.detectedFormats)
    }
}
