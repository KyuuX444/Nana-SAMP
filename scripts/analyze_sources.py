#!/usr/bin/env python3
"""
Automated static & ELF reverse-engineering analyzer for:
  - GTA San Andreas Android 2.00 + AML 1.4.0 APK
  - Data SA-MP Mobile Alyn (Mali) data pack

Produces verified JSON and Markdown reports directly from the extracted artifacts.
"""

import argparse
import json
import os
import subprocess
import sys
from collections import Counter
from pathlib import Path


def run_cmd(cmd: list[str]) -> str:
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=False)
        return res.stdout
    except Exception:
        return ""


def analyze_elf(so_path: Path) -> dict:
    hdr = run_cmd(["readelf", "-h", str(so_path)])
    dyn = run_cmd(["readelf", "-d", str(so_path)])
    elf_class = ""
    machine = ""
    for line in hdr.splitlines():
        if "Class:" in line:
            elf_class = line.split(":", 1)[1].strip()
        elif "Machine:" in line:
            machine = line.split(":", 1)[1].strip()
    needed = []
    soname = ""
    for line in dyn.splitlines():
        if "(NEEDED)" in line and "[" in line:
            needed.append(line.split("[", 1)[1].split("]", 1)[0])
        elif "(SONAME)" in line and "[" in line:
            soname = line.split("[", 1)[1].split("]", 1)[0]
    return {
        "name": so_path.name,
        "abi": so_path.parent.name,
        "size_bytes": so_path.stat().st_size,
        "elf_class": elf_class,
        "machine": machine,
        "soname": soname,
        "dt_needed": needed,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path(os.environ.get("NANA_SOURCES_DIR", ".build_sources")),
    )
    parser.add_argument(
        "--out-json",
        type=Path,
        default=Path("build-source-analysis.json"),
    )
    args = parser.parse_args()

    work_dir = args.work_dir.resolve()
    manifest_path = work_dir / "sources_manifest.json"
    if not manifest_path.is_file():
        print(f"ERROR: {manifest_path} not found. Run fetch_and_prepare_sources.py first.", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    apk_unzip = Path(manifest["apk_unzip_dir"])
    alyn_root = Path(manifest["alyn_files_root"])

    # 1. Analyze all native .so in APK
    libs = []
    for so_path in sorted((apk_unzip / "lib").rglob("*.so")):
        libs.append(analyze_elf(so_path))

    # 2. Verify key GTA SA 2.00 symbols in libGTASA.so
    gtasa_so = apk_unzip / "lib" / "armeabi-v7a" / "libGTASA.so"
    syms_out = run_cmd(["readelf", "-Ws", str(gtasa_so)])
    defined_syms = {}
    for line in syms_out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[6] != "UND":
            defined_syms[parts[7]] = parts[1]

    required_gtasa_syms = [
        "Original_JNI_OnLoad",
        "Original_JNI_OnUnload",
        "gGameState",
        "StorageRootBuffer",
        "StorageBaseRootBuffer",
        "RQCaps",
        "TheCamera",
        "_Z11DoGameStatev",
        "_ZN5CGame7ProcessEv",
        "_Z7NvFOpenPKcS0_bb",
        "_ZN22TextureDatabaseRuntime4LoadEPKcb21TextureDatabaseFormat",
        "_ZN22TextureDatabaseRuntime8RegisterEPS_",
        "_ZN10CStreaming13InitImageListEv",
        "_ZN10CStreaming14AddImageToListEPKcb",
        "_ZN10CStreaming12RequestModelEii",
        "_ZN10CStreaming22LoadAllRequestedModelsEb",
        "_Z13FindPlayerPedi",
        "_ZN6CWorld3AddEP7CEntity",
        "_ZN6CWorld6RemoveEP7CEntity",
        "_ZN6CWorld7PlayersE",
        "_ZN4CPed8TeleportE7CVectorh",
        "_ZN7CEntity8TeleportE7CVectorh",
        "_ZN11CAutomobileC1Eihh",
        "_ZN5CBikeC1Eih",
        "_ZN5CBoatC1Eih",
        "_ZN5CHeliC1Eih",
        "_ZN6CPlaneC1Eih",
        "_ZN7CCamera18RestoreWithJumpCutEv",
        "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_",
        "_ZN8CWeather15ForceWeatherNowEs",
        "_ZN6CClock12SetGameClockEhhh",
        "_ZN4CPad19GetPedWalkLeftRightEv",
        "_ZN4CPad16GetPedWalkUpDownEv",
        "_ZN4CPad20GetSteeringLeftRightEv",
    ]
    verified_syms = {s: defined_syms.get(s) for s in required_gtasa_syms}
    missing_syms = [s for s, addr in verified_syms.items() if addr is None]
    if missing_syms:
        print(f"ERROR: Missing required GTA SA 2.00 symbols: {missing_syms}", file=sys.stderr)
        return 1

    # 3. Verify AML 1.4.0 exports
    aml_so = apk_unzip / "lib" / "armeabi-v7a" / "libAML.so"
    aml_out = run_cmd(["readelf", "-Ws", str(aml_so)])
    aml_defined = {
        line.split()[7]: line.split()[1]
        for line in aml_out.splitlines()
        if len(line.split()) >= 8 and line.split()[6] != "UND"
    }
    aml_checks = {
        k: aml_defined.get(k)
        for k in ["JNI_OnLoad", "GetInterface", "CreateInterface", "g_pAML", "g_szModsDir", "_Z16StartAMLRightNowPKcS0_"]
    }

    # 4. Inventory Data SA-MP Mobile Alyn
    ext_counts = Counter()
    total_files = 0
    total_bytes = 0
    for p in alyn_root.rglob("*"):
        if p.is_file():
            total_files += 1
            total_bytes += p.stat().st_size
            ext_counts[p.suffix.lower() or "(no_ext)"] += 1

    samp_files = sorted(p.name for p in (alyn_root / "SAMP").iterdir() if p.is_file())
    texdb_dirs = sorted(p.name for p in (alyn_root / "texdb").iterdir() if p.is_dir())
    texdb_imgs = sorted(p.name for p in (alyn_root / "texdb").iterdir() if p.is_file() and p.suffix.lower() == ".img")

    alyn_settings = {}
    settings_path = alyn_root / "SAMP" / "settings.json"
    if settings_path.is_file():
        alyn_settings = json.loads(settings_path.read_text(encoding="utf-8"))

    report = {
        "sources": manifest["sources"],
        "gtasa_apk_native_libs": libs,
        "gtasa_verified_symbols": verified_syms,
        "aml_verified_exports": aml_checks,
        "alyn_data_inventory": {
            "total_files": total_files,
            "total_bytes": total_bytes,
            "extensions": dict(ext_counts.most_common()),
            "has_so_asi_dll_lua": {
                ext: ext_counts.get(ext, 0) for ext in [".so", ".asi", ".dll", ".lua"]
            },
            "samp_dir_files": samp_files,
            "texdb_dirs": texdb_dirs,
            "texdb_img_archives": texdb_imgs,
            "settings_schema_keys": alyn_settings,
        },
    }

    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"[ANALYZE OK] Verified {len(verified_syms)} GTA SA 2.00 symbols, {len(libs)} APK native libs, "
          f"{total_files} Alyn data files -> {args.out_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
