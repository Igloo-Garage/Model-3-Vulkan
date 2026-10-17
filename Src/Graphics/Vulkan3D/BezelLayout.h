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
#ifndef INCLUDED_VULKAN3D_BEZELLAYOUT_H
#define INCLUDED_VULKAN3D_BEZELLAYOUT_H

#include <cstdint>

namespace Vulkan3D {

/*
 * Geometry for the bezel backdrop, kept apart from the renderer so it can be checked without a GPU.
 *
 * Getting this wrong is not obvious on screen: a bar that overlaps the picture by a few pixels, or a
 * seam between two bars, reads as "the backdrop looks slightly off" rather than as a bug, and the
 * two cases that matter (4:3 with side bars, point-to-point with bars on all four edges) are easy to
 * test by inspection only at one resolution. The arithmetic is therefore a pure function with a
 * property test beside it - see Scripts/../the note in develop.md.
 *
 * Deliberately free of Vulkan types so a test can include it and nothing else.
 */

struct BezelBar
{
	int32_t x = 0;
	int32_t y = 0;
	uint32_t w = 0;
	uint32_t h = 0;
};

/*
 * The area of the output the emulated picture occupies, i.e. everything the backdrop must keep away
 * from.
 *
 * Two rectangles have to be covered, and they are not quite the same: the 3D viewport, and the 2D
 * tile layers, which are drawn `correction` pixels away from it. Taking the union means the backdrop
 * can never clip a stray line of the tile layer; taking either alone would.
 *
 * wideBackground means the tile layer already spans the full width, in which case there are no bars
 * at all and the whole output is picture.
 *
 * wideScreen means the 3D scene spans the full width - the widescreen option widens the view frustum
 * and sets the viewport to the whole output. The rectangle has to follow, because this is also the
 * scissor the 2D compositor clips every layer to: with it left at the 4:3 viewable area the widened
 * sides were cut straight back off, and since widening a frustum leaves the centre of the picture
 * pixel-identical the result was indistinguishable from having the option switched off. The OpenGL
 * backend has always handled this (its scissor is full width when widescreen is on), so this brings
 * the two backends into agreement rather than adding anything new.
 */
inline void ComputeBezelGameRect(int extentWidth, int extentHeight,
                                 int xOffset, int yOffset, int xRes, int yRes,
                                 int correction, bool wideBackground, bool wideScreen,
                                 int& outX, int& outY, int& outW, int& outH)
{
	if (wideBackground || wideScreen)
	{
		/*
		 * The picture spans the full width, so there is nothing to hide at the sides - but the same
		 * 2D-versus-3D misalignment applies vertically, and the OpenGL backend crops only top and
		 * bottom here (its scissor is full width). Matching that keeps the two backends showing the
		 * same picture.
		 *
		 * The two cases are one branch because they want the same rectangle. They differ in where the
		 * extra width comes from - the tile layer being stretched, or the 3D frustum being widened -
		 * not in what the picture occupies.
		 */
		outX = 0;
		outY = correction;
		outW = extentWidth;
		outH = extentHeight - 2 * correction;
		if (outY < 0) { outH += outY; outY = 0; }
		if (outH < 0) outH = 0;
		return;
	}

	/*
	 * Inset by correction, not expanded by it.
	 *
	 * The 2D tile layer is drawn correction pixels away from the 3D scene because the two are known to
	 * disagree by that much, and the OpenGL backend hides the overhang with a scissor box inset by the
	 * same amount. This renderer does the same thing, so the picture the user sees is the inset
	 * rectangle - which is therefore the rectangle the backdrop has to keep away from.
	 */
	int x = xOffset + correction;
	// The inset is about the 3D viewport, which is what the OpenGL scissor insets - the 2D layer's own
	// offset is the thing being hidden, not the thing being measured.
	int y = extentHeight - (yOffset + yRes) + correction;
	int w = xRes - 2 * correction;
	int h = yRes - 2 * correction;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > extentWidth) w = extentWidth - x;
	if (y + h > extentHeight) h = extentHeight - y;
	if (w < 0) w = 0;
	if (h < 0) h = 0;

	outX = x;
	outY = y;
	outW = w;
	outH = h;
}

/*
 * Splits everything outside the picture into at most four rectangles - top, bottom, left, right.
 *
 * The left and right bars are restricted to the rows the picture occupies, which is what keeps the
 * four rectangles from overlapping each other. Together they tile the complement of the game rect
 * exactly: every pixel is in exactly one of them, or in the picture.
 *
 * Returns how many entries of `bars` were filled (0 when the picture fills the output).
 */
inline int ComputeBezelBars(int extentWidth, int extentHeight,
                            int gameX, int gameY, int gameW, int gameH,
                            BezelBar bars[4])
{
	const int gameRight = gameX + gameW;
	const int gameBottom = gameY + gameH;
	int count = 0;

	if (gameY > 0)
		bars[count++] = { 0, 0, (uint32_t)extentWidth, (uint32_t)gameY };
	if (gameBottom < extentHeight)
		bars[count++] = { 0, gameBottom, (uint32_t)extentWidth, (uint32_t)(extentHeight - gameBottom) };
	if (gameX > 0 && gameH > 0)
		bars[count++] = { 0, gameY, (uint32_t)gameX, (uint32_t)gameH };
	if (gameRight < extentWidth && gameH > 0)
		bars[count++] = { gameRight, gameY, (uint32_t)(extentWidth - gameRight), (uint32_t)gameH };

	return count;
}

} // Vulkan3D

#endif
