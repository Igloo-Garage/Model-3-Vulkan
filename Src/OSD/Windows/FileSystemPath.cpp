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
 * FileSystemPath.cpp
 *
 * Windows file and directory locations.
 *
 * Everything the emulator reads or writes lives under one "Data" directory next to the executable,
 * so the folder a user unpacks contains a single .exe and nothing else at the top level:
 *
 *     Model 3 Vulkan\
 *         Supermodel.exe
 *         Data\
 *             Config\      Supermodel.ini, Games.xml, Music.xml
 *             NVRAM\       per-game .nv files
 *             Saves\       per-game save states
 *             Screenshots\
 *             Analysis\
 *             Assets\      crosshair bitmaps
 *             Supermodel.log
 *
 * Two decisions are worth stating explicitly.
 *
 * Anchoring to the executable rather than the working directory: previously every path was relative,
 * so launching through a shortcut whose "Start in" was blank - or from a command prompt sitting
 * somewhere else - silently produced a second configuration and a second NVRAM set in whatever the
 * current directory happened to be. The user's settings appeared to vanish. Resolving against the
 * executable's own directory makes the location independent of how the program was started.
 *
 * Creating directories on demand: GetPath() now makes sure the directory it is about to return
 * actually exists, the same way the Unix implementation does. Windows had no PathExists() or
 * MakeDir() at all, so the directories had to be shipped in the archive; deleting Data\NVRAM\ meant
 * NVRAM silently stopped saving.
 */

#include "FileSystemPath.h"

#include <windows.h>

#include <string>
#include <vector>

namespace FileSystemPath
{
    namespace
    {
    // Directory holding the executable, with a trailing separator. Computed once: the module
    // path cannot change while the process is running.
    const std::string& BasePath()
    {
        static const std::string base = []() -> std::string
        {
            // GetModuleFileNameW would be more correct for paths outside the ANSI code page, but
            // the rest of the emulator opens files through fopen() with narrow strings, so a wide
            // path would not survive the trip anyway.
            char buffer[MAX_PATH];
            DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);

            if (length == 0 || length >= MAX_PATH)
                return "";      // fall back to the working directory

            // Trim the file name, keeping the separator.
            for (DWORD i = length; i > 0; --i)
            {
                if (buffer[i - 1] == '\\' || buffer[i - 1] == '/')
                {
                    buffer[i] = '\0';
                    break;
                }
            }
            return std::string(buffer);
        }();
        return base;
    }

    // Every location the emulator uses is relative to this.
    const char* const kDataDir = "Data/";

    // True for "C:\x", "\\server\share" and other paths that are already rooted.
    bool IsAbsolute(const std::string& path)
    {
        if (path.empty())
            return false;
        if (path[0] == '\\' || path[0] == '/')
            return true;
        return path.size() >= 2 && path[1] == ':';
    }

    /*
     * Which of the two layouts this installation uses.
     *
     * 0.9.2 archives put everything under the Data folder, while the Makefile-built packages and
     * earlier releases kept Config, Assets and the runtime folders next to the executable. The
     * choice is made once, from what is actually on disk, and it prefers the directory that
     * contains a Config folder: that is the one which can actually be read. A stray Data folder -
     * the log creates one on the first run if the configured log path says so - therefore cannot
     * make the setup look like the new layout and hide Games.xml.
     */
    const std::string& DataPrefix()
    {
        static const std::string prefix = []() -> std::string
        {
            const std::string& base = BasePath();
            if (!base.empty())
            {
                if (PathExists(base + kDataDir + "Config"))
                    return kDataDir;
                if (PathExists(base + "Config"))
                    return "";
            }
            return kDataDir;        // fresh installation
        }();
        return prefix;
    }

        void MakeDirRecursive(const std::string& dir)
        {
            // Creating "Data/NVRAM/" means creating "Data" first, so walk the separators. Cheap
            // enough to redo on every call, and GetPath() caches its results anyway.
            for (size_t i = 1; i < dir.size(); ++i)
            {
                if (dir[i] != '/' && dir[i] != '\\')
                    continue;

                std::string partial = dir.substr(0, i);
                if (!partial.empty())
                    ::CreateDirectoryA(partial.c_str(), nullptr);
            }
            ::CreateDirectoryA(dir.c_str(), nullptr);
        }

        // Relative location for each type, without the Data/ prefix.
        const char* SubPath(PathType pathType)
        {
            switch (pathType)
            {
            case Analysis:    return "Analysis/";
            case Config:      return "Config/";
            case Log:         return "";
            case NVRAM:       return "NVRAM/";
            case Saves:       return "Saves/";
            case Screenshots: return "Screenshots/";
            case Assets:      return "Assets/";
            }
            return "";
        }
    }

    bool PathExists(std::string fileSystemPath)
    {
        DWORD attributes = ::GetFileAttributesA(fileSystemPath.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    int MakeDir(std::string dir)
    {
        if (PathExists(dir))
            return 0;
        MakeDirRecursive(dir);
        return PathExists(dir) ? 0 : -1;
    }

    // Generates a path to be used by Supermodel files
    std::string GetPath(PathType pathType)
    {
        static std::vector<std::string> created;

        std::string path = BasePath() + DataPrefix() + SubPath(pathType);

        // Create it once. Without this a fresh archive relies on the directories having been shipped,
        // and writing fails silently if the user removes one.
        bool known = false;
        for (const std::string& seen : created)
        {
            if (seen == path)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            MakeDirRecursive(path);
            created.push_back(path);
        }

        return path;
    }

    std::string GetBasePath()
    {
        return BasePath();
    }

    // Windows keeps the historical default: a romset that was just downloaded is usually still in
    // the Downloads folder, and an existing Supermodel.ini keeps whatever the user chose anyway.
    std::string GetDefaultRomPath()
    {
        return "C:/Downloads";
    }

    std::string ResolvePath(const std::string& path)
    {
        if (IsAbsolute(path))
            return path;

        const std::string& base = BasePath();
        if (base.empty())
            return path;        // leave it relative to the working directory

        /*
         * A flat installation has no Data folder, so a path stored as "Data/Supermodel.log" by a
         * newer default must not create a second tree beside Config. Strip the prefix there.
         */
        if (DataPrefix().empty())
        {
            const std::string prefix = kDataDir;
            if (path.compare(0, prefix.size(), prefix) == 0)
                return base + path.substr(prefix.size());
        }

        return base + path;
    }
}
