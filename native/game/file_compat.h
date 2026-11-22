#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nana::game {

/**
 * Exact 8-byte layout returned by Rockstar's _Z7NvFOpenPKcS0_bb in libGTASA.so 2.00 ARM32
 * (verified at 0x266e4a..0x266f76: malloc(8), type=0 for AAsset*, type=1 for FILE*).
 */
struct NvFileHandle {
    int type;
    void* ptr;
};

/**
 * Normalizes a GTA SA relative path:
 *  - converts '\\' to '/'
 *  - strips leading "./" and leading '/'
 *  - collapses repeated "//"
 *  - trims trailing ASCII spaces
 * Returns empty string if input is null, empty, or contains path traversal ("..").
 */
std::string NormalizeGameRelPath(const char* dir, const char* filename, const std::string& storageRoot);

/**
 * Resolves a GTA SA file request against an unpacked Android game data root:
 *  1. Checks <storageRoot>/SAMP/<basename> (case-insensitively) for SA-MP multiplayer override files
 *     (main.scm, script.img, gta.dat, peds.ide, vehicles.ide, handling.cfg, weapon.dat,
 *      object.dat, default.dat, default.ide, pedstats.dat, timecyc.dat).
 *  2. Resolves <storageRoot>/<relPath> with exact-match fast path + cached case-insensitive traversal.
 *  3. Checks cross-folder fallbacks (texdb/<x>.img <-> models/<x>.img <-> SAMP/<x>.img,
 *     data/script/<x> <-> SAMP/<x>, data/<x> <-> SAMP/<x>).
 *  4. Strictly verifies S_ISREG (regular file) so directories (e.g. empty filename from ZIPFile::Initialize(""))
 *     are NEVER returned as openable files.
 */
class FileCompat {
public:
    static FileCompat& Instance();

    void Configure(const std::string& storageRoot);
    bool InstallHooks(void* nvFOpenFn);

    std::string ResolveExistingRegularFile(const char* dir, const char* filename);
    NvFileHandle* OnNvFOpen(const char* dir, const char* filename, bool isUserFile, bool checkApk);

    int GetOpenedFileCount() const { return openedCount_; }
    int GetRedirectedSampCount() const { return sampRedirectCount_; }

private:
    FileCompat() = default;

    std::string ResolveCaseInsensitiveLocked(const std::string& baseDir, const std::string& relPath);
    std::string FindEntryInDirLocked(const std::string& dirPath, const std::string& targetLower);
    bool IsRegularFile(const std::string& fullPath) const;

    std::string storageRoot_;
    bool hooked_ = false;
    int openedCount_ = 0;
    int sampRedirectCount_ = 0;

    std::mutex cacheMutex_;
    // dirPath -> (lowercaseEntryName -> actualEntryName)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dirCache_;
    // normalized lowercase relPath -> resolved absolute path (empty if known missing)
    std::unordered_map<std::string, std::string> resolvedCache_;

    using NvFOpenFn = NvFileHandle* (*)(const char* dir, const char* filename, bool isUserFile, bool checkApk);
    NvFOpenFn origNvFOpen_ = nullptr;
};

} // namespace nana::game
