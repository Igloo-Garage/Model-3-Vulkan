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
#ifndef INCLUDED_VULKAN3D_RING_H
#define INCLUDED_VULKAN3D_RING_H

#include "VulkanMemory.h"

namespace Vulkan3D {

class CVulkanRingBuffer
{
public:
	bool Init(CVulkanMemory& memory, uint32_t frames, VkDeviceSize sliceBytes);
	void Shutdown(CVulkanMemory& memory);
	void BeginFrame(uint32_t frame);
	bool Alloc(VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset, void*& ptr);
	VkBuffer Buffer() const { return m_buffer.buffer; }
	VkDeviceSize SliceBytes() const { return m_slice; }
	// Bytes left in the current frame's slice. A caller that can split its transfer - the
	// block-compressed texture upload does - sizes the next piece from this, so that running out
	// of ring space is a deferral to the next frame instead of a logged allocation failure.
	VkDeviceSize Remaining() const { return m_slice > m_cursor ? m_slice - m_cursor : 0; }

private:
	AllocatedBuffer m_buffer;
	uint32_t m_frames = 0;
	uint32_t m_frame = 0;
	VkDeviceSize m_slice = 0;
	VkDeviceSize m_cursor = 0;
};

} // Vulkan3D

#endif
