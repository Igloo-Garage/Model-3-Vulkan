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

/*
 * PlatformSDL2Internal.h
 *
 * Private definitions shared by the three SDL2 backend translation units.
 *
 * Nothing outside Src/OSD/Platform/SDL2/ may include this file.  Shared OSD
 * code sees only the opaque Window/GLContext handles in Platform.h.
 */

#ifndef INCLUDED_PLATFORM_SDL2_INTERNAL_H
#define INCLUDED_PLATFORM_SDL2_INTERNAL_H

#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformInput.h"
#include "SDLIncludes.h"

namespace OSD
{
namespace Platform
{

struct Window
{
    SDL_Window* sdl = nullptr;

    // The display the window was created on, kept because SDL_GetWindowDisplayIndex()
    // is not meaningful while the window is fullscreen on another output.
    DisplayId display = kPrimaryDisplay;

    // Creation flags, so GetWindowPixelSize() can call the matching SDL drawable query.
    uint32_t flags = WindowFlag_None;

    bool fullscreenDesktop = false;
    bool exclusiveFullscreen = false;
    SDL_DisplayMode exclusiveMode = {};

    // False for a wrapper created by AdoptSDLWindow(): the caller keeps
    // ownership of the SDL_Window for the duration of the migration.
    bool ownsSDL = true;
};

struct GLContext
{
    SDL_GLContext sdl = nullptr;
};

/*
 * Registry access.  SDL window user data is used rather than a side table so
 * the lookup is O(1) and cannot go stale when a window is destroyed.
 */
Window* WindowFromSDLWindow(SDL_Window* window);
SDL_Window* SDLWindowFromWindow(Window* window);

/*
 * Transitional helper for Main.cpp while the window lifecycle is still SDL2
 * code.  It wraps an existing SDL_Window without taking ownership, so the
 * renderer can receive a Platform::Window* before the rest of Main.cpp has
 * been converted.  Remove once window creation moves into Platform.
 */
Window* AdoptSDLWindow(SDL_Window* window, uint32_t flags = WindowFlag_None);

} // Platform
} // OSD

#endif // INCLUDED_PLATFORM_SDL2_INTERNAL_H
