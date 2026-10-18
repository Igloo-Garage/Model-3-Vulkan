/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2022 The Supermodel Team
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
 * FileSystemPaths.h
 *
 * Header file for OS-dependent Supermodel files locations.
 */

#ifndef INCLUDED_FILESYSTEMPATH_H
#define INCLUDED_FILESYSTEMPATH_H

#include <string>

namespace FileSystemPath
{
    enum PathType { Analysis, Config, Log, NVRAM, Saves, Screenshots, Assets }; // Filesystem path types
    bool PathExists(std::string fileSystemPath); // Checks if a directory exists (returns true if exists, false if it doesn't)
    int MakeDir(std::string dir); // Create a directory
    std::string GetPath(PathType pathType);  // Generates a path to be used by Supermodel files

    /*
     * Anchors a relative path to the installation directory, so the file it names lands in the
     * emulator's own folder no matter how the program was started. An already-absolute path is
     * returned unchanged, and so is a relative path on a platform where the installation directory
     * cannot be determined (the caller then gets the historical working-directory behaviour).
     *
     * This is what makes it safe to keep relative paths in the configuration file: storing
     * "Data/Supermodel.log" rather than a full path keeps the whole folder movable - copy it to
     * another drive and the setting still means the right thing - while resolving it here means
     * launching through a shortcut, or from a command prompt somewhere else, still writes to the
     * same place.
     */
    std::string ResolvePath(const std::string& path);

    /*
     * Directory the executable lives in, with a trailing separator, or an empty string where the
     * platform cannot determine it. Used to find files left behind by older layouts, which stored
     * everything relative to the working directory instead of under Data\.
     */
    std::string GetBasePath();

    /*
     * Directory a fresh installation should look in for ROM sets.
     *
     * The release archives ship an empty "ROMs" folder next to the executable. On macOS, where
     * the archive is a plain folder the user can put anywhere, the default is that folder and it
     * is created when missing. Windows keeps the historical Downloads location, because that is
     * where a downloaded romset usually still is. An empty result means the platform cannot name
     * a sensible default.
     */
    std::string GetDefaultRomPath();
}


#endif // INCLUDED_FILESYSTEMPATH_H
