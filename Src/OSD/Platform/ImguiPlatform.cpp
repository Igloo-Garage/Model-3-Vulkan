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
 * ImguiPlatform.cpp
 *
 * The window/input half of ImGui, implemented over OSD::Platform events.
 *
 * The renderer half remains imgui_impl_opengl3 / imgui_impl_vulkan.  This
 * file only exists so neither Gui.cpp nor Overlay.cpp needs SDL_Event or
 * imgui_impl_sdl2/sdl3.  The key table is expressed in USB HID usage IDs,
 * which both SDL2 and SDL3 use for scancodes and which do not change between
 * the two.
 */

#include "ImguiPlatform.h"

#include "../../Pkgs/imgui/imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>

namespace OSD
{
namespace Platform
{

namespace
{

Nanoseconds g_lastFrame = 0;
std::string g_clipboard;

bool g_leftCtrl = false;
bool g_rightCtrl = false;
bool g_leftShift = false;
bool g_rightShift = false;
bool g_leftAlt = false;
bool g_rightAlt = false;
bool g_leftSuper = false;
bool g_rightSuper = false;

ImGuiKey KeycodeToImGuiKey(Keycode keycode)
{
    // SDLK_a .. SDLK_z and SDLK_0 .. SDLK_9 are the ASCII values in both SDL2
    // and SDL3.  Taking the keycode first, rather than the scancode, keeps
    // shortcuts on the key printed on the user's layout.
    if (keycode >= 'a' && keycode <= 'z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (keycode - 'a'));
    if (keycode >= 'A' && keycode <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (keycode - 'A'));
    if (keycode >= '0' && keycode <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (keycode - '0'));

    return ImGuiKey_None;
}

ImGuiKey ScancodeToImGuiKey(Scancode scancode)
{
    // Letters: USB HID 0x04..0x1D.
    if (scancode >= 0x04 && scancode <= 0x1D)
        return static_cast<ImGuiKey>(ImGuiKey_A + (scancode - 0x04));

    // Digits 1..9, then 0: USB HID 0x1E..0x27.
    if (scancode >= 0x1E && scancode <= 0x26)
        return static_cast<ImGuiKey>(ImGuiKey_1 + (scancode - 0x1E));
    if (scancode == 0x27)
        return ImGuiKey_0;

    // F1..F12: 0x3A..0x45; F13..F24: 0x68..0x73.
    if (scancode >= 0x3A && scancode <= 0x45)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (scancode - 0x3A));
    if (scancode >= 0x68 && scancode <= 0x73)
        return static_cast<ImGuiKey>(ImGuiKey_F13 + (scancode - 0x68));

    // Keypad 1..9: 0x59..0x61; 0: 0x62.
    if (scancode >= 0x59 && scancode <= 0x61)
        return static_cast<ImGuiKey>(ImGuiKey_Keypad1 + (scancode - 0x59));
    if (scancode == 0x62)
        return ImGuiKey_Keypad0;

    switch (scancode)
    {
    case 0x28: return ImGuiKey_Enter;
    case 0x29: return ImGuiKey_Escape;
    case 0x2A: return ImGuiKey_Backspace;
    case 0x2B: return ImGuiKey_Tab;
    case 0x2C: return ImGuiKey_Space;
    case 0x2D: return ImGuiKey_Minus;
    case 0x2E: return ImGuiKey_Equal;
    case 0x2F: return ImGuiKey_LeftBracket;
    case 0x30: return ImGuiKey_RightBracket;
    case 0x31: return ImGuiKey_Backslash;
    case 0x32: return ImGuiKey_Oem102;
    case 0x33: return ImGuiKey_Semicolon;
    case 0x34: return ImGuiKey_Apostrophe;
    case 0x35: return ImGuiKey_GraveAccent;
    case 0x36: return ImGuiKey_Comma;
    case 0x37: return ImGuiKey_Period;
    case 0x38: return ImGuiKey_Slash;
    case 0x39: return ImGuiKey_CapsLock;
    case 0x46: return ImGuiKey_PrintScreen;
    case 0x47: return ImGuiKey_ScrollLock;
    case 0x48: return ImGuiKey_Pause;
    case 0x49: return ImGuiKey_Insert;
    case 0x4A: return ImGuiKey_Home;
    case 0x4B: return ImGuiKey_PageUp;
    case 0x4C: return ImGuiKey_Delete;
    case 0x4D: return ImGuiKey_End;
    case 0x4E: return ImGuiKey_PageDown;
    case 0x4F: return ImGuiKey_RightArrow;
    case 0x50: return ImGuiKey_LeftArrow;
    case 0x51: return ImGuiKey_DownArrow;
    case 0x52: return ImGuiKey_UpArrow;
    case 0x53: return ImGuiKey_NumLock;
    case 0x54: return ImGuiKey_KeypadDivide;
    case 0x55: return ImGuiKey_KeypadMultiply;
    case 0x56: return ImGuiKey_KeypadSubtract;
    case 0x57: return ImGuiKey_KeypadAdd;
    case 0x58: return ImGuiKey_KeypadEnter;
    case 0x63: return ImGuiKey_KeypadDecimal;
    case 0x64: return ImGuiKey_Oem102;
    case 0x65: return ImGuiKey_Menu;
    case 0x67: return ImGuiKey_KeypadEqual;
    case 0xE0: return ImGuiKey_LeftCtrl;
    case 0xE1: return ImGuiKey_LeftShift;
    case 0xE2: return ImGuiKey_LeftAlt;
    case 0xE3: return ImGuiKey_LeftSuper;
    case 0xE4: return ImGuiKey_RightCtrl;
    case 0xE5: return ImGuiKey_RightShift;
    case 0xE6: return ImGuiKey_RightAlt;
    case 0xE7: return ImGuiKey_RightSuper;
    default:   return ImGuiKey_None;
    }
}

void UpdateModifierState(Scancode scancode, bool down)
{
    switch (scancode)
    {
    case 0xE0: g_leftCtrl = down; break;
    case 0xE4: g_rightCtrl = down; break;
    case 0xE1: g_leftShift = down; break;
    case 0xE5: g_rightShift = down; break;
    case 0xE2: g_leftAlt = down; break;
    case 0xE6: g_rightAlt = down; break;
    case 0xE3: g_leftSuper = down; break;
    case 0xE7: g_rightSuper = down; break;
    default: return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, g_leftCtrl || g_rightCtrl);
    io.AddKeyEvent(ImGuiMod_Shift, g_leftShift || g_rightShift);
    io.AddKeyEvent(ImGuiMod_Alt, g_leftAlt || g_rightAlt);
    io.AddKeyEvent(ImGuiMod_Super, g_leftSuper || g_rightSuper);
}

SystemCursor ToSystemCursor(ImGuiMouseCursor cursor)
{
    switch (cursor)
    {
    case ImGuiMouseCursor_TextInput: return SystemCursor::TextInput;
    case ImGuiMouseCursor_ResizeAll: return SystemCursor::ResizeAll;
    case ImGuiMouseCursor_ResizeNS:  return SystemCursor::ResizeNS;
    case ImGuiMouseCursor_ResizeEW:  return SystemCursor::ResizeEW;
    case ImGuiMouseCursor_ResizeNESW:return SystemCursor::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE:return SystemCursor::ResizeNWSE;
    case ImGuiMouseCursor_Hand:      return SystemCursor::Hand;
    case ImGuiMouseCursor_NotAllowed:return SystemCursor::NotAllowed;
    default:                         return SystemCursor::Arrow;
    }
}

int MouseButtonToImGui(uint8_t button)
{
    switch (button)
    {
    case 1: return 0;   // SDL_BUTTON_LEFT
    case 3: return 1;   // SDL_BUTTON_RIGHT
    case 2: return 2;   // SDL_BUTTON_MIDDLE
    case 4: return 3;   // SDL_BUTTON_X1
    case 5: return 4;   // SDL_BUTTON_X2
    default: return -1;
    }
}

const char* GetClipboardTextCallback(void*)
{
    if (!GetClipboardText(g_clipboard))
        g_clipboard.clear();
    return g_clipboard.c_str();
}

void SetClipboardTextCallback(void*, const char* text)
{
    SetClipboardText(text);
}

} // namespace

bool ImGuiPlatformInit(Window* window)
{
    (void)window;

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "Model 3-Vulkan Platform";
    io.GetClipboardTextFn = GetClipboardTextCallback;
    io.SetClipboardTextFn = SetClipboardTextCallback;
    g_lastFrame = 0;
    return true;
}

void ImGuiPlatformShutdown()
{
    // The context is still alive at this point; clear the callbacks so a
    // destroyed platform does not leave ImGui pointing at our statics.
    ImGuiIO& io = ImGui::GetIO();
    io.GetClipboardTextFn = nullptr;
    io.SetClipboardTextFn = nullptr;
    io.BackendPlatformName = nullptr;
    g_lastFrame = 0;
}

void ImGuiPlatformNewFrame(Window* window)
{
    ImGuiIO& io = ImGui::GetIO();

    int width = 0;
    int height = 0;
    GetWindowSize(window, &width, &height);
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

    int pixelWidth = width;
    int pixelHeight = height;
    GetWindowPixelSize(window, &pixelWidth, &pixelHeight);
    io.DisplayFramebufferScale = (width > 0 && height > 0)
        ? ImVec2(static_cast<float>(pixelWidth) / width, static_cast<float>(pixelHeight) / height)
        : ImVec2(1.0f, 1.0f);

    // WantTextInput describes the state at the end of the previous frame, which is what the stock
    // ImGui backends use too. SDL3 keeps text input off until asked, so without this a focused text
    // field never sees a character.
    SetTextInputActive(window, io.WantTextInput);

    const Nanoseconds now = Now();
    if (g_lastFrame != 0)
    {
        const float delta = static_cast<float>(now - g_lastFrame) / 1.0e9f;
        io.DeltaTime = std::max(1.0e-6f, std::min(delta, 0.1f));
    }
    else
    {
        io.DeltaTime = 1.0f / 60.0f;
    }
    g_lastFrame = now;

    /*
     * Select the OS pointer for what the widgets asked for last frame.
     *
     * The stock imgui_impl_sdl backends do this in their NewFrame. This platform layer replaces
     * them, and without it every SetMouseCursor() in a widget is invisible: the preview/game-list
     * splitter never shows its up-down arrow, and text fields never show an I-beam, which reads as
     * "this control cannot be dragged" even though the drag itself works.
     */
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0)
    {
        const ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
        if (io.MouseDrawCursor || imguiCursor == ImGuiMouseCursor_None)
        {
            SetCursorVisible(false);
        }
        else
        {
            SetCursorVisible(true);
            SetSystemCursor(window, ToSystemCursor(imguiCursor));
        }
    }
}

bool ImGuiPlatformProcessEvent(const Event& event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event.type)
    {
    case EventType::MouseMotion:
        io.AddMousePosEvent(event.x, event.y);
        return true;

    case EventType::MouseButtonDown:
    case EventType::MouseButtonUp:
        {
            const int button = MouseButtonToImGui(event.button);
            if (button < 0)
                return false;
            io.AddMouseButtonEvent(button, event.type == EventType::MouseButtonDown);
            return true;
        }

    case EventType::MouseWheel:
        // ImGui's SDL backend passes -x for horizontal and +y for vertical.
        io.AddMouseWheelEvent(-event.wheelX, event.wheelY);
        return true;

    case EventType::TextInput:
        if (event.text[0] != '\0')
            io.AddInputCharactersUTF8(event.text);
        return true;

    case EventType::KeyDown:
    case EventType::KeyUp:
        {
            const bool down = event.type == EventType::KeyDown;
            UpdateModifierState(event.scancode, down);

            ImGuiKey key = KeycodeToImGuiKey(event.keycode);
            if (key == ImGuiKey_None)
                key = ScancodeToImGuiKey(event.scancode);
            if (key == ImGuiKey_None)
                return false;

            io.AddKeyEvent(key, down);
            io.SetKeyEventNativeData(key, static_cast<int>(event.keycode),
                static_cast<int>(event.scancode), static_cast<int>(event.scancode));
            return true;
        }

    case EventType::WindowFocusGained:
        io.AddFocusEvent(true);
        return true;

    case EventType::WindowFocusLost:
        io.AddFocusEvent(false);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return true;

    default:
        return false;
    }
}

} // Platform
} // OSD
