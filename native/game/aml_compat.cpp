#include "aml_compat.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <sstream>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define AML_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-AML", __VA_ARGS__)
#define AML_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "NanaSAMP-AML", __VA_ARGS__)
#else
#define AML_LOGI(...) do {} while (0)
#define AML_LOGW(...) do {} while (0)
#endif

namespace nana::game {

namespace {

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool EndsWithSo(const std::string& name) {
    return name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0;
}

void ScanModsDirectory(const std::string& dirPath,
                       std::vector<std::string>& outMods,
                       std::vector<std::string>& outWarnings) {
    if (dirPath.empty()) return;
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;

    while (struct dirent* ent = readdir(dir)) {
        if (!ent->d_name[0] || ent->d_name[0] == '.') continue;
        std::string name(ent->d_name);
        std::string lower = ToLowerAscii(name);
        if (!EndsWithSo(lower)) continue;

        outMods.push_back(name);
        // Flag mods known to conflict with SA-MP networking/script hooks
        if (lower.find("samp") != std::string::npos ||
            lower.find("multiplayer") != std::string::npos ||
            lower.find("cleo") != std::string::npos) {
            outWarnings.push_back(
                "Potential SA-MP hook conflict detected with AML mod: " + name);
        }
    }
    closedir(dir);
    std::sort(outMods.begin(), outMods.end());
}

} // namespace

AmlCompat& AmlCompat::Instance() {
    static AmlCompat inst;
    return inst;
}

void AmlCompat::ProbeAndConfigure(const std::string& storageRoot) {
    status_ = AmlStatus{};

#ifdef __ANDROID__
    void* amlHandle = dlopen("libAML.so", RTLD_NOLOAD | RTLD_LAZY);
    if (amlHandle != nullptr) {
        status_.libraryLoaded = true;

        auto* startedFlag = reinterpret_cast<bool*>(dlsym(amlHandle, "g_bAMLStarted"));
        if (startedFlag != nullptr) {
            status_.amlStarted = *startedFlag;
        }

        using GetInterfaceFn = void* (*)(const char*);
        auto getIface = reinterpret_cast<GetInterfaceFn>(dlsym(amlHandle, "GetInterface"));
        if (getIface != nullptr) {
            void* iface = getIface("AML");
            status_.interfaceAvailable = (iface != nullptr);
        } else {
            auto* pAml = reinterpret_cast<void**>(dlsym(amlHandle, "g_pAML"));
            status_.interfaceAvailable = (pAml != nullptr && *pAml != nullptr);
        }

        const char* modsDirBuf = reinterpret_cast<const char*>(dlsym(amlHandle, "g_szModsDir"));
        if (modsDirBuf != nullptr && modsDirBuf[0] != '\0') {
            status_.modsDir = modsDirBuf;
        }
    }
#endif

    // Derive fallback mods dir (<appExternalDir>/mods) from storageRoot (<appExternalDir>/files)
    if (status_.modsDir.empty() && !storageRoot.empty()) {
        std::string clean = storageRoot;
        while (!clean.empty() && clean.back() == '/') clean.pop_back();
        size_t slash = clean.find_last_of('/');
        if (slash != std::string::npos && clean.substr(slash + 1) == "files") {
            status_.modsDir = clean.substr(0, slash) + "/mods";
        } else {
            status_.modsDir = clean + "/mods";
        }
    }

    // Ensure mods directory exists so users can drop compatible AML 1.4.0 plugins
    if (!status_.modsDir.empty()) {
        mkdir(status_.modsDir.c_str(), 0775);
        ScanModsDirectory(status_.modsDir, status_.discoveredMods, status_.conflictWarnings);
    }

    AML_LOGI("AML 1.4.0 probe: loaded=%d started=%d iface=%d modsDir='%s' mods=%zu warnings=%zu",
             status_.libraryLoaded ? 1 : 0,
             status_.amlStarted ? 1 : 0,
             status_.interfaceAvailable ? 1 : 0,
             status_.modsDir.c_str(),
             status_.discoveredMods.size(),
             status_.conflictWarnings.size());
    for (const auto& w : status_.conflictWarnings) {
        AML_LOGW("%s", w.c_str());
    }
}

std::string AmlCompat::GetSummaryText() const {
    std::ostringstream oss;
    if (status_.libraryLoaded) {
        oss << "AML 1.4.0 Active";
        if (status_.interfaceAvailable) oss << " (IAML OK)";
        oss << " | Mods: " << status_.discoveredMods.size();
    } else {
        oss << "AML 1.4.0 Standby";
    }
    if (!status_.conflictWarnings.empty()) {
        oss << " | Warnings: " << status_.conflictWarnings.size();
    }
    return oss.str();
}

} // namespace nana::game
