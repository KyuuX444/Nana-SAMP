#!/usr/bin/env python3
"""
Download, validate, extract, and stage Nana SAMP external sources:
  1. Data_Sem_Mods_Alyn_-_Mali.zip (SA-MP Mobile Alyn reference data pack)
  2. APK_GTA_SA_2.00_AML_1.4.0.zip (GTA San Andreas Android 2.00 + AML 1.4.0)

Handles MediaFire landing page direct-link resolution, HTTP redirects, retries,
HTTP status/size verification, ZIP CRC integrity validation, and staging into
Gradle's generated build directory (keeping proprietary binaries out of git).
"""

import argparse
import hashlib
import http.cookiejar
import json
import os
import re
import shutil
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

SOURCES = [
    {
        "id": "alyn_data",
        "filename": "Data_Sem_Mods_Alyn_-_Mali.zip",
        "page_url": "https://www.mediafire.com/file/5wtczp82gz8rcy1/Data_Sem_Mods_Alyn_-_Mali.zip/file",
        "min_bytes": 100 * 1024 * 1024,  # ~341 MiB
    },
    {
        "id": "gtasa_aml",
        "filename": "APK_GTA_SA_2.00_AML_1.4.0.zip",
        "page_url": "https://www.mediafire.com/file/ca3888pr67vyxjl/APK_GTA_SA_2.00_AML_1.4.0.zip/file",
        "min_bytes": 10 * 1024 * 1024,  # ~17.6 MiB
    },
]

# Native libraries staged from GTASA 2.00 AML 1.4.0 APK into armeabi-v7a.
# Note: libSCAnd.so and libFastman92pluginLoader.so are built cleanly from C++ in native/scand_stub/
# so Social Club login is bypassed cleanly and PluginLoader_OnLoad chains directly into Original_JNI_OnLoad
# without requiring com.fastman92.main_activity_launcher or FLA INI files.
STAGED_APK_LIBS = [
    "libGTASA.so",
    "libAML.so",
    "libthreadfix.so",
    "libImmEmulatorJ.so",
    "libTouchSenseSDK.so",
]

# Compact SA-MP override defaults staged from Data_Sem_Mods_Alyn_-_Mali.zip into assets/samp_defaults/
# so Nana SAMP works both with full unpacked Mali/Adreno packs AND vanilla GTA SA 2.00 OBBs.
ALYN_SAMP_DEFAULTS = [
    "SAMP/main.scm",
    "SAMP/script.img",
    "SAMP/gta.dat",
    "SAMP/SAMP.ide",
    "SAMP/handling.cfg",
    "SAMP/peds.ide",
    "SAMP/vehicles.ide",
    "SAMP/WEAPON.dat",
    "SAMP/TIMECYC.DAT",
    "SAMP/settings.json",
    "SAMP/tracks2.dat",
    "SAMP/tracks4.dat",
    "fonts/arial_bold.ttf",
    "stream.ini",
]

USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"
)


def make_opener() -> urllib.request.OpenerDirector:
    cj = http.cookiejar.CookieJar()
    return urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cj),
        urllib.request.HTTPRedirectHandler(),
    )


def resolve_mediafire_direct_url(opener: urllib.request.OpenerDirector, page_url: str) -> str:
    req = urllib.request.Request(page_url, headers={"User-Agent": USER_AGENT})
    with opener.open(req, timeout=30) as resp:
        if resp.status != 200:
            raise RuntimeError(f"MediaFire page HTTP {resp.status} for {page_url}")
        html = resp.read().decode("utf-8", errors="ignore")

    # 1. Direct href="https://downloadXXXX.mediafire.com/..."
    direct = re.findall(r'https://download\d+\.mediafire\.com/[^\s"\'<>]+', html)
    if direct:
        return direct[0]

    # 2. Fallback: base64-encoded data-scrambled-url attribute if MediaFire scrambles the button
    import base64

    scrambled = re.findall(r'data-scrambled-url="([^"]+)"', html)
    for token in scrambled:
        try:
            decoded = base64.b64decode(token).decode("utf-8")
            if decoded.startswith("http"):
                return decoded
        except Exception:
            pass

    raise RuntimeError(f"Unable to extract direct download URL from MediaFire page: {page_url}")


def validate_zip(zip_path: Path, min_bytes: int) -> dict:
    if not zip_path.is_file():
        raise RuntimeError(f"File missing: {zip_path}")
    size = zip_path.stat().st_size
    if size < min_bytes:
        raise RuntimeError(f"File {zip_path.name} too small ({size} bytes < {min_bytes})")
    if not zipfile.is_zipfile(zip_path):
        raise RuntimeError(f"File {zip_path.name} is not a valid ZIP archive")
    with zipfile.ZipFile(zip_path, "r") as zf:
        bad = zf.testzip()
        if bad is not None:
            raise RuntimeError(f"Corrupt ZIP entry '{bad}' inside {zip_path.name}")
        count = len(zf.infolist())
    sha256 = hashlib.sha256()
    with zip_path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            sha256.update(chunk)
    return {
        "path": str(zip_path),
        "size_bytes": size,
        "entry_count": count,
        "sha256": sha256.hexdigest(),
    }


def download_source(src: dict, work_dir: Path, max_retries: int = 4) -> dict:
    out_path = work_dir / src["filename"]
    if out_path.is_file():
        try:
            info = validate_zip(out_path, src["min_bytes"])
            print(
                f"[CACHE HIT] {src['filename']} validated "
                f"({info['size_bytes'] / (1024*1024):.2f} MiB, {info['entry_count']} entries)"
            )
            return info
        except Exception as exc:
            print(f"[CACHE INVALID] {src['filename']}: {exc}; re-downloading...")
            out_path.unlink(missing_ok=True)

    last_err = None
    for attempt in range(1, max_retries + 1):
        tmp_path = work_dir / f".{src['filename']}.part"
        try:
            print(f"[DOWNLOAD {attempt}/{max_retries}] Resolving {src['filename']} from MediaFire...")
            opener = make_opener()
            dl_url = resolve_mediafire_direct_url(opener, src["page_url"])
            print(f"  Direct URL: {dl_url[:90]}...")
            dl_req = urllib.request.Request(
                dl_url,
                headers={"User-Agent": USER_AGENT, "Referer": src["page_url"]},
            )
            with opener.open(dl_req, timeout=180) as resp:
                status = getattr(resp, "status", 200)
                cl_header = resp.headers.get("Content-Length")
                ct_header = resp.headers.get("Content-Type", "")
                print(f"  HTTP {status}, Content-Type={ct_header}, Content-Length={cl_header}")
                if status != 200:
                    raise RuntimeError(f"Unexpected HTTP status {status}")
                if "text/html" in ct_header.lower():
                    raise RuntimeError(f"Received HTML instead of ZIP (Content-Type={ct_header})")
                total = 0
                with tmp_path.open("wb") as f:
                    while True:
                        chunk = resp.read(1024 * 1024)
                        if not chunk:
                            break
                        f.write(chunk)
                        total += len(chunk)
                if cl_header is not None and int(cl_header) != total:
                    raise RuntimeError(
                        f"Incomplete download: got {total} bytes, expected {cl_header}"
                    )
            tmp_path.replace(out_path)
            info = validate_zip(out_path, src["min_bytes"])
            print(
                f"[OK] {src['filename']}: {info['size_bytes'] / (1024*1024):.2f} MiB, "
                f"{info['entry_count']} entries, sha256={info['sha256'][:16]}..."
            )
            return info
        except Exception as exc:
            last_err = exc
            print(f"[WARN] Attempt {attempt} failed for {src['filename']}: {exc}", file=sys.stderr)
            tmp_path.unlink(missing_ok=True)
            if attempt < max_retries:
                time.sleep(attempt * 3)

    raise RuntimeError(f"Failed to download {src['filename']} after {max_retries} attempts: {last_err}")


def extract_zip(zip_path: Path, dest_dir: Path) -> None:
    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    dest_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(dest_dir)


def find_apk(gtasa_dir: Path) -> Path:
    apks = sorted(gtasa_dir.rglob("*.apk"))
    if not apks:
        raise RuntimeError(f"No .apk found inside {gtasa_dir}")
    return apks[0]


def find_alyn_files_root(alyn_dir: Path) -> Path:
    for candidate in alyn_dir.rglob("settings.json"):
        if candidate.parent.name.lower() == "samp":
            return candidate.parent.parent
    raise RuntimeError(f"Could not locate SAMP/settings.json inside {alyn_dir}")


def stage_for_gradle(apk_unzip_dir: Path, alyn_files_root: Path, stage_dir: Path) -> dict:
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    jni_out = stage_dir / "jniLibs" / "armeabi-v7a"
    assets_out = stage_dir / "assets"
    jni_out.mkdir(parents=True, exist_ok=True)
    assets_out.mkdir(parents=True, exist_ok=True)

    # 1. Stage GTA SA 2.00 + AML 1.4.0 native .so libraries
    apk_lib_dir = apk_unzip_dir / "lib" / "armeabi-v7a"
    staged_libs = []
    for lib_name in STAGED_APK_LIBS:
        src_lib = apk_lib_dir / lib_name
        if not src_lib.is_file():
            raise RuntimeError(f"Expected native library missing from GTA SA APK: {src_lib}")
        shutil.copy2(src_lib, jni_out / lib_name)
        staged_libs.append({"name": lib_name, "size": src_lib.stat().st_size})

    # 2. Stage GTA SA 2.00 base APK assets (Fonts, Text, Textures, audio, data, images, xml, texdb/sanltd_fonts)
    apk_assets_dir = apk_unzip_dir / "assets"
    staged_asset_count = 0
    if apk_assets_dir.is_dir():
        for src_file in apk_assets_dir.rglob("*"):
            if not src_file.is_file():
                continue
            rel = src_file.relative_to(apk_assets_dir)
            # Skip proprietary Social Club UI PNGs not needed by Nana SAMP
            if rel.parts and rel.parts[0].lower().startswith("social"):
                continue
            dst_file = assets_out / rel
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_file, dst_file)
            staged_asset_count += 1

    # 3. Stage compact Alyn SA-MP defaults into assets/samp_defaults/
    samp_defaults_out = assets_out / "samp_defaults"
    staged_samp_defaults = []
    for rel_path in ALYN_SAMP_DEFAULTS:
        src_file = alyn_files_root / rel_path
        if src_file.is_file():
            dst_file = samp_defaults_out / rel_path
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_file, dst_file)
            staged_samp_defaults.append({"rel": rel_path, "size": src_file.stat().st_size})

    return {
        "stage_dir": str(stage_dir),
        "staged_libs": staged_libs,
        "staged_base_asset_files": staged_asset_count,
        "staged_samp_defaults": staged_samp_defaults,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Download, validate, extract & stage Nana SAMP sources")
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=Path(os.environ.get("NANA_SOURCES_DIR", ".build_sources")),
        help="Temporary directory for downloaded ZIPs and extracted files",
    )
    parser.add_argument(
        "--stage-dir",
        type=Path,
        default=Path("app/build/generated/gtasa200"),
        help="Gradle staging output directory for jniLibs and assets",
    )
    parser.add_argument(
        "--skip-extract-if-present",
        action="store_true",
        help="Reuse already-extracted directories if present in --work-dir",
    )
    args = parser.parse_args()

    work_dir: Path = args.work_dir.resolve()
    work_dir.mkdir(parents=True, exist_ok=True)

    manifest = {"sources": {}}
    for src in SOURCES:
        manifest["sources"][src["id"]] = download_source(src, work_dir)

    extracted_dir = work_dir / "extracted"
    alyn_dir = extracted_dir / "alyn_data"
    gtasa_dir = extracted_dir / "gtasa_aml"
    apk_unzip_dir = extracted_dir / "gtasa_unzip"

    if not (args.skip_extract_if_present and alyn_dir.is_dir() and any(alyn_dir.iterdir())):
        print("[EXTRACT] Extracting Data_Sem_Mods_Alyn_-_Mali.zip...")
        extract_zip(work_dir / "Data_Sem_Mods_Alyn_-_Mali.zip", alyn_dir)

    if not (args.skip_extract_if_present and gtasa_dir.is_dir() and any(gtasa_dir.iterdir())):
        print("[EXTRACT] Extracting APK_GTA_SA_2.00_AML_1.4.0.zip...")
        extract_zip(work_dir / "APK_GTA_SA_2.00_AML_1.4.0.zip", gtasa_dir)

    apk_path = find_apk(gtasa_dir)
    if not (args.skip_extract_if_present and apk_unzip_dir.is_dir() and (apk_unzip_dir / "lib").is_dir()):
        print(f"[EXTRACT] Unpacking {apk_path.name}...")
        extract_zip(apk_path, apk_unzip_dir)

    alyn_files_root = find_alyn_files_root(alyn_dir)
    print(f"[STAGE] Staging runtime binaries & assets into {args.stage_dir}...")
    stage_info = stage_for_gradle(apk_unzip_dir, alyn_files_root, args.stage_dir.resolve())

    manifest["apk_path"] = str(apk_path)
    manifest["apk_unzip_dir"] = str(apk_unzip_dir)
    manifest["alyn_files_root"] = str(alyn_files_root)
    manifest["staging"] = stage_info

    manifest_path = work_dir / "sources_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(
        f"[DONE] Staged {len(stage_info['staged_libs'])} native libs, "
        f"{stage_info['staged_base_asset_files']} GTA SA base assets, "
        f"{len(stage_info['staged_samp_defaults'])} Alyn SA-MP defaults -> {args.stage_dir}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
