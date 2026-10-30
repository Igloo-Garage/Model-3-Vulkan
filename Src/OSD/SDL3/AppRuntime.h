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
 * AppRuntime.h
 *
 * Executable-aware runtime paths for the SDL3 build.
 *
 * Model 3-Vulkan and Supermodel-LegacyOG are separate processes with separate
 * settings.  The Vulkan build therefore binds its configuration file to its own
 * executable name (Model3-Vulkan.exe -> Model3-Vulkan.ini) instead of sharing the
 * historical Supermodel.ini.  On first run it seeds that file from the shared
 * Supermodel.ini written by the OpenGL build.
 * The directory still comes from FileSystemPath, so the Data/Config and XDG layouts keep their
 * existing rules.
 *
 * Errors are returned, never logged away: a missing executable path or an
 * unwritable configuration directory is fatal at startup and the caller shows
 * the SDL message box.
 */

#ifndef INCLUDED_OSD_SDL3_APPRUNTIME_H
#define INCLUDED_OSD_SDL3_APPRUNTIME_H

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>

namespace OSD
{
namespace SDL3App
{

struct RuntimePathError
{
    std::string detail;         // English, suitable for a message box and the log
    std::error_code code;       // the filesystem error, if there was one
};

struct RuntimePaths
{
    std::filesystem::path executable;       // absolute path to this executable
    std::string executableStem;             // "Model3-Vulkan"
    std::filesystem::path configFile;       // <Config dir>/<stem>.ini
};

/*
 * Resolves this process's executable and derives the per-executable settings
 * file from it.  The configuration directory is created if it does not exist.
 */
std::expected<RuntimePaths, RuntimePathError> DeriveRuntimePaths();

} // SDL3App
} // OSD

#endif // INCLUDED_OSD_SDL3_APPRUNTIME_H
