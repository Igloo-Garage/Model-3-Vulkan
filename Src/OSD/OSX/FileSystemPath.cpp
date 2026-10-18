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

#include "FileSystemPath.h"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <sys/param.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>

namespace FileSystemPath
{
    // Relative location for each type, before it is anchored to the executable's directory.
    static const char* SubPath(PathType pathType)
    {
        switch (pathType)
        {
        case Analysis:
            return "Analysis/";
        case Config:
            return "Config/";
        case Log:
            return "";
        case NVRAM:
            return "NVRAM/";
        case Saves:
            return "Saves/";
        case Screenshots:
            return "Screenshots/";
        case Assets:
            return "Assets/";
        }
        return "";
    }

    /*
     * Generates a path to be used by Supermodel files.
     *
     * Anchored to the executable's directory rather than the working directory. The release is a
     * plain folder that the user can start from Finder, from a Terminal somewhere else, or through
     * a launcher; leaving Config relative to the working directory meant that in the latter cases
     * the settings were read from and written to a stray Config folder that did not exist next to
     * the program - the interface worked, but "save settings" had nowhere to go.
     *
     * The directories are created on demand for the same reason the other platforms do it: the
     * package ships Config, NVRAM and Saves, but Analysis and Screenshots are created on first use,
     * and a user who deletes one should not lose the feature silently.
     */
    std::string GetPath(PathType pathType)
    {
        const std::string base = GetBasePath();
        if (base.empty())
            return pathType == Log ? "" : SubPath(pathType);

        if (pathType == Log)
            return base;

        const std::string path = base + SubPath(pathType);
        (void)mkdir(path.c_str(), 0775);
        return path;
    }

    /*
     * Anchors relative paths to the directory holding the executable.
     *
     * The release archive is a plain folder rather than a .app bundle, and it can be started from
     * any working directory - from a terminal somewhere else, or through a launcher. Leaving a
     * relative path relative to the working directory meant the same setting named different files
     * depending on how the program was started. That is what made a typed relative ROM path appear
     * to do nothing: it was looked for under whatever directory the shell happened to be in.
     *
     * An absolute path is returned unchanged, and so is a relative one when the executable's
     * location cannot be determined.
     */
    std::string ResolvePath(const std::string& path)
    {
        if (path.empty() || path[0] == '/')
            return path;

        const std::string base = GetBasePath();
        if (base.empty())
            return path;

        return base + path;
    }

    // Directory holding the executable, with a trailing separator. Computed once: the executable
    // cannot move while the process is running. _NSGetExecutablePath may return a symlinked path,
    // so realpath() resolves it first; otherwise the "ROMs" folder would be created next to the
    // symlink rather than next to the real binary.
    std::string GetBasePath()
    {
        static const std::string base = []() -> std::string
        {
            char raw[PATH_MAX];
            uint32_t size = sizeof(raw);
            if (_NSGetExecutablePath(raw, &size) != 0)
                return "";

            char resolved[PATH_MAX];
            const char* real = realpath(raw, resolved);
            if (real == nullptr)
                return "";

            std::string path(real);
            const size_t slash = path.find_last_of('/');
            if (slash == std::string::npos)
                return "";

            return path.substr(0, slash + 1);
        }();
        return base;
    }

    std::string GetDefaultRomPath()
    {
        static const std::string roms = []() -> std::string
        {
            const std::string base = GetBasePath();
            if (!base.empty())
            {
                // EEXIST is the normal case; a real failure is reported by the settings window as
                // a missing directory rather than being hidden here.
                (void)mkdir((base + "ROMs/").c_str(), 0775);
            }

            // Stored as a relative path on purpose: ResolvePath() anchors it to the executable's
            // directory, so the whole folder can be moved without breaking the setting.
            return "ROMs/";
        }();
        return roms;
    }
}
