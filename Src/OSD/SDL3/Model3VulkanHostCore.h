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
 * Model3VulkanHostCore.h
 *
 * The C ABI between the C++23 engine and the C# (.NET 11 Native AOT)
 * cold-path module.  This header is deliberately plain C: the C# side mirrors
 * every struct byte-for-byte, and the exe loads Model3VulkanHostCore.dll dynamically so the
 * build does not gain a link-time dependency on .NET.
 *
 * Rules:
 *   - Every struct starts with StructSize; a mismatch is rejected by the
 *     caller before any field is used.
 *   - Strings are fixed-size UTF-8 buffers, NUL-terminated, not pointers.
 *   - Every function returns HostCoreStatus and writes a UTF-8 message into
 *     the caller's error buffer.  No exception crosses the boundary.
 *   - Lists are returned as one self-describing blob (see below), so the C++
 *     side can allocate exactly once and own the memory.
 */

#ifndef INCLUDED_MODEL3VULKANHOSTCORE_H
#define INCLUDED_MODEL3VULKANHOSTCORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOSTCORE_ABI_VERSION 1u
#define HOSTCORE_OK 0

typedef enum HostCoreStatus
{
    HOSTCORE_OK_STATUS          = 0,
    HOSTCORE_ERR_INVALID_ARG    = 1,
    HOSTCORE_ERR_IO             = 2,
    HOSTCORE_ERR_FORMAT         = 3,
    HOSTCORE_ERR_BUFFER_TOO_SMALL = 4,
    HOSTCORE_ERR_UNSUPPORTED    = 5,
    HOSTCORE_ERR_INTERNAL       = 6
} HostCoreStatus;

typedef struct HostCoreConfig
{
    uint32_t StructSize;
    uint32_t AbiVersion;

    int32_t XResolution;
    int32_t YResolution;
    int32_t Msaa;
    int32_t VSync;
    int32_t Vrr;
    int32_t CrtShader;
    int32_t MousePollingHz;
    int32_t MouseFilter;
    int32_t RecenterKey;
    int32_t SteamDeckBackButtons;

    char CrtShaderName[64];
} HostCoreConfig;

typedef struct HostSaveMeta
{
    uint32_t StructSize;

    char GameName[64];
    char RomCrc32[16];
    char Title[128];
    char SavedAt[32];
    char Version[32];

    uint64_t FileSize;
    uint64_t RawSize;
    uint64_t ThumbSize;
} HostSaveMeta;

/* Version/capability.  The caller must compare AbiVersion before using the rest. */
uint32_t HostCore_AbiVersion(void);
const char* HostCore_Version(void);

/* Module 1: .ini cold path. */
int32_t HostCore_LoadIni(const char* path, HostCoreConfig* output, char* error, size_t errorCapacity);
int32_t HostCore_SaveIni(const char* path, const HostCoreConfig* config, char* error, size_t errorCapacity);

/* Module 2: .zst container (ZIP + zstd; the .vkstate format under a shorter suffix). */
int32_t HostCore_SaveVkState(
    const char* path,
    HostSaveMeta* meta,
    const uint8_t* core,
    size_t coreSize,
    const uint8_t* thumbnail,
    size_t thumbnailSize,
    char* error,
    size_t errorCapacity);

/*
 * Reads either a .zst container or a legacy raw .st.  destination may be null or
 * too small: the required size is written to *written and
 * HOSTCORE_ERR_BUFFER_TOO_SMALL is returned, so the caller can resize and
 * call again.  metaOut may be null.
 */
int32_t HostCore_LoadVkState(
    const char* path,
    uint8_t* destination,
    size_t destinationCapacity,
    size_t* written,
    HostSaveMeta* metaOut,
    char* error,
    size_t errorCapacity);

/* 1 = .zst/.vkstate ZIP container, 0 = legacy raw .st. */
int32_t HostCore_SniffState(const char* path, int32_t* isVkState, char* error, size_t errorCapacity);

int32_t HostCore_RenameVkState(const char* path, const char* newTitle, char* error, size_t errorCapacity);

int32_t HostCore_ConvertStToVkState(
    const char* legacyPath,
    const char* vkStatePath,
    HostSaveMeta* meta,
    char* error,
    size_t errorCapacity);

/*
 * Batch listing.  The blob is little-endian:
 *
 *   uint32 version;       // 1
 *   uint32 count;
 *   repeat count times:
 *     HostSaveMeta meta;
 *     uint32 pathLength;  uint8_t path[pathLength];   // UTF-8, no NUL
 *     uint32 thumbLength; uint8_t thumb[thumbLength]; // PNG bytes
 *
 * Pass destination = NULL to obtain *required, then call again with that much
 * space.  core.zst is never opened.
 */
int32_t HostCore_ListVkStates(
    const char* directory,
    uint8_t* destination,
    size_t destinationCapacity,
    size_t* required,
    char* error,
    size_t errorCapacity);

/*
 * One raw zstd frame, decompressed into caller memory.
 *
 * Added after the 1.0 ABI was published, so it is optional: a module without
 * this export still loads, and the only feature it loses is .dds.zst texture
 * replacements (the pack falls back to its uncompressed .dds / .webp / .png
 * files).
 *
 * destination may be null or too small: the decompressed size is written to
 * *written and HOSTCORE_ERR_BUFFER_TOO_SMALL is returned, so the caller can
 * allocate exactly once.  A frame that does not hold exactly the number of
 * bytes the caller expects is HOSTCORE_ERR_FORMAT, not a partial write.
 */
int32_t HostCore_DecompressZstd(
    const uint8_t* source,
    size_t sourceSize,
    uint8_t* destination,
    size_t destinationCapacity,
    size_t* written,
    char* error,
    size_t errorCapacity);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // INCLUDED_MODEL3VULKANHOSTCORE_H
