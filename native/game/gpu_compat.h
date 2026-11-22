#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace nana::game {

// Exact TextureDatabaseFormat enum verified from libGTASA.so 2.00 ARM32
// (_ZN15TextureDatabase10LoadThumbsE21TextureDatabaseFormatb & TextureDatabaseRuntime::Load)
enum class GtaTextureFormat : int {
    UNC = 0, // texdb/<db>/<db>.unc.{tmb,toc,dat}
    DXT = 1, // texdb/<db>/<db>.dxt.{tmb,toc,dat} (Adreno / Tegra)
    X360 = 2,
    PS3 = 3,
    PVR = 4, // texdb/<db>/<db>.pvr.{tmb,toc,dat} (PowerVR)
    ETC = 5, // texdb/<db>/<db>.etc.{tmb,toc,dat} (Mali / Immortalis / Universal ES2)
    AUTO = 6 // Auto-selected from RQCaps inside TextureDatabaseRuntime::Load
};

// Bitmask of texture formats detected on disk for a specific texdb/<db>/ directory
enum TexDbFormatMask : uint32_t {
    TEXDB_NONE = 0,
    TEXDB_UNC  = 1u << 0,
    TEXDB_DXT  = 1u << 1,
    TEXDB_PVR  = 1u << 2,
    TEXDB_ETC  = 1u << 3,
};

struct GpuClassification {
    GtaTextureFormat preferredFormat = GtaTextureFormat::ETC;
    bool isAdreno = false;
    bool isMali = false;
    bool isPowerVR = false;
    bool supportsDxtOrAtc = false;
    bool supportsPvr = false;
};

/**
 * Pure deterministic GPU classifier matching Android GL_RENDERER / GL_EXTENSIONS strings:
 *   - Adreno / Qualcomm -> DXT (1)
 *   - Mali / Immortalis / ARM / Xclipse -> ETC (5)
 *   - PowerVR / Rogue / SGX / Imagination -> PVR (4)
 *   - Tegra / NVIDIA -> DXT (1)
 *   - Fallback -> GL_EXTENSIONS (pvrtc -> PVR, dxt1/s3tc/atc -> DXT, else ETC)
 */
GpuClassification ClassifyGpu(const std::string& renderer, const std::string& extensions);

/**
 * Resolves the safest format for loading a specific texture database given the GPU classification
 * and the bitmask of files actually present inside <storageRoot>/texdb/<dbName>/.
 *
 * Ensures unpacked Mali packs (like Data_Sem_Mods_Alyn_-_Mali.zip which only ship .etc)
 * work seamlessly on Mali AND cross-GPU on Adreno/PowerVR via GL ETC1 without crashing,
 * while native Adreno (.dxt) and PowerVR (.pvr) packs load their native compressed format.
 */
GtaTextureFormat ResolveDatabaseFormat(const GpuClassification& gpu,
                                       uint32_t availableOnDiskMask,
                                       int requestedFormat);

/**
 * Scans <storageRoot>/texdb/<dbName>/ (case-insensitive) for .etc/.dxt/.pvr/.unc files.
 */
uint32_t ScanTexDbFormats(const std::string& storageRoot, const char* dbName);

/**
 * Runtime GPU & TextureDatabaseRuntime compatibility hook manager for libGTASA.so 2.00 ARM32.
 */
class GpuCompat {
public:
    static GpuCompat& Instance();

    void Configure(const std::string& storageRoot,
                   int preferredFormatHint,
                   const std::string& gpuRendererHint,
                   const std::string& gpuExtensionsHint);

    bool InstallHooks(void* rqCapsPtr, void* textureDbLoadFn);

    void* OnTextureDatabaseLoad(const char* name, bool fullyLoad, int requestedFormat);
    int GetLoadedDbCount() const { return loadedDbCount_.load(std::memory_order_relaxed); }

private:
    GpuCompat() = default;

    void RefreshLiveGlIfNeeded();
    void SyncRqCapsForGpu();

    std::string storageRoot_;
    std::string renderer_;
    std::string extensions_;
    GpuClassification gpu_{};
    int preferredHint_ = -1;
    bool liveGlChecked_ = false;
    bool hooked_ = false;
    std::atomic<int> loadedDbCount_{0};

    uint8_t* rqCaps_ = nullptr;
    using LoadFn = void* (*)(const char* name, bool fullyLoad, int format);
    LoadFn origLoad_ = nullptr;
};

} // namespace nana::game
