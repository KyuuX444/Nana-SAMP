package com.nana.samp.core

import android.opengl.GLES20
import android.util.Log
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.egl.EGLSurface

/**
 * Detects the Android device GPU (Adreno, Mali, PowerVR, Tegra, etc.) and maps it to the
 * exact GTA San Andreas 2.00 TextureDatabaseFormat enum:
 *   0 = UNC (.unc — uncompressed universal fallback)
 *   1 = DXT (.dxt — Adreno / Tegra)
 *   4 = PVR (.pvr — PowerVR)
 *   5 = ETC (.etc — Mali / Immortalis / Xclipse / universal ES2)
 */
enum class TextureFormat(val nativeCode: Int, val ext: String, val label: String) {
    UNC(0, "unc", "UNC"),
    DXT(1, "dxt", "DXT"),
    PVR(4, "pvr", "PVR"),
    ETC(5, "etc", "ETC");

    companion object {
        fun fromCode(code: Int): TextureFormat? = entries.firstOrNull { it.nativeCode == code }
    }
}

enum class GpuVendor(val displayName: String, val nativeFormat: TextureFormat) {
    ADRENO("Adreno", TextureFormat.DXT),
    MALI("Mali", TextureFormat.ETC),
    POWERVR("PowerVR", TextureFormat.PVR),
    TEGRA("Tegra", TextureFormat.DXT),
    OTHER("Generic GLES2", TextureFormat.ETC)
}

data class GpuInfo(
    val renderer: String,
    val vendor: GpuVendor,
    val preferredFormat: TextureFormat,
    val extensions: String,
    val supportsDxt: Boolean,
    val supportsAtc: Boolean,
    val supportsPvr: Boolean,
    val supportsEtc1: Boolean
) {
    fun shortLabel(): String {
        val name = renderer.ifBlank { vendor.displayName }
        return "$name (${vendor.displayName} → ${preferredFormat.label})"
    }
}

object GpuDetector {
    private const val TAG = "NanaSAMP-GPU"
    private const val EGL_CONTEXT_CLIENT_VERSION = 0x3098
    private const val EGL_OPENGL_ES2_BIT = 4

    @Volatile
    private var cachedInfo: GpuInfo? = null

    /**
     * Pure deterministic GPU classifier (unit-tested on JVM).
     * Maps Adreno -> DXT, Mali/Immortalis/Xclipse -> ETC, PowerVR -> PVR, Tegra -> DXT,
     * and inspects GL_EXTENSIONS as fallback for unknown GPUs.
     */
    fun classify(rendererRaw: String, extensionsRaw: String): GpuInfo {
        val renderer = rendererRaw.trim()
        val lowerR = renderer.lowercase()
        val lowerExt = extensionsRaw.lowercase()

        val supportsDxt = lowerExt.contains("texture_compression_dxt1") ||
            lowerExt.contains("texture_compression_s3tc")
        val supportsAtc = lowerExt.contains("compressed_atc_texture")
        val supportsPvr = lowerExt.contains("texture_compression_pvrtc")
        val supportsEtc1 = lowerExt.contains("compressed_etc1_rgb8") || lowerExt.isEmpty()

        val vendor = when {
            lowerR.contains("adreno") || lowerR.contains("qualcomm") -> GpuVendor.ADRENO
            lowerR.contains("mali") || lowerR.contains("immortalis") || lowerR.contains("arm") ||
                lowerR.contains("xclipse") -> GpuVendor.MALI
            lowerR.contains("powervr") || lowerR.contains("rogue") ||
                lowerR.contains("sgx") || lowerR.contains("imagination") -> GpuVendor.POWERVR
            lowerR.contains("tegra") || lowerR.contains("nvidia") -> GpuVendor.TEGRA
            else -> GpuVendor.OTHER
        }

        val preferred = when (vendor) {
            GpuVendor.ADRENO -> TextureFormat.DXT
            GpuVendor.MALI -> TextureFormat.ETC
            GpuVendor.POWERVR -> TextureFormat.PVR
            GpuVendor.TEGRA -> TextureFormat.DXT
            GpuVendor.OTHER -> when {
                supportsPvr -> TextureFormat.PVR
                supportsDxt || supportsAtc -> TextureFormat.DXT
                supportsEtc1 -> TextureFormat.ETC
                else -> TextureFormat.UNC
            }
        }

        return GpuInfo(
            renderer = renderer.ifEmpty { "Unknown GPU" },
            vendor = vendor,
            preferredFormat = preferred,
            extensions = extensionsRaw,
            supportsDxt = supportsDxt,
            supportsAtc = supportsAtc,
            supportsPvr = supportsPvr,
            supportsEtc1 = supportsEtc1
        )
    }

    /**
     * Picks the safest texture database format given BOTH the device GPU and the texture
     * databases actually installed on storage (e.g., Data_Sem_Mods_Alyn_-_Mali.zip only
     * contains .etc files; official OBB packs contain multiple formats).
     */
    fun resolveEffectiveFormat(
        gpu: GpuInfo,
        availableOnDisk: Set<TextureFormat>,
        hasObb: Boolean
    ): TextureFormat {
        // 1. Exact GPU-native match available on disk
        if (availableOnDisk.contains(gpu.preferredFormat)) {
            return gpu.preferredFormat
        }
        // 2. OBB archives ship multi-GPU databases -> use GPU-native format
        if (availableOnDisk.isEmpty() && hasObb) {
            return gpu.preferredFormat
        }
        // 3. Cross-GPU compatibility (e.g. Mali .etc data pack installed on Adreno/PowerVR):
        //    ETC1 and UNC work universally on OpenGL ES 2.0/3.0 Android GPUs.
        if (availableOnDisk.contains(TextureFormat.ETC)) {
            return TextureFormat.ETC
        }
        if (availableOnDisk.contains(TextureFormat.UNC)) {
            return TextureFormat.UNC
        }
        if (availableOnDisk.contains(TextureFormat.DXT) && (gpu.supportsDxt || gpu.supportsAtc || gpu.vendor == GpuVendor.ADRENO)) {
            return TextureFormat.DXT
        }
        if (availableOnDisk.contains(TextureFormat.PVR) && (gpu.supportsPvr || gpu.vendor == GpuVendor.POWERVR)) {
            return TextureFormat.PVR
        }
        return availableOnDisk.firstOrNull() ?: gpu.preferredFormat
    }

    /**
     * Probes the real OpenGL ES 2.0 renderer & extension strings via a temporary 1x1 EGL Pbuffer.
     * Safe to invoke from a background thread; caches result for the process lifetime.
     */
    fun detect(): GpuInfo {
        cachedInfo?.let { return it }

        var egl: EGL10? = null
        var display: EGLDisplay = EGL10.EGL_NO_DISPLAY
        var context: EGLContext = EGL10.EGL_NO_CONTEXT
        var surface: EGLSurface = EGL10.EGL_NO_SURFACE
        try {
            egl = EGLContext.getEGL() as EGL10
            display = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY)
            val version = IntArray(2)
            if (display == EGL10.EGL_NO_DISPLAY || !egl.eglInitialize(display, version)) {
                return classify("", "").also { cachedInfo = it }
            }

            val configAttribs = intArrayOf(
                EGL10.EGL_SURFACE_TYPE, EGL10.EGL_PBUFFER_BIT,
                EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL10.EGL_RED_SIZE, 8,
                EGL10.EGL_GREEN_SIZE, 8,
                EGL10.EGL_BLUE_SIZE, 8,
                EGL10.EGL_NONE
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfigs = IntArray(1)
            if (!egl.eglChooseConfig(display, configAttribs, configs, 1, numConfigs) || numConfigs[0] == 0) {
                val minimalAttribs = intArrayOf(
                    EGL10.EGL_SURFACE_TYPE, EGL10.EGL_PBUFFER_BIT,
                    EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                    EGL10.EGL_NONE
                )
                if (!egl.eglChooseConfig(display, minimalAttribs, configs, 1, numConfigs) || numConfigs[0] == 0) {
                    return classify("", "").also { cachedInfo = it }
                }
            }

            val ctxAttribs = intArrayOf(EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE)
            context = egl.eglCreateContext(display, configs[0], EGL10.EGL_NO_CONTEXT, ctxAttribs)
            val pbufAttribs = intArrayOf(EGL10.EGL_WIDTH, 1, EGL10.EGL_HEIGHT, 1, EGL10.EGL_NONE)
            surface = egl.eglCreatePbufferSurface(display, configs[0], pbufAttribs)

            if (context != null && context != EGL10.EGL_NO_CONTEXT &&
                surface != null && surface != EGL10.EGL_NO_SURFACE &&
                egl.eglMakeCurrent(display, surface, surface, context)
            ) {
                val renderer = GLES20.glGetString(GLES20.GL_RENDERER) ?: ""
                val extensions = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
                val info = classify(renderer, extensions)
                Log.i(TAG, "Detected GPU: ${info.shortLabel()}")
                cachedInfo = info
                return info
            }
        } catch (t: Throwable) {
            Log.w(TAG, "EGL GPU probe failed", t)
        } finally {
            try {
                if (egl != null && display != EGL10.EGL_NO_DISPLAY) {
                    egl.eglMakeCurrent(display, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT)
                    if (surface != EGL10.EGL_NO_SURFACE) egl.eglDestroySurface(display, surface)
                    if (context != EGL10.EGL_NO_CONTEXT) egl.eglDestroyContext(display, context)
                    egl.eglTerminate(display)
                }
            } catch (_: Throwable) {
            }
        }
        return classify("", "").also { cachedInfo = it }
    }
}
