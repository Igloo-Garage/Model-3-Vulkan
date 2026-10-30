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
 * StartupLog.h
 *
 * Best-effort diagnostics before the normal logger exists.
 *
 * std::print/println throw std::system_error when the target stream cannot be
 * written.  A GUI executable launched from Explorer has no console and no
 * usable stderr, so an unguarded std::println in the startup path calls
 * std::terminate -> abort() before the first window ever appears.  That is a
 * process that looks like it "does not run at all".
 *
 * Everything printed here is therefore optional: if the write fails, startup
 * continues to the message box and the real log file.
 */

#ifndef INCLUDED_OSD_SDL3_STARTUPLOG_H
#define INCLUDED_OSD_SDL3_STARTUPLOG_H

#include <cstdio>
#include <exception>
#include <format>
#include <print>
#include <utility>

namespace OSD
{
namespace SDL3App
{

template <typename... Args>
void StartupPrint(std::format_string<Args...> format, Args&&... args)
{
    try
    {
        std::println(stderr, format, std::forward<Args>(args)...);
    }
    catch (const std::exception&)
    {
        // No usable stderr.  The caller's message box and the log file remain.
    }
}

} // SDL3App
} // OSD

#endif // INCLUDED_OSD_SDL3_STARTUPLOG_H
