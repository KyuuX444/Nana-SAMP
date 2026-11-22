#include "../game/gpu_compat.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

int main() {
    using namespace nana::game;

    // 1. Adreno -> DXT (1)
    {
        auto g = ClassifyGpu("Adreno (TM) 650", "GL_AMD_compressed_ATC_texture GL_OES_compressed_ETC1_RGB8_texture");
        CHECK(g.isAdreno);
        CHECK(!g.isMali);
        CHECK(!g.isPowerVR);
        CHECK(g.preferredFormat == GtaTextureFormat::DXT);
    }

    // 2. Mali & Immortalis -> ETC (5), even if driver advertises s3tc/dxt1
    {
        auto g = ClassifyGpu("Mali-G68 MC4", "GL_EXT_texture_compression_s3tc GL_OES_compressed_ETC1_RGB8_texture");
        CHECK(g.isMali);
        CHECK(g.preferredFormat == GtaTextureFormat::ETC);

        auto g2 = ClassifyGpu("Immortalis-G715", "");
        CHECK(g2.isMali);
        CHECK(g2.preferredFormat == GtaTextureFormat::ETC);
    }

    // 3. PowerVR -> PVR (4)
    {
        auto g = ClassifyGpu("PowerVR Rogue GE8320", "GL_IMG_texture_compression_pvrtc");
        CHECK(g.isPowerVR);
        CHECK(g.preferredFormat == GtaTextureFormat::PVR);
    }

    // 4. Data_Sem_Mods_Alyn_-_Mali.zip layout scan + cross-GPU format resolution
    {
        fs::path root = fs::temp_directory_path() / "nana_samp_test_mali_data";
        fs::remove_all(root);
        fs::create_directories(root / "texdb" / "gta3");
        std::ofstream(root / "texdb" / "gta3" / "gta3.etc.tmb") << "x";
        std::ofstream(root / "texdb" / "gta3" / "gta3.etc.toc") << "x";
        std::ofstream(root / "texdb" / "gta3" / "gta3.etc.dat") << "x";

        uint32_t mask = ScanTexDbFormats(root.string(), "gta3");
        CHECK(mask == TEXDB_ETC);

        auto mali = ClassifyGpu("Mali-G57 MC2", "");
        auto adreno = ClassifyGpu("Adreno (TM) 640", "GL_AMD_compressed_ATC_texture");
        auto pvr = ClassifyGpu("PowerVR Rogue GE8320", "GL_IMG_texture_compression_pvrtc");

        // All three GPUs resolve Mali-only pack safely to ETC (5) instead of missing .dxt/.pvr
        CHECK(ResolveDatabaseFormat(mali, mask, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::ETC);
        CHECK(ResolveDatabaseFormat(adreno, mask, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::ETC);
        CHECK(ResolveDatabaseFormat(pvr, mask, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::ETC);

        // Multi-GPU pack picks native format per GPU
        uint32_t multi = TEXDB_ETC | TEXDB_DXT | TEXDB_PVR;
        CHECK(ResolveDatabaseFormat(mali, multi, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::ETC);
        CHECK(ResolveDatabaseFormat(adreno, multi, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::DXT);
        CHECK(ResolveDatabaseFormat(pvr, multi, static_cast<int>(GtaTextureFormat::AUTO)) == GtaTextureFormat::PVR);

        fs::remove_all(root);
    }

    std::printf("All NanaGpuCompatTests passed!\n");
    return 0;
}
