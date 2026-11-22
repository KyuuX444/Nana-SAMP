#include <jni.h>
#include <mutex>
#include <sstream>
#include <string>

#include "crash_handler.h"
#include "../game/aml_compat.h"
#include "../game/gtasa_world.h"
#include "../net/samp_query.h"

namespace {

JavaVM* gJvm = nullptr;
std::mutex gListenerMutex;
jobject gListenerGlobal = nullptr;
jmethodID gOnStateChanged = nullptr;
jmethodID gOnChatMessage = nullptr;
jmethodID gOnClassSelection = nullptr;
jmethodID gOnShowDialog = nullptr;
jmethodID gOnHideDialog = nullptr;
jmethodID gOnScoreboardUpdated = nullptr;
jmethodID gOnGameText = nullptr;
jmethodID gOnAudioStream = nullptr;
jmethodID gOnTextDrawsChanged = nullptr;
jmethodID gOnTextDrawSelectChanged = nullptr;

class ScopedJniEnv {
public:
    ScopedJniEnv() {
        if (!gJvm) return;
        jint rc = gJvm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            if (gJvm->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
                attached_ = true;
            }
        }
    }
    ~ScopedJniEnv() {
        if (attached_ && gJvm) {
            gJvm->DetachCurrentThread();
        }
    }
    JNIEnv* get() const { return env_; }
private:
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

std::string JStringToStd(JNIEnv* env, jstring js) {
    if (!js) return {};
    const char* chars = env->GetStringUTFChars(js, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(js, chars);
    return out;
}

jstring SafeNewJString(JNIEnv* env, const std::string& raw) {
    // Sanitize to valid ASCII/UTF-8 so JNI NewStringUTF never aborts on legacy Windows-1252 SA-MP strings
    std::string clean;
    clean.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == 0) continue;
        if (c < 0x80) {
            clean.push_back(static_cast<char>(c));
        } else {
            // Convert CP1252/Latin-1 byte to 2-byte UTF-8
            clean.push_back(static_cast<char>(0xC0 | (c >> 6)));
            clean.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return env->NewStringUTF(clean.c_str());
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += ' ';
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

void BindWorldCallbacks() {
    nana::game::UiEventCallbacks cb{};

    cb.onStateChanged = [](int state, const std::string& msg) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnStateChanged) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        jstring jmsg = SafeNewJString(env, msg);
        env->CallVoidMethod(gListenerGlobal, gOnStateChanged, state, jmsg);
        env->DeleteLocalRef(jmsg);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onChatMessage = [](uint32_t rgba, const std::string& msg) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnChatMessage) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        jstring jmsg = SafeNewJString(env, msg);
        env->CallVoidMethod(gListenerGlobal, gOnChatMessage, static_cast<jint>(rgba), jmsg);
        env->DeleteLocalRef(jmsg);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onClassSelection = [](bool visible, int classId, int skinId) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnClassSelection) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        env->CallVoidMethod(gListenerGlobal, gOnClassSelection, visible ? JNI_TRUE : JNI_FALSE, classId, skinId);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onShowDialog = [](const nana::net::DialogData& dlg) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnShowDialog) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        jstring jTitle = SafeNewJString(env, dlg.title);
        jstring jInfo = SafeNewJString(env, dlg.info);
        jstring jBtn1 = SafeNewJString(env, dlg.button1);
        jstring jBtn2 = SafeNewJString(env, dlg.button2);
        env->CallVoidMethod(gListenerGlobal, gOnShowDialog,
                            static_cast<jint>(dlg.dialogId),
                            static_cast<jint>(dlg.style),
                            jTitle, jInfo, jBtn1, jBtn2);
        env->DeleteLocalRef(jTitle);
        env->DeleteLocalRef(jInfo);
        env->DeleteLocalRef(jBtn1);
        env->DeleteLocalRef(jBtn2);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onHideDialog = []() {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnHideDialog) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        env->CallVoidMethod(gListenerGlobal, gOnHideDialog);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onScoreboardUpdated = [](const std::vector<nana::net::ScoreboardEntry>&) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnScoreboardUpdated) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        env->CallVoidMethod(gListenerGlobal, gOnScoreboardUpdated);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onGameText = [](uint32_t style, uint32_t timeMs, const std::string& text) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnGameText) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        jstring jtext = SafeNewJString(env, text);
        env->CallVoidMethod(gListenerGlobal, gOnGameText, static_cast<jint>(style), static_cast<jint>(timeMs), jtext);
        env->DeleteLocalRef(jtext);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onAudioStream = [](const nana::net::AudioStreamState& audio) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnAudioStream) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        jstring jurl = SafeNewJString(env, audio.url);
        env->CallVoidMethod(gListenerGlobal, gOnAudioStream, audio.playing ? JNI_TRUE : JNI_FALSE, jurl);
        env->DeleteLocalRef(jurl);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onTextDrawsChanged = []() {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnTextDrawsChanged) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        env->CallVoidMethod(gListenerGlobal, gOnTextDrawsChanged);
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    cb.onTextDrawSelectChanged = [](bool enabled, uint32_t hoverColor) {
        std::lock_guard<std::mutex> lock(gListenerMutex);
        if (!gListenerGlobal || !gOnTextDrawSelectChanged) return;
        ScopedJniEnv senv;
        JNIEnv* env = senv.get();
        if (!env) return;
        env->CallVoidMethod(gListenerGlobal, gOnTextDrawSelectChanged,
                            enabled ? JNI_TRUE : JNI_FALSE, static_cast<jint>(hoverColor));
        if (env->ExceptionCheck()) env->ExceptionClear();
    };

    nana::game::GtaSaWorld::Instance().SetCallbacks(std::move(cb));
}

} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeInstallCrashHandler(JNIEnv* env, jobject, jstring logPath) {
    nana::core::CrashHandler::Install(JStringToStd(env, logPath));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeSetListener(JNIEnv* env, jobject, jobject listener) {
    std::lock_guard<std::mutex> lock(gListenerMutex);
    if (gListenerGlobal) {
        env->DeleteGlobalRef(gListenerGlobal);
        gListenerGlobal = nullptr;
    }
    gOnStateChanged = nullptr;
    gOnChatMessage = nullptr;
    gOnClassSelection = nullptr;
    gOnShowDialog = nullptr;
    gOnHideDialog = nullptr;
    gOnScoreboardUpdated = nullptr;
    gOnGameText = nullptr;
    gOnAudioStream = nullptr;
    gOnTextDrawsChanged = nullptr;
    gOnTextDrawSelectChanged = nullptr;

    if (listener) {
        gListenerGlobal = env->NewGlobalRef(listener);
        jclass cls = env->GetObjectClass(listener);
        gOnStateChanged = env->GetMethodID(cls, "onNativeStateChanged", "(ILjava/lang/String;)V");
        gOnChatMessage = env->GetMethodID(cls, "onNativeChatMessage", "(ILjava/lang/String;)V");
        gOnClassSelection = env->GetMethodID(cls, "onNativeClassSelection", "(ZII)V");
        gOnShowDialog = env->GetMethodID(cls, "onNativeShowDialog",
                                         "(IILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        gOnHideDialog = env->GetMethodID(cls, "onNativeHideDialog", "()V");
        gOnScoreboardUpdated = env->GetMethodID(cls, "onNativeScoreboardUpdated", "()V");
        gOnGameText = env->GetMethodID(cls, "onNativeGameText", "(IILjava/lang/String;)V");
        gOnAudioStream = env->GetMethodID(cls, "onNativeAudioStream", "(ZLjava/lang/String;)V");
        gOnTextDrawsChanged = env->GetMethodID(cls, "onNativeTextDrawsChanged", "()V");
        gOnTextDrawSelectChanged = env->GetMethodID(cls, "onNativeTextDrawSelectChanged", "(ZI)V");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
    }
    BindWorldCallbacks();
}

JNIEXPORT jboolean JNICALL
Java_com_nana_samp_core_NativeClient_nativeInitGtaHooks(JNIEnv* env, jobject,
                                                        jstring storageRoot,
                                                        jint preferredTexFormat,
                                                        jstring gpuRenderer,
                                                        jstring gpuExtensions) {
    bool ok = nana::game::GtaSaWorld::Instance().InitializeHooks(
        JStringToStd(env, storageRoot),
        static_cast<int>(preferredTexFormat),
        JStringToStd(env, gpuRenderer),
        JStringToStd(env, gpuExtensions));
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeConnect(JNIEnv* env, jobject,
                                                   jstring host, jint port,
                                                   jstring nickname, jstring password) {
    nana::game::GtaSaWorld::Instance().ConnectAsync(
        JStringToStd(env, host),
        static_cast<uint16_t>(port),
        JStringToStd(env, nickname),
        JStringToStd(env, password));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeDisconnect(JNIEnv*, jobject) {
    nana::game::GtaSaWorld::Instance().DisconnectAsync();
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeReconnect(JNIEnv*, jobject) {
    nana::game::GtaSaWorld::Instance().ReconnectAsync();
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeSendChat(JNIEnv* env, jobject, jstring text) {
    nana::game::GtaSaWorld::Instance().SendChatAsync(JStringToStd(env, text));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeRequestClassDelta(JNIEnv*, jobject, jint delta) {
    nana::game::GtaSaWorld::Instance().RequestClassDeltaAsync(static_cast<int>(delta));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeRequestSpawn(JNIEnv*, jobject) {
    nana::game::GtaSaWorld::Instance().RequestSpawnAsync();
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeSendDialogResponse(JNIEnv* env, jobject,
                                                              jint dialogId, jint button,
                                                              jint listItem, jstring inputText) {
    nana::game::GtaSaWorld::Instance().SendDialogResponseAsync(
        static_cast<int16_t>(dialogId),
        static_cast<uint8_t>(button),
        static_cast<int16_t>(listItem),
        JStringToStd(env, inputText));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeRequestScoreboard(JNIEnv*, jobject) {
    nana::game::GtaSaWorld::Instance().RequestScoreboardAsync();
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeClickPlayer(JNIEnv*, jobject, jint playerId) {
    nana::game::GtaSaWorld::Instance().SendClickPlayerAsync(static_cast<uint16_t>(playerId));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeClickTextDraw(JNIEnv*, jobject, jint textDrawId) {
    nana::game::GtaSaWorld::Instance().SendClickTextDrawAsync(static_cast<uint16_t>(textDrawId));
}

JNIEXPORT void JNICALL
Java_com_nana_samp_core_NativeClient_nativeCancelTextDrawSelect(JNIEnv*, jobject) {
    nana::game::GtaSaWorld::Instance().CancelTextDrawSelectAsync();
}

JNIEXPORT jstring JNICALL
Java_com_nana_samp_core_NativeClient_nativeGetSnapshotJson(JNIEnv* env, jobject) {
    auto s = nana::game::GtaSaWorld::Instance().GetSnapshot();
    std::ostringstream oss;
    oss << "{"
        << "\"state\":" << s.connectionState << ","
        << "\"status\":\"" << JsonEscape(s.statusMessage) << "\","
        << "\"serverName\":\"" << JsonEscape(s.serverName) << "\","
        << "\"playerId\":" << s.localPlayerId << ","
        << "\"ping\":" << s.pingMs << ","
        << "\"players\":" << s.playerCount << ","
        << "\"score\":" << s.score << ","
        << "\"money\":" << s.money << ","
        << "\"inClassSelect\":" << (s.inClassSelection ? "true" : "false") << ","
        << "\"classId\":" << s.currentClassId << ","
        << "\"worldReady\":" << (s.gameWorldReady ? "true" : "false") << ","
        << "\"gameState\":" << s.gameState << ","
        << "\"loadingPercent\":" << s.loadingPercent << ","
        << "\"texDbLoaded\":" << s.texDbLoadedCount << ","
        << "\"ticks\":" << s.doGameStateTicks << ","
        << "\"amlLoaded\":" << (s.amlLoaded ? "true" : "false") << ","
        << "\"amlMods\":" << s.amlModCount << ","
        << "\"amlSummary\":\"" << JsonEscape(s.amlSummary) << "\","
        << "\"health\":" << s.localHealth << ","
        << "\"armour\":" << s.localArmour << ","
        << "\"x\":" << s.localX << ","
        << "\"y\":" << s.localY << ","
        << "\"z\":" << s.localZ
        << "}";
    return SafeNewJString(env, oss.str());
}

JNIEXPORT jstring JNICALL
Java_com_nana_samp_core_NativeClient_nativeGetScoreboardJson(JNIEnv* env, jobject) {
    auto list = nana::game::GtaSaWorld::Instance().GetScoreboardSnapshot();
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"id\":" << list[i].playerId
            << ",\"name\":\"" << JsonEscape(list[i].nickname) << "\""
            << ",\"score\":" << list[i].score
            << ",\"ping\":" << list[i].ping
            << ",\"color\":" << list[i].colorRgba
            << "}";
    }
    oss << "]";
    return SafeNewJString(env, oss.str());
}

JNIEXPORT jstring JNICALL
Java_com_nana_samp_core_NativeClient_nativeGetTextDrawsJson(JNIEnv* env, jobject) {
    auto list = nana::game::GtaSaWorld::Instance().GetTextDrawsSnapshot();
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& td = list[i];
        int packedFlags = (td.useBox ? 0x01 : 0) |
                          (td.alignment == 1 ? 0x02 : (td.alignment == 3 ? 0x04 : (td.alignment == 2 ? 0x08 : 0))) |
                          (td.proportional ? 0x10 : 0);
        if (i > 0) oss << ",";
        oss << "{\"id\":" << td.id
            << ",\"flags\":" << packedFlags
            << ",\"letterW\":" << td.letterWidth
            << ",\"letterH\":" << td.letterHeight
            << ",\"letterColor\":" << td.letterColorAbgr
            << ",\"lineW\":" << td.lineWidth
            << ",\"lineH\":" << td.lineHeight
            << ",\"boxColor\":" << td.boxColorAbgr
            << ",\"shadow\":" << static_cast<int>(td.shadow)
            << ",\"outline\":" << static_cast<int>(td.outline)
            << ",\"bgColor\":" << td.bgColorAbgr
            << ",\"style\":" << static_cast<int>(td.style)
            << ",\"selectable\":" << (td.selectable ? "true" : "false")
            << ",\"x\":" << td.x
            << ",\"y\":" << td.y
            << ",\"modelId\":" << td.modelId
            << ",\"text\":\"" << JsonEscape(td.text) << "\""
            << "}";
    }
    oss << "]";
    return SafeNewJString(env, oss.str());
}

JNIEXPORT jstring JNICALL
Java_com_nana_samp_core_NativeClient_nativeGetAmlStatusJson(JNIEnv* env, jobject) {
    auto st = nana::game::AmlCompat::Instance().GetStatus();
    std::ostringstream oss;
    oss << "{"
        << "\"loaded\":" << (st.libraryLoaded ? "true" : "false") << ","
        << "\"started\":" << (st.amlStarted ? "true" : "false") << ","
        << "\"interfaceOk\":" << (st.interfaceAvailable ? "true" : "false") << ","
        << "\"modsDir\":\"" << JsonEscape(st.modsDir) << "\","
        << "\"mods\":[";
    for (size_t i = 0; i < st.discoveredMods.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << JsonEscape(st.discoveredMods[i]) << "\"";
    }
    oss << "],\"warnings\":[";
    for (size_t i = 0; i < st.conflictWarnings.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << JsonEscape(st.conflictWarnings[i]) << "\"";
    }
    oss << "]}";
    return SafeNewJString(env, oss.str());
}

JNIEXPORT jstring JNICALL
Java_com_nana_samp_core_NativeClient_nativeQueryServerInfoJson(JNIEnv* env, jobject,
                                                               jstring host, jint port, jint timeoutMs) {
    auto info = nana::net::SampQuery::QueryInfo(
        JStringToStd(env, host),
        static_cast<uint16_t>(port),
        static_cast<int>(timeoutMs));
    std::ostringstream oss;
    oss << "{"
        << "\"reachable\":" << (info.success ? "true" : "false") << ","
        << "\"passworded\":" << (info.hasPassword ? "true" : "false") << ","
        << "\"players\":" << info.players << ","
        << "\"maxPlayers\":" << info.maxPlayers << ","
        << "\"pingMs\":" << info.pingMs << ","
        << "\"hostname\":\"" << JsonEscape(info.hostname) << "\","
        << "\"gamemode\":\"" << JsonEscape(info.gamemode) << "\","
        << "\"language\":\"" << JsonEscape(info.language) << "\""
        << "}";
    return SafeNewJString(env, oss.str());
}

} // extern "C"
