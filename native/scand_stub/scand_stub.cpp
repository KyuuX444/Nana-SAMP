#include <cstdint>

/**
 * Clean from-scratch Social Club symbol shim (libSCAnd.so) required by Rockstar's
 * GTA San Andreas Android 2.10 ARM64 engine (libGTASA.so).
 *
 * Satisfies all 24 undefined Social Club symbols referenced by libGTASA.so without
 * bundling proprietary Social Club SDK libraries, and immediately forwards
 * EnterSocialCLub() to EnterGameFromSCFunc when registered.
 */

extern "C" {

void (*EnterGameFromSCFunc)() = nullptr;
bool SigningOutfromApp = false;
bool hasTouchScreen = true;
int RTPrioLevel = 0;

int GetCloudUploadResult() { return 0; }
int64_t GetRockstarID() { return 0; }
bool IsProfileStatsBusy() { return false; }

int cloudGetBufferLen() { return 0; }
void* cloudGetBufferPtr() { return nullptr; }
int cloudGetFree() { return 0; }
bool cloudIsBusy() { return false; }
int cloudModAddWatch(const char*) { return 0; }
int cloudModFind(const char*) { return -1; }
void cloudModReset() {}
int cloudStartCheckMod(const char*) { return 0; }
int cloudStartDownload(const char*) { return 0; }
int cloudStartUpload(const char*, const void*, int) { return 0; }
void scmainUpdate() {}

} // extern "C"

// C++ mangled symbols expected by libGTASA.so
void SetJNEEnvFunc(void* (*/*fn*/)()) {}

bool IsSCSignedIn() {
    return false;
}

void EnterSocialCLub() {
    if (EnterGameFromSCFunc != nullptr) {
        EnterGameFromSCFunc();
    }
}

void ProfileStatsSend(const char*, int) {}
void TelemetryDataSend(const char*, const char*) {}
void TelemetryDataFlush() {}
