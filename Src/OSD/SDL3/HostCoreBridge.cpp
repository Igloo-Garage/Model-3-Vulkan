/**
 ** Supermodel-VK
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright 2026 The Supermodel-VK authors
 **
 ** This file is part of Supermodel-VK, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "OSD/SDL3/HostCoreBridge.h"
#include "OSD/SDL3/HostCoreZstd.h"

#include "OSD/FileSystemPath.h"

#include <algorithm>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace OSD
{
namespace HostCore
{

namespace
{

using AbiVersionFn = uint32_t (*)();
using VersionFn = const char* (*)();
using LoadIniFn = int32_t (*)(const char*, HostCoreConfig*, char*, size_t);
using SaveIniFn = int32_t (*)(const char*, const HostCoreConfig*, char*, size_t);
using SaveVkStateFn = int32_t (*)(const char*, HostSaveMeta*, const uint8_t*, size_t,
                                  const uint8_t*, size_t, char*, size_t);
using LoadVkStateFn = int32_t (*)(const char*, uint8_t*, size_t, size_t*, HostSaveMeta*, char*, size_t);
using SniffStateFn = int32_t (*)(const char*, int32_t*, char*, size_t);
using RenameVkStateFn = int32_t (*)(const char*, const char*, char*, size_t);
using ConvertStToVkStateFn = int32_t (*)(const char*, const char*, HostSaveMeta*, char*, size_t);
using ListVkStatesFn = int32_t (*)(const char*, uint8_t*, size_t, size_t*, char*, size_t);
using DecompressZstdFn = int32_t (*)(const uint8_t*, size_t, uint8_t*, size_t, size_t*, char*, size_t);

/*
 * The module's file name on each platform.  Native AOT names a shared library
 * lib<AssemblyName>.so on Linux and lib<AssemblyName>.dylib on macOS, while
 * the Windows build keeps the bare .dll name.
 */
constexpr const char* ModuleFileName()
{
#if defined(_WIN32)
    return "Model3VulkanHostCore.dll";
#elif defined(__APPLE__)
    return "libModel3VulkanHostCore.dylib";
#else
    return "libModel3VulkanHostCore.so";
#endif
}

std::string PathUtf8(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
    return path.string();
#endif
}

std::filesystem::path PathFromUtf8(const char* data, size_t length)
{
#if defined(_WIN32)
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(data), length));
#else
    return std::filesystem::path(std::string(data, length));
#endif
}

Error MakeError(int32_t status, const char* message, const char* fallback)
{
    return Error{status, (message != nullptr && message[0] != '\0') ? message : fallback};
}

bool ReadU32(const uint8_t*& cursor, const uint8_t* end, uint32_t& value)
{
    if (cursor + 4 > end)
        return false;
    std::memcpy(&value, cursor, 4);
    cursor += 4;
    return true;
}

} // namespace

struct Bridge::Api
{
    void* handle = nullptr;
    AbiVersionFn abiVersion = nullptr;
    VersionFn version = nullptr;
    LoadIniFn loadIni = nullptr;
    SaveIniFn saveIni = nullptr;
    SaveVkStateFn saveVkState = nullptr;
    LoadVkStateFn loadVkState = nullptr;
    SniffStateFn sniffState = nullptr;
    RenameVkStateFn renameVkState = nullptr;
    ConvertStToVkStateFn convertStToVkState = nullptr;
    ListVkStatesFn listVkStates = nullptr;
    DecompressZstdFn decompressZstd = nullptr;

    ~Api()
    {
        if (handle == nullptr)
            return;
#if defined(_WIN32)
        ::FreeLibrary(static_cast<HMODULE>(handle));
#else
        ::dlclose(handle);
#endif
    }
};

std::expected<Bridge, Error> Bridge::Load(const std::filesystem::path& modulePath)
{
    void* handle = nullptr;

#if defined(_WIN32)
    handle = static_cast<void*>(::LoadLibraryW(modulePath.wstring().c_str()));
    if (handle == nullptr)
    {
        return std::unexpected(Error{
            HOSTCORE_ERR_IO,
            std::string(ModuleFileName()) + " could not be loaded from '" + PathUtf8(modulePath) +
                "'. Run build-hostcore.bat, or continue without the cold-path module."});
    }
#else
    handle = ::dlopen(modulePath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* reason = ::dlerror();
        return std::unexpected(Error{
            HOSTCORE_ERR_IO,
            std::string(ModuleFileName()) + " could not be loaded: " +
                std::string(reason != nullptr ? reason : "unknown error")});
    }
#endif

    auto api = std::make_shared<Api>();
    api->handle = handle;

    auto resolve = [handle](const char* name) -> void*
    {
#if defined(_WIN32)
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
        return ::dlsym(handle, name);
#endif
    };

    api->abiVersion = reinterpret_cast<AbiVersionFn>(resolve("HostCore_AbiVersion"));
    api->version = reinterpret_cast<VersionFn>(resolve("HostCore_Version"));
    api->loadIni = reinterpret_cast<LoadIniFn>(resolve("HostCore_LoadIni"));
    api->saveIni = reinterpret_cast<SaveIniFn>(resolve("HostCore_SaveIni"));
    api->saveVkState = reinterpret_cast<SaveVkStateFn>(resolve("HostCore_SaveVkState"));
    api->loadVkState = reinterpret_cast<LoadVkStateFn>(resolve("HostCore_LoadVkState"));
    api->sniffState = reinterpret_cast<SniffStateFn>(resolve("HostCore_SniffState"));
    api->renameVkState = reinterpret_cast<RenameVkStateFn>(resolve("HostCore_RenameVkState"));
    api->convertStToVkState = reinterpret_cast<ConvertStToVkStateFn>(resolve("HostCore_ConvertStToVkState"));
    api->listVkStates = reinterpret_cast<ListVkStatesFn>(resolve("HostCore_ListVkStates"));
    // Optional: see the note on HasZstdDecompressor().  An older module still loads, it just
    // cannot read .dds.zst texture packs.
    api->decompressZstd = reinterpret_cast<DecompressZstdFn>(resolve("HostCore_DecompressZstd"));

    if (api->abiVersion == nullptr || api->version == nullptr || api->loadIni == nullptr ||
        api->saveIni == nullptr || api->saveVkState == nullptr || api->loadVkState == nullptr ||
        api->sniffState == nullptr || api->renameVkState == nullptr ||
        api->convertStToVkState == nullptr || api->listVkStates == nullptr)
    {
        return std::unexpected(Error{
            HOSTCORE_ERR_FORMAT,
            std::string(ModuleFileName()) + " does not export the complete HostCore 1 ABI."});
    }

    const uint32_t abi = api->abiVersion();
    if (abi != HOSTCORE_ABI_VERSION)
    {
        return std::unexpected(Error{
            HOSTCORE_ERR_FORMAT,
            std::string(ModuleFileName()) + " speaks ABI " + std::to_string(abi) +
                ", this executable expects ABI " + std::to_string(HOSTCORE_ABI_VERSION) + "."});
    }

    return Bridge(api);
}

const std::string& Bridge::Version() const
{
    static const std::string empty;
    if (!m_api || m_api->version == nullptr)
        return empty;

    static thread_local std::string text;
    text = m_api->version();
    return text;
}

std::expected<HostCoreConfig, Error> Bridge::LoadIni(const std::filesystem::path& path) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    HostCoreConfig config = {};
    char error[512] = {};
    const std::string utf8Path = PathUtf8(path);

    const int32_t status = m_api->loadIni(utf8Path.c_str(), &config, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The configuration could not be read."));

    if (config.StructSize != sizeof(HostCoreConfig))
    {
        return std::unexpected(Error{
            HOSTCORE_ERR_FORMAT,
            "HostCoreConfig size mismatch (module " + std::to_string(config.StructSize) +
                ", exe " + std::to_string(sizeof(HostCoreConfig)) + ")."});
    }

    return config;
}

std::expected<void, Error> Bridge::SaveIni(
    const std::filesystem::path& path, const HostCoreConfig& config) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    char error[512] = {};
    const std::string utf8Path = PathUtf8(path);
    const int32_t status = m_api->saveIni(utf8Path.c_str(), &config, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The configuration could not be written."));

    return {};
}

std::expected<void, Error> Bridge::SaveVkState(
    const std::filesystem::path& path,
    const HostSaveMeta& meta,
    const std::vector<std::uint8_t>& core,
    const std::vector<std::uint8_t>& thumbnail) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    HostSaveMeta copy = meta;
    copy.StructSize = sizeof(HostSaveMeta);
    copy.RawSize = core.size();
    copy.ThumbSize = thumbnail.size();

    char error[512] = {};
    const std::string utf8Path = PathUtf8(path);
    const int32_t status = m_api->saveVkState(
        utf8Path.c_str(),
        &copy,
        core.empty() ? nullptr : core.data(), core.size(),
        thumbnail.empty() ? nullptr : thumbnail.data(), thumbnail.size(),
        error, sizeof(error));

    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save state could not be written."));

    return {};
}

std::expected<LoadedState, Error> Bridge::LoadVkState(const std::filesystem::path& path) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    const std::string utf8Path = PathUtf8(path);
    char error[512] = {};

    size_t required = 0;
    HostSaveMeta meta = {};
    meta.StructSize = sizeof(HostSaveMeta);

    int32_t status = m_api->loadVkState(
        utf8Path.c_str(), nullptr, 0, &required, &meta, error, sizeof(error));

    if (status != HOSTCORE_ERR_BUFFER_TOO_SMALL && status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save state could not be read."));

    LoadedState loaded;
    loaded.bytes.resize(required);

    size_t written = 0;
    error[0] = '\0';
    status = m_api->loadVkState(
        utf8Path.c_str(),
        loaded.bytes.empty() ? nullptr : loaded.bytes.data(), loaded.bytes.size(),
        &written, &meta, error, sizeof(error));

    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save state could not be read."));

    loaded.bytes.resize(written);
    loaded.meta = meta;
    return loaded;
}

std::expected<bool, Error> Bridge::IsVkState(const std::filesystem::path& path) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    char error[512] = {};
    int32_t isZip = 0;
    const std::string utf8Path = PathUtf8(path);
    const int32_t status = m_api->sniffState(utf8Path.c_str(), &isZip, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save state could not be inspected."));

    return isZip != 0;
}

std::expected<void, Error> Bridge::RenameVkState(
    const std::filesystem::path& path, const std::string& newTitle) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    char error[512] = {};
    const std::string utf8Path = PathUtf8(path);
    const int32_t status = m_api->renameVkState(
        utf8Path.c_str(), newTitle.c_str(), error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save state could not be renamed."));

    return {};
}

std::expected<void, Error> Bridge::ConvertLegacyState(
    const std::filesystem::path& legacyPath,
    const std::filesystem::path& vkStatePath,
    const HostSaveMeta& meta) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    HostSaveMeta copy = meta;
    copy.StructSize = sizeof(HostSaveMeta);

    char error[512] = {};
    const std::string legacyUtf8 = PathUtf8(legacyPath);
    const std::string targetUtf8 = PathUtf8(vkStatePath);
    const int32_t status = m_api->convertStToVkState(
        legacyUtf8.c_str(), targetUtf8.c_str(), &copy, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The legacy save state could not be converted."));

    return {};
}

std::expected<std::vector<SaveEntry>, Error> Bridge::ListVkStates(
    const std::filesystem::path& directory) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});

    const std::string utf8Directory = PathUtf8(directory);
    char error[512] = {};

    size_t required = 0;
    int32_t status = m_api->listVkStates(
        utf8Directory.c_str(), nullptr, 0, &required, error, sizeof(error));
    if (status != HOSTCORE_ERR_BUFFER_TOO_SMALL && status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save states could not be listed."));

    std::vector<uint8_t> blob(required);
    error[0] = '\0';
    status = m_api->listVkStates(
        utf8Directory.c_str(),
        blob.empty() ? nullptr : blob.data(), blob.size(), &required, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The save states could not be listed."));

    if (blob.size() < 8)
        return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "The save list is truncated."});

    const uint8_t* cursor = blob.data();
    const uint8_t* end = cursor + blob.size();

    uint32_t version = 0;
    uint32_t count = 0;
    if (!ReadU32(cursor, end, version) || !ReadU32(cursor, end, count))
        return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "The save list header is truncated."});
    if (version != 1)
    {
        return std::unexpected(Error{
            HOSTCORE_ERR_FORMAT,
            "The save list uses version " + std::to_string(version) + ", expected 1."});
    }

    std::vector<SaveEntry> entries;
    entries.reserve(count);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (cursor + sizeof(HostSaveMeta) > end)
            return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "A save list entry is truncated."});

        SaveEntry entry;
        std::memcpy(&entry.meta, cursor, sizeof(HostSaveMeta));
        cursor += sizeof(HostSaveMeta);

        if (entry.meta.StructSize != sizeof(HostSaveMeta))
            return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "A save entry has a different ABI size."});

        uint32_t pathLength = 0;
        if (!ReadU32(cursor, end, pathLength) || cursor + pathLength > end)
            return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "A save path is truncated."});
        entry.path = PathFromUtf8(reinterpret_cast<const char*>(cursor), pathLength);
        cursor += pathLength;

        uint32_t thumbLength = 0;
        if (!ReadU32(cursor, end, thumbLength) || cursor + thumbLength > end)
            return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "A save thumbnail is truncated."});
        entry.thumbnail.assign(cursor, cursor + thumbLength);
        cursor += thumbLength;

        entries.push_back(std::move(entry));
    }

    return entries;
}

bool Bridge::HasZstdDecompressor() const
{
    return m_api != nullptr && m_api->decompressZstd != nullptr;
}

std::expected<std::vector<std::uint8_t>, Error> Bridge::DecompressZstd(
    const std::vector<std::uint8_t>& frame) const
{
    if (!m_api)
        return std::unexpected(Error{HOSTCORE_ERR_INTERNAL, "The host core module is not loaded."});
    if (m_api->decompressZstd == nullptr)
    {
        return std::unexpected(Error{HOSTCORE_ERR_UNSUPPORTED,
            "The host core module does not export HostCore_DecompressZstd."});
    }
    if (frame.empty())
        return std::unexpected(Error{HOSTCORE_ERR_INVALID_ARG, "The compressed frame is empty."});

    char error[512] = {};

    size_t required = 0;
    int32_t status = m_api->decompressZstd(
        frame.data(), frame.size(), nullptr, 0, &required, error, sizeof(error));
    if (status != HOSTCORE_ERR_BUFFER_TOO_SMALL && status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The zstd frame could not be read."));

    // A frame that decompresses to nothing is not a texture; refuse rather than hand back
    // an empty buffer that the parser would then reject with a less useful message.
    if (required == 0)
        return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "The zstd frame decompresses to zero bytes."});

    std::vector<std::uint8_t> decompressed(required);

    size_t written = 0;
    error[0] = '\0';
    status = m_api->decompressZstd(
        frame.data(), frame.size(), decompressed.data(), decompressed.size(), &written, error, sizeof(error));
    if (status != HOSTCORE_OK_STATUS)
        return std::unexpected(MakeError(status, error, "The zstd frame could not be decompressed."));
    if (written == 0 || written > decompressed.size())
        return std::unexpected(Error{HOSTCORE_ERR_FORMAT, "The zstd frame reported an impossible length."});

    decompressed.resize(written);
    return decompressed;
}

std::filesystem::path DefaultModulePath()
{
    const std::string base = FileSystemPath::GetBasePath();
    return std::filesystem::path(base) / ModuleFileName();
}

std::expected<Bridge*, Error> Shared()
{
    static std::once_flag once;
    static std::unique_ptr<Bridge> bridge;
    static Error failure{HOSTCORE_ERR_IO, "The host core module was not loaded."};

    std::call_once(once, []()
    {
        auto loaded = Bridge::Load(DefaultModulePath());
        if (loaded)
            bridge = std::make_unique<Bridge>(std::move(*loaded));
        else
            failure = loaded.error();
    });

    if (bridge)
        return bridge.get();
    return std::unexpected(failure);
}

/*
 * C++17 half of the wrapper.  The Vulkan core cannot include HostCoreBridge.h, so these
 * two functions are the only surface it sees; both live here, where the module's
 * std::expected interface is available.
 */
bool ZstdAvailable()
{
    auto shared = Shared();
    return shared && (*shared)->HasZstdDecompressor();
}

bool DecompressZstdFrame(const std::vector<std::uint8_t>& frame,
    std::vector<std::uint8_t>& out, std::string& error)
{
    auto shared = Shared();
    if (!shared)
    {
        error = shared.error().message;
        return false;
    }

    auto decompressed = (*shared)->DecompressZstd(frame);
    if (!decompressed)
    {
        error = decompressed.error().message;
        return false;
    }

    out = std::move(*decompressed);
    return true;
}

} // HostCore
} // OSD
