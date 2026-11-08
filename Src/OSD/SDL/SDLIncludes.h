/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2019 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
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
 * SDLIncludes.h
 *
 * Header file for SDL library.
 */

#ifndef INCLUDED_SDLINCLUDES_H
#define INCLUDED_SDLINCLUDES_H

/*
 * The macOS build used to take SDL2 and SDL2_net from frameworks, where the headers are
 * addressed as <SDL2/SDL.h> and <SDL2_net/SDL_net.h>.  The CMake build compiles the same
 * libraries from Libraries/ and gets the plain header names instead, so each macOS include
 * is used only when it actually resolves.  __has_include keeps both layouts working; the
 * framework path stays available for Makefiles/Makefile.OSX.
 */
#if defined(SUPERMODEL_PLATFORM_SDL3)

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

#elif defined(SUPERMODEL_OSX) && defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#else
#include <SDL.h>
#include <SDL_audio.h>
#endif
#elif defined(SUPERMODEL_OSX)
#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#else
#include <SDL.h>
#include <SDL_audio.h>
#endif

#if !defined(SUPERMODEL_PLATFORM_SDL3)
#if defined(SUPERMODEL_OSX) && defined(__has_include) && __has_include(<SDL2_net/SDL_net.h>)
#include <SDL2_net/SDL_net.h>
#else
#include <SDL_net.h>
#endif
#endif

#endif  // INCLUDED_SDLINCLUDES_H