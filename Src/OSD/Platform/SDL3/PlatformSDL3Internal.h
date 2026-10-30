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

/*
 * PlatformSDL3Internal.h
 *
 * Private definitions shared by the three SDL3 backend translation units.
 */

#ifndef INCLUDED_PLATFORM_SDL3_INTERNAL_H
#define INCLUDED_PLATFORM_SDL3_INTERNAL_H

#include "OSD/Platform/Platform.h"
#include "OSD/Platform/PlatformInput.h"

#include <SDL3/SDL.h>

namespace OSD
{
namespace Platform
{

struct Window
{
    SDL_Window* sdl = nullptr;
    DisplayId display = kPrimaryDisplay;
    uint32_t flags = WindowFlag_None;
    bool fullscreen = false;
    bool exclusiveFullscreen = false;
    SDL_DisplayMode exclusiveMode = {};
    bool ownsSDL = true;
};

struct GLContext
{
    SDL_GLContext sdl = nullptr;
};

Window* WindowFromSDLWindow(SDL_Window* window);
SDL_Window* SDLWindowFromWindow(Window* window);
Window* AdoptSDLWindow(SDL_Window* window, uint32_t flags = WindowFlag_None);

} // Platform
} // OSD

#endif // INCLUDED_PLATFORM_SDL3_INTERNAL_H
