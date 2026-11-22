#pragma once

#include <string>
#include <vector>

namespace nana::game {

struct AmlStatus {
    bool libraryLoaded = false;
    bool amlStarted = false;
    bool interfaceAvailable = false;
    std::string modsDir;
    std::vector<std::string> discoveredMods;
    std::vector<std::string> conflictWarnings;
};

/**
 * Android Mod Loader (AML 1.4.0) compatibility & diagnostics manager.
 *
 * Keeps libAML.so fully functional inside com.nana.samp, inspects its runtime state via
 * RTLD_NOLOAD dlsym (GetInterface, g_pAML, g_szModsDir, g_bAMLStarted), scans for installed
 * AML plugins in the app's mods directory, and flags potential SA-MP hook conflicts.
 */
class AmlCompat {
public:
    static AmlCompat& Instance();

    // Probe libAML.so after System.loadLibrary("AML") and scan <storageRoot>/../mods
    void ProbeAndConfigure(const std::string& storageRoot);

    AmlStatus GetStatus() const { return status_; }
    std::string GetSummaryText() const;

private:
    AmlCompat() = default;
    AmlStatus status_{};
};

} // namespace nana::game
