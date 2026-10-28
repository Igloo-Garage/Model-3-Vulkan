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
#ifndef _FONT_PROBE_H_
#define _FONT_PROBE_H_

#include <cstddef>

/*
 * Whether the font rasteriser bundled with the interface can actually read a font file.
 *
 * ImGui 1.92 rasterises through stb_truetype, which understands TrueType outlines (glyf/loca) and
 * OpenType/CFF (the "CFF " table) but not the "CFF2" table used by variable fonts. Handing it one
 * anyway does not degrade gracefully: the loader asserts, and the assert is fatal - the program exits
 * before the first window is drawn. Issue #5 was exactly that: openSUSE Tumbleweed's
 * NotoSansCJKsc-VF.otf, chosen by Fontconfig as the desktop's Chinese sans-serif.
 *
 * The probe runs the same parser over the same bytes before ImGui sees them, so a font accepted here
 * cannot fail there. It is a parse, not a guess: no filename patterns, no version sniffing, and no
 * dependence on which distribution installed what.
 *
 * It is a format/compatibility check, not a security boundary. stb_truetype does no range checking
 * on a hostile file, and neither does this; both are asked to read fonts the user installed.
 */
struct FontProbeResult
{
    bool usable = false;
    const char* reason = "";   // static string, meant for one log line
};

/*
 * faceIndex selects a face inside a collection (.ttc); 0 is the first.
 *
 * Reads the file, so call it at start-up rather than per frame. Anything that stops the file from
 * being read is reported as unusable, with the reason naming which step failed.
 */
FontProbeResult ProbeFontFile(const char* path, int faceIndex = 0);

/*
 * The same test on bytes the caller already has.
 */
FontProbeResult ProbeFontData(const void* data, std::size_t size, int faceIndex = 0);

#endif // _FONT_PROBE_H_
