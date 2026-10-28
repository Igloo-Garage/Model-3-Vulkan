@echo off
setlocal

call Get_SDL.cmd
call Get_SDL3.cmd
call Get_SDL_Net.cmd
call Get_ZLib.cmd
call Get_Vulkan.cmd

echo All downloads complete.
endlocal
