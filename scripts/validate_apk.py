#!/usr/bin/env python3
"""
Validate a built Nana SAMP APK:
  - Confirms APK exists and is a valid ZIP
  - Verifies package name com.nana.samp via aapt2
  - Verifies all required armeabi-v7a native libraries are packaged
  - Audits every DT_NEEDED dependency against APK libs + Android NDK system libs
  - Verifies critical exported symbols across libGTASA, libAML, libSCAnd, libFastman92pluginLoader, libnana_samp
  - Verifies APK signature using apksigner (if available)
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REQUIRED_LIBS = {
    "libGTASA.so",
    "libAML.so",
    "libthreadfix.so",
    "libImmEmulatorJ.so",
    "libTouchSenseSDK.so",
    "libSCAnd.so",
    "libFastman92pluginLoader.so",
    "libnana_samp.so",
}

ANDROID_SYSTEM_LIBS = {
    "libc.so",
    "libm.so",
    "libdl.so",
    "liblog.so",
    "libz.so",
    "libandroid.so",
    "libEGL.so",
    "libGLESv1_CM.so",
    "libGLESv2.so",
    "libGLESv3.so",
    "libOpenSLES.so",
    "libstdc++.so",
    "libjnigraphics.so",
}


def find_build_tool(name: str) -> str | None:
    if shutil.which(name):
        return shutil.which(name)
    for env_var in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        root = os.environ.get(env_var)
        if root:
            matches = sorted(Path(root).glob(f"build-tools/*/{name}"))
            if matches:
                return str(matches[-1])
    for root in ("/opt/android-sdk", "/usr/local/lib/android/sdk"):
        matches = sorted(Path(root).glob(f"build-tools/*/{name}"))
        if matches:
            return str(matches[-1])
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("apk", type=Path)
    args = parser.parse_args()

    apk: Path = args.apk.resolve()
    if not apk.is_file():
        print(f"ERROR: APK not found: {apk}", file=sys.stderr)
        return 1

    size_mb = apk.stat().st_size / (1024 * 1024)
    print(f"[APK] {apk.name}: {size_mb:.2f} MiB")
    if size_mb < 8.0:
        print(f"ERROR: APK suspiciously small ({size_mb:.2f} MiB) — missing GTA SA 2.00 native libraries?", file=sys.stderr)
        return 1

    aapt2 = find_build_tool("aapt2")
    if aapt2:
        badging = subprocess.check_output([aapt2, "dump", "badging", str(apk)], text=True)
        pkg_line = next((l for l in badging.splitlines() if l.startswith("package:")), "")
        print(f"[AAPT2] {pkg_line}")
        if "name='com.nana.samp'" not in pkg_line:
            print("ERROR: Package name is not com.nana.samp!", file=sys.stderr)
            return 1
        if "native-code: 'armeabi-v7a'" not in badging:
            print("ERROR: Expected armeabi-v7a native ABI!", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory(prefix="nana_apk_val_") as tmp:
        tmp_path = Path(tmp)
        with zipfile.ZipFile(apk, "r") as zf:
            bad = zf.testzip()
            if bad is not None:
                print(f"ERROR: Corrupt APK entry: {bad}", file=sys.stderr)
                return 1
            names = set(zf.namelist())
            so_entries = [n for n in names if n.startswith("lib/armeabi-v7a/") and n.endswith(".so")]
            packaged_libs = {Path(n).name for n in so_entries}
            missing = REQUIRED_LIBS - packaged_libs
            if missing:
                print(f"ERROR: Missing required native libraries in APK: {sorted(missing)}", file=sys.stderr)
                return 1
            print(f"[LIBS] Packaged armeabi-v7a libs ({len(packaged_libs)}): {sorted(packaged_libs)}")

            # Verify essential GTA SA assets and SA-MP defaults exist
            required_assets = [
                "assets/Text/american.gxt",
                "assets/data/fonts.dat",
                "assets/samp_defaults/SAMP/main.scm",
                "assets/samp_defaults/SAMP/gta.dat",
                "assets/samp_defaults/SAMP/SAMP.ide",
                "assets/samp_defaults/SAMP/settings.json",
            ]
            missing_assets = [a for a in required_assets if a not in names]
            if missing_assets:
                print(f"ERROR: Missing required APK assets: {missing_assets}", file=sys.stderr)
                return 1

            for entry in so_entries:
                zf.extract(entry, tmp_path)

        # Audit DT_NEEDED and key cross-library symbols
        lib_dir = tmp_path / "lib" / "armeabi-v7a"
        all_defined = {}
        for lib_file in sorted(lib_dir.glob("*.so")):
            dyn = subprocess.run(["readelf", "-d", str(lib_file)], capture_output=True, text=True).stdout
            needed = [
                l.split("[", 1)[1].split("]", 1)[0]
                for l in dyn.splitlines()
                if "(NEEDED)" in l and "[" in l
            ]
            unresolved = [n for n in needed if n not in packaged_libs and n not in ANDROID_SYSTEM_LIBS]
            if unresolved:
                print(f"ERROR: {lib_file.name} has unresolved DT_NEEDED: {unresolved}", file=sys.stderr)
                return 1
            syms_out = subprocess.run(["readelf", "-Ws", str(lib_file)], capture_output=True, text=True).stdout
            all_defined[lib_file.name] = {
                l.split()[7].split("@")[0]
                for l in syms_out.splitlines()
                if len(l.split()) >= 8 and l.split()[6] != "UND"
            }

        # Confirm critical linker contracts
        assert "PluginLoader_OnLoad" in all_defined["libFastman92pluginLoader.so"], "Missing PluginLoader_OnLoad"
        assert "PluginLoader_OnUnload" in all_defined["libFastman92pluginLoader.so"], "Missing PluginLoader_OnUnload"
        assert "Original_JNI_OnLoad" in all_defined["libGTASA.so"], "Missing Original_JNI_OnLoad in libGTASA.so"
        assert "EnterGameFromSCFunc" in all_defined["libSCAnd.so"], "Missing EnterGameFromSCFunc in libSCAnd.so"
        assert "_Z15EnterSocialCLubv" in all_defined["libSCAnd.so"], "Missing EnterSocialCLub in libSCAnd.so"
        assert "GetInterface" in all_defined["libAML.so"], "Missing AML GetInterface"
        assert any("Java_com_nana_samp_core_NativeClient_" in s for s in all_defined["libnana_samp.so"]), (
            "Missing NativeClient JNI exports in libnana_samp.so"
        )
        print("[ELF] All DT_NEEDED and cross-library symbol contracts PASSED!")

    apksigner = find_build_tool("apksigner")
    if apksigner:
        res = subprocess.run([apksigner, "verify", "--print-certs", str(apk)], capture_output=True, text=True)
        if res.returncode != 0:
            print(f"ERROR: apksigner verify failed:\n{res.stderr}", file=sys.stderr)
            return 1
        print("[SIGN] apksigner verify PASSED!")

    return 0


if __name__ == "__main__":
    sys.exit(main())
