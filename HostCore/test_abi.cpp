// Standalone ABI smoke test for Model3VulkanHostCore.dll.
// Not part of the emulator build; compiled ad hoc with MSVC.

#include "Src/OSD/SDL3/Model3VulkanHostCore.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static_assert(sizeof(HostCoreConfig) == 112, "HostCoreConfig ABI size");
static_assert(sizeof(HostSaveMeta) == 304, "HostSaveMeta ABI size");
static_assert(offsetof(HostSaveMeta, FileSize) == 280, "HostSaveMeta.FileSize offset");
static_assert(offsetof(HostSaveMeta, RawSize) == 288, "HostSaveMeta.RawSize offset");
static_assert(offsetof(HostSaveMeta, ThumbSize) == 296, "HostSaveMeta.ThumbSize offset");

using AbiVersionFn = uint32_t (*)();
using VersionFn = const char* (*)();
using LoadIniFn = int32_t (*)(const char*, HostCoreConfig*, char*, size_t);
using SaveIniFn = int32_t (*)(const char*, const HostCoreConfig*, char*, size_t);
using SaveVkStateFn = int32_t (*)(const char*, HostSaveMeta*, const uint8_t*, size_t,
                                  const uint8_t*, size_t, char*, size_t);
using LoadVkStateFn = int32_t (*)(const char*, uint8_t*, size_t, size_t*, HostSaveMeta*, char*, size_t);
using SniffFn = int32_t (*)(const char*, int32_t*, char*, size_t);
using RenameFn = int32_t (*)(const char*, const char*, char*, size_t);
using ConvertFn = int32_t (*)(const char*, const char*, HostSaveMeta*, char*, size_t);
using ListFn = int32_t (*)(const char*, uint8_t*, size_t, size_t*, char*, size_t);

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void SetString(char* dst, size_t cap, const char* value)
{
    std::snprintf(dst, cap, "%s", value);
}

int main(int argc, char** argv)
{
    const char* dllPath = argc > 1 ? argv[1] : "HostCore/publish/Model3VulkanHostCore.dll";
    HMODULE module = LoadLibraryA(dllPath);
    if (module == nullptr)
    {
        std::printf("FAIL: LoadLibrary('%s') error %lu\n", dllPath, GetLastError());
        return 1;
    }

    auto abiVersion = (AbiVersionFn)GetProcAddress(module, "HostCore_AbiVersion");
    auto version = (VersionFn)GetProcAddress(module, "HostCore_Version");
    auto loadIni = (LoadIniFn)GetProcAddress(module, "HostCore_LoadIni");
    auto saveIni = (SaveIniFn)GetProcAddress(module, "HostCore_SaveIni");
    auto saveVk = (SaveVkStateFn)GetProcAddress(module, "HostCore_SaveVkState");
    auto loadVk = (LoadVkStateFn)GetProcAddress(module, "HostCore_LoadVkState");
    auto sniff = (SniffFn)GetProcAddress(module, "HostCore_SniffState");
    auto renameVk = (RenameFn)GetProcAddress(module, "HostCore_RenameVkState");
    auto convertVk = (ConvertFn)GetProcAddress(module, "HostCore_ConvertStToVkState");
    auto listVk = (ListFn)GetProcAddress(module, "HostCore_ListVkStates");

    CHECK(abiVersion && version && loadIni && saveIni && saveVk && loadVk &&
          sniff && renameVk && convertVk && listVk);
    if (g_failures)
        return 1;

    CHECK(abiVersion() == HOSTCORE_ABI_VERSION);
    std::printf("version: %s\n", version());

    char error[512] = {};

    // ---- ini ----
    const char* iniPath = "HostCore/test.ini";
    {
        FILE* f = std::fopen(iniPath, "wb");
        std::fputs("[Global]\nXResolution = 1280\nYResolution = 960\nMSAA = 4\nVSync = 1\n"
                   "MousePollingHz = 1000\nMouseFilter = 1\nCrtShaderName = \"crt.cfg\"\n", f);
        std::fclose(f);
    }
    HostCoreConfig config = {};
    CHECK(loadIni(iniPath, &config, error, sizeof(error)) == HOSTCORE_OK_STATUS);
    CHECK(config.StructSize == sizeof(HostCoreConfig));
    CHECK(config.XResolution == 1280 && config.YResolution == 960);
    CHECK(config.Msaa == 4 && config.VSync == 1 && config.MousePollingHz == 1000);
    CHECK(std::strcmp(config.CrtShaderName, "crt.cfg") == 0);
    CHECK(saveIni("HostCore/test-out.ini", &config, error, sizeof(error)) == HOSTCORE_OK_STATUS);

    // ---- core buffer ----
    const size_t coreSize = 2 * 1024 * 1024;
    std::vector<uint8_t> core(coreSize);
    for (size_t i = 0; i < coreSize; ++i)
        core[i] = (i % 251 == 0) ? (uint8_t)(i * 31) : (uint8_t)0;
    for (size_t i = 4096; i < 65536; ++i)
        core[i] = 0x5A;

    std::vector<uint8_t> thumbnail;
    if (argc > 2)
    {
        FILE* image = std::fopen(argv[2], "rb");
        if (image != nullptr)
        {
            std::fseek(image, 0, SEEK_END);
            long length = std::ftell(image);
            std::fseek(image, 0, SEEK_SET);
            if (length > 0)
            {
                thumbnail.resize((size_t)length);
                std::fread(thumbnail.data(), 1, thumbnail.size(), image);
            }
            std::fclose(image);
        }
    }

    HostSaveMeta meta = {};
    meta.StructSize = sizeof(HostSaveMeta);
    SetString(meta.GameName, sizeof(meta.GameName), "daytona2");
    SetString(meta.RomCrc32, sizeof(meta.RomCrc32), "deadbeef");
    SetString(meta.Title, sizeof(meta.Title), "Initial Save");
    SetString(meta.Version, sizeof(meta.Version), "0.9.2.3a");
    meta.RawSize = core.size();

    const char* vkPath = "HostCore/test.zst";
    const char* stPath = "HostCore/test.st";
    const char* vk2Path = "HostCore/test2.zst";
    std::remove(vkPath);
    std::remove(stPath);
    std::remove(vk2Path);

    CHECK(saveVk(vkPath, &meta, core.data(), core.size(),
        thumbnail.empty() ? nullptr : thumbnail.data(), thumbnail.size(),
        error, sizeof(error)) == HOSTCORE_OK_STATUS);

    {
        FILE* f = std::fopen(vkPath, "rb");
        unsigned char magic[4] = {};
        std::fread(magic, 1, 4, f);
        std::fclose(f);
        CHECK(magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4);
    }

    int32_t isZip = 0;
    CHECK(sniff(vkPath, &isZip, error, sizeof(error)) == HOSTCORE_OK_STATUS && isZip == 1);

    size_t needed = 0;
    int loadQuery = loadVk(vkPath, nullptr, 0, &needed, nullptr, error, sizeof(error));
    std::printf("load query rc=%d needed=%zu err='%s'\n", loadQuery, needed, error);
    error[0] = 0;
    CHECK(loadQuery == HOSTCORE_ERR_BUFFER_TOO_SMALL);
    CHECK(needed == core.size());

    std::vector<uint8_t> restored(needed);
    HostSaveMeta loadedMeta = {};
    int loadActual = loadVk(vkPath, restored.data(), restored.size(), &needed, &loadedMeta, error, sizeof(error));
    std::printf("load actual rc=%d needed=%zu err='%s'\n", loadActual, needed, error);
    error[0] = 0;
    CHECK(loadActual == HOSTCORE_OK_STATUS);
    CHECK(restored == core);
    CHECK(std::strcmp(loadedMeta.GameName, "daytona2") == 0);
    CHECK(std::strcmp(loadedMeta.Title, "Initial Save") == 0);

    CHECK(renameVk(vkPath, "Renamed Save", error, sizeof(error)) == HOSTCORE_OK_STATUS);
    {
        HostSaveMeta checkMeta = {};
        size_t size2 = 0;
        CHECK(loadVk(vkPath, nullptr, 0, &size2, &checkMeta, error, sizeof(error)) == HOSTCORE_ERR_BUFFER_TOO_SMALL);
        CHECK(std::strcmp(checkMeta.Title, "Renamed Save") == 0);
    }

    // Legacy raw .st sniff + conversion.
    {
        FILE* f = std::fopen(stPath, "wb");
        std::fwrite(core.data(), 1, core.size(), f);
        std::fclose(f);
    }
    CHECK(sniff(stPath, &isZip, error, sizeof(error)) == HOSTCORE_OK_STATUS && isZip == 0);
    {
        std::vector<uint8_t> rawLoaded(core.size());
        size_t rawSize = 0;
        CHECK(loadVk(stPath, rawLoaded.data(), rawLoaded.size(), &rawSize, nullptr, error, sizeof(error)) == HOSTCORE_OK_STATUS);
        CHECK(rawSize == core.size() && rawLoaded == core);
    }
    {
        HostSaveMeta converted = meta;
        SetString(converted.Title, sizeof(converted.Title), "Converted Legacy");
        CHECK(convertVk(stPath, vk2Path, &converted, error, sizeof(error)) == HOSTCORE_OK_STATUS);
    }

    // ---- listing ----
    size_t listSize = 0;
    int listQuery = listVk("HostCore", nullptr, 0, &listSize, error, sizeof(error));
    std::printf("list query rc=%d size=%zu err='%s'\n", listQuery, listSize, error);
    error[0] = 0;
    CHECK(listQuery == HOSTCORE_ERR_BUFFER_TOO_SMALL);
    std::vector<uint8_t> blob(listSize);
    CHECK(listVk("HostCore", blob.data(), blob.size(), &listSize, error, sizeof(error)) == HOSTCORE_OK_STATUS);

    uint32_t blobVersion = 0, count = 0;
    std::memcpy(&blobVersion, blob.data(), 4);
    std::memcpy(&count, blob.data() + 4, 4);
    CHECK(blobVersion == 1);
    CHECK(count == 2);
    std::printf("list: version=%u count=%u bytes=%zu\n", blobVersion, count, listSize);

    size_t cursor = 8;
    for (uint32_t i = 0; i < count && cursor + sizeof(HostSaveMeta) <= blob.size(); ++i)
    {
        HostSaveMeta item = {};
        std::memcpy(&item, blob.data() + cursor, sizeof(item));
        cursor += sizeof(item);

        uint32_t pathLen = 0;
        std::memcpy(&pathLen, blob.data() + cursor, 4);
        cursor += 4;
        std::string path((const char*)blob.data() + cursor, pathLen);
        cursor += pathLen;

        uint32_t thumbLen = 0;
        std::memcpy(&thumbLen, blob.data() + cursor, 4);
        cursor += 4 + thumbLen;

        std::printf("  [%u] title='%s' game='%s' file=%llu raw=%llu thumb=%u path=%s\n",
            i, item.Title, item.GameName,
            (unsigned long long)item.FileSize, (unsigned long long)item.RawSize, thumbLen, path.c_str());
        CHECK(item.StructSize == sizeof(HostSaveMeta));
    }

    FreeLibrary(module);

    if (g_failures == 0)
        std::printf("ALL ABI TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
