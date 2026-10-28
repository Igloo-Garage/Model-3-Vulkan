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
 * AppRuntime.cpp
 *
 * See AppRuntime.h.  C++23 is used deliberately here: std::expected carries
 * the failure paths and the monadic optional chain turns "executable -> stem
 * -> settings file name" into one expression with no empty-string sentinel.
 */

#include "OSD/SDL3/AppRuntime.h"

#include "OSD/FileSystemPath.h"

#include <optional>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace OSD
{
namespace SDL3App
{

namespace
{

/*
 * Absolute path of the running executable.
 *
 * std::filesystem has no standard way to ask for this, so the one platform
 * call is isolated here and everything downstream stays in <filesystem>.
 * Returns nullopt rather than an empty path, which makes the failure explicit
 * at every call site.
 */
std::optional<std::filesystem::path> QueryExecutablePath()
{
#if defined(_WIN32)
    // Wide characters: the executable name only matters here, but a path
    // containing characters outside the ANSI code page must still resolve.
    std::wstring buffer(1024, L'\0');
    for (;;)
    {
        const DWORD length = ::GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return std::nullopt;

        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }

        // Truncated: the returned length equals the buffer size.
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return std::nullopt;

    std::string buffer(size, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0)
        return std::nullopt;

    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(std::filesystem::path(buffer), error);
    return error ? std::optional<std::filesystem::path>(std::filesystem::path(buffer))
                 : std::optional<std::filesystem::path>(resolved);
#else
    std::error_code error;
    const std::filesystem::path target =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || target.empty())
        return std::nullopt;
    return target;
#endif
}

} // namespace

std::expected<RuntimePaths, RuntimePathError> DeriveRuntimePaths()
{
    const std::optional<std::filesystem::path> executable = QueryExecutablePath();
    if (!executable)
    {
        return std::unexpected(RuntimePathError{
            "The path of the running executable could not be determined, so the "
            "per-executable settings file cannot be named.",
            {}});
    }

    /*
     * Monadic chain: <exe>.stem() -> "<stem>.ini".  A stem is never empty for a
     * real executable, but the and_then step keeps the invalid case in the type
     * instead of relying on that assumption.
     */
    const std::string settingsName = std::optional<std::string>(executable->stem().string())
        .and_then([](std::string stem) -> std::optional<std::string>
        {
            if (stem.empty())
                return std::nullopt;
            stem += ".ini";
            return stem;
        })
        .value_or(std::string{});

    if (settingsName.empty())
    {
        return std::unexpected(RuntimePathError{
            "The executable has no file name stem, so the settings file name "
            "cannot be derived from it.",
            {}});
    }

    // FileSystemPath owns the platform layout rules (Data/Config next to the
    // executable, or the XDG config directory on Linux).  Only the file name
    // changes here.
    std::filesystem::path configDirectory(FileSystemPath::GetPath(FileSystemPath::Config));
    if (configDirectory.empty())
    {
        return std::unexpected(RuntimePathError{
            "The platform could not resolve a configuration directory.",
            {}});
    }

    std::error_code error;
    std::filesystem::create_directories(configDirectory, error);
    if (error)
    {
        return std::unexpected(RuntimePathError{
            "The configuration directory could not be created: " + configDirectory.string(),
            error});
    }

    if (!std::filesystem::is_directory(configDirectory, error) || error)
    {
        return std::unexpected(RuntimePathError{
            "The configuration path is not a directory: " + configDirectory.string(),
            error});
    }

    RuntimePaths paths;
    paths.executable = *executable;
    paths.executableStem = executable->stem().string();
    paths.configFile = configDirectory / settingsName;
    return paths;
}

} // SDL3App
} // OSD
