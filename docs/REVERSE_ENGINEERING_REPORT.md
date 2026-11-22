# Nana SAMP — Reverse Engineering & Architecture Report

> All findings in this document were obtained directly by downloading, validating, extracting, and statically/ELF-analyzing both required sources (`Data_Sem_Mods_Alyn_-_Mali.zip` and `APK_GTA_SA_2.00_AML_1.4.0.zip`). No symbols, offsets, or binary dependencies were guessed.

---

## 1. Source Inventory & Validation

| Source | Archive Name | Verified Size | ZIP Entries | CRC Test |
|---|---|---|---|---|
| **Source 1 — Data SA-MP Mobile Alyn** | `Data_Sem_Mods_Alyn_-_Mali.zip` | 357,923,188 bytes (341.34 MiB) | 358 | `testzip() == None` (PASS) |
| **Source 2 — GTA SA 2.00 AML 1.4.0** | `APK_GTA_SA_2.00_AML_1.4.0.zip` | 18,424,698 bytes (17.57 MiB) | 1 (`GTASA 2.00 AML1.4.0 FLA7.6 Unprot.apk`, 18,863,325 bytes) | `testzip() == None` (PASS) |

Automated reproducible download, validation, extraction, and staging are implemented in `scripts/fetch_and_prepare_sources.py` and `scripts/analyze_sources.py`.

---

## 2. Reverse Engineering: GTA San Andreas Android 2.00 AML 1.4.0 APK

### 2.1 APK Manifest & Entry Points (`aapt2` + `jadx`)
- **Package**: `com.rockstargames.gtasa` (`versionCode=26`, `versionName=2.00`)
- **SDK**: `minSdk=24`, `targetSdk=26`, `compileSdk=34`
- **Graphics**: `uses-gl-es=0x30000` (OpenGL ES 2.0/3.0)
- **ABI**: **`armeabi-v7a` (ARM32 ELF32)** exclusively
- **Native Library Boot Sequence** (verified from `com.rockstargames.gtasa.GTASA` static initializer):
  ```java
  System.loadLibrary("ImmEmulatorJ");
  System.loadLibrary("SCAnd");
  System.loadLibrary("GTASA");
  System.loadLibrary("threadfix");
  System.loadLibrary("AML");
  ```
- **JNI Bridge Contract**:
  - `com.nvidia.devtech.NvEventQueueActivity` (13 methods registered via `RegisterNatives` in `Original_JNI_OnLoad`: `init`, `touchEvent`, `multiTouchEvent`, `multiTouchEvent4`, `keyEvent`, `accelerometerEvent`, `cleanup`, `setWindowSize`, `quitAndWait`, `resumeEvent`, `pauseEvent`, `imeClosed`, `lowMemoryEvent`; plus exported `Java_com_nvidia_devtech_NvEventQueueActivity_jniNvAPKInit`)
  - `com.nvidia.devtech.NvUtil` (`getInstance`, `getParameter("STORAGE_ROOT")`, `getParameter("STORAGE_ROOT_BASE")`, `setAppLocalValue`)
  - `com.wardrumstudios.utils.{WarBase, WarGamepad, WarMedia, WarBilling, WarHttp, WarService, WarAdMob}`

### 2.2 Native Libraries (`lib/armeabi-v7a/`)

| Library | Size (bytes) | Role | `DT_NEEDED` | Integration in Nana SAMP (`com.nana.samp`) |
|---|---|---|---|---|
| `libGTASA.so` | 11,707,712 | Core GTA San Andreas 2.00 RenderWare / OpenGL ES engine | `libSCAnd.so`, `libImmEmulatorJ.so`, `libFastman92pluginLoader.so`, Android system libs (`EGL`, `GLESv2`, `OpenSLES`, etc.) | **Preserved** (staged at build time from Source 2; hooked natively by `libnana_samp.so`) |
| `libAML.so` | 727,560 | Android Mod Loader 1.4.0 (RusJJ) | `liblog.so`, `libdl.so`, `libandroid.so`, `libc.so`, `libm.so` | **Preserved** (loaded cleanly after `libGTASA.so` & `libthreadfix.so`; monitored by `AmlCompat`) |
| `libthreadfix.so` | 173,664 | Multi-core CPU affinity stability fix (`sched_setaffinity` over `/proc/self/task`) | `libdl.so`, `libstdc++.so`, `libm.so`, `libc.so` | **Preserved** |
| `libImmEmulatorJ.so` | 1,307,640 | Immersion TouchSense haptics runtime | `libdl.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so` | **Preserved** |
| `libTouchSenseSDK.so` | 302,396 | TouchSense SDK companion library | `libdl.so`, `liblog.so`, `libstdc++.so`, `libm.so`, `libc.so` | **Preserved** |
| `libSCAnd.so` | 5,223,872 | Rockstar Social Club SDK | `libGLESv1_CM.so`, `libz.so`, etc. | **Replaced with clean independent C++ shim** (`native/scand_stub/scand_stub.cpp`) satisfying all 24 `UND` symbols and bypassing Social Club login directly via `EnterGameFromSCFunc()` |
| `libFastman92pluginLoader.so` | 157,308 | FLA 7.6 plugin loader hooking `libGTASA.so` `JNI_OnLoad` | `liblog.so`, `libandroid.so`, `libdl.so`, `libc.so` | **Replaced with clean independent C++ bridge** (`native/scand_stub/fastman92_stub.cpp`) forwarding `PluginLoader_OnLoad` → `Original_JNI_OnLoad` (`0x002716e5`) and `PluginLoader_OnUnload` → `Original_JNI_OnUnload` (`0x0025f7ed`) without requiring `com.fastman92.main_activity_launcher` or FLA INI files |
| `libplugin_fastman92limitAdjuster.so` | 981,336 | Fastman92 Limit Adjuster 7.6 | `libFastman92pluginLoader.so`, Android system libs | **Not required** (verified `CModelInfo::ms_modelInfoPtrs` = `80,000` bytes = `20,000` model IDs `0..19999`, covering all 1,445 entries of `SAMP/SAMP.ide` whose max ID is `19999`) |

### 2.3 Critical Binary Finding: `libGTASA.so` `JNI_OnLoad` Trampoline
Disassembly of `.fastman92.code` at `0x00da8000` (`libGTASA.so`):
```asm
00da8000 <JNI_OnLoad>:
  da8000: mov  r2, pc               ; caller PC inside libGTASA.so
  da8002: ldr  r3, [pc, #0xc]       ; offset -> .fastman92.got (0x00ee1bc0)
  da8004: add  r3, pc
  da8006: ldr  r3, [r3]             ; PluginLoader_OnLoad
  da8008: bx   r3                   ; tail-call PluginLoader_OnLoad(vm, reserved, callerPc)
```
Rockstar's original `JNI_OnLoad` is exported as `Original_JNI_OnLoad` (`0x002716e5`). Our clean `libFastman92pluginLoader.so` resolves `Original_JNI_OnLoad` via `dladdr(callerPc)` + `dlopen(..., RTLD_NOLOAD)` + `dlsym`, invokes it, and returns `JNI_VERSION_1_4` (`0x10004`), guaranteeing full registration of all `NvEventQueueActivity` JNI methods.

### 2.4 Verified ARM32 Symbols & Struct Offsets in `libGTASA.so`
- **Globals**: `gGameState` (`0x00a987c8`), `CGame::currArea` (`0x0095957c`), `TheCamera` (`0x00951fa8`), `gMobileMenu` (`0x006e006c`), `CLoadingScreen::m_PercentLoaded` (`0x009920c0`), `StorageRootBuffer` (`0x006d687c`), `StorageBaseRootBuffer` (`0x006d9ed1`), `RQCaps` (`0x006b8b9c`), `CWorld::Players` (`0x0096b69c`), `CModelInfo::ms_modelInfoPtrs` (`0x0091dcb8`, 80,000 bytes).
- **Verified Mangled C++ Methods**:
  - Lifecycle: `_Z11DoGameStatev` (`0x005e4715`), `_ZN5CGame7ProcessEv` (`0x003f3fb1`), `_ZN15StartGameScreen14OnNewGameCheckEv` (`0x002a7201`)
  - Files & Textures: `_Z7NvFOpenPKcS0_bb` (`0x00266e2d`), `_ZN22TextureDatabaseRuntime4LoadEPKcb21TextureDatabaseFormat` (`0x001ea8e5`), `_ZN22TextureDatabaseRuntime8RegisterEPS_` (`0x001e9bc9`)
  - Streaming & IMG: `_ZN10CStreaming13InitImageListEv` (`0x002cf611`), `_ZN10CStreaming14AddImageToListEPKcb` (`0x002cf761`), `_ZN10CStreaming12RequestModelEii` (`0x002d292d`), `_ZN10CStreaming22LoadAllRequestedModelsEb` (`0x002d4639`)
  - Vehicles (2-arg vs 3-arg verified): `_ZN11CAutomobileC1Eihh`, `_ZN5CBikeC1Eih`, `_ZN4CBmxC1Eih`, `_ZN9CQuadBikeC1Eih`, `_ZN5CBoatC1Eih`, `_ZN5CHeliC1Eih`, `_ZN6CPlaneC1Eih`, `_ZN8CTrailerC1Eih`
  - Entities/World/Objects: `_ZN7CEntity8TeleportE7CVectorh`, `_ZN4CPed8TeleportE7CVectorh`, `_ZN7CObject6CreateEib`
  - Camera/Weather/Clock/Pad: `_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_`, `_ZN7CCamera19TakeControlNoEntityERK7CVectorsi`, `_ZN7CCamera18RestoreWithJumpCutEv`, `_ZN8CWeather15ForceWeatherNowEs`, `_ZN6CClock12SetGameClockEhhh`, `_ZN4CPad19GetPedWalkLeftRightEv`, `_ZN4CPad16GetPedWalkUpDownEv`, `_ZN4CPad20GetSteeringLeftRightEv`
- **Verified Struct Offsets**: `CPlaceable::m_matrix = +0x14`, `CPhysical::m_nPhysicalFlags = +0x44`, `CPhysical::m_vecMoveSpeed = +0x48` (confirmed in `CPhysical::ApplyGravity` at `0x003fe734`).

---

## 3. Reverse Engineering: Data SA-MP Mobile Alyn (`Data_Sem_Mods_Alyn_-_Mali.zip`)

- **Directory Root**: `Data Sem Mods Alyn - Mali/ro.alyn_sampmobile.game/files/`
- **Executable / Plugin Audit**: **0** `.so`, **0** `.asi`, **0** `.dll`, **0** `.lua` files. This package is a clean Mali-GPU GTA SA + SA-MP 0.3.7 data & resource pack.
- **SA-MP Multiplayer Resources Identified**:
  - `SAMP/settings.json`: JSON client configuration schema (`client.server.{host,port,password}`, `client.settings.{nick_name,new_interface,timestamp,fast_connect,voice_chat,display_fps,cleo_aml,modify_files,fps_limit,chat_strings,chat_posx,chat_posy,chat_sizex,chat_sizey}`). Fully supported by `AlynSettingsManager.kt`.
  - `SAMP/gta.dat`: Drops `IMG DATA\PATHS\CARREC.IMG` and appends `IDE SAMP\samp.IDE`.
  - `SAMP/SAMP.ide`: 1,445 SA-MP 0.3.7 custom models & skins (IDs `300..19999`).
  - `SAMP/main.scm` (96,997 bytes) & `SAMP/script.img` (184,320 bytes): Minimal multiplayer SCM script replacing `data/script/mainV1.scm` (3.17 MB) and `data/script/scriptv1.img` (561 KB). Automatically redirected by `FileCompat::OnNvFOpen`.
  - `SAMP/{handling.cfg,peds.ide,vehicles.ide,WEAPON.dat,TIMECYC.DAT,tracks2.dat,tracks4.dat}`.
  - `texdb/samp.img` (36.9 MB) & `texdb/SAMPCOL.img` (4.25 MB): Automatically mounted in `GtaSaWorld::OnInitImageListHook()` via `CStreaming::AddImageToList`.
  - `texdb/samp/`: Multi-GPU texture DB (`.dxt`, `.etc`, `.pvr`, `.unc`). Automatically loaded and registered via `TextureDatabaseRuntime::Load("samp", ...)` + `Register`.
  - `texdb/{gta3,gta_int,txd}/`: Mali `.etc` texture databases (+ `.unc.tmb`); handled seamlessly on Mali, Adreno, and PowerVR GPUs by `GpuCompat` & `RQCaps` synchronization.
  - `fonts/{arial_bold.ttf,arial_bold_thai.ttf,gtaweap3.ttf}`.

---

## 4. AML 1.4.0 Compatibility Analysis

- **Initialization**: `libAML.so` exports `JNI_OnLoad` (`0x00057ab9`), calling `_Z16StartAMLRightNowPKcS0_`.
- **Package Name Independence**: Disassembly of `_Z23GetExternalFilesDir_FLAP7_JNIEnvP8_jobjectPcj` (`0x00057880`) confirms `libAML.so` checks `com/fastman92/main_activity_launcher/Settings`, explicitly runs `ExceptionCheck()` + `ExceptionClear()` if absent, and falls back to `Context.getExternalFilesDir(null)` — automatically resolving `/Android/data/com.nana.samp/mods/` and `/Android/data/com.nana.samp/configs/`.
- **Conflict Mitigation**:
  - `libAML.so` is loaded before `libnana_samp.so`; `CrashHandler` chains cleanly with AML's `sigaction` crash logger.
  - `AmlCompat` (`native/game/aml_compat.cpp`) inspects `libAML.so` via `RTLD_NOLOAD` (`GetInterface("AML")`, `g_pAML`, `g_szModsDir`, `g_bAMLStarted`), lists installed `.so` mods, and warns in diagnostics if a user installs conflicting third-party `samp`/`cleo` `.so` mods.
  - Users can toggle AML 1.4.0 cleanly from the in-app Settings dialog.
