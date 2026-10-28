/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2021 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzy
 **
 ** This file is part of Supermodel.
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
 * PNGFile.h
 *
 * Minimal PNG writer. Unlike BMPFile.h, which writes uncompressed pixel data and therefore
 * produces files of several tens of megabytes at 4K, this compresses the pixels with zlib (already
 * a dependency of the project) so a screenshot lands in the low single-digit megabytes.
 *
 * Only what is needed for screenshots is implemented: 8-bit RGB, no interlacing, no ancillary
 * chunks, and the "None" scanline filter throughout. Screenshots are mostly flat regions - large
 * black pillarbox bars, smooth shading - which the deflate stage handles well even without
 * per-row filter selection, so the extra complexity of adaptive filtering would not pay for itself.
 */

#ifndef INCLUDED_PNGFILE_H
#define INCLUDED_PNGFILE_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "OSD/Logger.h"
#include "zlib.h"

namespace Util
{
namespace PNG
{
	namespace detail
	{
		inline void AppendBigEndian32(std::vector<uint8_t>& out, uint32_t value)
		{
			out.push_back((uint8_t)(value >> 24));
			out.push_back((uint8_t)(value >> 16));
			out.push_back((uint8_t)(value >> 8));
			out.push_back((uint8_t)value);
		}

		// Length, type, payload, CRC. The CRC covers the type and the payload but not the length.
		inline void AppendChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t length)
		{
			AppendBigEndian32(out, (uint32_t)length);

			const size_t crcStart = out.size();
			out.insert(out.end(), type, type + 4);
			if (length > 0 && data != nullptr)
				out.insert(out.end(), data, data + length);

			uLong crc = crc32(0L, Z_NULL, 0);
			crc = crc32(crc, out.data() + crcStart, (uInt)(out.size() - crcStart));
			AppendBigEndian32(out, (uint32_t)crc);
		}

		inline uint32_t ReadBigEndian32(const uint8_t* p)
		{
			return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
		}

		// Row filter predictor from the PNG specification: pick whichever of the three neighbours the
		// plane through all three passes closest to, which is what makes gradients filter well.
		inline uint8_t PaethPredictor(uint8_t left, uint8_t above, uint8_t upperLeft)
		{
			const int a = (int)left;
			const int b = (int)above;
			const int c = (int)upperLeft;
			const int p = a + b - c;
			const int pa = p > a ? p - a : a - p;
			const int pb = p > b ? p - b : b - p;
			const int pc = p > c ? p - c : c - p;
			if (pa <= pb && pa <= pc)
				return left;
			if (pb <= pc)
				return above;
			return upperLeft;
		}
	}

	/*
	 * WriteRGB():
	 *
	 * Writes an 8-bit RGB image, rows top to bottom, 3 bytes per pixel.
	 *
	 * NOTE: like WriteSurfaceToBMP() this returns true on FAILURE, for consistency with the
	 * existing image-writing helper in this codebase.
	 */
	inline bool WriteRGB(const std::string& file_name, const uint8_t* rgb, int32_t width, int32_t height)
	{
		using namespace detail;

		if (rgb == nullptr || width <= 0 || height <= 0)
			return true;

		const size_t rowBytes = (size_t)width * 3;

		// Raw scanlines for deflate: each row is prefixed with its filter type byte.
		std::vector<uint8_t> raw;
		raw.reserve(((size_t)height) * (rowBytes + 1));
		for (int32_t y = 0; y < height; y++)
		{
			raw.push_back(0);       // filter: None
			const uint8_t* src = rgb + (size_t)y * rowBytes;
			raw.insert(raw.end(), src, src + rowBytes);
		}

		uLongf compressedSize = compressBound((uLong)raw.size());
		std::vector<uint8_t> compressed(compressedSize);
		if (compress2(compressed.data(), &compressedSize, raw.data(), (uLong)raw.size(), 6) != Z_OK)
			return true;
		compressed.resize(compressedSize);

		std::vector<uint8_t> png;
		png.reserve(compressed.size() + 128);

		// Signature
		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		png.insert(png.end(), signature, signature + 8);

		// IHDR: width, height, bit depth 8, colour type 2 (truecolour RGB), the rest default.
		uint8_t ihdr[13];
		ihdr[0] = (uint8_t)(width >> 24);  ihdr[1] = (uint8_t)(width >> 16);
		ihdr[2] = (uint8_t)(width >> 8);   ihdr[3] = (uint8_t)width;
		ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
		ihdr[6] = (uint8_t)(height >> 8);  ihdr[7] = (uint8_t)height;
		ihdr[8] = 8;    // bit depth
		ihdr[9] = 2;    // colour type: RGB
		ihdr[10] = 0;   // compression method (deflate)
		ihdr[11] = 0;   // filter method
		ihdr[12] = 0;   // interlace: none
		AppendChunk(png, "IHDR", ihdr, sizeof(ihdr));
		AppendChunk(png, "IDAT", compressed.data(), compressed.size());
		AppendChunk(png, "IEND", nullptr, 0);

		FILE* fp = fopen(file_name.c_str(), "wb");
		if (fp == nullptr)
		{
			ErrorLog("Unable to open '%s' for writing.", file_name.c_str());
			return true;
		}
		const size_t written = fwrite(png.data(), 1, png.size(), fp);
		fclose(fp);
		return written != png.size();
	}

	/*
	 * WriteRGBA():
	 *
	 * Writes an 8-bit RGBA image, rows top to bottom, 4 bytes per pixel. The alpha
	 * channel is written as given - this is what the HD texture dumper uses, and
	 * its whole point is that a replacement may carry a smooth alpha channel rather
	 * than the source texture's one-bit transparency.
	 *
	 * Same convention as WriteRGB(): returns true on FAILURE.
	 */
	inline bool WriteRGBA(const std::string& file_name, const uint8_t* rgba, int32_t width, int32_t height)
	{
		using namespace detail;

		if (rgba == nullptr || width <= 0 || height <= 0)
			return true;

		const size_t rowBytes = (size_t)width * 4;

		std::vector<uint8_t> raw;
		raw.reserve(((size_t)height) * (rowBytes + 1));
		for (int32_t y = 0; y < height; y++)
		{
			raw.push_back(0);       // filter: None
			const uint8_t* src = rgba + (size_t)y * rowBytes;
			raw.insert(raw.end(), src, src + rowBytes);
		}

		uLongf compressedSize = compressBound((uLong)raw.size());
		std::vector<uint8_t> compressed(compressedSize);
		if (compress2(compressed.data(), &compressedSize, raw.data(), (uLong)raw.size(), 6) != Z_OK)
			return true;
		compressed.resize(compressedSize);

		std::vector<uint8_t> png;
		png.reserve(compressed.size() + 128);

		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		png.insert(png.end(), signature, signature + 8);

		// IHDR: width, height, bit depth 8, colour type 6 (truecolour with alpha).
		uint8_t ihdr[13];
		ihdr[0] = (uint8_t)(width >> 24);  ihdr[1] = (uint8_t)(width >> 16);
		ihdr[2] = (uint8_t)(width >> 8);   ihdr[3] = (uint8_t)width;
		ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
		ihdr[6] = (uint8_t)(height >> 8);  ihdr[7] = (uint8_t)height;
		ihdr[8] = 8;    // bit depth
		ihdr[9] = 6;    // colour type: RGBA
		ihdr[10] = 0;   // compression method (deflate)
		ihdr[11] = 0;   // filter method
		ihdr[12] = 0;   // interlace: none
		AppendChunk(png, "IHDR", ihdr, sizeof(ihdr));
		AppendChunk(png, "IDAT", compressed.data(), compressed.size());
		AppendChunk(png, "IEND", nullptr, 0);

		FILE* fp = fopen(file_name.c_str(), "wb");
		if (fp == nullptr)
		{
			ErrorLog("Unable to open '%s' for writing.", file_name.c_str());
			return true;
		}
		const size_t written = fwrite(png.data(), 1, png.size(), fp);
		fclose(fp);
		return written != png.size();
	}

	/*
	 * DecodeRGBA():
	 *
	 * Decodes a PNG held in memory into tightly packed non-premultiplied RGBA, one byte per
	 * channel, rows top to bottom.
	 *
	 * Why this exists at all: the project's only image *reader* was SDL_LoadBMP, which cannot open
	 * the PNG or JPEG a user is going to have on disk, and the screenshot writer above goes the
	 * other way. zlib is already linked, so decoding costs no new dependency - just chunk parsing
	 * and undoing the per-scanline filters.
	 *
	 * Scope is the whole specification except Adam7 interlacing: every colour type (greyscale,
	 * truecolour, palette, and the two with alpha), bit depths 1 to 16, and tRNS transparency.
	 * Interlacing is rejected with a message that says what to do about it, rather than being
	 * silently mis-decoded - it is rare in files produced by normal tools, and re-saving without it
	 * is a single click.
	 *
	 * Returns false on failure and, when error is not null, fills it with a reason fit to show a
	 * user. Note the convention: unlike WriteRGB() above - which returns true on failure, following
	 * an older helper in this codebase - this returns true on success.
	 *
	 * The chunk CRC is deliberately not verified. A corrupt file will fail in the inflate step
	 * anyway, and checking CRCs would only trade a diagnosis that names the real problem for one
	 * that names a checksum.
	 */
	inline bool DecodeRGBA(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, uint32_t& outWidth, uint32_t& outHeight, std::string* error)
	{
		auto fail = [error](const char* message) -> bool
		{
			if (error)
				*error = message;
			return false;
		};

		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		if (size < sizeof(signature) || std::memcmp(data, signature, sizeof(signature)) != 0)
			return fail("not a PNG file");

		uint32_t width = 0;
		uint32_t height = 0;
		int bitDepth = 0;
		int colourType = 0;
		bool haveHeader = false;

		std::vector<uint8_t> palette;           // RGB triples
		std::vector<uint8_t> paletteAlpha;      // per-index alpha, from tRNS
		std::vector<uint8_t> compressed;        // all IDAT chunks, concatenated
		uint32_t transparentGrey = 0;
		uint32_t transparentRed = 0;
		uint32_t transparentGreen = 0;
		uint32_t transparentBlue = 0;
		bool haveTransparentGrey = false;
		bool haveTransparentRgb = false;

		for (size_t cursor = 8; cursor + 12 <= size; )
		{
			const uint32_t length = detail::ReadBigEndian32(data + cursor);
			const uint8_t* type = data + cursor + 4;
			const size_t payload = cursor + 8;
			if (payload + length + 4 > size)
				return fail("truncated chunk");

			if (std::memcmp(type, "IHDR", 4) == 0)
			{
				if (length != 13)
					return fail("bad IHDR length");
				width = detail::ReadBigEndian32(data + payload);
				height = detail::ReadBigEndian32(data + payload + 4);
				bitDepth = data[payload + 8];
				colourType = data[payload + 9];
				if (data[payload + 10] != 0 || data[payload + 11] != 0)
					return fail("unsupported compression or filter method");
				if (data[payload + 12] != 0)
					return fail("interlaced PNG is not supported; re-save the image without interlacing");
				haveHeader = true;
			}
			else if (std::memcmp(type, "PLTE", 4) == 0)
			{
				palette.assign(data + payload, data + payload + length);
			}
			else if (std::memcmp(type, "tRNS", 4) == 0)
			{
				if (colourType == 3)
					paletteAlpha.assign(data + payload, data + payload + length);
				else if (colourType == 0 && length >= 2)
				{
					haveTransparentGrey = true;
					transparentGrey = ((uint32_t)data[payload] << 8) | data[payload + 1];
				}
				else if (colourType == 2 && length >= 6)
				{
					haveTransparentRgb = true;
					transparentRed   = ((uint32_t)data[payload + 0] << 8) | data[payload + 1];
					transparentGreen = ((uint32_t)data[payload + 2] << 8) | data[payload + 3];
					transparentBlue  = ((uint32_t)data[payload + 4] << 8) | data[payload + 5];
				}
			}
			else if (std::memcmp(type, "IDAT", 4) == 0)
			{
				compressed.insert(compressed.end(), data + payload, data + payload + length);
			}
			else if (std::memcmp(type, "IEND", 4) == 0)
			{
				break;
			}
			// Anything else is ancillary - colour space, gamma, timing, text - and is skipped by
			// length. That is the whole reason every chunk carries its own length.

			cursor = payload + length + 4;
		}

		if (!haveHeader)
			return fail("no IHDR chunk");
		if (width == 0 || height == 0 || width > 32768 || height > 32768)
			return fail("implausible image dimensions");
		if (compressed.empty())
			return fail("no image data");

		int channels = 0;
		switch (colourType)
		{
		case 0: channels = 1; break;    // greyscale
		case 2: channels = 3; break;    // truecolour
		case 3: channels = 1; break;    // palette index
		case 4: channels = 2; break;    // greyscale with alpha
		case 6: channels = 4; break;    // truecolour with alpha
		default: return fail("unsupported colour type");
		}

		// Which bit depths the specification allows for each colour type.
		switch (colourType)
		{
		case 0:
			if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8 && bitDepth != 16)
				return fail("unsupported bit depth");
			break;
		case 3:
			if (bitDepth != 1 && bitDepth != 2 && bitDepth != 4 && bitDepth != 8)
				return fail("unsupported bit depth");
			break;
		default:
			if (bitDepth != 8 && bitDepth != 16)
				return fail("unsupported bit depth");
			break;
		}
		if (colourType == 3 && palette.empty())
			return fail("palette image without a PLTE chunk");

		const uint64_t rowBytes = ((uint64_t)width * (uint32_t)(channels * bitDepth) + 7) / 8;
		const uint64_t rawBytes = (uint64_t)height * (rowBytes + 1);
		if (rawBytes > (uint64_t)512 * 1024 * 1024)
			return fail("image is too large to decode");

		// The inflate size is known exactly - one filter byte plus one scanline per row - which is
		// what lets this use uncompress() instead of driving the zlib stream by hand.
		std::vector<uint8_t> raw((size_t)rawBytes);
		uLongf rawLength = (uLongf)rawBytes;
		if (uncompress(raw.data(), &rawLength, compressed.data(), (uLong)compressed.size()) != Z_OK ||
			(uint64_t)rawLength != rawBytes)
			return fail("image data is corrupt or truncated");

		// Undo the per-scanline filters. Each row is reconstructed against the row above it, so the
		// result cannot be filtered in place - the filter byte gets in the way.
		const size_t bitsPerPixel = (size_t)channels * (size_t)bitDepth;
		const size_t filterStride = bitsPerPixel >= 8 ? bitsPerPixel / 8 : 1;
		std::vector<uint8_t> pixels((size_t)(rowBytes * height), 0);
		for (uint32_t y = 0; y < height; y++)
		{
			const uint8_t* source = raw.data() + (size_t)y * (rowBytes + 1);
			const int filter = *source++;
			const uint8_t* above = y > 0 ? pixels.data() + (size_t)(y - 1) * rowBytes : nullptr;
			uint8_t* row = pixels.data() + (size_t)y * rowBytes;

			if (filter == 0)
			{
				std::memcpy(row, source, (size_t)rowBytes);
				continue;
			}

			for (size_t i = 0; i < rowBytes; i++)
			{
				const uint8_t left = i >= filterStride ? row[i - filterStride] : 0;
				const uint8_t up = above ? above[i] : 0;
				const uint8_t upLeft = (above && i >= filterStride) ? above[i - filterStride] : 0;
				uint8_t value = source[i];
				switch (filter)
				{
				case 1: value = (uint8_t)(value + left); break;
				case 2: value = (uint8_t)(value + up); break;
				case 3: value = (uint8_t)(value + (uint8_t)(((int)left + (int)up) / 2)); break;
				case 4: value = (uint8_t)(value + detail::PaethPredictor(left, up, upLeft)); break;
				default: return fail("unknown scanline filter type");
				}
				row[i] = value;
			}
		}

		// Samples are recovered un-scaled so that tRNS, which stores them the same way, can be
		// compared directly; expansion to 8 bits happens on the way out.
		auto rawSample = [&](size_t index, int channel) -> uint32_t
		{
			if (bitDepth == 8)
				return pixels[index * channels + channel];
			if (bitDepth == 16)
			{
				const size_t at = (index * channels + channel) * 2;
				return ((uint32_t)pixels[at] << 8) | pixels[at + 1];
			}
			// 1, 2 or 4 bits: a single channel (greyscale or palette index) packed most significant
			// bit first, with each scanline starting on a fresh byte.
			const size_t perByte = (size_t)(8 / bitDepth);
			const size_t x = index % width;
			const size_t y = index / width;
			const size_t byteIndex = y * (size_t)rowBytes + x / perByte;
			const int shift = 8 - bitDepth * (int)(x % perByte) - bitDepth;
			return (pixels[byteIndex] >> shift) & ((1u << bitDepth) - 1);
		};

		const auto scaleSample = [bitDepth](uint32_t value) -> uint8_t
		{
			switch (bitDepth)
			{
			case 16: return (uint8_t)(value >> 8);
			case 1:  return (uint8_t)(value * 255);
			case 2:  return (uint8_t)(value * 85);
			case 4:  return (uint8_t)(value * 17);
			default: return (uint8_t)value;
			}
		};

		rgba.assign((size_t)width * height * 4, 255);
		const size_t pixelCount = (size_t)width * height;
		for (size_t index = 0; index < pixelCount; index++)
		{
			uint8_t* out = rgba.data() + index * 4;
			switch (colourType)
			{
			case 0:
			{
				const uint32_t grey = rawSample(index, 0);
				out[0] = out[1] = out[2] = scaleSample(grey);
				if (haveTransparentGrey && grey == transparentGrey)
					out[3] = 0;
				break;
			}
			case 2:
			{
				const uint32_t r = rawSample(index, 0);
				const uint32_t g = rawSample(index, 1);
				const uint32_t b = rawSample(index, 2);
				out[0] = scaleSample(r);
				out[1] = scaleSample(g);
				out[2] = scaleSample(b);
				if (haveTransparentRgb && r == transparentRed && g == transparentGreen && b == transparentBlue)
					out[3] = 0;
				break;
			}
			case 3:
			{
				const uint32_t entry = rawSample(index, 0);
				if (entry * 3 + 2 >= palette.size())
					return fail("palette index out of range");
				out[0] = palette[entry * 3 + 0];
				out[1] = palette[entry * 3 + 1];
				out[2] = palette[entry * 3 + 2];
				out[3] = entry < paletteAlpha.size() ? paletteAlpha[entry] : 255;
				break;
			}
			case 4:
				out[0] = out[1] = out[2] = scaleSample(rawSample(index, 0));
				out[3] = scaleSample(rawSample(index, 1));
				break;
			default:    // 6: truecolour with alpha
				out[0] = scaleSample(rawSample(index, 0));
				out[1] = scaleSample(rawSample(index, 1));
				out[2] = scaleSample(rawSample(index, 2));
				out[3] = scaleSample(rawSample(index, 3));
				break;
			}
		}

		outWidth = width;
		outHeight = height;
		return true;
	}

	/*
	 * LoadRGBA():
	 *
	 * Reads a PNG from disk and decodes it. See DecodeRGBA() for what is and is not supported.
	 * Returns true on success.
	 */
	inline bool LoadRGBA(const std::string& file_name, std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, std::string* error)
	{
		FILE* file = fopen(file_name.c_str(), "rb");
		if (file == nullptr)
		{
			if (error)
				*error = "cannot open '" + file_name + "'";
			return false;
		}

		std::vector<uint8_t> contents;
		uint8_t buffer[64 * 1024];
		for (size_t read = 0; (read = fread(buffer, 1, sizeof(buffer), file)) > 0; )
			contents.insert(contents.end(), buffer, buffer + read);
		fclose(file);

		return DecodeRGBA(contents.data(), contents.size(), rgba, width, height, error);
	}
} // PNG
} // Util

#endif  // INCLUDED_PNGFILE_H
