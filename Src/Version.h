/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
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
 * Version.h
 *
 * Version and identification strings.
 *
 * Two separate things live here and they must not be conflated:
 *
 *   SUPERMODEL_VERSION   the emulator core's version, written into save-state and NVRAM files and
 *                        checked when they are loaded. Changing it invalidates every save state a
 *                        user already has, so it stays pinned to the upstream value.
 *
 *   SUPERMODEL_TITLE     what the interface calls itself. This build is the Vulkan-focused fork, so
 *                        the title names the renderer and credits the upstream release it is based
 *                        on rather than passing itself off as a plain upstream build.
 *
 * Naming: the product/executable is "Model 3-Vulkan" / Model3-Vulkan.exe.  "Supermodel-VK"
 * remains the repository and console-banner name, so do not rename the licence headers or the
 * upstream references to match the executable.
 */

#ifndef INCLUDED_VERSION_H
#define INCLUDED_VERSION_H

// Upstream core version. Tied to save-state compatibility - do not change casually.
#ifndef SUPERMODEL_VERSION
#define SUPERMODEL_VERSION	"0.3a-WIP"
#endif

// Upstream release this fork tracks. Kept as a separate string because the name of this build,
// SUPERMODEL_TITLE, no longer mentions it: the credit appears in the console banner and the about
// line so the origin of the emulation core is still recorded.
#ifndef SUPERMODEL_BASE_RELEASE
#define SUPERMODEL_BASE_RELEASE	"0.3a-2026.09.28"
#endif

// Window title and settings header. Kept short: it has to fit a title bar and sit next to a
// one-line hint without pushing it off the window.
//
// The two executables are named apart on purpose: they are not two renderer choices of one program
// but two builds whose renderer is fixed at compile time.  The Vulkan build is the product called
// "Model 3-Vulkan"; the OpenGL build keeps its own historical name.
#ifndef SUPERMODEL_TITLE
#if defined(SUPERMODEL_OPENGL_ONLY)
#define SUPERMODEL_TITLE	"Supermodel-OpenGL"
#else
#define SUPERMODEL_TITLE	"Model 3-Vulkan"
#endif
#endif

/*
 * This fork's own version.
 *
 * Separate from SUPERMODEL_VERSION above, which is the upstream core's and is written into save
 * states - changing that one would invalidate every save state a user already has. This one
 * identifies the fork instead and is bumped for every release. It is also the release tag, so
 * reusing a version updates (and overwrites) the previous release rather than creating a new one.
 */
#ifndef SUPERMODEL_FORK_VERSION
#define SUPERMODEL_FORK_VERSION	"0.9.8.3b"
#endif

/*
 * What the interface calls itself, version included, wherever a user sees it.
 *
 * Assembled from the two parts above so the pieces cannot drift apart, and defined here rather than
 * at each use so that the title bar, the in-game window and the console banner cannot disagree about
 * which build is running. build.bat reads these same lines out of this file to label the release, so
 * this is the single place the version is written down.
 */
#define SUPERMODEL_TITLE_FULL	SUPERMODEL_TITLE " v" SUPERMODEL_FORK_VERSION

#endif	// INCLUDED_VERSION_H
