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
#ifndef INCLUDED_VULKAN3D_HDTEXTURES_H
#define INCLUDED_VULKAN3D_HDTEXTURES_H

#include "VulkanContext.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vulkan3D {

/*
 * How many replacement textures a session can bind at once.
 *
 * This is the length of the shader's descriptor array, so it is also the number of
 * descriptors the pool reserves. 2048 slots cover every pack we know of with room
 * to spare, and an unbound slot costs nothing at run time: the binding is partially
 * bound and the shader only indexes a slot that has been resolved.
 */
constexpr uint32_t kMaxHDTextures = 2048;

// Rough ceiling on device memory held by replacement textures, including mips. A pack larger than
// this is truncated rather than allowed to exhaust the heap; the log says so.
constexpr uint64_t kMaxHDTextureBytes = 3ull * 1024ull * 1024ull * 1024ull;

/*
 * The four file kinds a pack may use, in the order they win when one hash has several.
 *
 * Compressed DDS comes first because it is the smallest on disk and the cheapest to upload -
 * the data goes to the GPU untouched. The uncompressed DDS is next, then the old WebP/PNG pair
 * (a lossless PNG still beating a lossy WebP, as before).
 */
enum class HDTextureFileKind : uint8_t
{
	Png = 0,
	Webp = 1,
	Dds = 2,
	DdsZst = 3
};

/*
 * Hot reload request, from the interface to the renderer.
 *
 * The button in the in-game panel and the F4 binding both call RequestHDTextureReload();
 * CVulkan3D::BuildDrawList() compares the serial against the last one it applied. A plain
 * process-wide counter is enough because there is exactly one renderer per session, and it
 * avoids the interface having to know anything about the renderer object.
 */
void RequestHDTextureReload();
uint32_t HDTextureReloadSerial();

/*
 * 64-bit content hash of one base-level sub-texture.
 *
 * `pageRam` points at the first texel of the texture page, `x`/`y` are page-local,
 * and the rectangle is read with the same 2048x1024 wrap mask the shader applies,
 * so a texture that crosses the page edge hashes exactly the texels it samples.
 * The format, width and height are folded into the hash as well: the same 16-bit
 * words interpreted as RGBA4 and as T1RGB5 are different pictures, and they must
 * not share a replacement.
 *
 * FNV-1a rather than XXH64: a few thousand hashes per second at most, and this
 * keeps the feature dependency-free.
 */
uint64_t HashTextureRegion(const uint16_t* pageRam, int x, int y, int width, int height,
	int format, int pageWidth = 2048, int pageHeight = 1024);

/*
 * HD texture pack for one ROM set.
 *
 * The CPU side (scanning, decoding, dumping) and the GPU side (images, samplers,
 * mip generation, descriptors) live together because they share one slot table:
 * a slot is reserved when a referenced hash is found in the pack, and becomes
 * sampleable only once its upload has finished. Until then Resolve() reports -1
 * and the material keeps sampling the original packed page, which is why a large
 * pack never shows white or uninitialised textures while it loads.
 *
 * A replacement may be a PNG or WebP that is decoded to RGBA8 on the CPU, or a
 * DDS (optionally zstd-compressed) that is passed through as block-compressed data
 * and uploaded with its own mip chain. The two paths differ only in how the image
 * is created and filled; once a slot is ready the shader cannot tell them apart.
 */
class CHDTexturePack
{
public:
	CHDTexturePack();
	~CHDTexturePack();

	bool Init(CVulkanContext& context, const std::string& gameName, VkDescriptorSet descriptorSet);
	void Shutdown();

	void SetGameName(const std::string& gameName);

	// Called once per simulation frame, before the draw list is built.
	void Tick(bool loadEnabled, bool dumpEnabled);

	bool LoadEnabled() const { return m_loadEnabled; }
	bool DumpEnabled() const { return m_dumpEnabled; }
	bool IndexEmpty() const { return m_index.empty(); }

	// Bindless slot for a hash, or -1 when the pack has no usable replacement (yet).
	int Resolve(uint64_t hash, int wrapU, int wrapV);

	// Snapshots a decoded sub-texture and queues the PNG write on the dump thread.
	void QueueDump(uint64_t hash, int page, int x, int y, int width, int height, int format,
		const uint16_t* pageRam, int pageWidth, int pageHeight);

	// Creates pending images, finishes their uploads/mips and publishes descriptors.
	void RecordUploads(VkCommandBuffer cmd, CVulkanRingBuffer& ring);

	const std::string& Directory() const { return m_directory; }
	uint32_t LoadedCount() const { return m_loadedCount; }
	uint32_t PendingCount() const { return m_pendingCount; }

private:
	struct LoadKey
	{
		uint64_t hash = 0;
		uint32_t wrap = 0;
		bool operator==(const LoadKey& other) const { return hash == other.hash && wrap == other.wrap; }
	};
	struct LoadKeyHash
	{
		size_t operator()(const LoadKey& key) const
		{
			return (size_t)(key.hash ^ ((uint64_t)key.wrap * 0x9E3779B97F4A7C15ull));
		}
	};

	/*
	 * The pack's index and slots use HDTextureFileKind directly; the alias keeps the member
	 * declarations below readable.
	 */
	using FileKind = HDTextureFileKind;

	struct IndexEntry
	{
		std::string path;
		FileKind kind = FileKind::Png;
	};

	struct Slot
	{
		uint64_t hash = 0;
		uint32_t wrap = 0;
		AllocatedImage image;
		VkSampler sampler = VK_NULL_HANDLE;
		std::vector<uint8_t> pixels;		// decoded RGBA, mip 0 only
		std::vector<uint8_t> payload;		// block-compressed mip chain, DDS only
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mipLevels = 1;
		uint32_t blockBytes = 4;			// bytes per 4x4 block when compressed, 4 otherwise
		uint32_t uploadRow = 0;			// next row of the RGBA image to copy
		uint32_t uploadMip = 0;			// next level of a compressed chain to copy
		uint32_t uploadBlockRow = 0;		// next 4x4 block row within uploadMip
		uint32_t uploadCalls = 0;			// chunks recorded so far (compressed only)
		uint32_t uploadPass = 0;			// m_uploadPass when the slot was last touched
		uint32_t uploadFrames = 0;			// separate upload passes (frames) used so far
		bool compressed = false;			// image holds BC blocks and is filled from the file's mip chain
		bool loading = false;				// slot reserved, file not decoded yet
		bool failed = false;
		bool ready = false;					// image is complete and its descriptor written
	};

	struct LoadJob
	{
		uint64_t hash = 0;
		uint32_t wrap = 0;
		uint32_t slot = 0;
		uint32_t generation = 0;
		std::string path;
		FileKind kind = FileKind::Png;
	};

	struct LoadResult
	{
		uint32_t slot = 0;
		uint32_t generation = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mipLevels = 1;
		uint32_t blockBytes = 16;
		VkFormat format = VK_FORMAT_UNDEFINED;
		bool compressed = false;
		std::vector<uint8_t> pixels;    // RGBA for PNG/WebP
		std::vector<uint8_t> payload;   // BC mip chain for DDS
		std::string error;
	};

	struct DumpJob
	{
		std::string path;
		uint32_t width = 0;
		uint32_t height = 0;
		int format = 0;
		std::vector<uint16_t> texels;
	};

	void Scan();
	void ResetSlots();
	void ApplyReload();
	bool EnsureDirectory(const std::string& path);

	void StartLoader();
	void StopLoader();
	void LoaderMain();
	void StartDumper();
	void StopDumper();
	void DumperMain();

	VkSampler CreateSampler(int wrapU, int wrapV);
	bool UploadMip0(Slot& slot, VkCommandBuffer cmd, CVulkanRingBuffer& ring);
	bool UploadCompressed(Slot& slot, VkCommandBuffer cmd, CVulkanRingBuffer& ring);
	void FinishImage(Slot& slot, VkCommandBuffer cmd);
	void FinishCompressedImage(Slot& slot, VkCommandBuffer cmd);
	bool CompressedFormatUsable(VkFormat format) const;
	void WriteDescriptor(uint32_t index, const Slot& slot);

	static std::string SanitizedName(const std::string& name);
	static bool ParseHashFromFilename(const std::string& stem, uint64_t& hash);

	CVulkanContext* m_context = nullptr;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	std::string m_gameName;
	std::string m_directory;

	bool m_loadEnabled = false;
	bool m_dumpEnabled = false;
	bool m_scanned = false;
	bool m_anisotropy = false;
	bool m_supportsBC = false;		// device can sample BC images; DDS files are skipped otherwise
	bool m_zstdAvailable = false;	// Model3VulkanHostCore exports HostCore_DecompressZstd
	float m_maxAnisotropy = 1.0f;
	uint32_t m_maxImageDim = 4096;
	uint32_t m_reloadSerial = 0;
	uint32_t m_generation = 0;
	// Counts calls to RecordUploads(), i.e. simulation frames that could carry texture data. A
	// compressed chain that does not fit in one frame's ring slice records how many of these it
	// spanned, which is the only way to tell a deliberate deferral from a stuck upload.
	uint32_t m_uploadPass = 0;
	uint64_t m_bytes = 0;
	uint32_t m_loadedCount = 0;
	uint32_t m_pendingCount = 0;
	uint32_t m_failedCount = 0;
	uint32_t m_skippedBudget = 0;
	uint32_t m_missingReported = 0;

	std::unordered_map<uint64_t, IndexEntry> m_index;		// hash -> highest-priority file
	std::unordered_set<uint64_t> m_missing;						// hashes asked for and absent
	std::unordered_map<LoadKey, uint32_t, LoadKeyHash> m_lookup;
	std::vector<Slot> m_slots;

	std::thread m_loader;
	std::mutex m_loaderMutex;
	std::condition_variable m_loaderCv;
	std::deque<LoadJob> m_loadJobs;
	std::vector<LoadResult> m_loadResults;
	bool m_loaderStop = false;

	std::thread m_dumper;
	std::mutex m_dumperMutex;
	std::condition_variable m_dumperCv;
	std::deque<DumpJob> m_dumpJobs;
	bool m_dumperStop = false;
	uint32_t m_dumpedCount = 0;
	uint32_t m_dumpErrors = 0;
	bool m_dumpStarted = false;
	bool m_firstResolveLogged = false;
};

} // Vulkan3D

#endif
