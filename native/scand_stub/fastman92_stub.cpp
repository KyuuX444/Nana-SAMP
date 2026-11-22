#include <dlfcn.h>
#include <jni.h>
#include <cstdint>

#ifdef __ANDROID__
#include <android/log.h>
#define F92_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-F92", __VA_ARGS__)
#define F92_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NanaSAMP-F92", __VA_ARGS__)
#else
#define F92_LOGI(...) do {} while (0)
#define F92_LOGE(...) do {} while (0)
#endif

/**
 * Clean independent libFastman92pluginLoader.so bridge for GTA SA Android 2.00 (AML 1.4.0 / FLA 7.6 APK).
 *
 * Verified from libGTASA.so (.fastman92.code at 0xda8000):
 *   JNI_OnLoad(vm, reserved) puts caller PC (inside libGTASA.so) in r2 and tail-calls:
 *     jint PluginLoader_OnLoad(JavaVM* vm, void* reserved, void* callerPcInLibGTASA)
 *   Original Rockstar JNI_OnLoad is exported by libGTASA.so as:
 *     jint Original_JNI_OnLoad(JavaVM* vm, void* reserved)        @ 0x002716e5
 *     void Original_JNI_OnUnload(JavaVM* vm, void* reserved)      @ 0x0025f7ed
 *
 * This bridge forwards directly to Original_JNI_OnLoad / Original_JNI_OnUnload so all 13
 * NvEventQueueActivity native methods, WarGamepad, WarBilling, and NVThreadInit register
 * cleanly inside com.nana.samp without conflicting with AML 1.4.0 or requiring FLA INI files.
 */
namespace {

using JniOnLoadFn = jint (*)(JavaVM*, void*);
using JniOnUnloadFn = void (*)(JavaVM*, void*);

void* ResolveGtaSaHandle(void* callerPc) {
    if (callerPc != nullptr) {
        Dl_info info{};
        if (dladdr(callerPc, &info) != 0 && info.dli_fname != nullptr) {
            void* h = dlopen(info.dli_fname, RTLD_NOLOAD | RTLD_LAZY);
            if (h != nullptr) {
                return h;
            }
        }
    }
    void* h = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_LAZY);
    if (!h) {
        h = dlopen("libGTASA.so", RTLD_LAZY);
    }
    return h;
}

} // namespace

extern "C" {

jint PluginLoader_OnLoad(JavaVM* vm, void* reserved, void* callerPc) {
    F92_LOGI("PluginLoader_OnLoad invoked (vm=%p, callerPc=%p)", vm, callerPc);
    void* gtaHandle = ResolveGtaSaHandle(callerPc);
    if (gtaHandle != nullptr) {
        auto origOnLoad = reinterpret_cast<JniOnLoadFn>(dlsym(gtaHandle, "Original_JNI_OnLoad"));
        if (origOnLoad != nullptr) {
            jint ver = origOnLoad(vm, reserved);
            F92_LOGI("Forwarded to libGTASA.so Original_JNI_OnLoad -> 0x%x", ver);
            return ver;
        }
        F92_LOGE("Original_JNI_OnLoad symbol not found in libGTASA.so");
    } else {
        F92_LOGE("Failed to obtain libGTASA.so handle in PluginLoader_OnLoad");
    }
    return JNI_VERSION_1_6;
}

void PluginLoader_OnUnload(JavaVM* vm, void* reserved) {
    void* gtaHandle = ResolveGtaSaHandle(nullptr);
    if (gtaHandle != nullptr) {
        auto origOnUnload = reinterpret_cast<JniOnUnloadFn>(dlsym(gtaHandle, "Original_JNI_OnUnload"));
        if (origOnUnload != nullptr) {
            origOnUnload(vm, reserved);
        }
    }
}

// Legacy FLA compatibility entry point
void ProcessPluginLoadingWithJava(JavaVM* /*vm*/, void* /*reserved*/,
                                  uint32_t /*gameId*/, void* /*libBase*/) {}

} // extern "C"
