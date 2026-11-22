# Nana SAMP (`com.nana.samp`)

Native Android **SA-MP 0.3.7 / open.mp** multiplayer client built directly on top of the **GTA San Andreas Android 2.00 (`armeabi-v7a`)** engine with **Android Mod Loader (AML 1.4.0)** compatibility and **Data SA-MP Mobile Alyn** integration.

> Not a launcher-only wrapper, not a WebView, and not a simulated engine — `GameActivity` hosts Rockstar's native `libGTASA.so` 2.00 OpenGL ES engine directly with original rendering, map, vehicles, audio, physics, and touch controls preserved.

## Implemented Architecture

1. **Automated Source Pipeline (`scripts/`)**
   - `scripts/fetch_and_prepare_sources.py`: Downloads both required MediaFire archives (`Data_Sem_Mods_Alyn_-_Mali.zip` and `APK_GTA_SA_2.00_AML_1.4.0.zip`) with redirect/retry handling, HTTP status/size checks, ZIP CRC validation, extraction, and staging into Gradle's generated build directory (keeping proprietary binaries out of Git).
   - `scripts/analyze_sources.py`: Verifies ELF headers, `DT_NEEDED` graphs, 34+ GTA SA 2.00 symbols, AML 1.4.0 exports, and Alyn SA-MP data layout.
   - `scripts/validate_apk.py`: Validates `aapt2` badging (`com.nana.samp`, `armeabi-v7a`), packaged `.so` inventory, full `DT_NEEDED` cross-resolution, critical ELF symbol contracts, and `apksigner` signature integrity.
2. **GTA SA Android 2.00 + AML 1.4.0 Native Integration (`native/game/`, `native/scand_stub/`, `native/core/`)**
   - `libFastman92pluginLoader.so`: Clean C++ bridge forwarding `PluginLoader_OnLoad` → `libGTASA.so`'s `Original_JNI_OnLoad` so all 13 `NvEventQueueActivity` JNI methods register cleanly.
   - `libSCAnd.so`: Clean Social Club symbol shim satisfying all 24 `libGTASA.so` dependencies and invoking `EnterGameFromSCFunc()` directly.
   - `libAML.so` (AML 1.4.0) & `libthreadfix.so`: Preserved intact; `AmlCompat` provides runtime introspection (`GetInterface("AML")`, mods directory scanning, conflict detection).
   - `ArmHook`: Thumb-2 / ARM32 inline trampoline hooker with PC-relative instruction relocation.
   - `FileCompat` (`NvFOpen` hook): Redirects `data/script/mainV1.scm` → `SAMP/main.scm`, `data/script/scriptv1.img` → `SAMP/script.img`, `gta.dat`, `SAMP.ide`, `handling.cfg`, `peds.ide`, `vehicles.ide`, `WEAPON.dat`, `TIMECYC.DAT` with cached case-insensitive path resolution.
   - `GpuCompat` (`TextureDatabaseRuntime::Load` hook + `RQCaps` sync): Automatic Mali (`.etc`), Adreno (`.dxt`), PowerVR (`.pvr`), and fallback (`.unc`) compatibility plus automatic `texdb/samp` registration.
   - `GtaSaWorld`: Hooks `CStreaming::InitImageList` (`samp.img` + `SAMPCOL.img`), `DoGameState`, and `CGame::Process` for singleplayer menu/AI suppression, local on-foot/in-car/passenger/camera sync, death detection, and remote player/vehicle/object streaming & interpolation.
3. **Clean-Room SA-MP 0.3.7 & 0.3.DL-R1 / open.mp Networking (`native/net/`)**
   - Complete RakNet Legacy UDP transport, cookie handshake, 256-key `ID_AUTH_KEY`, SA-MP datagram encryption/checksum, Huffman `StringCompressor`, `SampQuery`, `RPC_ClientJoin`, `RPC_InitGame`, `RPC_ShowDialog`/`RPC_DialogResponse` (6 styles), `RPC_UpdateScoresPingsIPs`, `RPC_ShowTextDraw`/`ClickTextDraw`, `RPC_Chat`/`RPC_ClientMessage`/`RPC_ServerCommand`, `RPC_Death`, `ID_PLAYER_SYNC`, `ID_VEHICLE_SYNC`, `ID_PASSENGER_SYNC`, `ID_AIM_SYNC`, `ID_MARKERS_SYNC`, and clean disconnect/reconnect.
4. **Native Android Multiplayer UI (`app/`)**
   - `MainActivity`: Native Server Browser (live UDP Query), Direct Connect (`IP:PORT`), Nickname settings, Alyn `SAMP/settings.json` sync, Settings (AML 1.4.0, FPS HUD, Chat Timestamps), GPU & Game Data diagnostics, Crash Log banner.
   - `GameActivity` (`extends WarMedia`): Real GTA SA 2.00 OpenGL ES `SurfaceView` + original touch controls + SA-MP loading overlay + Chat (`{RRGGBB}` colors & `/commands`) + Native Dialogs (6 styles) + Interactive Scoreboard (TAB) + 640×480 Clickable TextDraw Overlay + Class Selection + Pause Menu (Resume, TAB, Reconnect, AML Diagnostics, Disconnect) + URL Audio Streaming.

## GitHub Actions CI/CD

Workflow: `.github/workflows/android.yml`
1. Compiles and runs native C++20 unit tests (`ctest`) + live 2-client UDP multiplayer integration test against a real `open.mp` server (`omp-server`).
2. Downloads, validates, extracts, and analyzes both MediaFire sources into temporary runner storage.
3. Compiles Android NDK (`armeabi-v7a`) native libraries and runs Android JVM unit tests.
4. Builds & validates both Debug and Release APKs (`com.nana.samp`) and uploads them as GitHub Actions Artifacts alongside analysis & build logs.

See [docs/REVERSE_ENGINEERING_REPORT.md](docs/REVERSE_ENGINEERING_REPORT.md) for detailed ELF, symbol, offset, Alyn data, and AML 1.4.0 analysis.
