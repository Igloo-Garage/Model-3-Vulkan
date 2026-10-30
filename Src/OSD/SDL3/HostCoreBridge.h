/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
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

/*
 * HostCoreBridge.h
 *
 * C++23 wrapper around Model3VulkanHostCore.dll.
 *
 * The module is loaded dynamically, so the executable has no link-time
 * dependency on .NET.  Every call returns std::expected; the caller decides
 * whether a missing module or a damaged save is fatal for the operation or
 * merely disables the optional cold-path feature.
 */

#ifndef INCLUDED_OSD_SDL3_HOSTCOREBRIDGE_H
#define INCLUDED_OSD_SDL3_HOSTCOREBRIDGE_H

#include "OSD/SDL3/Model3VulkanHostCore.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace OSD
{
namespace HostCore
{

struct Error
{
    int32_t code = HOSTCORE_ERR_INTERNAL;
    std::string message;
};

struct SaveEntry
{
    HostSaveMeta meta{};
    std::filesystem::path path;
    std::vector<std::uint8_t> thumbnail;
};

struct LoadedState
{
    std::vector<std::uint8_t> bytes;
    HostSaveMeta meta{};
};

class Bridge
{
public:
    Bridge() = default;
    Bridge(const Bridge&) = default;
    Bridge& operator=(const Bridge&) = default;

    /*
     * Loads the module and resolves every export.  Fails with a complete
     * message when the DLL is missing, is not a Native AOT image, or was built
     * for a different ABI.
     */
    static std::expected<Bridge, Error> Load(const std::filesystem::path& modulePath);

    bool Ready() const { return m_api != nullptr; }
    const std::string& Version() const;

    std::expected<HostCoreConfig, Error> LoadIni(const std::filesystem::path& path) const;
    std::expected<void, Error> SaveIni(const std::filesystem::path& path, const HostCoreConfig& config) const;

    std::expected<void, Error> SaveVkState(
        const std::filesystem::path& path,
        const HostSaveMeta& meta,
        const std::vector<std::uint8_t>& core,
        const std::vector<std::uint8_t>& thumbnail) const;

    std::expected<LoadedState, Error> LoadVkState(const std::filesystem::path& path) const;
    std::expected<bool, Error> IsVkState(const std::filesystem::path& path) const;
    std::expected<void, Error> RenameVkState(const std::filesystem::path& path, const std::string& newTitle) const;
    std::expected<void, Error> ConvertLegacyState(
        const std::filesystem::path& legacyPath,
        const std::filesystem::path& vkStatePath,
        const HostSaveMeta& meta) const;

    std::expected<std::vector<SaveEntry>, Error> ListVkStates(const std::filesystem::path& directory) const;

    /*
     * One raw zstd frame (the .dds.zst form of an HD texture replacement).
     *
     * The export was added after the 1.0 ABI was published, so an older module answers
     * false here and the caller falls back to the uncompressed files.  Kept out of the
     * "complete ABI" check in Load() for the same reason.
     */
    bool HasZstdDecompressor() const;
    std::expected<std::vector<std::uint8_t>, Error> DecompressZstd(
        const std::vector<std::uint8_t>& frame) const;

private:
    struct Api;
    explicit Bridge(std::shared_ptr<Api> api) : m_api(std::move(api)) {}

    std::shared_ptr<Api> m_api;
};

/*
 * Process-wide lazy handle.  The first call searches next to the executable;
 * subsequent calls reuse the result, including a failed one, so a missing
 * module does not retry on every UI frame.
 */
std::expected<Bridge*, Error> Shared();

/* Next to the executable: Model3VulkanHostCore.dll, libModel3VulkanHostCore.so or
 * libModel3VulkanHostCore.dylib, depending on the platform. */
std::filesystem::path DefaultModulePath();

} // HostCore
} // OSD

#endif // INCLUDED_OSD_SDL3_HOSTCOREBRIDGE_H
