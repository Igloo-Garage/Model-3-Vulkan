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
#ifndef INCLUDED_VULKAN3D_TEXTUREDECODE_H
#define INCLUDED_VULKAN3D_TEXTUREDECODE_H

#include <algorithm>
#include <cstdint>

namespace Vulkan3D {

/*
 * One Real3D texel in one of the twelve texture formats, decoded to 8-bit RGBA.
 *
 * This is the CPU twin of ExtractColour() in real3d_common.glsl, and it has to stay
 * that way: the dump writes these bytes to disk and the shader decodes the same
 * source texels at run time, so a difference between the two would show up as an
 * HD texture that no longer matches the picture it replaces.
 *
 * Formats 5, 6 and 8..11 are luminance formats with a one-bit transparency key
 * derived from the luminance value (a texel that decodes to pure white is the
 * transparent colour). The comparison is on the normalized value, as in the
 * shader, not on the raw nibble/byte - format 5 with value 0x00FF is opaque white
 * and therefore transparent, but 0x00FE is opaque white-ish and visible.
 */
inline void DecodeTexel(int type, uint32_t value, uint8_t out[4])
{
	auto u8 = [](float c) { return (uint8_t)std::min(255, std::max(0, (int)(c * 255.0f + 0.5f))); };

	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
	switch (type)
	{
	case 0:			// T1RGB5
		r = ((value >> 10) & 0x1F) / 31.0f;
		g = ((value >> 5) & 0x1F) / 31.0f;
		b = (value & 0x1F) / 31.0f;
		a = 1.0f - (float)((value >> 15) & 1u);
		break;
	case 1:			// interleaved A4L4 (low byte)
		r = g = b = (value & 0xF) / 15.0f;
		a = ((value >> 4) & 0xF) / 15.0f;
		break;
	case 2:
		a = (value & 0xF) / 15.0f;
		r = g = b = ((value >> 4) & 0xF) / 15.0f;
		break;
	case 3:
		r = g = b = ((value >> 8) & 0xF) / 15.0f;
		a = ((value >> 12) & 0xF) / 15.0f;
		break;
	case 4:
		a = ((value >> 8) & 0xF) / 15.0f;
		r = g = b = ((value >> 12) & 0xF) / 15.0f;
		break;
	case 5:
		r = g = b = (value & 0xFF) / 255.0f;
		a = (r >= 1.0f) ? 0.0f : 1.0f;
		break;
	case 6:
		r = g = b = ((value >> 8) & 0xFF) / 255.0f;
		a = (r >= 1.0f) ? 0.0f : 1.0f;
		break;
	case 7:			// RGBA4
		r = ((value >> 12) & 0xF) / 15.0f;
		g = ((value >> 8) & 0xF) / 15.0f;
		b = ((value >> 4) & 0xF) / 15.0f;
		a = (value & 0xF) / 15.0f;
		break;
	case 8: case 9: case 10: case 11:
	{
		int shift = (type - 8) * 4;
		r = g = b = ((value >> shift) & 0xF) / 15.0f;
		a = (r >= 1.0f) ? 0.0f : 1.0f;
		break;
	}
	default:
		break;
	}

	out[0] = u8(r);
	out[1] = u8(g);
	out[2] = u8(b);
	out[3] = u8(a);
}

} // Vulkan3D

#endif
