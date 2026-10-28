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

#ifndef INCLUDED_UTIL_CONFIGBUILDERS_H
#define INCLUDED_UTIL_CONFIGBUILDERS_H

#include <string>
#include <vector>

namespace Util
{
  namespace Config
  {
    class Node;

    bool FromXML(Node *config, const std::string &text);
    bool FromXMLFile(Node *config, const std::string &filename);
    bool FromINIFile(Node *config, const std::string &filename);
    void MergeINISections(Node *merged, const Node &x, const Node &y);
    void CopyINISection(Node *dest, const Node &src);
    //
    // Erases settings this build cannot honour, in the section and in every
    // child section (a per-game section can carry the same key).  SetEmpty() is
    // the config model's erase: the node stays but has no value, so the settings
    // window skips it and WriteINIFile() never emits it.
    //
    void StripSettings(Node &section, const std::vector<std::string> &keys);
    //
    // Removes the settings that belonged to the Vulkan renderer, which the OpenGL-only
    // build neither defines nor can honour.  Kept here rather than in the callers so
    // that every place a configuration file enters the process (startup, a game
    // session, the settings window's "load settings") uses the same list.
    //
    void StripRetiredVulkanSettings(Node &section);
    void WriteINIFile(const std::string &filename, const Node &config, const std::string &header_comment);
  }
}

#endif  // INCLUDED_UTIL_CONFIGBUILDERS_H
