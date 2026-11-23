#include <jni.h>

#ifdef __ANDROID__
#include <android/log.h>
#define F92_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-F92", __VA_ARGS__)
#else
#define F92_LOGI(...) do {} while (0)
#endif

/**
 * Clean independent libFastman92pluginLoader.so bridge for GTA SA Android 2.00 (AML 1.4.0 / FLA 7.6 APK).
 *
 * In libGTASA.so, JNI_OnLoad at 0x00aa8000 executes fastman92 plugin loading, calls
 * PluginLoader_OnLoad(vm, reserved, callerPc), and then natively tail-calls Rockstar's
 * internal JNI_OnLoad at 0x002716e5.
 *
 * Returning JNI_VERSION_1_4 immediately ensures all NvEventQueueActivity native methods,
 * WarGamepad, and NVThreadInit register cleanly without infinite recursive recursion.
 */
extern "C" {

jint PluginLoader_OnLoad(JavaVM* vm, void* /*reserved*/, void* callerPc) {
    F92_LOGI("PluginLoader_OnLoad invoked (vm=%p, callerPc=%p)", vm, callerPc);
    return JNI_VERSION_1_4;
}

void PluginLoader_OnUnload(JavaVM* /*vm*/, void* /*reserved*/) {
}

// Legacy FLA compatibility entry point
void ProcessPluginLoadingWithJava(JavaVM* /*vm*/, void* /*reserved*/,
                                  uint32_t /*gameId*/, void* /*libBase*/) {}

} // extern "C"
