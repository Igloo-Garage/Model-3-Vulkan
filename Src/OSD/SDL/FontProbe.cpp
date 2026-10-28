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
#include "OSD/SDL/FontProbe.h"

#include <cstdio>
#include <cstring>
#include <vector>

/*
 * The same stb_truetype the interface rasterises with, compiled here with internal linkage.
 *
 * ImGui builds its copy inside imgui_draw.cpp with STBTT_STATIC, so those symbols are not reachable
 * from another translation unit; a private copy is the price of asking the parser itself rather than
 * approximating its answer. Keep this include pointing at the header ImGui uses: the two being the
 * same source is what makes "passes the probe" mean "cannot fail in ImGui".
 *
 * The whole implementation comes along, including the rasteriser this file never calls. It is small
 * next to the rest of the interface and costs nothing at run time; the warning suppression is only
 * because -Wall reports every unused static function in it.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../Pkgs/imgui/imstb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace
{
    // No real font is anywhere near this, and refusing to allocate for something that is not one is
    // better than reading a multi-gigabyte file because a path was wrong.
    constexpr long long kMaxFontBytes = 64LL * 1024 * 1024;

    /*
     * ImGui asserts that a font source is larger than 100 bytes before parsing it, so anything below
     * this floor would fail in the loader rather than in the parser. The directory checks below imply
     * roughly 90 bytes for a font carrying the required tables, which is close enough to that assert
     * to be worth a floor of our own; no real font is under a kilobyte.
     */
    constexpr std::size_t kMinFontBytes = 1024;

    // A real font has a few dozen tables. The cap only stops a non-font whose fourth and fifth bytes
    // happen to look like a table count from making us allocate a directory that cannot exist.
    constexpr unsigned kMaxTables = 512;

    unsigned ReadU16(const unsigned char* p)
    {
        return ((unsigned)p[0] << 8) | (unsigned)p[1];
    }

    unsigned ReadU32(const unsigned char* p)
    {
        return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
    }

    bool TagIs(const unsigned char* p, const char* tag)
    {
        return std::memcmp(p, tag, 4) == 0;
    }

    struct TableRecord
    {
        bool present = false;
        unsigned offset = 0;
        unsigned length = 0;
    };

    /*
     * The table directory, read with bounds checks.
     *
     * stb_truetype explicitly does no range checking ("an attacker can use it to read arbitrary
     * memory", says its own header). It is fine for real fonts, but the probe feeds it whatever a
     * font path points at - and a 512-byte file of 0x5A bytes has a byte pair that reads as 23,130
     * tables, which sends the parser tens of kilobytes past the buffer. Verified: that is a
     * segmentation fault, not a rejection.
     *
     * So the structure is validated here first: signature, table count, every table inside the file,
     * and the tables the rasteriser requires. Only then is stb asked for its opinion, where it is the
     * final authority on CFF internals and glyph data.
     */
    struct FontDirectory
    {
        bool valid = false;
        const char* reason = "";
        TableRecord cmap, head, hhea, hmtx, maxp, glyf, loca, cff, cff2;
    };

    FontDirectory ParseDirectory(const unsigned char* data, std::size_t size, long sfntOffset)
    {
        FontDirectory out;

        if (sfntOffset < 0 || (std::size_t)sfntOffset + 12 > size)
        {
            out.reason = "no font face at the requested index";
            return out;
        }

        const unsigned char* header = data + sfntOffset;

        // 0x00010000 is TrueType, "OTTO" is OpenType/CFF, "true"/"typ1" are Apple's spellings.
        const unsigned version = ReadU32(header);
        if (version != 0x00010000 && version != 0x4F54544Fu &&
            version != 0x74727565u && version != 0x74797031u)
        {
            out.reason = "not a TrueType/OpenType font (unrecognised signature)";
            return out;
        }

        const unsigned numTables = ReadU16(header + 4);
        if (numTables == 0 || numTables > kMaxTables)
        {
            out.reason = "an implausible table count for a font";
            return out;
        }

        const std::size_t directoryEnd = (std::size_t)sfntOffset + 12ull + 16ull * numTables;
        if (directoryEnd > size)
        {
            out.reason = "the table directory runs past the end of the file";
            return out;
        }

        struct Entry
        {
            const char* tag;
            TableRecord* record;
        };
        const Entry entries[] = {
            { "cmap", &out.cmap }, { "head", &out.head }, { "hhea", &out.hhea },
            { "hmtx", &out.hmtx }, { "maxp", &out.maxp }, { "glyf", &out.glyf },
            { "loca", &out.loca }, { "CFF ", &out.cff }, { "CFF2", &out.cff2 },
        };

        for (unsigned i = 0; i < numTables; ++i)
        {
            const unsigned char* record = header + 12 + 16 * i;
            const unsigned tableOffset = ReadU32(record + 8);
            const unsigned tableLength = ReadU32(record + 12);

            // 64-bit arithmetic: the two fields are 32-bit and their sum overflows a 32-bit size_t on
            // a 32-bit build, which would make a truncated file look valid.
            if ((unsigned long long)tableOffset + tableLength > (unsigned long long)size)
            {
                out.reason = "a font table lies outside the file (truncated or corrupt)";
                return out;
            }

            for (const Entry& entry : entries)
            {
                if (TagIs(record, entry.tag))
                {
                    entry.record->present = true;
                    entry.record->offset = tableOffset;
                    entry.record->length = tableLength;
                }
            }
        }

        if (!out.cmap.present || !out.head.present || !out.hhea.present ||
            !out.hmtx.present || !out.maxp.present)
        {
            out.reason = "a table the rasteriser requires is missing (cmap/head/hhea/hmtx/maxp)";
            return out;
        }

        if (out.glyf.present != out.loca.present)
        {
            out.reason = "glyf and loca disagree, so the outlines cannot be indexed";
            return out;
        }

        if (!out.glyf.present && !out.cff.present && !out.cff2.present)
        {
            out.reason = "it has neither TrueType outlines nor a CFF table";
            return out;
        }

        out.valid = true;
        return out;
    }
}

FontProbeResult ProbeFontData(const void* data, std::size_t size, int faceIndex)
{
    const unsigned char* bytes = (const unsigned char*)data;
    if (bytes == nullptr || size < kMinFontBytes)
        return { false, "too small to be a font file" };

    // Offset of the face's sfnt header. Same computation as stbtt_GetFontOffsetForIndex(), done here
    // because that function trusts a collection's face count and reads past the buffer if it lies.
    long offset = 0;
    if (TagIs(bytes, "ttcf"))
    {
        const unsigned numFonts = ReadU32(bytes + 8);
        if (faceIndex < 0 || (unsigned)faceIndex >= numFonts || 12ull + 4ull * numFonts > size)
            return { false, "no font face at the requested index" };
        offset = (long)ReadU32(bytes + 12 + 4 * faceIndex);
    }
    else if (faceIndex != 0)
    {
        return { false, "no font face at the requested index" };
    }

    const FontDirectory directory = ParseDirectory(bytes, size, offset);
    if (!directory.valid)
        return { false, directory.reason };

    /*
     * Named before the parse rather than after it. Without this the file would be rejected as
     * unparseable, which is true but sends the reader looking for a corrupt font when the real
     * answer is "variable fonts are not supported" - which is exactly the report that started this
     * (Issue #5: a CFF2 variable font that Fontconfig ranked first).
     */
    if (directory.cff2.present)
        return { false, "a variable font (CFF2 table), which the bundled rasteriser cannot read" };

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, bytes, (int)offset))
        return { false, "the bundled rasteriser rejected it" };

    return { true, "" };
}

FontProbeResult ProbeFontFile(const char* path, int faceIndex)
{
    if (path == nullptr || path[0] == '\0')
        return { false, "no path" };

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return { false, "the file could not be opened" };

    std::fseek(file, 0, SEEK_END);
    const long long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (length <= 0 || length > kMaxFontBytes)
    {
        std::fclose(file);
        return { false, "the file is empty or implausibly large for a font" };
    }

    std::vector<unsigned char> bytes((std::size_t)length);
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);

    if (read != bytes.size())
        return { false, "the file could not be read completely" };

    return ProbeFontData(bytes.data(), bytes.size(), faceIndex);
}
