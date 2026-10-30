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
 * SplashResource.h
 *
 * Resource id and build signature of the start-up artwork.
 *
 * Kept as plain #defines, with no include guard work and no C++ in it, because
 * the resource scripts include this same file: the id the artwork is stored
 * under and the id the program looks it up with are then the same number by
 * construction, and cannot drift apart silently.
 *
 * The picture itself is the WebP at the project root. It is compiled into the
 * executable rather than shipped as a file, so replacing it means rebuilding -
 * a copied folder cannot swap it out. The hash below is that artwork's SHA-256:
 * the program hashes the resource it was built with and prints UNOFFICIAL BUILD
 * on the splash when the two disagree, which is what a rebranded binary shows.
 *
 * Do not edit the hash by hand. Run Scripts/UpdateSplashSignature.ps1 after
 * changing the picture; build.bat and build-cmake.bat both call it before
 * compiling, so the normal build path keeps this line in step on its own.
 */

#ifndef INCLUDED_SPLASH_RESOURCE_H
#define INCLUDED_SPLASH_RESOURCE_H

/* RCDATA id. 101 is the icon, so the artwork takes the next free number. */
#define IDR_SPLASH 102

/* SHA-256 of the artwork in lower-case hex, as a string. */
#define MODEL3VULKAN_SPLASH_SHA256 "a683268400fec115e2b0724106640dd3262acae616b5922c74701bb20ebe5bad"

#endif // INCLUDED_SPLASH_RESOURCE_H
