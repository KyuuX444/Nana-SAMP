#include "crash_handler.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
#include <ucontext.h>
#endif

namespace nana::core {

namespace {

constexpr size_t kFileRingSize = 6;
constexpr size_t kFileEntryLen = 96;

char gCrashLogPath[512] = {0};
struct sigaction gOldActions[NSIG]{};
std::atomic<int> gLastGameState{-1};
std::atomic<uint32_t> gFileRingIdx{0};
char gFileRing[kFileRingSize][kFileEntryLen] = {{0}};

#ifdef __ANDROID__
const char* BaseName(const char* path) {
    if (!path) return "?";
    const char* slash = std::strrchr(path, '/');
    return slash ? (slash + 1) : path;
}
#endif

void SignalHandler(int sig, siginfo_t* info, void* ctx) {
    char msg[1536];
    int n = 0;
    int lastState = gLastGameState.load(std::memory_order_relaxed);

#if defined(__ANDROID__) && defined(__arm__)
    uintptr_t pc = 0;
    uintptr_t lr = 0;
    uintptr_t sp = 0;
    if (ctx) {
        auto* uc = static_cast<ucontext_t*>(ctx);
        pc = static_cast<uintptr_t>(uc->uc_mcontext.arm_pc);
        lr = static_cast<uintptr_t>(uc->uc_mcontext.arm_lr);
        sp = static_cast<uintptr_t>(uc->uc_mcontext.arm_sp);
    }
    Dl_info pcInfo{};
    Dl_info lrInfo{};
    const char* pcLib = "?";
    const char* pcSym = "?";
    uintptr_t pcOff = 0;
    if (pc != 0 && dladdr(reinterpret_cast<void*>(pc), &pcInfo) != 0) {
        pcLib = BaseName(pcInfo.dli_fname);
        if (pcInfo.dli_sname) pcSym = pcInfo.dli_sname;
        if (pcInfo.dli_fbase) pcOff = pc - reinterpret_cast<uintptr_t>(pcInfo.dli_fbase);
    }
    const char* lrLib = "?";
    const char* lrSym = "?";
    uintptr_t lrOff = 0;
    if (lr != 0 && dladdr(reinterpret_cast<void*>(lr), &lrInfo) != 0) {
        lrLib = BaseName(lrInfo.dli_fname);
        if (lrInfo.dli_sname) lrSym = lrInfo.dli_sname;
        if (lrInfo.dli_fbase) lrOff = lr - reinterpret_cast<uintptr_t>(lrInfo.dli_fbase);
    }
    n = std::snprintf(
        msg, sizeof(msg),
        "Nana SAMP native crash: sig=%d code=%d addr=%p state=%d\n"
        "pc=0x%08x (%s+0x%x %s)\n"
        "lr=0x%08x (%s+0x%x %s) sp=0x%08x\n",
        sig, info ? info->si_code : 0, info ? info->si_addr : nullptr, lastState,
        static_cast<unsigned>(pc), pcLib, static_cast<unsigned>(pcOff), pcSym,
        static_cast<unsigned>(lr), lrLib, static_cast<unsigned>(lrOff), lrSym,
        static_cast<unsigned>(sp));
#else
    (void)ctx;
    n = std::snprintf(msg, sizeof(msg),
                      "Nana SAMP native crash: sig=%d code=%d addr=%p state=%d\n",
                      sig, info ? info->si_code : 0, info ? info->si_addr : nullptr, lastState);
#endif

    // Append last opened/missed files for instant data-loading diagnostics
    if (n > 0 && static_cast<size_t>(n) < sizeof(msg) - 32) {
        uint32_t head = gFileRingIdx.load(std::memory_order_relaxed);
        int wrote = std::snprintf(msg + n, sizeof(msg) - static_cast<size_t>(n), "Recent files:\n");
        if (wrote > 0) n += wrote;
        for (size_t i = 0; i < kFileRingSize; ++i) {
            uint32_t slot = (head + static_cast<uint32_t>(i)) % static_cast<uint32_t>(kFileRingSize);
            if (gFileRing[slot][0] != '\0' && static_cast<size_t>(n) < sizeof(msg) - 8) {
                wrote = std::snprintf(msg + n, sizeof(msg) - static_cast<size_t>(n),
                                      "  %s\n", gFileRing[slot]);
                if (wrote > 0) n += wrote;
            }
        }
    }

#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_FATAL, "NanaSAMP-Crash", "%s", msg);
#endif
    if (gCrashLogPath[0] != '\0' && n > 0) {
        int fd = open(gCrashLogPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            size_t len = static_cast<size_t>(n);
            if (len >= sizeof(msg)) len = sizeof(msg) - 1;
            (void)write(fd, msg, len);
            close(fd);
        }
    }

    sigaction(sig, &gOldActions[sig], nullptr);
    raise(sig);
}

} // namespace

void CrashHandler::Install(const std::string& logDirectory) {
    if (!logDirectory.empty()) {
        std::string clean = logDirectory;
        while (!clean.empty() && clean.back() == '/') {
            clean.pop_back();
        }
        std::snprintf(gCrashLogPath, sizeof(gCrashLogPath), "%s/nana_crash.log", clean.c_str());
    }
    const int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
    struct sigaction sa{};
    sa.sa_sigaction = &SignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    for (int s : signals) {
        sigaction(s, &sa, &gOldActions[s]);
    }
}

void CrashHandler::SetGameState(int gameState) {
    gLastGameState.store(gameState, std::memory_order_relaxed);
}

void CrashHandler::RecordFileOpen(const char* path, bool ok) {
    if (!path || !path[0]) return;
    uint32_t slot = gFileRingIdx.fetch_add(1, std::memory_order_relaxed) % static_cast<uint32_t>(kFileRingSize);
    std::snprintf(gFileRing[slot], kFileEntryLen, "[%s] %s", ok ? "OK" : "MISS", path);
}

} // namespace nana::core
