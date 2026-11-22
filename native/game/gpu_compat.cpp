#include "gpu_compat.h"
#include "arm_hook.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
#define GPU_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-GPU", __VA_ARGS__)
#define GPU_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "NanaSAMP-GPU", __VA_ARGS__)
#else
#include <cstdio>
#define GPU_LOGI(...) do { std::printf("[GPU] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define GPU_LOGW(...) do { std::printf("[GPU WARN] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace nana::game {

namespace {

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool ContainsI(const std::string& haystackLower, const char* needleLower) {
    return haystackLower.find(needleLower) != std::string::npos;
}

bool EndsWith(const std::string& s, const char* suffix) {
    size_t slen = std::strlen(suffix);
    if (s.size() < slen) return false;
    return s.compare(s.size() - slen, slen, suffix) == 0;
}

std::string FindChildCaseInsensitive(const std::string& parent, const std::string& childName) {
    if (parent.empty() || childName.empty()) return {};
    std::string direct = parent + "/" + childName;
    struct stat st{};
    if (stat(direct.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return direct;
    }
    DIR* dir = opendir(parent.c_str());
    if (!dir) return {};
    std::string targetLower = ToLower(childName);
    std::string match;
    while (struct dirent* ent = readdir(dir)) {
        if (!ent->d_name[0] || ent->d_name[0] == '.') continue;
        if (ToLower(ent->d_name) == targetLower) {
            std::string candidate = parent + "/" + ent->d_name;
            if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                match = candidate;
                break;
            }
        }
    }
    closedir(dir);
    return match;
}

void* Hook_TextureDatabaseRuntime_Load(const char* name, bool fullyLoad, int format) {
    return GpuCompat::Instance().OnTextureDatabaseLoad(name, fullyLoad, format);
}

const char* FormatName(GtaTextureFormat f) {
    switch (f) {
        case GtaTextureFormat::UNC: return "UNC";
        case GtaTextureFormat::DXT: return "DXT";
        case GtaTextureFormat::PVR: return "PVR";
        case GtaTextureFormat::ETC: return "ETC";
        case GtaTextureFormat::AUTO: return "AUTO";
        default: return "OTHER";
    }
}

} // namespace

GpuClassification ClassifyGpu(const std::string& renderer, const std::string& extensions) {
    GpuClassification out{};
    std::string r = ToLower(renderer);
    std::string e = ToLower(extensions);

    bool hasDxt = ContainsI(e, "texture_compression_dxt1") || ContainsI(e, "texture_compression_s3tc");
    bool hasAtc = ContainsI(e, "compressed_atc_texture");
    bool hasPvr = ContainsI(e, "texture_compression_pvrtc");

    out.supportsDxtOrAtc = hasDxt || hasAtc;
    out.supportsPvr = hasPvr;

    if (ContainsI(r, "adreno") || ContainsI(r, "qualcomm")) {
        out.isAdreno = true;
        out.supportsDxtOrAtc = true;
        out.preferredFormat = GtaTextureFormat::DXT;
    } else if (ContainsI(r, "mali") || ContainsI(r, "immortalis") ||
               ContainsI(r, "xclipse") || ContainsI(r, "arm")) {
        out.isMali = true;
        out.preferredFormat = GtaTextureFormat::ETC;
    } else if (ContainsI(r, "powervr") || ContainsI(r, "rogue") ||
               ContainsI(r, "sgx") || ContainsI(r, "imagination")) {
        out.isPowerVR = true;
        out.supportsPvr = true;
        out.preferredFormat = GtaTextureFormat::PVR;
    } else if (ContainsI(r, "tegra") || ContainsI(r, "nvidia")) {
        out.supportsDxtOrAtc = true;
        out.preferredFormat = GtaTextureFormat::DXT;
    } else if (hasPvr) {
        out.isPowerVR = true;
        out.preferredFormat = GtaTextureFormat::PVR;
    } else if (hasDxt || hasAtc) {
        out.preferredFormat = GtaTextureFormat::DXT;
    } else {
        out.preferredFormat = GtaTextureFormat::ETC;
    }
    return out;
}

uint32_t ScanTexDbFormats(const std::string& storageRoot, const char* dbName) {
    if (storageRoot.empty() || !dbName || !dbName[0]) return TEXDB_NONE;
    std::string texdbDir = FindChildCaseInsensitive(storageRoot, "texdb");
    if (texdbDir.empty()) return TEXDB_NONE;
    std::string subDir = FindChildCaseInsensitive(texdbDir, dbName);
    if (subDir.empty()) return TEXDB_NONE;

    DIR* dir = opendir(subDir.c_str());
    if (!dir) return TEXDB_NONE;

    uint32_t mask = TEXDB_NONE;
    while (struct dirent* ent = readdir(dir)) {
        if (!ent->d_name[0] || ent->d_name[0] == '.') continue;
        std::string lower = ToLower(ent->d_name);
        if (EndsWith(lower, ".etc.tmb") || EndsWith(lower, ".etc.toc") || EndsWith(lower, ".etc.dat")) {
            mask |= TEXDB_ETC;
        } else if (EndsWith(lower, ".dxt.tmb") || EndsWith(lower, ".dxt.toc") || EndsWith(lower, ".dxt.dat")) {
            mask |= TEXDB_DXT;
        } else if (EndsWith(lower, ".pvr.tmb") || EndsWith(lower, ".pvr.toc") || EndsWith(lower, ".pvr.dat")) {
            mask |= TEXDB_PVR;
        } else if (EndsWith(lower, ".unc.tmb") || EndsWith(lower, ".unc.toc") || EndsWith(lower, ".unc.dat")) {
            mask |= TEXDB_UNC;
        }
    }
    closedir(dir);
    return mask;
}

GtaTextureFormat ResolveDatabaseFormat(const GpuClassification& gpu,
                                       uint32_t availableOnDiskMask,
                                       int requestedFormat) {
    auto maskHas = [availableOnDiskMask](GtaTextureFormat fmt) -> bool {
        switch (fmt) {
            case GtaTextureFormat::UNC: return (availableOnDiskMask & TEXDB_UNC) != 0;
            case GtaTextureFormat::DXT: return (availableOnDiskMask & TEXDB_DXT) != 0;
            case GtaTextureFormat::PVR: return (availableOnDiskMask & TEXDB_PVR) != 0;
            case GtaTextureFormat::ETC: return (availableOnDiskMask & TEXDB_ETC) != 0;
            default: return false;
        }
    };

    // If unpacked files exist for this texture DB on disk, align strictly to what exists!
    if (availableOnDiskMask != TEXDB_NONE) {
        // 1. If the caller explicitly asked for a specific non-AUTO format AND it exists, honor it
        if (requestedFormat != static_cast<int>(GtaTextureFormat::AUTO)) {
            auto req = static_cast<GtaTextureFormat>(requestedFormat);
            if (maskHas(req)) return req;
        }
        // 2. Prefer the device GPU's native format (Adreno->DXT, Mali->ETC, PowerVR->PVR) if installed
        if (maskHas(gpu.preferredFormat)) {
            return gpu.preferredFormat;
        }
        // 3. Cross-GPU universal fallback: ETC1 (Mali packs like Data_Sem_Mods_Alyn_-_Mali.zip)
        //    and UNC are supported across Adreno, Mali, and PowerVR OpenGL ES 2.0/3.0 GPUs.
        if (maskHas(GtaTextureFormat::ETC)) return GtaTextureFormat::ETC;
        if (maskHas(GtaTextureFormat::UNC)) return GtaTextureFormat::UNC;
        if (maskHas(GtaTextureFormat::DXT) && gpu.supportsDxtOrAtc) return GtaTextureFormat::DXT;
        if (maskHas(GtaTextureFormat::PVR) && gpu.supportsPvr) return GtaTextureFormat::PVR;

        // Last resort: return whichever format is present on disk
        if (maskHas(GtaTextureFormat::DXT)) return GtaTextureFormat::DXT;
        if (maskHas(GtaTextureFormat::PVR)) return GtaTextureFormat::PVR;
    }

    // No unpacked files on disk (e.g. OBB archive with all formats): use GPU-detected format
    if (requestedFormat == static_cast<int>(GtaTextureFormat::AUTO)) {
        return gpu.preferredFormat;
    }
    return static_cast<GtaTextureFormat>(requestedFormat);
}

GpuCompat& GpuCompat::Instance() {
    static GpuCompat inst;
    return inst;
}

void GpuCompat::Configure(const std::string& storageRoot,
                          int preferredFormatHint,
                          const std::string& gpuRendererHint,
                          const std::string& gpuExtensionsHint) {
    storageRoot_ = storageRoot;
    while (!storageRoot_.empty() && storageRoot_.back() == '/') {
        storageRoot_.pop_back();
    }
    preferredHint_ = preferredFormatHint;
    renderer_ = gpuRendererHint;
    extensions_ = gpuExtensionsHint;
    gpu_ = ClassifyGpu(renderer_, extensions_);
    liveGlChecked_ = false;
    loadedDbCount_.store(0, std::memory_order_relaxed);
    GPU_LOGI("Configured GpuCompat root='%s' hint=%d renderer='%s' classified=%s",
             storageRoot_.c_str(), preferredHint_, renderer_.c_str(),
             FormatName(gpu_.preferredFormat));
}

bool GpuCompat::InstallHooks(void* rqCapsPtr, void* textureDbLoadFn) {
    rqCaps_ = static_cast<uint8_t*>(rqCapsPtr);
    if (hooked_ || !textureDbLoadFn) {
        return hooked_;
    }
    bool ok = ArmHook::Install(
        textureDbLoadFn,
        reinterpret_cast<void*>(&Hook_TextureDatabaseRuntime_Load),
        reinterpret_cast<void**>(&origLoad_));
    hooked_ = ok;
    GPU_LOGI("TextureDatabaseRuntime::Load hook installed=%d (RQCaps=%p)", ok ? 1 : 0, rqCapsPtr);
    return ok;
}

void GpuCompat::RefreshLiveGlIfNeeded() {
    if (liveGlChecked_) return;
    liveGlChecked_ = true;

#ifdef __ANDROID__
    using GlGetStringFn = const unsigned char* (*)(unsigned int);
    auto fn = reinterpret_cast<GlGetStringFn>(dlsym(RTLD_DEFAULT, "glGetString"));
    if (fn) {
        constexpr unsigned int kGlRenderer = 0x1F01u;
        constexpr unsigned int kGlExtensions = 0x1F03u;
        const char* liveRenderer = reinterpret_cast<const char*>(fn(kGlRenderer));
        const char* liveExt = reinterpret_cast<const char*>(fn(kGlExtensions));
        if (liveRenderer && liveRenderer[0]) {
            renderer_ = liveRenderer;
        }
        if (liveExt && liveExt[0]) {
            extensions_ = liveExt;
        }
        gpu_ = ClassifyGpu(renderer_, extensions_);
        GPU_LOGI("Live GL GPU detected: renderer='%s' -> preferred=%s (adreno=%d mali=%d pvr=%d)",
                 renderer_.c_str(), FormatName(gpu_.preferredFormat),
                 gpu_.isAdreno ? 1 : 0, gpu_.isMali ? 1 : 0, gpu_.isPowerVR ? 1 : 0);
    }
#endif
    SyncRqCapsForGpu();
}

void GpuCompat::SyncRqCapsForGpu() {
    if (!rqCaps_) return;
    // Verified RQCaps offsets in libGTASA.so 2.00 ARM32 (0x6b8b9c):
    //   +0x03 = hasDXT
    //   +0x04 = hasATC
    //   +0x05 = hasPVR
    //   +0x0c = isAdreno
    //   +0x0d = isETC
    if (gpu_.isMali) {
        // Prevent Mali drivers that advertise software/emulated DXT/S3TC from choosing .dxt
        rqCaps_[0x03] = 0;
        rqCaps_[0x04] = 0;
        rqCaps_[0x05] = 0;
        rqCaps_[0x0d] = 1;
        GPU_LOGI("Synchronized RQCaps for Mali GPU (ETC forced)");
    } else if (gpu_.isPowerVR) {
        rqCaps_[0x05] = 1;
        rqCaps_[0x0d] = 0;
    } else if (gpu_.isAdreno) {
        rqCaps_[0x0c] = 1;
    }
}

void* GpuCompat::OnTextureDatabaseLoad(const char* name, bool fullyLoad, int requestedFormat) {
    if (!origLoad_) return nullptr;

    RefreshLiveGlIfNeeded();

    const char* safeName = name ? name : "";
    uint32_t diskMask = ScanTexDbFormats(storageRoot_, safeName);
    GtaTextureFormat chosen = ResolveDatabaseFormat(gpu_, diskMask, requestedFormat);

    GPU_LOGI("TextureDatabaseRuntime::Load('%s', full=%d, req=%d) diskMask=0x%x -> chosen=%s(%d)",
             safeName, fullyLoad ? 1 : 0, requestedFormat, diskMask,
             FormatName(chosen), static_cast<int>(chosen));

    void* db = origLoad_(name, fullyLoad, static_cast<int>(chosen));
    if (db != nullptr) {
        loadedDbCount_.fetch_add(1, std::memory_order_relaxed);
        return db;
    }

    // Automatic resilient fallback chain if chosen format failed to open
    const std::array<GtaTextureFormat, 5> fallbacks = {
        GtaTextureFormat::ETC,
        GtaTextureFormat::UNC,
        GtaTextureFormat::DXT,
        GtaTextureFormat::PVR,
        GtaTextureFormat::AUTO,
    };
    for (GtaTextureFormat fb : fallbacks) {
        if (fb == chosen) continue;
        GPU_LOGW("Retrying TextureDatabaseRuntime::Load('%s') with fallback %s(%d)",
                 safeName, FormatName(fb), static_cast<int>(fb));
        db = origLoad_(name, fullyLoad, static_cast<int>(fb));
        if (db != nullptr) {
            loadedDbCount_.fetch_add(1, std::memory_order_relaxed);
            return db;
        }
    }
    return nullptr;
}

} // namespace nana::game
