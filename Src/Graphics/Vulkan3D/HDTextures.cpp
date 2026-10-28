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
#include "HDTextures.h"
#include "TextureDecode.h"

#include "OSD/FileSystemPath.h"
#include "OSD/Logger.h"
#include "Util/PNGFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32) || defined(__linux__)
#include <webp/decode.h>
#endif

namespace Vulkan3D {

namespace {

std::atomic<uint32_t> s_reloadSerial(0);

constexpr uint32_t kDumpChunkBytes = 4u * 1024u * 1024u;
constexpr uint32_t kMaxDumpsPerSession = 50000;

uint32_t MipCount(uint32_t width, uint32_t height)
{
	uint32_t mips = 1;
	while ((width > 1 || height > 1) && mips < 16)
	{
		width = std::max(1u, width / 2);
		height = std::max(1u, height / 2);
		++mips;
	}
	return mips;
}

std::string LowerExtension(const std::filesystem::path& path)
{
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return (char)std::tolower(c); });
	return ext;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& data)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return false;
	const std::streamoff size = file.tellg();
	if (size <= 0 || size > (std::streamoff)(256ll * 1024 * 1024))
		return false;
	data.resize((size_t)size);
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size());
	return file.good() || file.eof();
}

bool DecodeImageFile(const std::string& path, std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, std::string& error)
{
	const std::string ext = LowerExtension(path);
	std::vector<uint8_t> fileBytes;
	if (!ReadFileBytes(path, fileBytes))
	{
		error = "the file could not be read";
		return false;
	}

	if (ext == ".png")
	{
		std::string decodeError;
		if (!Util::PNG::DecodeRGBA(fileBytes.data(), fileBytes.size(), pixels, width, height, &decodeError))
		{
			error = decodeError.empty() ? "PNG decode failed" : decodeError;
			return false;
		}
		return true;
	}

#if defined(_WIN32) || defined(__linux__)
	if (ext == ".webp")
	{
		int w = 0, h = 0;
		if (WebPGetInfo(fileBytes.data(), fileBytes.size(), &w, &h) == 0 || w <= 0 || h <= 0)
		{
			error = "WebP header is invalid";
			return false;
		}
		uint8_t* decoded = WebPDecodeRGBA(fileBytes.data(), fileBytes.size(), &w, &h);
		if (decoded == nullptr)
		{
			error = "WebP decode failed";
			return false;
		}
		width = (uint32_t)w;
		height = (uint32_t)h;
		pixels.assign(decoded, decoded + (size_t)w * h * 4);
		WebPFree(decoded);
		return true;
	}
#endif

	error = "unsupported image format";
	return false;
}

/*
 * Address mode for one axis of a replacement image.
 *
 * Every wrap mode the real hardware offers repeats: mode 0 repeats within the page,
 * mode 1 repeats within the sub-texture and clamps its bilinear taps to the edges,
 * mode 2/3 mirror. The replacement image holds exactly the sub-texture, so the edge
 * clamp has nothing left to protect - and a CLAMP_TO_EDGE sampler turns every tile
 * boundary into a seam as wide as the filter footprint (visible stripes on a road
 * seen at a grazing angle). REPEAT/MIRRORED_REPEAT reproduce the tiling without it.
 */
VkSamplerAddressMode AddressModeForWrap(int wrap, bool /*mirrorClampSupported*/)
{
	switch (wrap)
	{
	case 0:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case 1:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	default: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	}
}

} // anonymous namespace

void RequestHDTextureReload()
{
	s_reloadSerial.fetch_add(1, std::memory_order_relaxed);
}

uint32_t HDTextureReloadSerial()
{
	return s_reloadSerial.load(std::memory_order_relaxed);
}

uint64_t HashTextureRegion(const uint16_t* pageRam, int x, int y, int width, int height,
	int format, int pageWidth, int pageHeight)
{
	if (pageRam == nullptr || width <= 0 || height <= 0)
		return 0;

	const uint64_t kOffset = 1469598103934665603ull;
	const uint64_t kPrime = 1099511628211ull;
	uint64_t hash = kOffset;

	auto mixByte = [&hash, kPrime](uint8_t value)
	{
		hash ^= (uint64_t)value;
		hash *= kPrime;
	};
	auto mixU32 = [&mixByte](uint32_t value)
	{
		mixByte((uint8_t)(value & 0xFF));
		mixByte((uint8_t)((value >> 8) & 0xFF));
		mixByte((uint8_t)((value >> 16) & 0xFF));
		mixByte((uint8_t)((value >> 24) & 0xFF));
	};

	// Format and rectangle belong to the picture's identity: the same words decoded as RGBA4 are a
	// different image from T1RGB5, and two rectangles of different sizes are different sub-textures
	// even when their first pixels happen to agree.
	mixU32((uint32_t)format);
	mixU32((uint32_t)width);
	mixU32((uint32_t)height);

	const int maskX = pageWidth - 1;
	const int maskY = pageHeight - 1;
	for (int row = 0; row < height; ++row)
	{
		const uint16_t* line = pageRam + (((y + row) & maskY) * pageWidth);
		for (int col = 0; col < width; ++col)
		{
			const uint16_t value = line[((x + col) & maskX)];
			mixByte((uint8_t)(value & 0xFF));
			mixByte((uint8_t)(value >> 8));
		}
	}
	return hash;
}

CHDTexturePack::CHDTexturePack() = default;

CHDTexturePack::~CHDTexturePack()
{
	Shutdown();
}

bool CHDTexturePack::Init(CVulkanContext& context, const std::string& gameName, VkDescriptorSet descriptorSet)
{
	m_context = &context;
	m_descriptorSet = descriptorSet;
	SetGameName(gameName);

	VkPhysicalDeviceProperties props = {};
	vkGetPhysicalDeviceProperties(context.PhysicalDevice(), &props);
	m_maxImageDim = std::min<uint32_t>(props.limits.maxImageDimension2D, 4096u);
	m_maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
	VkPhysicalDeviceFeatures features = {};
	vkGetPhysicalDeviceFeatures(context.PhysicalDevice(), &features);
	m_anisotropy = features.samplerAnisotropy != 0 && m_maxAnisotropy > 1.0f;

	StartLoader();
	InfoLog("HD textures: %s filters, up to %ux%u replacement images, %u bindless slot(s).",
		m_anisotropy ? "anisotropic" : "trilinear", m_maxImageDim, m_maxImageDim, kMaxHDTextures);
	return true;
}

void CHDTexturePack::Shutdown()
{
	StopLoader();
	StopDumper();
	if (m_dumper.joinable())
		m_dumper.join();

	if (m_context != nullptr && m_context->Device() != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(m_context->Device());
		ResetSlots();
	}
	else
	{
		m_slots.clear();
	}
	m_index.clear();
	m_missing.clear();
	m_lookup.clear();
	m_scanned = false;
	m_loadEnabled = false;
	m_dumpEnabled = false;
	m_descriptorSet = VK_NULL_HANDLE;
	m_context = nullptr;
}

void CHDTexturePack::SetGameName(const std::string& gameName)
{
	m_gameName = SanitizedName(gameName);
	m_directory = FileSystemPath::GetPath(FileSystemPath::Assets) + "HDTextures/" + m_gameName;
}

void CHDTexturePack::Tick(bool loadEnabled, bool dumpEnabled)
{
	m_loadEnabled = loadEnabled;
	m_dumpEnabled = dumpEnabled;

	if (dumpEnabled && !m_dumpStarted)
		StartDumper();

	const uint32_t serial = HDTextureReloadSerial();
	if (serial != m_reloadSerial)
	{
		m_reloadSerial = serial;
		ApplyReload();
	}

	if (loadEnabled && !m_scanned)
		Scan();
}

void CHDTexturePack::ApplyReload()
{
	if (m_context != nullptr && m_context->Device() != VK_NULL_HANDLE)
		vkDeviceWaitIdle(m_context->Device());
	ResetSlots();

	{
		std::lock_guard<std::mutex> guard(m_loaderMutex);
		m_loadJobs.clear();
		m_loadResults.clear();
	}
	++m_generation;

	m_index.clear();
	m_missing.clear();
	m_lookup.clear();
	m_scanned = false;
	m_bytes = 0;
	m_loadedCount = 0;
	m_pendingCount = 0;
	m_failedCount = 0;
	m_skippedBudget = 0;
	m_missingReported = 0;

	InfoLog("HD textures: reloading the pack for '%s' from '%s'.", m_gameName.c_str(), m_directory.c_str());
}

void CHDTexturePack::ResetSlots()
{
	if (m_context == nullptr || m_context->Device() == VK_NULL_HANDLE)
	{
		m_slots.clear();
		return;
	}
	for (Slot& slot : m_slots)
	{
		if (slot.image.view != VK_NULL_HANDLE)
		{
			m_context->DestroyView(slot.image.view);
			slot.image.view = VK_NULL_HANDLE;
		}
		if (slot.sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_context->Device(), slot.sampler, nullptr);
			slot.sampler = VK_NULL_HANDLE;
		}
		if (slot.image.image != VK_NULL_HANDLE)
			m_context->Memory().DestroyImage(slot.image);
	}
	m_slots.clear();
}

void CHDTexturePack::Scan()
{
	m_scanned = true;
	m_index.clear();

	std::error_code ec;
	if (!std::filesystem::exists(m_directory, ec) || ec)
	{
		InfoLog("HD textures: no pack for '%s' (looked in '%s').", m_gameName.c_str(), m_directory.c_str());
		return;
	}

	uint32_t files = 0;
	uint32_t duplicates = 0;
	uint32_t unnamed = 0;

	for (std::filesystem::recursive_directory_iterator it(m_directory,
			std::filesystem::directory_options::skip_permission_denied, ec), end;
		it != end; it.increment(ec))
	{
		if (ec)
			break;
		if (!it->is_regular_file(ec))
			continue;

		const std::filesystem::path& path = it->path();
		const std::string ext = LowerExtension(path);
		if (ext != ".png" && ext != ".webp")
			continue;

		uint64_t hash = 0;
		if (!ParseHashFromFilename(path.stem().string(), hash))
		{
			++unnamed;
			continue;
		}

		auto found = m_index.find(hash);
		if (found == m_index.end())
		{
			m_index.emplace(hash, path.string());
			++files;
		}
		else
		{
			// Two files for one hash: prefer the lossless one, keep the first if both are equal.
			++duplicates;
			const std::string existingExt = LowerExtension(found->second);
			if (ext == ".png" && existingExt != ".png")
				found->second = path.string();
		}
	}

	InfoLog("HD textures: %u replacement(s) indexed in '%s' (duplicate hashes %u, files without a hash %u).",
		files, m_directory.c_str(), duplicates, unnamed);
}

bool CHDTexturePack::EnsureDirectory(const std::string& path)
{
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	if (ec)
	{
		ErrorLog("HD textures: cannot create '%s': %s", path.c_str(), ec.message().c_str());
		return false;
	}
	return true;
}

int CHDTexturePack::Resolve(uint64_t hash, int wrapU, int wrapV)
{
	if (!m_loadEnabled || hash == 0 || m_descriptorSet == VK_NULL_HANDLE || m_index.empty())
		return -1;

	const uint32_t wrap = (uint32_t)(wrapU & 3) | ((uint32_t)(wrapV & 3) << 2);
	const LoadKey key{ hash, wrap };

	auto found = m_lookup.find(key);
	if (found != m_lookup.end())
	{
		const Slot& slot = m_slots[found->second];
		if (slot.ready && !slot.failed)
		{
			if (!m_firstResolveLogged)
			{
				m_firstResolveLogged = true;
				InfoLog("HD textures: first draw using replacement slot %u.", found->second);
			}
			return (int)found->second;
		}
		return -1;
	}

	if (m_missing.find(hash) != m_missing.end())
		return -1;

	// A long session with animated textures can ask about thousands of hashes that no pack entry
	// covers. The set is only a memo; dropping it costs one extra lookup per live texture and keeps
	// the memory bounded.
	if (m_missing.size() > 65536)
		m_missing.clear();

	auto file = m_index.find(hash);
	if (file == m_index.end())
	{
		// Remembered so a texture the pack does not cover is not looked up on every draw of every
		// frame. Cleared by a reload, which is what makes dropping a file in while playing work.
		if (m_missing.size() < 4)
			InfoLog("HD textures: no replacement for hash %016llx.", (unsigned long long)hash);
		m_missing.insert(hash);
		return -1;
	}

	if (m_slots.size() >= kMaxHDTextures)
	{
		if (m_missingReported++ == 0)
			ErrorLog("HD textures: all %u bindless slots are in use; '%s' and later replacements are "
				"ignored for this session.", kMaxHDTextures, m_gameName.c_str());
		return -1;
	}

	const uint32_t index = (uint32_t)m_slots.size();
	m_slots.emplace_back();
	Slot& slot = m_slots.back();
	slot.hash = hash;
	slot.wrap = wrap;
	slot.loading = true;
	m_lookup.emplace(key, index);
	++m_pendingCount;
	if (m_lookup.size() == 1)
		InfoLog("HD textures: first match - hash %016llx -> '%s' (slot %u).",
			(unsigned long long)hash, file->second.c_str(), index);

	{
		std::lock_guard<std::mutex> guard(m_loaderMutex);
		LoadJob job;
		job.hash = hash;
		job.wrap = wrap;
		job.slot = index;
		job.path = file->second;
		job.generation = m_generation;
		m_loadJobs.push_back(std::move(job));
	}
	m_loaderCv.notify_one();
	return -1;
}

void CHDTexturePack::QueueDump(uint64_t hash, int page, int x, int y, int width, int height,
	int format, const uint16_t* pageRam, int pageWidth, int pageHeight)
{
	if (!m_dumpEnabled || hash == 0 || pageRam == nullptr || width <= 0 || height <= 0)
		return;

	{
		std::lock_guard<std::mutex> guard(m_dumperMutex);
		if (m_dumpedCount + (uint32_t)m_dumpJobs.size() >= kMaxDumpsPerSession)
			return;
	}

	const std::string directory = FileSystemPath::GetPath(FileSystemPath::Assets) + "DumpTextures/" + m_gameName;
	if (!EnsureDirectory(directory))
		return;

	// Snapshot the texels now, with the same wrap mask the shader and the hash use. Handing the
	// page pointer to the worker would let the game rewrite the region while the file is being
	// encoded, which is exactly how a dump ends up not matching its own hash.
	DumpJob job;
	job.width = (uint32_t)width;
	job.height = (uint32_t)height;
	job.format = format;
	job.texels.resize((size_t)width * height);
	const int maskX = pageWidth - 1;
	const int maskY = pageHeight - 1;
	for (int row = 0; row < height; ++row)
	{
		const uint16_t* line = pageRam + (((y + row) & maskY) * pageWidth);
		uint16_t* dst = job.texels.data() + (size_t)row * width;
		for (int col = 0; col < width; ++col)
			dst[col] = line[(x + col) & maskX];
	}

	char name[160];
	std::snprintf(name, sizeof(name), "tex_%d_%d_%d_%dx%d_fmt%d_%016llx.png",
		page & 1, x, y, width, height, format, (unsigned long long)hash);
	job.path = directory + "/" + name;

	{
		std::lock_guard<std::mutex> guard(m_dumperMutex);
		m_dumpJobs.push_back(std::move(job));
	}
	m_dumperCv.notify_one();
}

void CHDTexturePack::StartLoader()
{
	if (m_loader.joinable())
		return;
	m_loaderStop = false;
	m_loader = std::thread(&CHDTexturePack::LoaderMain, this);
}

void CHDTexturePack::StopLoader()
{
	if (!m_loader.joinable())
		return;
	{
		std::lock_guard<std::mutex> guard(m_loaderMutex);
		m_loaderStop = true;
	}
	m_loaderCv.notify_all();
	m_loader.join();
}

void CHDTexturePack::LoaderMain()
{
	for (;;)
	{
		LoadJob job;
		{
			std::unique_lock<std::mutex> lock(m_loaderMutex);
			m_loaderCv.wait(lock, [this] { return m_loaderStop || !m_loadJobs.empty(); });
			if (m_loaderStop)
				return;
			job = std::move(m_loadJobs.front());
			m_loadJobs.pop_front();
		}

		LoadResult result;
		result.slot = job.slot;
		result.generation = job.generation;
		std::string error;
		if (!DecodeImageFile(job.path, result.pixels, result.width, result.height, error))
		{
			result.error = error;
			result.pixels.clear();
		}

		{
			std::lock_guard<std::mutex> guard(m_loaderMutex);
			m_loadResults.push_back(std::move(result));
		}
	}
}

void CHDTexturePack::StartDumper()
{
	if (m_dumper.joinable())
	{
		m_dumpStarted = true;
		return;
	}
	m_dumperStop = false;
	m_dumpStarted = true;
	m_dumper = std::thread(&CHDTexturePack::DumperMain, this);
}

void CHDTexturePack::StopDumper()
{
	if (!m_dumper.joinable())
		return;
	{
		std::lock_guard<std::mutex> guard(m_dumperMutex);
		m_dumperStop = true;
	}
	m_dumperCv.notify_all();
	m_dumper.join();
	m_dumpStarted = false;
}

void CHDTexturePack::DumperMain()
{
	for (;;)
	{
		DumpJob job;
		{
			std::unique_lock<std::mutex> lock(m_dumperMutex);
			m_dumperCv.wait(lock, [this] { return m_dumperStop || !m_dumpJobs.empty(); });
			if (m_dumperStop && m_dumpJobs.empty())
				return;
			if (m_dumpJobs.empty())
				continue;
			job = std::move(m_dumpJobs.front());
			m_dumpJobs.pop_front();
		}

		std::vector<uint8_t> rgba((size_t)job.width * job.height * 4);
		for (size_t i = 0; i < job.texels.size(); ++i)
			DecodeTexel(job.format, job.texels[i], &rgba[i * 4]);

		// WriteRGBA follows the older helper convention and reports true on failure.
		if (Util::PNG::WriteRGBA(job.path, rgba.data(), (int32_t)job.width, (int32_t)job.height))
		{
			if (m_dumpErrors++ == 0)
				ErrorLog("HD textures: could not write '%s'.", job.path.c_str());
		}
		else
		{
			std::lock_guard<std::mutex> guard(m_dumperMutex);
			++m_dumpedCount;
			if (m_dumpedCount == 1)
				InfoLog("HD textures: dumped the first texture to '%s'.", job.path.c_str());
		}
	}
}

VkSampler CHDTexturePack::CreateSampler(int wrapU, int wrapV)
{
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.magFilter = VK_FILTER_LINEAR;
	info.minFilter = VK_FILTER_LINEAR;
	info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	info.addressModeU = AddressModeForWrap(wrapU, m_context->SamplerMirrorClampSupported());
	info.addressModeV = AddressModeForWrap(wrapV, m_context->SamplerMirrorClampSupported());
	info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	info.mipLodBias = 0.0f;
	info.anisotropyEnable = m_anisotropy ? VK_TRUE : VK_FALSE;
	info.maxAnisotropy = m_anisotropy ? m_maxAnisotropy : 1.0f;
	info.compareEnable = VK_FALSE;
	info.minLod = 0.0f;
	info.maxLod = VK_LOD_CLAMP_NONE;
	info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	info.unnormalizedCoordinates = VK_FALSE;

	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(m_context->Device(), &info, nullptr, &sampler) != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return sampler;
}

void CHDTexturePack::WriteDescriptor(uint32_t index, const Slot& slot)
{
	if (m_descriptorSet == VK_NULL_HANDLE || slot.image.view == VK_NULL_HANDLE || slot.sampler == VK_NULL_HANDLE)
		return;

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.sampler = slot.sampler;
	imageInfo.imageView = slot.image.view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptorSet;
	write.dstBinding = 0;
	write.dstArrayElement = index;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_context->Device(), 1, &write, 0, nullptr);
}

bool CHDTexturePack::UploadMip0(Slot& slot, VkCommandBuffer cmd, CVulkanRingBuffer& ring)
{
	if (slot.pixels.empty())
		return true;

	if (slot.layout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		CmdTransition(cmd, slot.image.image, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
		slot.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	if (slot.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		return false;

	const VkDeviceSize rowBytes = (VkDeviceSize)slot.width * 4;
	const uint32_t rowsPerChunk = (uint32_t)std::max<VkDeviceSize>(1,
		std::min<VkDeviceSize>(slot.height, kDumpChunkBytes / rowBytes));

	while (slot.uploadRow < slot.height)
	{
		const uint32_t rows = std::min(rowsPerChunk, slot.height - slot.uploadRow);
		const VkDeviceSize bytes = rowBytes * rows;
		VkDeviceSize offset = 0;
		void* ptr = nullptr;
		if (!ring.Alloc(bytes, 4, offset, ptr))
			return false;
		std::memcpy(ptr, slot.pixels.data() + (size_t)slot.uploadRow * rowBytes, (size_t)bytes);

		VkBufferImageCopy copy = {};
		copy.bufferOffset = offset;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset = { 0, (int32_t)slot.uploadRow, 0 };
		copy.imageExtent = { slot.width, rows, 1 };
		vkCmdCopyBufferToImage(cmd, ring.Buffer(), slot.image.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		slot.uploadRow += rows;
	}
	return true;
}

void CHDTexturePack::FinishImage(Slot& slot, VkCommandBuffer cmd)
{
	const uint32_t mips = slot.mipLevels;
	if (mips > 1)
	{
		CmdTransition(cmd, slot.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
		for (uint32_t level = 1; level < mips; ++level)
		{
			CmdTransition(cmd, slot.image.image, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);

			VkImageBlit blit = {};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = level - 1;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { (int32_t)std::max(1u, slot.width >> (level - 1)),
				(int32_t)std::max(1u, slot.height >> (level - 1)), 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = level;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { (int32_t)std::max(1u, slot.width >> level),
				(int32_t)std::max(1u, slot.height >> level), 1 };
			vkCmdBlitImage(cmd, slot.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				slot.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			if (level + 1 < mips)
			{
				CmdTransition(cmd, slot.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);
			}
		}
		for (uint32_t level = 0; level < mips; ++level)
		{
			const VkImageLayout oldLayout = (level + 1 < mips)
				? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			CmdTransition(cmd, slot.image.image, oldLayout,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, level, 1);
		}
	}
	else
	{
		CmdTransition(cmd, slot.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
	}

	slot.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	slot.pixels.clear();
	slot.pixels.shrink_to_fit();
	slot.ready = true;
	slot.loading = false;
	m_bytes += slot.image.size;
	++m_loadedCount;
}

void CHDTexturePack::RecordUploads(VkCommandBuffer cmd, CVulkanRingBuffer& ring)
{
	if (m_context == nullptr)
		return;

	std::vector<LoadResult> results;
	{
		std::lock_guard<std::mutex> guard(m_loaderMutex);
		results.swap(m_loadResults);
	}

	for (LoadResult& result : results)
	{
		if (result.generation != m_generation || result.slot >= m_slots.size())
			continue;

		Slot& slot = m_slots[result.slot];
		if (slot.ready || slot.failed)
			continue;

		m_pendingCount = m_pendingCount > 0 ? m_pendingCount - 1 : 0;

		if (!result.error.empty())
		{
			slot.failed = true;
			slot.loading = false;
			if (m_failedCount++ < 8)
				ErrorLog("HD textures: slot %u could not be loaded: %s", result.slot, result.error.c_str());
			continue;
		}

		if (result.width == 0 || result.height == 0 ||
			result.width > m_maxImageDim || result.height > m_maxImageDim)
		{
			slot.failed = true;
			slot.loading = false;
			if (m_failedCount++ < 8)
				ErrorLog("HD textures: slot %u is %ux%u, outside the supported range.",
					result.slot, result.width, result.height);
			continue;
		}

		const uint64_t estimated = (uint64_t)result.width * result.height * 4ull;
		if (m_bytes + estimated > kMaxHDTextureBytes)
		{
			slot.failed = true;
			slot.loading = false;
			if (m_skippedBudget++ == 0)
				ErrorLog("HD textures: the %llu MB budget is exhausted; '%s' and later replacements are ignored.",
					(unsigned long long)(kMaxHDTextureBytes / (1024ull * 1024ull)), m_gameName.c_str());
			continue;
		}

		const uint32_t mips = MipCount(result.width, result.height);
		if (!m_context->Memory().CreateImage(result.width, result.height, mips,
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				slot.image))
		{
			slot.failed = true;
			slot.loading = false;
			if (m_failedCount++ < 8)
				ErrorLog("HD textures: a %ux%u image could not be allocated.", result.width, result.height);
			continue;
		}
		slot.image.view = m_context->CreateView(slot.image.image, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, mips);
		if (slot.image.view == VK_NULL_HANDLE)
		{
			m_context->Memory().DestroyImage(slot.image);
			slot.failed = true;
			slot.loading = false;
			continue;
		}

		slot.sampler = CreateSampler(slot.wrap & 3, (slot.wrap >> 2) & 3);
		if (slot.sampler == VK_NULL_HANDLE)
		{
			m_context->DestroyView(slot.image.view);
			slot.image.view = VK_NULL_HANDLE;
			m_context->Memory().DestroyImage(slot.image);
			slot.failed = true;
			slot.loading = false;
			continue;
		}

		slot.width = result.width;
		slot.height = result.height;
		slot.mipLevels = mips;
		slot.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		slot.uploadRow = 0;
		slot.pixels = std::move(result.pixels);
		// The file has been decoded; the image now awaits its upload. Leaving `loading` set here made
		// the upload loop below skip it forever, because that loop treats a loading slot as one whose
		// pixels do not exist yet.
		slot.loading = false;
	}

	for (uint32_t i = 0; i < m_slots.size(); ++i)
	{
		Slot& slot = m_slots[i];
		if (slot.ready || slot.failed || slot.loading || slot.pixels.empty())
			continue;
		if (!UploadMip0(slot, cmd, ring))
			break;
		FinishImage(slot, cmd);
		WriteDescriptor(i, slot);
		if (m_loadedCount == 1)
			InfoLog("HD textures: first replacement is sampleable (%ux%u, %u mip(s)).",
				slot.width, slot.height, slot.mipLevels);
	}
}

std::string CHDTexturePack::SanitizedName(const std::string& name)
{
	std::string out;
	out.reserve(name.size());
	for (char c : name)
	{
		const unsigned char u = (unsigned char)c;
		if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
			c == '|' || c == '?' || c == '*')
			out.push_back('_');
		else
			out.push_back(c);
	}
	if (out.empty() || out == "." || out == "..")
		out = "unknown";
	return out;
}

bool CHDTexturePack::ParseHashFromFilename(const std::string& stem, uint64_t& hash)
{
	// The exporter writes ..._<16 hex>.png, but a hand-organised pack may name files
	// <hash>.png or <anything>_<hash>_<description>.  Splitting on the usual separators
	// and taking the last bare 16-digit hexadecimal token covers all three without
	// being confused by hexadecimal-looking words elsewhere in the name.
	uint64_t found = 0;
	bool have = false;
	size_t start = 0;
	while (start <= stem.size())
	{
		size_t end = start;
		while (end < stem.size() && stem[end] != '_' && stem[end] != '-' && stem[end] != ' ')
			++end;
		const size_t length = end - start;
		if (length == 16)
		{
			uint64_t value = 0;
			bool valid = true;
			for (size_t i = start; i < end; ++i)
			{
				const char c = stem[i];
				int digit = -1;
				if (c >= '0' && c <= '9')
					digit = c - '0';
				else if (c >= 'a' && c <= 'f')
					digit = 10 + (c - 'a');
				else if (c >= 'A' && c <= 'F')
					digit = 10 + (c - 'A');
				if (digit < 0)
				{
					valid = false;
					break;
				}
				value = (value << 4) | (uint64_t)digit;
			}
			if (valid)
			{
				found = value;
				have = true;
			}
		}
		if (end >= stem.size())
			break;
		start = end + 1;
	}
	hash = found;
	return have;
}

} // Vulkan3D
