#include "file_compat.h"
#include "arm_hook.h"
#include "../core/crash_handler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define FILE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-File", __VA_ARGS__)
#define FILE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "NanaSAMP-File", __VA_ARGS__)
#else
#define FILE_LOGI(...) do {} while (0)
#define FILE_LOGW(...) do {} while (0)
#endif

namespace nana::game {

namespace {

std::string ToLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool EndsWithStr(const std::string& s, const char* suffix) {
    size_t slen = std::strlen(suffix);
    if (s.size() < slen) return false;
    return s.compare(s.size() - slen, slen, suffix) == 0;
}

std::string ExtractBaseName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string MapSampOverrideFileName(const std::string& lowerBaseName) {
    // GTA SA Android 2.00 opens data/script/mainV1.scm and data/script/scriptv1.img;
    // Alyn SA-MP data pack places the multiplayer script at SAMP/main.scm & SAMP/script.img.
    if (lowerBaseName == "mainv1.scm" || lowerBaseName == "main.scm") {
        return "main.scm";
    }
    if (lowerBaseName == "scriptv1.img" || lowerBaseName == "script.img") {
        return "script.img";
    }
    static const std::array<const char*, 13> kOverrides = {
        "gta.dat",
        "samp.ide",
        "peds.ide",
        "vehicles.ide",
        "handling.cfg",
        "weapon.dat",
        "object.dat",
        "default.dat",
        "default.ide",
        "pedstats.dat",
        "timecyc.dat",
        "tracks2.dat",
        "tracks4.dat",
    };
    for (const char* name : kOverrides) {
        if (lowerBaseName == name) return lowerBaseName;
    }
    return {};
}

NvFileHandle* Hook_NvFOpen(const char* dir, const char* filename, bool isUserFile, bool checkApk) {
    return FileCompat::Instance().OnNvFOpen(dir, filename, isUserFile, checkApk);
}

} // namespace

std::string NormalizeGameRelPath(const char* dir, const char* filename, const std::string& storageRoot) {
    if (!filename || filename[0] == '\0') return {};

    std::string combined;
    if (dir && dir[0] != '\0') {
        combined.append(dir);
        if (combined.back() != '/' && combined.back() != '\\') {
            combined.push_back('/');
        }
    }
    combined.append(filename);

    // Replace Windows backslashes with POSIX forward slashes
    for (char& c : combined) {
        if (c == '\\') c = '/';
    }

    // Trim trailing spaces (mirrors OS_FileOpen behavior at 0x266bf2)
    while (!combined.empty() && combined.back() == ' ') {
        combined.pop_back();
    }
    if (combined.empty()) return {};

    // Collapse repeated slashes
    std::string collapsed;
    collapsed.reserve(combined.size());
    bool prevSlash = false;
    for (char c : combined) {
        if (c == '/') {
            if (!prevSlash) collapsed.push_back('/');
            prevSlash = true;
        } else {
            collapsed.push_back(c);
            prevSlash = false;
        }
    }

    // Strip storageRoot prefix if the caller already passed an absolute path under storageRoot
    std::string cleanRoot = storageRoot;
    while (!cleanRoot.empty() && cleanRoot.back() == '/') {
        cleanRoot.pop_back();
    }
    if (!cleanRoot.empty() && collapsed.size() > cleanRoot.size() &&
        collapsed.compare(0, cleanRoot.size(), cleanRoot) == 0 &&
        collapsed[cleanRoot.size()] == '/') {
        collapsed = collapsed.substr(cleanRoot.size() + 1);
    }

    // Strip leading "./" and leading '/'
    while (collapsed.size() >= 2 && collapsed[0] == '.' && collapsed[1] == '/') {
        collapsed.erase(0, 2);
    }
    while (!collapsed.empty() && collapsed.front() == '/') {
        collapsed.erase(0, 1);
    }
    while (!collapsed.empty() && collapsed.back() == '/') {
        collapsed.pop_back();
    }

    if (collapsed.empty()) return {};
    if (collapsed.find("..") != std::string::npos) return {};
    return collapsed;
}

FileCompat& FileCompat::Instance() {
    static FileCompat inst;
    return inst;
}

void FileCompat::Configure(const std::string& storageRoot) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    storageRoot_ = storageRoot;
    while (!storageRoot_.empty() && storageRoot_.back() == '/') {
        storageRoot_.pop_back();
    }
    dirCache_.clear();
    resolvedCache_.clear();
    openedCount_ = 0;
    sampRedirectCount_ = 0;
    FILE_LOGI("Configured FileCompat root='%s'", storageRoot_.c_str());
}

bool FileCompat::InstallHooks(void* nvFOpenFn) {
    if (hooked_ || !nvFOpenFn) return hooked_;
    bool ok = ArmHook::Install(
        nvFOpenFn,
        reinterpret_cast<void*>(&Hook_NvFOpen),
        reinterpret_cast<void**>(&origNvFOpen_));
    hooked_ = ok;
    FILE_LOGI("NvFOpen hook installed=%d (orig=%p)", ok ? 1 : 0, reinterpret_cast<void*>(origNvFOpen_));
    return ok;
}

bool FileCompat::IsRegularFile(const std::string& fullPath) const {
    if (fullPath.empty()) return false;
    struct stat st{};
    return stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string FileCompat::FindEntryInDirLocked(const std::string& dirPath, const std::string& targetLower) {
    auto it = dirCache_.find(dirPath);
    if (it == dirCache_.end()) {
        std::unordered_map<std::string, std::string> entries;
        if (DIR* d = opendir(dirPath.c_str())) {
            while (struct dirent* ent = readdir(d)) {
                if (!ent->d_name[0]) continue;
                if (ent->d_name[0] == '.' &&
                    (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0'))) {
                    continue;
                }
                std::string actual(ent->d_name);
                entries.emplace(ToLowerStr(actual), std::move(actual));
            }
            closedir(d);
        }
        it = dirCache_.emplace(dirPath, std::move(entries)).first;
    }

    auto found = it->second.find(targetLower);
    return (found != it->second.end()) ? found->second : std::string{};
}

std::string FileCompat::ResolveCaseInsensitiveLocked(const std::string& baseDir, const std::string& relPath) {
    if (baseDir.empty() || relPath.empty()) return {};

    // Fast path: exact case regular file on disk
    std::string direct = baseDir + "/" + relPath;
    if (IsRegularFile(direct)) {
        return direct;
    }

    // Walk each path segment case-insensitively using the cached directory maps
    std::string current = baseDir;
    size_t start = 0;
    while (start < relPath.size()) {
        size_t slash = relPath.find('/', start);
        std::string segment = (slash == std::string::npos)
                                  ? relPath.substr(start)
                                  : relPath.substr(start, slash - start);
        if (!segment.empty() && segment != ".") {
            std::string exactNext = current + "/" + segment;
            struct stat st{};
            if (stat(exactNext.c_str(), &st) == 0) {
                current = std::move(exactNext);
            } else {
                std::string matched = FindEntryInDirLocked(current, ToLowerStr(segment));
                if (matched.empty()) return {};
                current.push_back('/');
                current.append(matched);
            }
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }

    return IsRegularFile(current) ? current : std::string{};
}

std::string FileCompat::ResolveExistingRegularFile(const char* dir, const char* filename) {
    if (storageRoot_.empty() || !filename || filename[0] == '\0') return {};

    // If caller passed a direct existing absolute regular file outside storageRoot (e.g. OBB)
    if ((!dir || dir[0] == '\0') && filename[0] == '/') {
        std::string absPath(filename);
        while (!absPath.empty() && absPath.back() == ' ') absPath.pop_back();
        if (IsRegularFile(absPath)) {
            return absPath;
        }
    }

    std::string rel = NormalizeGameRelPath(dir, filename, storageRoot_);
    if (rel.empty()) return {};

    std::string relLower = ToLowerStr(rel);
    std::string baseLower = ToLowerStr(ExtractBaseName(rel));

    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto cached = resolvedCache_.find(relLower);
    if (cached != resolvedCache_.end()) {
        return cached->second;
    }

    std::string resolved;

    // 1. SA-MP multiplayer overrides inside <storageRoot>/SAMP/ take highest priority
    std::string sampOverrideName = MapSampOverrideFileName(baseLower);
    if (!sampOverrideName.empty()) {
        resolved = ResolveCaseInsensitiveLocked(storageRoot_, "SAMP/" + sampOverrideName);
        if (!resolved.empty()) {
            sampRedirectCount_++;
            FILE_LOGI("SA-MP override: '%s' -> '%s'", rel.c_str(), resolved.c_str());
        }
    }

    // 2. Standard case-insensitive lookup inside <storageRoot>
    if (resolved.empty()) {
        resolved = ResolveCaseInsensitiveLocked(storageRoot_, rel);
    }

    // 3. Cross-folder fallbacks for SA-MP Mobile & Mali/Alyn data packs
    if (resolved.empty() && EndsWithStr(baseLower, ".img")) {
        const std::array<std::string, 3> imgAlts = {
            "texdb/" + baseLower,
            "models/" + baseLower,
            "SAMP/" + baseLower,
        };
        for (const auto& alt : imgAlts) {
            resolved = ResolveCaseInsensitiveLocked(storageRoot_, alt);
            if (!resolved.empty()) {
                FILE_LOGI("IMG fallback: '%s' -> '%s'", rel.c_str(), resolved.c_str());
                break;
            }
        }
    }

    if (resolved.empty() && (relLower.rfind("data/", 0) == 0 || relLower.rfind("models/", 0) == 0)) {
        resolved = ResolveCaseInsensitiveLocked(storageRoot_, "SAMP/" + baseLower);
    }

    resolvedCache_.emplace(relLower, resolved);
    return resolved;
}

NvFileHandle* FileCompat::OnNvFOpen(const char* dir, const char* filename, bool isUserFile, bool checkApk) {
    // Reject empty filenames cleanly so ZIPFile::Initialize("") never opens storageRoot directory as a FILE*
    if (!filename || filename[0] == '\0') {
        return nullptr;
    }

    std::string resolved = ResolveExistingRegularFile(dir, filename);
    if (!resolved.empty()) {
        FILE* fp = std::fopen(resolved.c_str(), "rb");
        if (fp != nullptr) {
            auto* handle = static_cast<NvFileHandle*>(std::malloc(sizeof(NvFileHandle)));
            if (!handle) {
                std::fclose(fp);
                return nullptr;
            }
            handle->type = 1; // POSIX FILE* (verified at libGTASA.so 0x266f70)
            handle->ptr = fp;
            openedCount_++;
            nana::core::CrashHandler::RecordFileOpen(filename, true);
            return handle;
        }
    }

    // Fallback to original Rockstar NvFOpen (handles APK assets via AAssetManager_open & OBBs)
    if (origNvFOpen_) {
        NvFileHandle* orig = origNvFOpen_(dir, filename, isUserFile, checkApk);
        if (orig != nullptr) {
            openedCount_++;
            nana::core::CrashHandler::RecordFileOpen(filename, true);
            return orig;
        }
    }

    nana::core::CrashHandler::RecordFileOpen(filename, false);
    FILE_LOGW("NvFOpen miss: dir='%s' file='%s'", dir ? dir : "", filename);
    return nullptr;
}

} // namespace nana::game
