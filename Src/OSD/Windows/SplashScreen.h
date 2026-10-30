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
 * SplashScreen.h
 *
 * Puts the startup artwork on screen while the settings window's renderer is
 * being brought up.
 *
 * Why this exists: the settings window is created by SDL and then spends one to
 * two seconds creating its Vulkan device and swapchain (or its GL context)
 * before anything can be drawn into it. During that time the window is on
 * screen and empty, which reads as a black flash on every launch. The window is
 * therefore created hidden, this picture is shown in its place, and the window
 * is revealed once the first frame has been presented.
 *
 * The picture is the RCDATA resource described in SplashResource.h: compiled
 * into the executable, not shipped as a file, and checked against the hash the
 * build was made with.
 *
 * Windows only - it is implemented with a layered Win32 window and WIC, and
 * both callers are inside #ifdef _WIN32. Elsewhere the settings window is
 * created visible exactly as before.
 */

#ifndef INCLUDED_OSD_WINDOWS_SPLASHSCREEN_H
#define INCLUDED_OSD_WINDOWS_SPLASHSCREEN_H

namespace OSD
{
namespace Platform
{
struct Window;
}

namespace Windows
{

/*
 * Shows the artwork in the middle of the screen the settings window is on.
 *
 * The splash is square and centred on that display's work area, at a size
 * derived from the screen rather than from the window: the window can be any
 * shape the user left it in, including larger than the desktop, and a splash
 * that copied that would repeat the problem it is there to solve.
 *
 * The splash is owned by the settings window, so destroying that window takes
 * the splash with it.
 *
 * coverMonitor makes the splash fill the whole monitor instead of covering only
 * the artwork's square, with the picture still centred at its normal size. That
 * is the form used while a game boots behind it (Manage Saves -> Load), where
 * the point is that none of the boot sequence is visible. The window is topmost
 * either way, so the game window can be revealed underneath without showing
 * through.
 *
 * Returns true when a splash is on screen, in which case the caller must call
 * HideStartupSplash() once the first frame has been presented. Returns false
 * when the artwork could not be loaded or decoded: the caller then shows the
 * window immediately, which is the behaviour this replaced.
 */
bool ShowStartupSplash(OSD::Platform::Window* window, bool coverMonitor = false);

/*
 * Destroys the splash. Safe to call when no splash is up, and safe to call
 * after the window that owns it has already been destroyed.
 */
void HideStartupSplash();

} // Windows
} // OSD

#endif // INCLUDED_OSD_WINDOWS_SPLASHSCREEN_H
