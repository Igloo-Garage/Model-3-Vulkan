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

/*
 * HostCoreZstd.h
 *
 * Raw zstd decompression through Model3VulkanHostCore, for compressed assets that
 * are not save containers - today that means .dds.zst files in an HD texture pack.
 *
 * A deliberately plain C++17 header: this is included from the Vulkan core, which is
 * still built as C++17, while the module wrapper next to it (HostCoreBridge.h) is the
 * C++23 startup layer and returns std::expected. The bridge keeps the two apart.
 *
 * Both calls are safe to make when the module is absent; they report false and a
 * message, and the caller falls back to the uncompressed files.
 */

#ifndef INCLUDED_OSD_SDL3_HOSTCOREZSTD_H
#define INCLUDED_OSD_SDL3_HOSTCOREZSTD_H

#include <cstdint>
#include <string>
#include <vector>

namespace OSD
{
namespace HostCore
{

// True when the module is loadable and exports HostCore_DecompressZstd.
bool ZstdAvailable();

// Decompresses one complete frame into `out`. Returns false and fills `error` on failure.
bool DecompressZstdFrame(const std::vector<std::uint8_t>& frame,
    std::vector<std::uint8_t>& out, std::string& error);

} // HostCore
} // OSD

#endif // INCLUDED_OSD_SDL3_HOSTCOREZSTD_H
