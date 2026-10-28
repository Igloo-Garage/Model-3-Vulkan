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
 * FileDialogs.mm
 *
 * Native macOS file pickers, the Cocoa counterpart to the IFileDialog code in Gui.cpp.
 *
 * The panels run modally rather than as sheets. A sheet has to be attached to an NSWindow, and the
 * only window here belongs to SDL, whose delegate and window management are its own business. A
 * modal panel is the documented way to ask for a file without taking that over, and it still gives
 * the standard sidebar, search field, Cmd+Shift+G and the rest of the system UI.
 *
 * UTF-8 is the boundary format in both directions, matching the rest of the program.
 */

#import <Cocoa/Cocoa.h>

#include <string>

#include "Localization.h"
#include "FileDialogs.h"

namespace
{
std::string ToUtf8(NSString* text)
{
    if (text == nil)
        return {};

    const char* utf8 = [text UTF8String];
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

NSString* ToNSString(const char* text)
{
    if (text == nullptr)
        return nil;
    return [NSString stringWithUTF8String:text];
}

/*
 * Starts the panel at the directory a setting names, or at the directory containing it when the
 * setting names a file. A path that no longer exists is ignored, which leaves the panel at its own
 * default rather than pointing it at nothing.
 */
void SetStartLocation(NSSavePanel* panel, const std::string& path)
{
    if (path.empty())
        return;

    NSString* target = ToNSString(path.c_str());
    if (target == nil)
        return;

    NSFileManager* manager = [NSFileManager defaultManager];
    BOOL isDirectory = NO;
    if (![manager fileExistsAtPath:target isDirectory:&isDirectory])
        target = [target stringByDeletingLastPathComponent];

    BOOL directory = NO;
    if (target.length > 0 && [manager fileExistsAtPath:target isDirectory:&directory] && directory)
        [panel setDirectoryURL:[NSURL fileURLWithPath:target isDirectory:YES]];
}

void ActivateApplication()
{
    // Without a .app bundle the process can be behind the terminal that launched it; activating it
    // keeps the panel (and the window it belongs to) where the user is looking.
    if (NSApp != nil)
        [NSApp activateIgnoringOtherApps:YES];
}

std::string PanelResult(NSSavePanel* panel)
{
    NSURL* url = [panel URL];
    return url != nil ? ToUtf8([url path]) : std::string();
}

// setAllowedFileTypes: is deprecated in macOS 12 in favour of UTType, but UTType requires the
// UniformTypeIdentifiers framework to be linked and a deployment target of 11.0. The old selector
// still works on every supported release, so it stays and the warning is silenced locally.
void SetAllowedTypes(NSSavePanel* panel, NSString* type)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ type ]];
#pragma clang diagnostic pop
}
}

namespace OSXFileDialogs
{
void ApplyLightAppearance()
{
    if (NSApp != nil)
        [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameAqua]];
}

void ApplyDarkAppearance()
{
    // DarkAqua arrived in 10.14. On an older system the lookup returns nil and
    // setAppearance:nil simply returns the application to the system default, which is the right
    // fallback: the setting is cosmetic and must never be the reason a launch fails.
    if (NSApp != nil)
        [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
}

bool BrowseFolder(SDL_Window* window, std::string& path)
{
    (void)window;

    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:ToNSString(Tr("选择 Model 3 ROM 目录"))];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanCreateDirectories:YES];
        [panel setResolvesAliases:YES];
        SetStartLocation(panel, path);

        ActivateApplication();
        if ([panel runModal] != NSModalResponseOK)
            return false;

        path = PanelResult(panel);
        return !path.empty();
    }
}

bool BrowseSettingsFile(SDL_Window* window, std::string& path)
{
    (void)window;

    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:ToNSString(Tr("选择要载入的设置文件"))];
        [panel setMessage:ToNSString(Tr("Supermodel 设置 (*.ini)"))];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];

        // Deliberately unfiltered: the parser accepts any file, so a settings file that has been
        // renamed, or saved under a different extension, must still be selectable.
        ActivateApplication();
        if ([panel runModal] != NSModalResponseOK)
            return false;

        path = PanelResult(panel);
        return !path.empty();
    }
}

bool BrowseImageFile(SDL_Window* window, std::string& path)
{
    (void)window;

    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:ToNSString(Tr("选择背景图片（仅支持 PNG）"))];
        [panel setMessage:ToNSString(Tr("PNG 图片 (*.png)"))];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        SetAllowedTypes(panel, @"png");
        SetStartLocation(panel, path);

        ActivateApplication();
        if ([panel runModal] != NSModalResponseOK)
            return false;

        path = PanelResult(panel);
        return !path.empty();
    }
}

bool BrowseSaveFile(SDL_Window* window, std::string& path)
{
    (void)window;

    @autoreleasepool
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:ToNSString(Tr("设置另存为"))];
        [panel setNameFieldStringValue:@"Supermodel.ini"];
        [panel setCanCreateDirectories:YES];
        [panel setExtensionHidden:NO];
        [panel setAllowsOtherFileTypes:YES];
        SetAllowedTypes(panel, @"ini");
        SetStartLocation(panel, path);

        ActivateApplication();
        if ([panel runModal] != NSModalResponseOK)
            return false;

        path = PanelResult(panel);
        return !path.empty();
    }
}
}
