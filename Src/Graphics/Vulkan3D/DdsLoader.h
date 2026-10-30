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
#ifndef INCLUDED_VULKAN3D_DDSLOADER_H
#define INCLUDED_VULKAN3D_DDSLOADER_H

/*
 * DDS reader for the HD texture replacement pack.
 *
 * The pack only needs block-compressed images: BC1/BC3 for the 4x4 colour blocks the
 * original 16-bit pages lose the most detail to, BC5 for the two-channel normal-ish
 * maps some packs carry, and BC7 as the high-quality default. A DDS file is therefore
 * parsed into the exact byte range of its mip chain and handed to Vulkan as-is - no
 * decode, no mip generation, and no CPU-side conversion.
 *
 * This is a header-only parser with no third-party dependency beyond the Vulkan
 * format enumerations. It is deliberately strict: the file is user content from the
 * internet, every offset is validated against the real buffer size before it is
 * used, and headers are copied with std::memcpy so nothing is read through a
 * potentially unaligned pointer.
 *
 * Byte order is assumed little-endian, which is what the DDS format mandates and
 * what every platform this emulator runs on uses.
 */

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Vulkan3D {

namespace Dds
{

constexpr uint32_t kMagic = 0x20534444u;			// "DDS "
constexpr uint32_t kHeaderSize = 124;
constexpr uint32_t kPixelFormatSize = 32;
constexpr uint32_t kDx10HeaderSize = 20;

constexpr uint32_t kFourCC = 0x00000004u;			// DDPF_FOURCC

constexpr uint32_t kDimensionTexture2D = 3;			// D3D10_RESOURCE_DIMENSION_TEXTURE2D
constexpr uint32_t kResourceMiscTextureCube = 0x4;	// D3D10_RESOURCE_MISC_TEXTURECUBE
constexpr uint32_t kCaps2Cubemap = 0x200;			// DDSCAPS2_CUBEMAP

/*
 * Largest side and payload accepted. The renderer already rejects anything wider than
 * maxImageDimension2D (clamped to 4096), so these are only here to keep the size
 * arithmetic below from being asked about absurd numbers.
 */
constexpr uint32_t kMaxDimension = 16384;
constexpr uint64_t kMaxPayloadBytes = 512ull * 1024ull * 1024ull;

inline constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
	return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) |
		((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

// 'DXT1', 'DXT3', 'DXT5', 'ATI2', 'BC5U' and the DX10 marker, as stored in dwFourCC.
constexpr uint32_t kFourCCDXT1 = MakeFourCC('D', 'X', 'T', '1');
constexpr uint32_t kFourCCDXT3 = MakeFourCC('D', 'X', 'T', '3');
constexpr uint32_t kFourCCDXT5 = MakeFourCC('D', 'X', 'T', '5');
constexpr uint32_t kFourCCATI2 = MakeFourCC('A', 'T', 'I', '2');
constexpr uint32_t kFourCCBC5U = MakeFourCC('B', 'C', '5', 'U');
constexpr uint32_t kFourCCDX10 = MakeFourCC('D', 'X', '1', '0');

/*
 * VkFormat and the block size in bytes for a DXGI format from the DX10 extended header.
 *
 * The sRGB variants map to the UNORM format: the renderer works in the game's own
 * (non-linear) colour space, and sampling an sRGB image would apply a decode the
 * original packed page never had.
 */
inline bool FormatFromDxgi(uint32_t dxgiFormat, VkFormat& vkFormat, uint32_t& blockBytes)
{
	switch (dxgiFormat)
	{
	case 71:  case 72:								// BC1_UNORM, BC1_UNORM_SRGB
		vkFormat = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		blockBytes = 8;
		return true;
	case 74:  case 75:								// BC2_UNORM, BC2_UNORM_SRGB
		vkFormat = VK_FORMAT_BC2_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	case 77:  case 78:								// BC3_UNORM, BC3_UNORM_SRGB
		vkFormat = VK_FORMAT_BC3_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	case 83:										// BC5_UNORM
		vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	case 98:  case 99:								// BC7_UNORM, BC7_UNORM_SRGB
		vkFormat = VK_FORMAT_BC7_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	default:
		return false;
	}
}

// VkFormat and block size for a legacy FourCC code.
inline bool FormatFromFourCC(uint32_t fourCC, VkFormat& vkFormat, uint32_t& blockBytes)
{
	switch (fourCC)
	{
	case kFourCCDXT1:
		// RGBA rather than RGB: DXT1 may carry the one-bit transparent key, and the
		// RGBA variant is the one that samples it. Frame buffer alpha would be lost
		// with VK_FORMAT_BC1_RGB_UNORM_BLOCK.
		vkFormat = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
		blockBytes = 8;
		return true;
	case kFourCCDXT3:
		vkFormat = VK_FORMAT_BC2_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	case kFourCCDXT5:
		vkFormat = VK_FORMAT_BC3_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	case kFourCCATI2:
	case kFourCCBC5U:
		vkFormat = VK_FORMAT_BC5_UNORM_BLOCK;
		blockBytes = 16;
		return true;
	default:
		return false;
	}
}

/*
 * DDS_HEADER, exactly 124 bytes as stored after the magic.
 *
 * Copied with std::memcpy, never dereferenced in place: the payload follows the header
 * at an offset that is a multiple of four in practice, but the caller's buffer carries
 * no alignment guarantee and strict-aliasing rules do not allow reading it as a struct.
 */
struct Header
{
	uint32_t size;
	uint32_t flags;
	uint32_t height;
	uint32_t width;
	uint32_t pitchOrLinearSize;
	uint32_t depth;
	uint32_t mipMapCount;
	uint32_t reserved1[11];
	uint32_t pixelFormatSize;
	uint32_t pixelFormatFlags;
	uint32_t fourCC;
	uint32_t rgbBitCount;
	uint32_t rBitMask;
	uint32_t gBitMask;
	uint32_t bBitMask;
	uint32_t aBitMask;
	uint32_t caps;
	uint32_t caps2;
	uint32_t caps3;
	uint32_t caps4;
	uint32_t reserved2;
};

struct HeaderDxt10
{
	uint32_t dxgiFormat;
	uint32_t resourceDimension;
	uint32_t miscFlag;
	uint32_t arraySize;
	uint32_t miscFlags2;
};

static_assert(sizeof(Header) == kHeaderSize, "DDS_HEADER must be 124 bytes");
static_assert(sizeof(HeaderDxt10) == kDx10HeaderSize, "DDS_HEADER_DXT10 must be 20 bytes");
static_assert(sizeof(Header) % 4 == 0, "DDS_HEADER must stay 4-byte aligned in size");

} // Dds

/*
 * A parsed replacement image: the Vulkan format to create the image with and the mip
 * chain as it exists in the file.
 */
struct DdsTextureInfo
{
	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipLevels = 1;
	uint32_t blockBytes = 16;
	std::vector<uint8_t> payloadData;		// every accepted level, tightly packed
};

// Width or height of one mip level, clamped at 1 and safe for any level index.
inline uint32_t DdsMipDimension(uint32_t base, uint32_t level)
{
	return level >= 32 ? 1u : std::max(1u, base >> level);
}

/*
 * Bytes one mip level of a block-compressed image occupies.
 *
 * Every 4x4 block is one storage unit, and an incomplete block at the right or bottom
 * edge is still stored whole - hence the rounding up after the divide. The multiplies
 * are done in 64 bits because a 16384x16384 level 0 is 256 MB with BC1 and the
 * intermediate product would overflow 32 bits.
 */
inline uint64_t DdsMipByteSize(uint32_t width, uint32_t height, uint32_t level, uint32_t blockBytes)
{
	const uint64_t mipWidth = DdsMipDimension(width, level);
	const uint64_t mipHeight = DdsMipDimension(height, level);
	const uint64_t blocksWide = std::max<uint64_t>(1, (mipWidth + 3u) / 4u);
	const uint64_t blocksHigh = std::max<uint64_t>(1, (mipHeight + 3u) / 4u);
	return blocksWide * blocksHigh * blockBytes;
}

/*
 * Parses a whole DDS file held in memory.
 *
 * On success outInfo describes the image and owns a copy of its mip chain; the caller
 * can then create a VkImage with those dimensions/format and upload payloadData in a
 * single vkCmdCopyBufferToImage. On failure `error` says which check rejected the file
 * and outInfo is left untouched.
 *
 * A truncated mip chain is not an error: the accepted level count is reduced to the
 * levels that are completely present, because a pack author who ran a compressor
 * without mipmaps still has a usable image. Only a missing or incomplete level 0 is
 * fatal.
 */
inline bool ParseDdsFromMemory(const uint8_t* data, size_t size, DdsTextureInfo& outInfo, std::string& error)
{
	if (data == nullptr || size < sizeof(uint32_t) + Dds::kHeaderSize)
	{
		error = "the file is too small to hold a DDS header";
		return false;
	}

	uint32_t magic = 0;
	std::memcpy(&magic, data, sizeof(magic));
	if (magic != Dds::kMagic)
	{
		error = "the file does not start with the DDS signature";
		return false;
	}

	Dds::Header header = {};
	std::memcpy(&header, data + sizeof(uint32_t), sizeof(header));

	if (header.size != Dds::kHeaderSize)
	{
		error = "dwSize is not 124";
		return false;
	}
	if (header.pixelFormatSize != Dds::kPixelFormatSize)
	{
		error = "the pixel format header is not 32 bytes";
		return false;
	}
	if (header.width == 0 || header.height == 0)
	{
		error = "the image has a zero dimension";
		return false;
	}
	if (header.width > Dds::kMaxDimension || header.height > Dds::kMaxDimension)
	{
		error = "the image is larger than 16384 pixels on a side";
		return false;
	}
	if (header.depth > 1)
	{
		error = "volume textures are not supported";
		return false;
	}
	if ((header.caps2 & Dds::kCaps2Cubemap) != 0)
	{
		error = "cubemaps are not supported";
		return false;
	}

	VkFormat vkFormat = VK_FORMAT_UNDEFINED;
	uint32_t blockBytes = 16;
	size_t payloadOffset = sizeof(uint32_t) + Dds::kHeaderSize;

	if (header.fourCC == Dds::kFourCCDX10)
	{
		if (size < payloadOffset + Dds::kDx10HeaderSize)
		{
			error = "the DX10 extended header is truncated";
			return false;
		}

		Dds::HeaderDxt10 dx10 = {};
		std::memcpy(&dx10, data + payloadOffset, sizeof(dx10));
		payloadOffset += Dds::kDx10HeaderSize;

		if (dx10.resourceDimension != Dds::kDimensionTexture2D)
		{
			error = "only 2D textures are supported";
			return false;
		}
		if (dx10.arraySize > 1 || (dx10.miscFlag & Dds::kResourceMiscTextureCube) != 0)
		{
			error = "texture arrays and cubemaps are not supported";
			return false;
		}
		if (!Dds::FormatFromDxgi(dx10.dxgiFormat, vkFormat, blockBytes))
		{
			error = "DXGI format " + std::to_string(dx10.dxgiFormat) +
				" is not one of the supported BC formats (BC1/BC2/BC3/BC5/BC7)";
			return false;
		}
	}
	else
	{
		if ((header.pixelFormatFlags & Dds::kFourCC) == 0)
		{
			error = "the pixel format is not a FourCC format";
			return false;
		}
		if (!Dds::FormatFromFourCC(header.fourCC, vkFormat, blockBytes))
		{
			error = "FourCC is not one of DXT1, DXT3, DXT5, ATI2 or BC5U";
			return false;
		}
	}

	if (size <= payloadOffset)
	{
		error = "the file holds no texel data";
		return false;
	}

	/*
	 * How many levels to expect.
	 *
	 * dwMipMapCount is only meaningful when dwFlags says so, and some exporters write
	 * a number that is larger than the chain can possibly be (a 1x1 level 31 that a
	 * 64x64 image cannot have). The natural chain length therefore caps the claim, and
	 * the remaining bytes cap it again below.
	 */
	uint32_t declaredMips = header.mipMapCount == 0 ? 1u : header.mipMapCount;
	uint32_t naturalMips = 1;
	{
		uint32_t w = header.width;
		uint32_t h = header.height;
		while ((w > 1 || h > 1) && naturalMips < 32)
		{
			w = std::max(1u, w / 2);
			h = std::max(1u, h / 2);
			++naturalMips;
		}
	}
	const uint32_t wantedMips = std::min(declaredMips, naturalMips);

	const uint64_t available = (uint64_t)size - payloadOffset;
	uint64_t consumed = 0;
	uint32_t acceptedMips = 0;
	for (uint32_t level = 0; level < wantedMips; ++level)
	{
		const uint64_t levelBytes = DdsMipByteSize(header.width, header.height, level, blockBytes);
		// Overflow-free comparison: consumed <= available <= 512 MB by the kMaxPayloadBytes
		// test further down, and levelBytes is bounded by the dimension cap.
		if (levelBytes > available - consumed)
			break;
		consumed += levelBytes;
		++acceptedMips;
	}

	if (acceptedMips == 0)
	{
		error = "level 0 is not completely present";
		return false;
	}
	if (consumed > Dds::kMaxPayloadBytes)
	{
		error = "the mip chain is larger than 512 MB";
		return false;
	}

	outInfo.vkFormat = vkFormat;
	outInfo.width = header.width;
	outInfo.height = header.height;
	outInfo.mipLevels = acceptedMips;
	outInfo.blockBytes = blockBytes;
	outInfo.payloadData.assign(data + payloadOffset, data + payloadOffset + (size_t)consumed);
	return true;
}

} // Vulkan3D

#endif
