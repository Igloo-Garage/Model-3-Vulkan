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
 * FileDialogs.h
 *
 * Native macOS file pickers for the settings window.
 *
 * These mirror the Windows IFileDialog helpers in Gui.cpp: same names, same signatures, same
 * convention that the chosen path is written back through the std::string reference. Keeping the
 * interface identical means the call sites need no platform-specific code beyond the wrapper.
 */

#ifndef INCLUDED_OSX_FILEDIALOGS_H
#define INCLUDED_OSX_FILEDIALOGS_H

#include <string>

struct SDL_Window;

namespace OSXFileDialogs
{
    /*
     * Forces the Aqua appearance for the application.
     *
     * The interface is a fixed light theme, so on a Mac set to Dark Mode the native title bar and
     * the panels above would otherwise be dark while the window content is warm paper. Setting the
     * application appearance once keeps the title bar, the file pickers and the menus consistent
     * with the interface, without changing any system-wide preference.
     */
    void ApplyLightAppearance();

    bool BrowseFolder(SDL_Window* window, std::string& path);
    bool BrowseSettingsFile(SDL_Window* window, std::string& path);
    bool BrowseImageFile(SDL_Window* window, std::string& path);
    bool BrowseSaveFile(SDL_Window* window, std::string& path);
}

#endif // INCLUDED_OSX_FILEDIALOGS_H
