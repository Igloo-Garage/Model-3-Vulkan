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

/*
 * SplashScreen.cpp
 *
 * The startup artwork, drawn with GDI into a layered popup window.
 *
 * Nothing here touches the renderer on purpose. The gap this covers is the one
 * before a renderer exists: ImGui has no context yet, Vulkan has no swapchain
 * yet, and GL has no context yet. A layered window carries its own pixels, so
 * it needs none of that, and it cannot interfere with the surface the renderer
 * is about to attach to the real window.
 *
 * The image is decoded with WIC, the same codec the cover previews try first, and falls back to the
 * vendored libwebp when Windows has no WebP codec (the Store "Webp Image Extensions" package) - the
 * same fallback the cover previews already use. It is scaled to a square that
 * is centred on the display, and drawn onto a canvas filled with the colour of
 * the picture's own corners so any remainder blends into it. A caption along
 * the bottom says whether the artwork still matches the hash this build
 * recorded; it deliberately carries no version, so the splash stays the same
 * picture across releases.
 */

#include "OSD/Windows/SplashScreen.h"

#include "OSD/Platform/Platform.h"
#include "OSD/Logger.h"
#include "SplashResource.h"
#include "Version.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <webp/decode.h>
#pragma comment(lib, "windowscodecs.lib")

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

/* ---------------- SHA-256 ------------------------------------------------- */

/*
 * SHA-256 of the resource bytes, compared against the hash the build recorded.
 *
 * BCrypt does the same job, but this keeps the module free of a second import
 * library: the build is verified on start-up, before anything else about the
 * process is known, and an extra DLL in that path is one more thing that can
 * fail. The implementation is the plain FIPS 180-4 one and is used for nothing
 * but comparing two fixed strings.
 */
struct Sha256
{
    std::uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::uint8_t block[64] = {};
    std::size_t blockLength = 0;
    std::uint64_t totalLength = 0;

    static std::uint32_t RotateRight(std::uint32_t value, int bits)
    {
        return (value >> bits) | (value << (32 - bits));
    }

    void Compress(const std::uint8_t* p)
    {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };

        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = ((std::uint32_t)p[i * 4] << 24) | ((std::uint32_t)p[i * 4 + 1] << 16) |
                   ((std::uint32_t)p[i * 4 + 2] << 8) | (std::uint32_t)p[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choose + k[i] + w[i];
            const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void Update(const std::uint8_t* data, std::size_t length)
    {
        totalLength += length;
        while (length > 0)
        {
            const std::size_t take = (std::min)(length, sizeof(block) - blockLength);
            std::memcpy(block + blockLength, data, take);
            blockLength += take;
            data += take;
            length -= take;
            if (blockLength == sizeof(block))
            {
                Compress(block);
                blockLength = 0;
            }
        }
    }

    /* Lower-case hex, 64 characters. */
    std::string Finish()
    {
        // Taken before the padding is appended: the length field covers the
        // message, not the padding.
        const std::uint64_t bits = totalLength * 8ull;

        block[blockLength++] = 0x80;
        if (blockLength > 56)
        {
            std::memset(block + blockLength, 0, sizeof(block) - blockLength);
            Compress(block);
            blockLength = 0;
        }
        std::memset(block + blockLength, 0, 56 - blockLength);
        for (int i = 0; i < 8; ++i)
            block[56 + i] = (std::uint8_t)(bits >> (56 - 8 * i));
        Compress(block);

        static const char digits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (std::uint32_t word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
                hex.push_back(digits[(word >> shift) & 0xfu]);
        }
        return hex;
    }
};

std::string Sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    Sha256 hash;
    hash.Update(bytes.data(), bytes.size());
    return hash.Finish();
}

/* ---------------- COM and WIC --------------------------------------------- */

/*
 * WIC is COM-based and this runs before the settings window has touched COM,
 * so the apartment is set up here. S_FALSE means already initialised and
 * RPC_E_CHANGED_MODE means initialised with the other threading model; both
 * leave the factory usable, which is all this needs.
 */
bool PrepareWic()
{
    static const bool ready = []()
    {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        return SUCCEEDED(init) || init == RPC_E_CHANGED_MODE;
    }();
    return ready;
}

IWICImagingFactory* WicFactory()
{
    static IWICImagingFactory* factory = []() -> IWICImagingFactory*
    {
        IWICImagingFactory* created = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&created))))
            return nullptr;
        return created;
    }();
    return factory;
}

/*
 * Releases a COM object at the end of the scope, so the error paths below can
 * just return false.
 */
template <typename T>
struct ComPointer
{
    T* pointer = nullptr;
    ~ComPointer() { if (pointer != nullptr) pointer->Release(); }
    T* operator->() const { return pointer; }
    T** Put() { return &pointer; }
    explicit operator bool() const { return pointer != nullptr; }
};

/* ---------------- Artwork ------------------------------------------------- */

/* The artwork compiled into this executable, or an empty vector. */
std::vector<std::uint8_t> LoadArtworkResource()
{
    std::vector<std::uint8_t> bytes;

    // RT_RCDATA and IDC_ARROW are MAKEINTRESOURCE() values, which the macros make narrow when
    // UNICODE is not defined; these are the explicit W entry points, so the cast is what is meant.
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SPLASH),
        reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (resource == nullptr)
    {
        ErrorLog("Splash: the artwork resource is missing from this build.");
        return bytes;
    }

    const HGLOBAL handle = LoadResource(nullptr, resource);
    if (handle == nullptr)
    {
        ErrorLog("Splash: the artwork resource could not be loaded.");
        return bytes;
    }

    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = LockResource(handle);
    if (data == nullptr || size == 0)
    {
        ErrorLog("Splash: the artwork resource is empty.");
        return bytes;
    }

    const auto* first = static_cast<const std::uint8_t*>(data);
    bytes.assign(first, first + size);
    return bytes;
}

/*
 * True when the artwork in this executable is byte for byte the one the build
 * recorded a hash for.
 *
 * This is a tripwire, not a copy protection: it catches a binary whose resource
 * was swapped after linking, which is what rebranding a release amounts to. A
 * rebuild from source with the check removed is not detectable by any amount of
 * checking, and the licence is the thing that governs that.
 */
bool ArtworkMatchesBuild(const std::vector<std::uint8_t>& artwork)
{
    if (artwork.empty())
        return false;

    const std::string actual = Sha256Hex(artwork);
    std::string expected(MODEL3VULKAN_SPLASH_SHA256);
    std::transform(expected.begin(), expected.end(), expected.begin(),
        [](char c) { return (char)std::tolower((unsigned char)c); });

    if (actual == expected)
    {
        InfoLog("Splash: artwork verified, sha256 %s.", actual.c_str());
        return true;
    }

    ErrorLog("Splash: artwork does not match this build (expected %s, found %s). "
             "The splash will be marked UNOFFICIAL BUILD.", expected.c_str(), actual.c_str());
    return false;
}

/*
 * Fallback for machines without the system WebP codec: WIC refuses the stream, so decode the same
 * bytes with the libwebp subset that is already linked for the cover previews. The pixels are kept
 * in `pixels` until DecodeArtwork() returns, because the WIC bitmap wraps that buffer.
 */
bool DecodeArtworkWithLibwebp(IWICImagingFactory* factory, const std::vector<std::uint8_t>& fileBytes,
    std::vector<std::uint8_t>& pixels, ComPointer<IWICBitmap>& bitmap, UINT& width, UINT& height)
{
    int decodedWidth = 0;
    int decodedHeight = 0;
    std::uint8_t* decoded = WebPDecodeBGRA(fileBytes.data(), fileBytes.size(), &decodedWidth, &decodedHeight);
    if (decoded == nullptr || decodedWidth <= 0 || decodedHeight <= 0)
    {
        if (decoded != nullptr)
            WebPFree(decoded);
        return false;
    }

    const UINT stride = (UINT)decodedWidth * 4;
    pixels.assign(decoded, decoded + (std::size_t)stride * (std::size_t)decodedHeight);
    WebPFree(decoded);

    if (FAILED(factory->CreateBitmapFromMemory((UINT)decodedWidth, (UINT)decodedHeight,
            GUID_WICPixelFormat32bppBGRA, stride, (UINT)pixels.size(), pixels.data(), bitmap.Put())))
        return false;

    width = (UINT)decodedWidth;
    height = (UINT)decodedHeight;
    return true;
}

bool DecodeArtwork(std::vector<std::uint8_t>& fileBytes, int boxWidth, int boxHeight,
    std::vector<std::uint8_t>& bgra, int& width, int& height)
{
    if (fileBytes.empty() || boxWidth <= 0 || boxHeight <= 0)
        return false;
    if (!PrepareWic())
        return false;

    IWICImagingFactory* factory = WicFactory();
    if (factory == nullptr)
        return false;

    ComPointer<IWICStream> stream;
    ComPointer<IWICBitmapDecoder> decoder;
    ComPointer<IWICBitmapFrameDecode> frame;
    ComPointer<IWICBitmap> fallbackBitmap;
    std::vector<std::uint8_t> fallbackPixels;
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;

    // Developer fallback test: force the bundled decoder even on a machine that has the system
    // codec, so the path can be exercised without uninstalling the Store package.
    static const bool forceLibwebp = std::getenv("SM3D_SPLASH_LIBWEBP") != nullptr;
    const bool wicDecoded = !forceLibwebp &&
        SUCCEEDED(factory->CreateStream(stream.Put())) &&
        SUCCEEDED(stream->InitializeFromMemory(fileBytes.data(), (DWORD)fileBytes.size())) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream.pointer, nullptr,
                                                  WICDecodeMetadataCacheOnLoad, decoder.Put())) &&
        SUCCEEDED(decoder->GetFrame(0, frame.Put())) &&
        SUCCEEDED(frame->GetSize(&sourceWidth, &sourceHeight)) &&
        sourceWidth > 0 && sourceHeight > 0;

    IWICBitmapSource* source = nullptr;
    if (wicDecoded)
    {
        source = frame.pointer;
    }
    else if (DecodeArtworkWithLibwebp(factory, fileBytes, fallbackPixels, fallbackBitmap,
                                      sourceWidth, sourceHeight))
    {
        source = fallbackBitmap.pointer;
        InfoLog("Splash: artwork decoded with the bundled libwebp decoder (the system WebP codec is unavailable).");
    }
    else
    {
        return false;
    }

    // Fit, centred: the smaller of the two ratios decides, so nothing is
    // cropped. The artwork is square and the window is not, so the spare room
    // becomes even bars on two sides.
    const double scale = (std::min)((double)boxWidth / (double)sourceWidth,
                                    (double)boxHeight / (double)sourceHeight);
    const UINT scaledWidth = (UINT)(std::max)(1.0, std::floor(sourceWidth * scale + 0.5));
    const UINT scaledHeight = (UINT)(std::max)(1.0, std::floor(sourceHeight * scale + 0.5));

    ComPointer<IWICBitmapScaler> scaler;
    if (scaledWidth != sourceWidth || scaledHeight != sourceHeight)
    {
        if (SUCCEEDED(factory->CreateBitmapScaler(scaler.Put())) &&
            SUCCEEDED(scaler->Initialize(source, scaledWidth, scaledHeight,
                                         WICBitmapInterpolationModeHighQualityCubic)))
        {
            source = scaler.pointer;
        }
        else
        {
            // Without the scaler the picture is drawn at its own size, which
            // is then reported as larger than the box and refused below; a
            // splash is not worth an image that has been clipped.
            scaler.pointer->Release();
            scaler.pointer = nullptr;
        }
    }

    ComPointer<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.Put())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                     nullptr, 0.0, WICBitmapPaletteTypeMedianCut)))
        return false;

    UINT outWidth = 0;
    UINT outHeight = 0;
    if (FAILED(converter->GetSize(&outWidth, &outHeight)) || outWidth == 0 || outHeight == 0)
        return false;
    if (outWidth > (UINT)boxWidth || outHeight > (UINT)boxHeight)
        return false;

    const UINT stride = outWidth * 4;
    bgra.resize((size_t)stride * outHeight);
    if (FAILED(converter->CopyPixels(nullptr, stride, (UINT)bgra.size(), bgra.data())))
        return false;

    width = (int)outWidth;
    height = (int)outHeight;
    return true;
}

/*
 * Draws the caption into the DIB that is about to become the splash.
 *
 * Everything is opaque (alpha 255 everywhere) because the layered window is
 * composited with ULW_ALPHA and everything it is given has to be
 * premultiplied; with an alpha of 255 premultiplied and straight are the same
 * values. GDI does not touch the fourth byte of a 32bpp DIB, so the alpha is
 * restored afterwards rather than trusted.
 */
void DrawCaption(std::uint8_t* canvas, HDC memory, int width, int height,
    const std::wstring& firstLine, const std::wstring& secondLine, bool official, int metricHeight)
{
    // Smaller than the old single-line caption (metricHeight / 38): the second line is a
    // repository URL and a licence tag, and at the old size it would run into the edge of a
    // 1080p splash.  Two lines at this size fit the same square without clipping either one.
    const int fontSize = (std::max)(13, metricHeight / 48);
    const int edge = (std::max)(16, metricHeight / 28);

    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font == nullptr)
        return;

    const HGDIOBJ previousFont = SelectObject(memory, font);
    SetBkMode(memory, TRANSPARENT);

    /*
     * Two lines, because one could not carry both the credit and the licence.
     * The lower line is the repository and licence notice and is deliberately
     * dimmer; the upper line is the author credit, or the red warning a
     * rebranded binary shows when its artwork no longer matches the hash this
     * build recorded.  Both are protected by LICENSE-ADDITIONAL-TERMS.md
     * (GPL v3 Section 7(b)), so neither is optional.
     */
    RECT second = { edge, height - edge - fontSize * 2, width - edge, height - edge };
    SetTextColor(memory, RGB(164, 166, 178));
    DrawTextW(memory, secondLine.c_str(), -1, &second,
        DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

    RECT first = { edge, height - edge - fontSize * 4, width - edge, height - edge - fontSize * 2 };
    SetTextColor(memory, official ? RGB(208, 210, 218) : RGB(255, 110, 110));
    DrawTextW(memory, firstLine.c_str(), -1, &first,
        DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(memory, previousFont);
    DeleteObject(font);

    for (size_t i = 3; i < (size_t)width * (size_t)height * 4; i += 4)
        canvas[i] = 0xff;
}

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (needed <= 0)
        return std::wstring();
    std::wstring wide((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), needed);
    return wide;
}

/* ---------------- The window ---------------------------------------------- */

const wchar_t kSplashClass[] = L"Model3VulkanStartupSplash";

HWND g_splashWindow = nullptr;

LRESULT CALLBACK SplashProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    // Not painting through WM_ERASEBKGND: the contents come from
    // UpdateLayeredWindow, and flicker here would be a black rectangle over the
    // artwork for as long as the message took.
    if (message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterSplashClass()
{
    static const bool registered = []()
    {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = SplashProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        windowClass.lpszClassName = kSplashClass;
        if (RegisterClassExW(&windowClass) != 0)
            return true;
        // Already registered: harmless, and the window can still be created.
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}

} // namespace

namespace OSD
{
namespace Windows
{

bool ShowStartupSplash(OSD::Platform::Window* window, bool coverMonitor)
{
    if (g_splashWindow != nullptr)
        return true;

    const HWND owner = static_cast<HWND>(OSD::Platform::GetNativeWindowHandle(window));
    if (owner == nullptr)
        return false;

    // Centred on the display the window is on, and square: the artwork is square and the window is
    // not, so following the window would stretch the picture or leave bars behind it, and a window
    // the user left larger than the desktop would take the splash off-screen with it.
    const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitorInfo))
        return false;

    const RECT work = monitorInfo.rcWork;
    const int workWidth = (int)(work.right - work.left);
    const int workHeight = (int)(work.bottom - work.top);
    if (workWidth <= 0 || workHeight <= 0)
        return false;

    // 62% of the shorter side reads as a splash rather than a wallpaper, and the artwork sets the
    // ceiling so it is never enlarged past its own resolution.
    int side = (int)((std::min)(workWidth, workHeight) * 0.62);
    side = (std::min)(side, 2048);
    if (side < 240)
        return false;

    /*
     * The window's own box, which is the artwork's square unless the picture is covering a game that
     * is booting behind it. In that form the box is the whole monitor rather than the work area: the
     * game is borderless fullscreen, so the strip the taskbar occupies is part of its picture and
     * leaving it uncovered would show a slice of the boot sequence along the bottom edge.
     */
    const RECT monitorBounds = monitorInfo.rcMonitor;
    const int width = coverMonitor ? (int)(monitorBounds.right - monitorBounds.left) : side;
    const int height = coverMonitor ? (int)(monitorBounds.bottom - monitorBounds.top) : side;
    const int left = coverMonitor ? (int)monitorBounds.left : work.left + (workWidth - side) / 2;
    const int top = coverMonitor ? (int)monitorBounds.top : work.top + (workHeight - side) / 2;
    if (width <= 0 || height <= 0)
        return false;

    // Logged because "the splash was not where I expected" has two very
    // different causes - a multi-monitor layout and a wrong size - and the
    // numbers say which without a round trip.
    InfoLog("Splash: %dx%d at (%d,%d) in work area (%d,%d %dx%d)%s.",
        width, height, left, top, (int)work.left, (int)work.top, workWidth, workHeight,
        coverMonitor ? ", covering the monitor" : "");

    std::vector<std::uint8_t> artwork = LoadArtworkResource();
    if (artwork.empty())
        return false;

    const bool official = ArtworkMatchesBuild(artwork);

    std::vector<std::uint8_t> image;
    int imageWidth = 0;
    int imageHeight = 0;
    if (!DecodeArtwork(artwork, width, height, image, imageWidth, imageHeight))
    {
        ErrorLog("Splash: the artwork could not be decoded; the window will be shown empty instead.");
        return false;
    }

    // Canvas filled with the artwork's own corner colour: rounding the square
    // can leave a pixel or two at the edge, and that must not show as a line.
    const size_t lastRow = (size_t)(imageHeight - 1) * (size_t)imageWidth;
    const size_t cornerIndex[4] = { 0, (size_t)imageWidth - 1, lastRow,
                                    lastRow + (size_t)imageWidth - 1 };
    unsigned blue = 0;
    unsigned green = 0;
    unsigned red = 0;
    for (size_t corner : cornerIndex)
    {
        blue += image[corner * 4 + 0];
        green += image[corner * 4 + 1];
        red += image[corner * 4 + 2];
    }

    std::vector<std::uint8_t> canvas((size_t)width * (size_t)height * 4);
    std::vector<std::uint8_t> row((size_t)width * 4);
    for (int x = 0; x < width; ++x)
    {
        row[(size_t)x * 4 + 0] = (std::uint8_t)(blue / 4);
        row[(size_t)x * 4 + 1] = (std::uint8_t)(green / 4);
        row[(size_t)x * 4 + 2] = (std::uint8_t)(red / 4);
        row[(size_t)x * 4 + 3] = 0xff;
    }
    for (int y = 0; y < height; ++y)
        std::memcpy(&canvas[(size_t)y * (size_t)width * 4], row.data(), row.size());

    // Copied pixel by pixel rather than with memcpy so the alpha can be forced
    // opaque: the layered window composites with ULW_ALPHA, and an image with an
    // alpha channel would otherwise be premultiplied from whatever it carries.
    const int offsetX = (width - imageWidth) / 2;
    const int offsetY = (height - imageHeight) / 2;
    for (int y = 0; y < imageHeight; ++y)
    {
        std::uint8_t* destination = &canvas[((size_t)(offsetY + y) * (size_t)width + (size_t)offsetX) * 4];
        const std::uint8_t* source = &image[(size_t)y * (size_t)imageWidth * 4];
        for (int x = 0; x < imageWidth; ++x)
        {
            destination[x * 4 + 0] = source[x * 4 + 0];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = source[x * 4 + 2];
            destination[x * 4 + 3] = 0xff;
        }
    }

    // GDI needs a DC and a DIB section to draw the caption into; the DIB's
    // pixel memory is the canvas, so the result needs no copy back.
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;   // top-down, same as the canvas
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(memory, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool drawn = false;
    if (memory != nullptr && bitmap != nullptr && bits != nullptr)
    {
        const HGDIOBJ previousBitmap = SelectObject(memory, bitmap);
        std::memcpy(bits, canvas.data(), canvas.size());

        // No version here on purpose: the splash is fixed artwork and the version changes every
        // release, while the build is already identified in the title bar, the console banner and
        // the About line. These lines carry the author credit, the repository and the hash
        // tripwire, not the number. A rebranded binary keeps the original author on screen and is
        // marked as an unofficial modified build.
        const std::string credit = std::string(MODEL3VULKAN_TITLE) +
            (official ? "  -  Developed by Rex (Igloo-Garage)"
                      : "  -  UNOFFICIAL MODIFIED BUILD (Original by Igloo-Garage)");
        const std::string legal = std::string(MODEL3VULKAN_REPO_URL) + "  |  " +
            MODEL3VULKAN_LEGAL_SHORT;
        DrawCaption(static_cast<std::uint8_t*>(bits), memory, width, height,
            Widen(credit), Widen(legal), official, side);

        if (!RegisterSplashClass())
        {
            ErrorLog("Splash: the splash window class could not be registered.");
        }
        else
        {
            HWND splash = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSplashClass,
                L"", WS_POPUP, left, top, width, height, owner, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            if (splash == nullptr)
            {
                ErrorLog("Splash: the splash window could not be created.");
            }
            else
            {
                // pptDst is the window's new screen position, not an offset: passing (0,0) here is
                // what moved the splash into the corner regardless of where it was created.
                POINT destination = { left, top };
                SIZE size = { width, height };
                POINT source = { 0, 0 };
                BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                drawn = UpdateLayeredWindow(splash, nullptr, &destination, &size, memory,
                                            &source, 0, &blend, ULW_ALPHA) != FALSE;
                if (drawn)
                {
                    // No activation: the settings window takes the foreground
                    // when it is revealed, exactly as it did before.
                    ShowWindow(splash, SW_SHOWNOACTIVATE);
                    g_splashWindow = splash;
                }
                else
                {
                    ErrorLog("Splash: the artwork could not be transferred to the splash window.");
                    DestroyWindow(splash);
                }
            }
        }

        SelectObject(memory, previousBitmap);
    }

    if (bitmap != nullptr)
        DeleteObject(bitmap);
    if (memory != nullptr)
        DeleteDC(memory);
    if (screen != nullptr)
        ReleaseDC(nullptr, screen);

    return drawn;
}

void HideStartupSplash()
{
    if (g_splashWindow == nullptr)
        return;
    // Owned by the settings window, so this may already be gone if that window
    // was destroyed first; DestroyWindow just fails in that case.
    DestroyWindow(g_splashWindow);
    g_splashWindow = nullptr;
}

} // Windows
} // OSD
