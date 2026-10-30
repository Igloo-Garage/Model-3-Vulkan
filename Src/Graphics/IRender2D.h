/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
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
#ifndef INCLUDED_IRENDER2D_H
#define INCLUDED_IRENDER2D_H

#include <cstdint>
#include <memory>
#include "Types.h"
#include "Supermodel.h"
#include "../Model3/TileGenBuffer.h"

/*
 * IRender2D:
 *
 * Interface for the tile-generator (2D) renderer. CRender2D is the OpenGL
 * implementation; CVulkan2D is the Vulkan implementation used with -vulkan3d.
 */
class IRender2D
{
public:
	virtual void BeginFrame(void) = 0;
	virtual void PreRenderFrame(void) = 0;
	virtual void RenderFrameBottom(void) = 0;
	virtual void RenderFrameTop(void) = 0;
	virtual void EndFrame(void) = 0;
	virtual void WriteVRAM(unsigned addr, uint32_t data) = 0;
	virtual void AttachRegisters(const uint32_t* regPtr) = 0;
	virtual void AttachPalette(const uint32_t* palPtr[2]) = 0;
	virtual void AttachVRAM(const uint8_t* vramPtr) = 0;
	virtual void AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top) = 0;
	virtual Result Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget, UpscaleMode upscaleMode) = 0;

	virtual ~IRender2D()
	{
	}
};

#endif  // INCLUDED_IRENDER2D_H
