#include "../game/file_compat.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

void WriteFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

} // namespace

int main() {
    using namespace nana::game;

    // 1. Path normalization
    assert(NormalizeGameRelPath(nullptr, "DATA\\GTA.DAT  ", "/storage/files") == "DATA/GTA.DAT");
    assert(NormalizeGameRelPath("", ".\\TEXDB\\GTA3.IMG", "/storage/files") == "TEXDB/GTA3.IMG");
    assert(NormalizeGameRelPath("", "/storage/files/data/peds.ide", "/storage/files") == "data/peds.ide");
    assert(NormalizeGameRelPath("", "", "/storage/files").empty());
    assert(NormalizeGameRelPath("", "../secret", "/storage/files").empty());

    // 2. Create synthetic Alyn/Mali unpacked GTA SA 2.00 + SAMP data pack tree
    fs::path root = fs::temp_directory_path() / "nana_samp_file_compat_test";
    fs::remove_all(root);
    fs::create_directories(root);

    WriteFile(root / "data" / "gta.dat", "STOCK_GTA_DAT");
    WriteFile(root / "data" / "script" / "main.scm", "STOCK_MAIN_SCM");
    WriteFile(root / "SAMP" / "main.scm", "SAMP_MAIN_SCM");
    WriteFile(root / "SAMP" / "gta.dat", "SAMP_GTA_DAT");
    WriteFile(root / "texdb" / "gta3.img", "GTA3_IMG");
    WriteFile(root / "models" / "gta_int.img", "GTA_INT_IN_MODELS");
    WriteFile(root / "audio" / "CONFIG" / "TrakLkup.dat", "TRAK");

    auto& fc = FileCompat::Instance();
    fc.Configure(root.string());

    // Empty filename (e.g. ZIPFile::Initialize("")) must NEVER resolve to root directory!
    assert(fc.ResolveExistingRegularFile("", "").empty());
    assert(fc.ResolveExistingRegularFile("", "texdb").empty());

    // SA-MP overrides in SAMP/ must win over stock data/ files (including GTA SA 2.00 mainV1.scm & scriptv1.img!)
    WriteFile(root / "SAMP" / "script.img", "SAMP_SCRIPT_IMG");
    std::string mainScm = fc.ResolveExistingRegularFile("", "DATA\\SCRIPT\\MAIN.SCM");
    assert(mainScm == (root / "SAMP" / "main.scm").string());

    std::string mainV1Scm = fc.ResolveExistingRegularFile("", "data\\script\\mainV1.scm");
    assert(mainV1Scm == (root / "SAMP" / "main.scm").string());

    std::string scriptV1 = fc.ResolveExistingRegularFile("", "data\\script\\scriptv1.img");
    assert(scriptV1 == (root / "SAMP" / "script.img").string());

    std::string gtaDat = fc.ResolveExistingRegularFile(nullptr, "DATA\\GTA.DAT");
    assert(gtaDat == (root / "SAMP" / "gta.dat").string());

    // Case-insensitive resolution of UPPERCASE engine paths against lowercase Android files
    std::string gta3 = fc.ResolveExistingRegularFile("", "TEXDB\\GTA3.IMG");
    assert(gta3 == (root / "texdb" / "gta3.img").string());

    std::string trak = fc.ResolveExistingRegularFile("", "AUDIO\\CONFIG\\TRAKLKUP.DAT");
    assert(trak == (root / "audio" / "CONFIG" / "TrakLkup.dat").string());

    // Cross-folder IMG fallback (engine asks TEXDB\GTA_INT.IMG, modpack put it in models/gta_int.img)
    std::string gtaInt = fc.ResolveExistingRegularFile("", "TEXDB\\GTA_INT.IMG");
    assert(gtaInt == (root / "models" / "gta_int.img").string());

    // Direct OnNvFOpen allocates valid type=1 FILE* handle
    NvFileHandle* h = fc.OnNvFOpen("", "DATA\\SCRIPT\\MAIN.SCM", false, true);
    assert(h != nullptr);
    assert(h->type == 1);
    assert(h->ptr != nullptr);
    char buf[32] = {0};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, static_cast<FILE*>(h->ptr));
    std::fclose(static_cast<FILE*>(h->ptr));
    std::free(h);
    assert(std::string(buf, n) == "SAMP_MAIN_SCM");

    fs::remove_all(root);
    std::cout << "[PASS] NanaFileCompatTests passed (SA-MP overrides, case-insensitive lookup, directory guard)\n";
    return 0;
}
