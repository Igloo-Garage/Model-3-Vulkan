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
#include "Util/Format.h"
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

namespace FileSystemPath
{
    // Checks if a directory exists (returns true if exists, false if it doesn't)
    bool PathExists(std::string fileSystemPath)
    {
        struct stat pathInfo;
        return stat(fileSystemPath.c_str(), &pathInfo) == 0 && S_ISDIR(pathInfo.st_mode);
    }

    // If directory doesn't exist, create it
    int MakeDir(std::string dir)
    {
        if (!PathExists(dir))
	{
            return mkdir(dir.c_str(), 0775);
	}

        return 0;
    }

    // Directory name for a path type, with the trailing separator. Log has none: the logger's own
    // setting names the file, and GetPath(Log) only has to say which directory it belongs in.
    static std::string SubPath(PathType pathType)
    {
        switch (pathType)
        {
        case Analysis:
            return "Analysis/";
        case Config:
            return "Config/";
        case Cache:
            return "Cache/";
        case Log:
            return "";
        case NVRAM:
            return "NVRAM/";
        case Saves:
            return "Saves/";
        case SavesLegacy:
            return "Saves_Legacy/";
        case Screenshots:
            return "Screenshots/";
        case RecordingsSound:
            return "Recordings/Sound/";
        case Assets:
            return "Assets/";
        }
        return "";
    }

    // Generates a path to be used by Supermodel files
    std::string GetPath(PathType pathType)
    {
        /*
         * The packaged layout keeps Config/, Assets/, NVRAM/, Saves/ and the log under Data/ next to
         * the executable - the same shape build.bat assembles for Windows and the macOS port uses.
         *
         * Linux used to look only at the working directory and then at XDG paths, so the release
         * tarball's Data/ directory was never read: Games.xml was not found (empty game list), the
         * boxarts were missing, and the bundled interface font was invisible to the loader. A tree
         * without Data/ (an in-tree development build, or a package made before this change) keeps
         * the old behaviour, so nothing that works today starts reading a second, empty config.
         */
        const std::string base = GetBasePath();
        if (!base.empty())
        {
            struct stat info;
            if (stat((base + "Data").c_str(), &info) == 0 && S_ISDIR(info.st_mode))
            {
                const std::string root = base + "Data/";
                (void)mkdir(root.c_str(), 0775);
                if (pathType == Log)
                    return root;

                const std::string path = root + SubPath(pathType);
                if (pathType == RecordingsSound)
                    (void)mkdir((root + "Recordings/").c_str(), 0775);
                (void)mkdir(path.c_str(), 0775);
                return path;
            }
        }

        std::string finalPath;
        std::string homePath;
        std::string strPathType;
        struct passwd* pwd = getpwuid(getuid());

        // Resolve pathType to string for later use
        switch (pathType)
        {
        case Analysis:
            strPathType = "Analysis";
            break;
        case Config:
            strPathType = "Config";
            break;
        case Cache:
            strPathType = "Cache";
            break;
        case Log:
            strPathType = "Log";
            break;
        case NVRAM:
            strPathType = "NVRAM";
            break;
        case Saves:
            strPathType = "Saves";
            break;
        case SavesLegacy:
            strPathType = "Saves_Legacy";
            break;
        case Screenshots:
            strPathType = "Screenshots";
            break;
        case RecordingsSound:
            strPathType = "Recordings/Sound";
            break;
        case Assets:
            strPathType = "Assets";
            break;
        }

        // Get user's HOME directory
        if (pwd)
        {
            homePath = pwd->pw_dir;
        }
        else
        {
            const char *envHome = getenv("HOME");
            homePath = envHome == NULL ? std::string() : envHome;
        }

        // If Config path exists in current directory or the user doesn't have a HOME directory use current directory
        if (FileSystemPath::PathExists("Config") || homePath.empty())
        {
            // Use current directory
            if (pathType != Screenshots && pathType != Log)
            {
                finalPath = strPathType;
                FileSystemPath::MakeDir(finalPath);
            }
        }
        // Check if $HOME/.supermodel exists
        else if (FileSystemPath::PathExists(Util::Format() << homePath << "/.supermodel"))
        {
            // Use $HOME/.supermodel
            finalPath = Util::Format() << homePath << "/.supermodel/" << strPathType;
            FileSystemPath::MakeDir(finalPath);
        }
        // On Linux one may want to follow the XDG base directory specs (https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html)
        else
        {
            // Use $HOME/.config/supermodel or $HOME/.local/share/supermodel depending on the file type
            if (pathType == Config)
            {
                finalPath = Util::Format() << homePath << "/.config/supermodel";
                if (!FileSystemPath::PathExists(finalPath))
                {
                    const char *envConfig = getenv("XDG_CONFIG_HOME");
                    std::string configPath = (envConfig == NULL ? std::string() : envConfig);
		    if (!configPath.empty())
		    {
		        finalPath = Util::Format() << configPath << "/supermodel";
	            }
                }
		FileSystemPath::MakeDir(finalPath);

		finalPath = Util::Format() << finalPath << "/Config";
		FileSystemPath::MakeDir(finalPath);
            }
            else
            {
                finalPath = Util::Format() << homePath << "/.local/share/supermodel";
                if (!FileSystemPath::PathExists(finalPath))
                {
		    const char *envData = getenv("XDG_DATA_HOME");
                    std::string dataPath = (envData == NULL ? std::string() : envData);
		    if (!dataPath.empty())
		    {
		        finalPath = Util::Format() << dataPath << "/supermodel";
	            }
                }
		FileSystemPath::MakeDir(finalPath);

		finalPath = Util::Format() << finalPath << "/" << strPathType;
		FileSystemPath::MakeDir(finalPath);
            }
        }

        if (!finalPath.empty())
        {
            finalPath = Util::Format() << finalPath << "/";
        }
        return finalPath;
    }

    // Anchors a relative path to the directory holding the executable, the same convention the other
    // platforms use. Without it the log - a relative "Data/Supermodel.log" in the configuration -
    // landed in whatever directory the program happened to be started from, and when that directory
    // had no Data/ the log was silently not written at all.
    std::string ResolvePath(const std::string& path)
    {
        if (path.empty() || path[0] == '/')
            return path;

        const std::string base = GetBasePath();
        if (base.empty())
            return path;

        return base + path;
    }

    std::string GetBasePath()
    {
#if defined(__linux__)
        // /proc/self/exe is the one reliable way to find the installation folder on Linux. The
        // settings themselves still live under XDG (~/.config/supermodel); this is only used for
        // files that are meant to sit beside the executable, such as a portable roms/ folder and
        // the optional Model3VulkanHostCore library.
        static const std::string base = []() -> std::string
        {
            std::error_code error;
            const std::filesystem::path executable =
                std::filesystem::read_symlink("/proc/self/exe", error);
            if (error || executable.empty())
                return "";

            const std::filesystem::path directory = executable.parent_path();
            if (directory.empty())
                return "";

            return directory.string() + "/";
        }();
        return base;
#else
        // Other Unix systems have no portable equivalent that is worth guessing at; keep the
        // historical working-directory behaviour there.
        return "";
#endif
    }

    // The portable layout: an empty "roms" folder next to the executable is created on first run,
    // and the settings window scans it before falling back to anything else. The value is stored
    // relative so the whole folder can be moved without breaking the setting.
    std::string GetDefaultRomPath()
    {
        const std::string base = GetBasePath();
        if (!base.empty())
            (void)mkdir((base + "roms/").c_str(), 0775);

        return "roms/";
    }
}
