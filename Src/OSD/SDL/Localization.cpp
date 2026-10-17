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
#include "Localization.h"

#include <string_view>
#include <unordered_map>

namespace
{
	UiLanguage s_language = UiLanguage::English;   // English is the default interface

	/*
	 * Chinese source text -> English.
	 *
	 * The left-hand side is exactly what appears in the source, including any "##id" suffix, because
	 * that suffix forms the widget's identifier in ImGui and has to survive translation unchanged.
	 *
	 * Entries whose value contains printf specifiers keep the same specifiers in the same order as
	 * their key, since the caller's arguments are positional.
	 */
	const std::pair<std::string_view, const char*> kEnglish[] =
	{
		// ---- Configuration labels (WidgetLabel) -------------------------------------------------
		{ "PowerPC 频率 (MHz，0=自动)",     "PowerPC Frequency (MHz, 0=auto)" },
		{ "多线程",                          "Multithreaded" },
		{ "图形多线程",                      "Multithreaded GPU" },
		{ "四边形渲染",                      "Quad Rendering" },
		{ "无边框窗口",                      "Borderless Window" },
		{ "超采样",                          "Supersampling" },
		{ "CRT 色彩",                        "CRT Colour" },
		{ "2D 放大",                         "2D Upscale" },
		{ "宽屏背景",                        "Widescreen Background" },
		{ "锁定帧率",                        "Frame Limit" },
		{ "刷新率",                          "Refresh Rate" },
		{ "准星",                            "Crosshairs" },
		{ "准星样式",                        "Crosshair Style" },
		{ "关闭白闪",                        "Disable White Flash" },
		{ "前后平衡",                        "Front/Rear Balance" },
		{ "左右平衡",                        "Left/Right Balance" },
		{ "前后声道",                        "Front/Rear Channels" },
		{ "声道数",                          "Channels" },
		{ "音频刷新率",                      "Audio Rate" },
		{ "旧版音效 DSP",                    "Legacy Sound DSP" },
		{ "交换左右声道",                    "Swap Stereo" },
		{ "恒力（左）",                      "Constant Force (Left)" },
		{ "恒力（右）",                      "Constant Force (Right)" },
		{ "回中力",                          "Self-Centering Force" },
		{ "摩擦力",                          "Friction" },
		{ "振动",                            "Vibration" },
		{ "XInput 恒力阈值",                 "XInput Constant Force Threshold" },
		{ "XInput 恒力",                     "XInput Constant Force" },
		{ "XInput 振动",                     "XInput Vibration" },
		{ "XInput 立体振动",                 "XInput Stereo Vibration" },
		{ "SDL 恒力",                        "SDL Constant Force" },
		{ "SDL 回中力",                      "SDL Self-Centering Force" },
		{ "SDL 摩擦力",                      "SDL Friction" },
		{ "SDL 振动",                        "SDL Vibration" },
		{ "SDL 恒力阈值",                    "SDL Constant Force Threshold" },
		{ "输出",                            "Outputs" },
		{ "输出加换行",                      "Outputs With Line Feed" },
		{ "TCP 端口",                        "TCP Port" },
		{ "UDP 端口",                        "UDP Broadcast Port" },
		{ "退出时导出内存",                  "Dump Memory On Exit" },
		{ "退出时导出贴图",                  "Dump Textures On Exit" },
		{ "数字灵敏度",                      "Digital Sensitivity" },
		{ "数字回中速度",                    "Digital Decay Speed" },
		{ "键盘灵敏度",                      "Keyboard Sensitivity" },
		{ "键盘回中速度",                    "Keyboard Decay Speed" },
		{ "鼠标 X 死区",                     "Mouse X Dead Zone" },
		{ "鼠标 Y 死区",                     "Mouse Y Dead Zone" },
		{ "鼠标 Z 死区",                     "Mouse Z Dead Zone" },

		// ---- Option lists (combos) --------------------------------------------------------------
		{ "关闭",                            "Off" },
		{ "锐利",                            "Sharp" },
		{ "双五次",                          "Biquintic" },
		{ "双线性",                          "Bilinear" },
		{ "双三次",                          "Bicubic" },
		{ "ARI/D93（日版）",                 "ARI/D93 (Japan)" },
		{ "BT.601 525/D65（美版）",          "BT.601 525/D65 (US)" },
		{ "BT.601 625/D65（欧版）",          "BT.601 625/D65 (EU)" },
		{ "57.5 Hz（真实机台）",             "57.5 Hz (arcade exact)" },
		{ "60 Hz（推荐）",                   "60 Hz (recommended)" },
		{ "仅玩家 1",                        "Player 1 only" },
		{ "仅玩家 2",                        "Player 2 only" },
		{ "玩家 1 和 2",                     "Players 1 and 2" },
		{ "标准布局（Xbox / PS 通用）",      "Standard layout (Xbox / PS)" },
		{ "仅键盘",                          "Keyboard only" },
		{ "自定义（不改动）",                "Custom (leave unchanged)" },
		{ "自动检测",                        "Auto-detect" },
		{ "Xbox 360 手柄",                   "Xbox 360 controller" },
		{ "Xbox One 手柄",                   "Xbox One controller" },
		{ "Xbox Series 手柄",                "Xbox Series controller" },
		{ "PS3 手柄 (DualShock 3)",          "PS3 controller (DualShock 3)" },
		{ "PS4 手柄 (DualShock 4)",          "PS4 controller (DualShock 4)" },
		{ "PS5 手柄 (DualSense)",            "PS5 controller (DualSense)" },
		{ "其他 PC 手柄",                    "Other PC controller" },
		{ "PS3 手柄",                        "PS3 controller" },
		{ "PS4 手柄",                        "PS4 controller" },
		{ "PS5 手柄",                        "PS5 controller" },
		{ "XInput（Xbox 手柄）",             "XInput (Xbox controllers)" },
		{ "SDL 手柄",                        "SDL Gamepad" },
		{ "VR1（红）",                       "VR1 (Red)" },
		{ "VR2（蓝）",                       "VR2 (Blue)" },
		{ "VR3（黄）",                       "VR3 (Yellow)" },
		{ "VR4（绿）",                       "VR4 (Green)" },
		{ "2x MSAA",                         "2x MSAA" },
		{ "4x MSAA",                         "4x MSAA" },
		{ "扫描线",                          "Scanlines" },
		{ "扫描线 + 荫罩",                   "Scanlines + Phosphor Mask" },
		{ "栅条式（特丽珑类）",              "Aperture grille (Trinitron)" },
		{ "槽孔式（世嘉街机）",              "Slot mask (Sega arcade)" },
		{ "点阵式（普通彩显）",              "Shadow mask (dot triad)" },

		// ---- Resolution presets -----------------------------------------------------------------
		// Keep these byte-identical to the labels in Gui.cpp: the source string is the lookup key, so
		// an edit on one side alone silently falls back to Chinese in the English UI. Only the two
		// multiples a real panel reaches carry a marker; the rest are deliberately plain.
		{ "1x 原生 (496 x 384)",                   "1x Native (496 x 384)" },
		{ "2x 分辨率 (992 x 768)",                 "2x (992 x 768)" },
		{ "3x 分辨率 (1488 x 1152 - 2K 屏推荐)",   "3x (1488 x 1152 - recommended for 2K)" },
		{ "4x 分辨率 (1984 x 1536)",               "4x (1984 x 1536)" },
		{ "5x 分辨率 (2480 x 1920 - 4K 屏推荐)",   "5x (2480 x 1920 - recommended for 4K)" },
		{ "6x 分辨率 (2976 x 2304)",               "6x (2976 x 2304)" },
		{ "8x 分辨率 (3968 x 3072)",               "8x (3968 x 3072)" },
		{ "自定义分辨率",                          "Custom" },

		// ---- Language selector ------------------------------------------------------------------
		{ "语言",                            "Language" },
		{ "界面语言##UiLanguage",            "Interface language##UiLanguage" },

		// ---- Tabs -------------------------------------------------------------------------------
		{ "常用",                            "Common" },
		{ "核心",                            "Core" },
		{ "画面",                            "Video" },
		{ "声音",                            "Sound" },
		{ "网络",                            "Network" },
		{ "按键",                            "Controls" },
		{ "高级设置",                        "Advanced" },

		// Section headings inside the combined force feedback / sensitivity page. These used to be
		// tab titles; they are kept as headings so the two groups stay distinguishable now that they
		// share a page.
		{ "力反馈",                          "Force feedback" },
		{ "灵敏度",                          "Sensitivity" },

		// ---- Section headings -------------------------------------------------------------------
		{ "抗锯齿",                          "Anti-aliasing" },
		{ "画面缩放",                        "Scaling" },
		{ "CRT 滤镜",                        "CRT Filter" },
		{ "玻璃辉光",                        "Glass Glow" },
		{ "手柄",                            "Controllers" },
		{ "视角切换",                        "View Cycling" },

		// ---- Device and resolution controls -----------------------------------------------------
		{ "显示设备##VulkanDevice",          "Display device##VulkanDevice" },
		{ "分辨率",                          "Resolution" },
		{ "自定义宽度##XResolution",         "Custom width##XResolution" },
		{ "自定义高度##YResolution",         "Custom height##YResolution" },
		{ "全屏##FullScreen",                "Fullscreen##FullScreen" },
		{ "拉伸##Stretch",                   "Stretch##Stretch" },
		{ "宽屏##WideScreen",                "Widescreen##WideScreen" },
		{ "垂直同步##VSync",                 "V-Sync##VSync" },
		{ "显示帧率##ShowFrameRate",         "Show frame rate##ShowFrameRate" },
		{ "独占全屏 · G-SYNC/VRR##ExclusiveFullscreen", "Exclusive fullscreen - G-SYNC/VRR##ExclusiveFullscreen" },
		{ "独占全屏 · 固定街机刷新率 57.524Hz##ExclusiveRefreshRate", "Exclusive fullscreen - arcade exact 57.524 Hz##ExclusiveRefreshRate" },
		{ "全屏刷新方式",                    "Fullscreen refresh mode" },
		{ "输出显示器##DisplayNumber",       "Output display##DisplayNumber" },
		{ "主显示器",                        "Primary" },
		{ "副显示器",                        "Secondary" },
		{ "自动 · 跟随系统主显示器",         "Automatic - follow the system's main display" },
		{ "57/58Hz 可用",                    "57/58 Hz available" },
		{ "游戏窗口与两种独占全屏都出现在所选显示器上，下次启动游戏时生效。\n选「自动」则始终跟随系统主显示器。\n\n每项末尾的 #n 是 Windows 给该显示器的编号（即「显示设置 → 标识」里的数字）。\n这个编号也是 Scripts\\ArcadeDisplay.ps1 的 -Monitor 参数 —— 两边必须填同一个，\n否则脚本在某一台上切换了 57.524Hz 模式，而游戏在另一台上全屏，结果是\n静默退回桌面刷新率（不报错）。",
		                                     "The game window and both exclusive fullscreen modes appear on the chosen display. Takes effect the next time a game starts.\n\"Automatic\" always follows the system's main display.\n\nThe trailing #n is the number Windows gives that display - the one Display Settings > Identify shows.\nThe same number is the -Monitor argument to Scripts\\ArcadeDisplay.ps1, and both must name the same display:\notherwise the script switches the 57.524 Hz mode on one monitor while the game goes fullscreen on another,\nand the result is a silent fall back to the desktop refresh rate." },
		{ "无边框全屏（默认）##SyncOff",     "Borderless fullscreen (default)##SyncOff" },
		{ "游戏和设置界面共用同一个窗口，来回切换更顺，\n代价是有时帧率略低。\n\n【例外】若驱动已为「窗口和全屏模式」启用 G-SYNC/FreeSync\n（NVIDIA 控制面板 → 设置 G-SYNC），本模式也能让显示器跟随模拟器，\n效果与下面的独占模式相同，且无需自定义分辨率。\n这一点本程序无法检测，只能看显示器自己的实时刷新率：\n游戏运行时显示约 57-58Hz 表示已生效；显示桌面刷新率（如 120Hz）则未生效，\n此时画面会有轻微帧节奏不均，用下面的独占模式可以消除。",
		                                     "The game and this settings window share one window, so switching between them is smoother.\nThe cost is a slightly lower frame rate at times.\n\nEXCEPTION: if the driver is set to apply G-SYNC/FreeSync to windowed output\n(NVIDIA Control Panel > Set up G-SYNC), this mode also lets the display follow the emulator,\nmatching the exclusive modes below and needing no custom resolution.\nThis program cannot detect that, so only the display's own live refresh readout can tell you:\nabout 57-58 Hz while a game runs means it is in effect. The desktop rate (e.g. 120 Hz) means it is not,\nin which case there is slight cadence unevenness that an exclusive mode below will remove." },
		{ "独占全屏 · G-SYNC/VRR##SyncGSync", "Exclusive fullscreen - G-SYNC/VRR##SyncGSync" },
		{ "独占全屏 · 固定街机刷新率 57.524Hz##SyncFixedRate", "Exclusive fullscreen - fixed arcade 57.524 Hz##SyncFixedRate" },
		{ "（由「独占全屏 · 固定街机刷新率」决定）", "  (set by Exclusive fullscreen - fixed arcade rate)" },
		{ "（G-SYNC 下为呈现节奏，显示器跟随）", "  (under G-SYNC this is the present rate the display follows)" },
		{ "已检测到显示器的 57/58 Hz 模式，可以 1:1 输出。", "A 57/58 Hz display mode was detected; 1:1 output is available." },
				{ "多亮的地方才会发光。调高更干净，\n调低整片画面会被抬亮、发蒙。",
                                     "How bright a pixel must be before it glows. Higher is cleaner;\nlower lifts the whole picture and makes it look washed out." },
		{ "未检测到显示器的 57/58 Hz 模式，此项不可选。",
		                                     "No 57/58 Hz display mode was found, so this option is unavailable." },
		{ "已声明 VRR",
		                                     "(declares VRR" },
		{ "→ 57.5Hz 在时序范围内，可尝试创建自定义分辨率",
		                                     "-> 57.5 Hz is inside the timing range; a custom resolution is worth trying" },
		{ "→ 57.5Hz 不在时序范围内，创建自定义分辨率无用",
		                                     "-> 57.5 Hz is outside the timing range; creating a custom resolution would be wasted effort" },
		{ "把显示器锁到真机的 57.5Hz，与帧率 1:1，画面最顺滑。\n少数显示器或驱动不接受该时序，遇到黑屏就换回上面的选项。",
		                                     "Locks the display to the arcade's 57.5 Hz, 1:1 with the frame rate, the smoothest option.\nSome displays or drivers reject the timing - on a black screen, use the option above." },
		{ "把显示器锁到真机的 57.5Hz，与帧率 1:1，画面最顺滑。\n这是给没有 VRR 的显示器用的替代方案，需要先创建自定义分辨率。\n少数显示器或驱动不接受该时序，遇到黑屏就换回上面的选项。\n\n【本项为何不可用】\n本项需要一个 57/58Hz 的「固定模式」，而 VRR 是另一回事：\nVRR 是一段可以落在其中的范围，不需要存在某个具体速率对应的模式。\n所以「没有 57/58Hz 模式」并不等于「做不到 57.5Hz」——\n若显示器支持 VRR，用上面的 G-SYNC 即可，不受此影响。\n\n【不支持 VRR 时怎么办】\n需在显卡控制面板创建一个 57.524Hz 自定义分辨率\n（见 Scripts\\ArcadeDisplay.ps1，创建后自动生效）。\n创建前请对照下方列出的该显示器「时序范围」：\n若 57.5Hz 不在其中，创建也不会被接受。\n\n注意「在时序范围内」只是必要条件 —— 它说明面板会接受该时序，\n是否真能创建还取决于显卡驱动是否开放自定义分辨率（多数笔记本面板不开放）。\n另：未标「已声明 VRR」不代表不支持，只是 EDID 里没写。",
		                                     "Locks the display to the arcade's 57.5 Hz, 1:1 with the frame rate, the smoothest option.\nThis is the alternative for a display without VRR, and needs a custom resolution first.\nSome displays or drivers reject the timing - on a black screen, use the option above.\n\nWHY THIS OPTION IS UNAVAILABLE\nIt needs a *discrete* 57/58 Hz mode, which is a different thing from VRR:\nVRR is a range the panel can scan anywhere within, and needs no mode for any particular rate.\nSo \"no 57/58 Hz mode\" does not mean \"cannot do 57.5 Hz\" -\nif the display supports VRR, use the G-SYNC option above and this does not affect you.\n\nIF THE DISPLAY DOES NOT SUPPORT VRR\nA custom 57.524 Hz resolution has to be created in the GPU control panel\n(see Scripts\\ArcadeDisplay.ps1; it is picked up automatically).\nCompare it against the timing range listed below first: if 57.5 Hz is not inside it,\nthe display will not accept the timing either.\n\nNote that \"inside the timing range\" is only a necessary condition - it says the panel will accept the\ntiming, not that a custom resolution can be created, which depends on the graphics driver exposing that\nat all (most laptop panels do not).\nAlso: a missing \"declares VRR\" note does not mean unsupported, only that the EDID does not say." },
		{ "无法读取显示器 EDID，故不能判断 57.5Hz 是否在其时序范围内。",
		                                     "The display EDID could not be read, so whether 57.5 Hz is inside its timing range is unknown." },
		{ "不锁定刷新率，让显示器的 G-SYNC/FreeSync 跟着模拟器的帧率走，画面不撕裂。\n需要 VRR 显示器，驱动里也要开启；不需要自定义分辨率。\n\n【必须满足】显示器支持 VRR，且其刷新率范围包含 57.5Hz。\n这两项本程序无法自动检测，可以在这些地方确认\n（都看「范围」而不是「是否支持」）：\n  · Windows 11：设置 → 系统 → 显示 → 高级显示器设置\n  · NVIDIA 控制面板 → 设置 G-SYNC（会列出该显示器的刷新率范围）\n  · 显示器自带的 OSD 菜单（多数游戏屏会显示当前实时刷新率）\n若显示器不支持，本项不会报错也不会更差，只是退回按面板自身的刷新率输出，\n画面会有和「无边框全屏」类似的轻微帧节奏不均。\n\n【笔记本内屏】多数笔记本内置屏不支持 VRR（外接电竞屏通常支持）。\n这类机器上本项与「无边框全屏」等价 —— 面板仍按自身刷新率，\n既不会更差也不会更好，可以留在第 1 项。",
		                                     "Does not lock the refresh rate; lets the display's G-SYNC/FreeSync follow the emulator's frame rate, so there is no tearing.\nNeeds a VRR display, enabled in the driver; no custom resolution required.\n\nREQUIREMENTS: a display that supports VRR, whose refresh range includes 57.5 Hz.\nThis program cannot detect either - check in one of these places, looking at the *range* rather than\nwhether it is supported at all:\n  - Windows 11: Settings > System > Display > Advanced display\n  - NVIDIA Control Panel > Set up G-SYNC (lists the refresh range of each display)\n  - The display's own on-screen menu (most gaming panels show the live refresh rate)\nA display that cannot do it is not an error and is no worse than the alternative: the session falls back\nto the panel's own rate, with the same slight unevenness as borderless fullscreen.\n\nLAPTOP INTERNAL PANELS: most built-in laptop screens do not support VRR (external gaming monitors\nusually do). On such a machine this option is equivalent to borderless fullscreen - the panel stays on\nits own rate, so it is neither better nor worse, and option 1 can be left selected." },
		{ "怎么选：\n  · 显示器支持 VRR（G-SYNC/FreeSync）→ 选第 2 项\n  · 显示器不支持 VRR，但已检测到 57/58Hz 模式 → 选第 3 项\n  · 两者都没有 → 留在第 1 项（第 3 项不可用）\n\n三种方式的区别只在于「谁定下一次扫描」：\n第 1 项由桌面合成器定（面板按自身刷新率），\n第 2 项由显示器跟随模拟器，第 3 项把显示器锁到 57.5Hz。",
		                                     "Which to pick:\n  - The display supports VRR (G-SYNC/FreeSync) -> option 2\n  - It does not, but a 57/58 Hz mode was detected -> option 3\n  - Neither -> stay on option 1 (option 3 is unavailable)\n\nThe three differ only in who decides when the next scan happens:\noption 1 leaves that to the desktop compositor (the panel keeps its own rate),\noption 2 lets the display follow the emulator, option 3 locks the display to 57.5 Hz." },
		{ "怎么选：\n  · 显示器支持 VRR（G-SYNC/FreeSync）→ 选第 2 项\n  · 否则留在第 1 项；想用第 3 项需先在显卡控制面板创建 57.524Hz 自定义分辨率\n\n三种方式的区别只在于「谁定下一次扫描」：\n第 1 项由桌面合成器定（面板按自身刷新率），\n第 2 项由显示器跟随模拟器，第 3 项把显示器锁到 57.5Hz。",
		                                     "Which to pick:\n  - The display supports VRR (G-SYNC/FreeSync) -> option 2\n  - Otherwise stay on option 1; option 3 needs a custom 57.524 Hz resolution created in the GPU\n    control panel first\n\nThe three differ only in who decides when the next scan happens:\noption 1 leaves that to the desktop compositor (the panel keeps its own rate),\noption 2 lets the display follow the emulator, option 3 locks the display to 57.5 Hz." },
		{ "显示控制台窗口##ShowConsole",     "Show console window##ShowConsole" },
		{ "Vulkan 校验层##VkValidation",     "Vulkan validation layers##VkValidation" },

		// ---- Display device helper text ---------------------------------------------------------
		{ "自动（默认高性能显卡）",          "Auto (default high-performance GPU)" },
		{ "没有检测到可用的 Vulkan 显卡。",  "No usable Vulkan device was detected." },
		{ "自动（系统默认）",                "Auto (system default)" },
		{ "没有检测到可用的音频输出设备。",  "No usable audio output device was detected." },
		{ " [系统默认]",                     " [system default]" },
		{ "OpenGL 无法在程序内选择显卡（OpenGL 没有设备枚举接口）。\n本程序只能“请求”使用独立显卡，驱动可以不理会。\n\n要改用另一块显卡：\nWindows 设置 → 系统 → 显示 → 显卡（或“高级显示器设置”里的图形设置），\n为 Supermodel-VK.exe 指定“高性能”，然后重新启动本程序。\n\n注意：该设置是否对 OpenGL 生效由驱动决定，且本程序无法检测它是否被采纳，\n以上一行 GL_RENDERER 为准。",
		                                     "OpenGL offers no way to choose a GPU from within the program (it has no device enumeration interface).\nThis program can only *ask* for the discrete GPU, and the driver is free to ignore that.\n\nTo use a different GPU:\nWindows Settings > System > Display > Graphics, set Supermodel-VK.exe to \"High performance\",\nthen restart this program.\n\nNote: whether that setting affects OpenGL is up to the driver, and this program cannot detect whether it\nwas honoured - the GL_RENDERER line above is the authority." },
		{ "实际渲染卡：未知（未能读取 GL_RENDERER）",
		                                     "Rendering GPU: unknown (GL_RENDERER could not be read)" },
		{ "实际渲染卡：",                    "Rendering GPU: " },
		{ "要改用另一块显卡：Windows 设置 → 系统 → 显示 → 显卡（或“高级显示器设置”里的图形设置），\n为 Supermodel-VK.exe 指定“高性能”，然后重新启动本程序。\n注意：该设置是否对 OpenGL 生效由驱动决定，且本程序无法检测它是否被采纳，以上一行 GL_RENDERER 为准。",
		                                     "To use a different GPU: Windows Settings > System > Display > Graphics,\nset Supermodel-VK.exe to \"High performance\", then restart this program.\nNote: whether that setting affects OpenGL is up to the driver, and this program cannot detect whether it was\nhonoured - the GL_RENDERER line above is the authority." },

		// ---- Sound ------------------------------------------------------------------------------
		{ "音效##SoundVolume",               "Sound effects##SoundVolume" },
		{ "音乐##MusicVolume",               "Music##MusicVolume" },
		{ "游戏音效##EmulateSound",          "Game sound effects##EmulateSound" },
		{ "MPEG 音乐##EmulateDSB",           "MPEG music##EmulateDSB" },
		{ "声卡设备##AudioDevice",           "Audio device##AudioDevice" },
		{ "Daytona 2 的背景音乐偏小，可以把音乐调到 150 以上。运行中 F9/F10 调音乐，F11/F12 调音效。",
		                                     "Daytona 2's background music is quiet; try music above 150. In game, F9/F10 adjust music and F11/F12 adjust sound effects." },

		// ---- Controllers ------------------------------------------------------------------------
		{ "输入系统##InputSystem",           "Input system##InputSystem" },
		{ "力反馈##ForceFeedback",           "Force feedback##ForceFeedback" },
		{ "按键在「按键」页里改。Xbox 手柄选 XInput，左右扳机才能分开。",
		                                     "Bindings are edited on the Controls tab. Choose XInput for Xbox controllers so the two triggers stay separate." },
		{ "手柄按键按「位置」对应，不看你手柄上印的字：",
		                                     "Controller buttons are matched by position, not by the labels printed on your pad:" },
		{ "    Back / Select / Share 位置 = 投币      Start / Options / Menu 位置 = 启动 / 确认",
		                                     "    Back / Select / Share = Insert coin      Start / Options / Menu = Start / Confirm" },
		{ "手柄型号##ControllerFamily",      "Controller model##ControllerFamily" },
		{ "自动检测（%s）",                  "Auto-detect (%s)" },
		{ "未检测到手柄。请确认已插好，然后点下面的「重新检测」。",
		                                     "No controller detected. Check that it is connected, then press Detect Again below." },
		{ "检测到：%s（建议输入系统：%s）", "Detected: %s (suggested input system: %s)" },
		{ "重新检测",                        "Detect Again" },
		{ "键位预设##ControlPreset",         "Binding preset##ControlPreset" },
		{ "应用推荐键位",                    "Apply Recommended Bindings" },
		{ "会覆盖「按键」页里已有的绑定。",  "This overwrites the bindings already on the Controls tab." },
		{ "单键循环切换视角##VRCycle",       "Cycle views with one button##VRCycle" },
		{ "起始视角##VRDefaultView",         "Starting view##VRDefaultView" },
		{ "把手柄和键盘键位重置为选定的预设？", "Reset controller and keyboard bindings to the selected preset?" },
		{ "这会覆盖你在「按键」页里做过的所有改动。", "This overwrites every change you made on the Controls tab." },
		{ "应用",                            "Apply" },
		{ "取消",                            "Cancel" },

		// ---- Network ----------------------------------------------------------------------------
		{ "启用网络板##Network",             "Enable network board##Network" },
		{ "模拟网络板##SimulateNet",         "Simulate network board##SimulateNet" },
		{ "本机端口##PortIn",                "Local port##PortIn" },
		{ "对方端口##PortOut",               "Peer port##PortOut" },
		{ "对方地址##AddressOut",            "Peer address##AddressOut" },
		{ "单机对连",                        "Local loopback link" },
		{ "Daytona 2 单机需要两个端口相同。", "Daytona 2 on one machine needs both ports set to the same value." },

		// ---- Advanced ---------------------------------------------------------------------------
		{ "这些选项需要重新开一局才会生效（不用退出程序：按 Esc 回到本窗口再启动即可）。",
		                                     "These take effect when a new game starts; there is no need to quit - press Esc to return here and start a game again." },
		{ "当前渲染引擎是 OpenGL，本页的抗锯齿与滤镜都不起作用。",
		                                     "The running renderer is OpenGL, so the antialiasing and filters on this page do nothing." },
		{ "想用这些效果，请到「常用」页把渲染引擎切换到 Vulkan 3D。",
		                                     "To use them, switch the renderer to Vulkan 3D on the Common tab." },
		{ "硬件 MSAA##MSAA",                 "Hardware MSAA##MSAA" },
		{ "FXAA 后处理抗锯齿##PostFXAA",     "FXAA post-process anti-aliasing##PostFXAA" },
		{ "整数倍缩放（点对点）##IntegerScale", "Integer scaling (pixel-exact)##IntegerScale" },
		{ "CRT 滤镜##PostCRT",               "CRT filter##PostCRT" },
		{ "扫描线数量##PostScanlines",       "Scanline count##PostScanlines" },
		{ "整数倍缩放下可见区高 %d 像素，画面里实际约 %d 条扫描线。",
		                                     "With integer scaling the visible area is %d pixels tall, so about %d scanlines fall inside the picture." },
		// Integer-scale readouts. These are the live figures, not settings, so they carry no shortcut
		// suffix; the text must match Gui.cpp byte for byte or the English UI silently shows Chinese.
		{ "整数倍缩放实际生效：%dx，可见区 %d x %d。",
		                                     "Integer scaling is in effect: %dx, visible area %d x %d." },
		{ "面板 %d x %d：按高度只能取到 %d 倍。",
		                                     "Panel %d x %d: the height only allows %dx." },
		{ "面板自身尺寸未能读取，以上按窗口尺寸估算。",
		                                     "The panel size could not be read, so the figures above are estimated from the window." },
		{ "下一档 %dx 需要面板至少 %d 行，本面板只有 %d 行，所以取不到。",
		                                     "%dx would need at least %d panel rows; this panel has %d, so it cannot be reached." },
		{ "「分辨率」里的 %d x %d 在此模式下会被覆盖为 %d x %d。",
		                                     "The %d x %d set in Resolution is overridden to %d x %d in this mode." },
		{ "实际生效 %dx：可见区 %d x %d（面板 %d x %d）。",
		                                     "In effect %dx: visible area %d x %d (panel %d x %d)." },
		{ "下一档 %dx 需要 %d 行，本面板只有 %d 行。",
		                                     "%dx would need %d rows; this panel has %d." },
		// Controller family and the two 8BitDo notes. The label replaces "other PC pad" in the
		// detection line; the notes state what the pad's own hardware does, which its name cannot
		// convey and which otherwise reads as a fault.
		{ "8BitDo 手柄",                     "8BitDo pad" },
		{ "L2/R2 是按钮式扳机：绑定为刹车或油门可用，但是有/无两档，不是线性。",
		                                     "L2/R2 are button-type triggers: usable for brake or throttle, but on/off rather than analogue." },
		{ "SDL 未识别这块手柄，按钮编号无法保证与预设一致。请改用 SDL 手柄以外的输入系统，或用「输入设置」逐个重新绑定。",
		                                     "SDL does not recognise this pad, so its button numbering may not match the preset. Choose an input system other than SDL Gamepad, or rebind each input in Input Settings." },
		{ "想要正好 384 条（街机行数）请填 %d。", "For exactly 384 lines (the arcade count) set %d." },
		{ "自动 (384 行)",                   "Auto (384 lines)" },
		{ "%d 行",                           "%d lines" },
		{ "扫描线强度##PostScanlineIntensity", "Scanline strength##PostScanlineIntensity" },
		{ "点阵类型##PostMaskType",          "Mask type##PostMaskType" },
		{ "点阵粗细##PostMaskScale",         "Mask pitch##PostMaskScale" },
		{ "1 像素 (1:1)",                    "1 pixel (1:1)" },
		{ "%d 像素",                         "%d pixels" },
		{ "点阵强度##PostMaskIntensity",     "Mask strength##PostMaskIntensity" },
		{ "亮度补偿##PostGain",              "Brightness compensation##PostGain" },
		{ "辉光强度##PostGlow",              "Glow strength##PostGlow" },
		{ "辉光级数##PostGlowLevels",        "Glow levels##PostGlowLevels" },
		{ "推荐配置##PostPreset",            "Recommended preset##PostPreset" },
		{ "自定义（手动）",                  "Custom (manual)" },
		{ "细 · 现代 CRT（BVM 观感）",      "Fine - modern CRT (BVM look)" },
		{ "细 · 点阵略粗",                  "Fine - coarser mask" },
		{ "街机原机 · 扫描线最接近真机",    "Arcade original - closest scanline pitch" },
		{ "街机原机 · 粗点阵",              "Arcade original - coarse mask" },
		{ "扫描线周期 %.1f px，点阵周期 %d px，比值 %.2f 倍",
		                                     "Scanline pitch %.1f px, mask period %d px, ratio %.2f x" },
		{ "整数倍对齐：不会出现彩色横带。",  "Whole-number ratio: no colour banding." },
		{ "不成整数倍：会出现周期约 %.0f px 的彩色横带。",
		                                     "Not a whole-number ratio: colour bands about %.0f px apart." },
		{ "辉光阈值##PostGlowThreshold",     "Glow threshold##PostGlowThreshold" },

		// ---- In-game settings overlay -----------------------------------------------------------
		{ "画面设置（实时）##OverlayPanel", "Picture settings (live)##OverlayPanel" },
		{ "应用##OverlayApply",              "Apply##OverlayApply" },
		{ "撤销##OverlayCancel",             "Cancel##OverlayCancel" },
		{ "关闭##OverlayClose",              "Close##OverlayClose" },
		{ "Tab 开关此面板。改动立即生效，无需重启。",
		                                     "Tab opens and closes this panel. Changes take effect immediately; no restart." },

		// ---- Statistics readout ------------------------------------------------------------------
		// Only the settings-window side needs translating. The readout's own labels are deliberately
		// left in English: CPU, GPU, VRAM and FPS are the same words in every language this window
		// speaks, and keeping them out of the table also keeps their format specifiers out of reach of
		// a translation that could reorder or drop one.
		{ "统计面板##ShowStatsOsd",           "Statistics panel##ShowStatsOsd" },
		{ "在游戏画面左上角显示一串实时数据：渲染引擎、屏幕刷新率与同步方式、CPU 频率与占用、GPU 频率与占用、显存与内存占用、帧率和 1% Low 帧率。\n游戏里按 Tab 上面那个键（`）也能开关。\n只有 Vulkan 渲染引擎下有效。",
		                                     "Shows a column of live figures in the top left of the game: renderer, display refresh rate and sync mode, CPU clock and load, GPU clock and load, video and system memory, frame rate and the 1% low.\nIn play, the key above Tab (`) toggles it.\nVulkan renderer only." },

		// ---- Bezel backdrop ---------------------------------------------------------------------
		{ "机柜背景图",                      "Cabinet backdrop" },
		{ "背景图##BezelMode",               "Backdrop##BezelMode" },
		{ "内置机柜图",                      "Built-in cabinet art" },
		{ "自选图片",                        "My own image" },
		{ "浏览##BezelBrowse",               "Browse##BezelBrowse" },
		{ "选择背景图片（仅支持 PNG）",      "Choose a backdrop image (PNG only)" },
		{ "PNG 图片 (*.png)",                "PNG images (*.png)" },
		{ "仅支持 PNG 格式。留空则使用内置机柜图。",
		                                     "PNG only. Leave empty to use the built-in cabinet art." },
		{ "背景图亮度##BezelBrightness",     "Backdrop brightness##BezelBrightness" },
		{ "100% 为原图亮度。太亮会抢游戏画面，可以调低一些。",
		                                     "100% is the image as authored. Dial it down if it competes with the game." },
		{ "背景图只在 Vulkan 渲染器下生效。",  "The backdrop only works with the Vulkan renderer." },
		{ "已开启宽屏：画面占满左右，没有黑边可填，背景图不会显示。",
		                                     "Widescreen is on: the picture fills the width, so there are no side bars left to fill and the backdrop is not shown." },
		{ "背景图只画在黑边上，不会盖住游戏画面。改完重新开一局生效。",
		                                     "The backdrop is drawn only on the black bars, never over the picture. Applies when a new game starts." },

		// ---- Renderer fallback notice -----------------------------------------------------------
		// Posted by the game session when Vulkan was selected but could not start. The window is hidden
		// while a game runs, so this is the only place the user is told the session silently changed
		// renderer - and it sits beside the setting that would change it back.
		//
		// Deliberately says "the OpenGL renderer" rather than naming New 3D: which of the two OpenGL
		// backends takes over depends on the New3DEngine setting, so naming one would be wrong half the
		// time. The log line next to it names the one actually used.
		{ "Vulkan 无法在此机器上启动（显卡或驱动不支持），本局已自动改用 OpenGL 渲染器。若想重试 Vulkan，请更新显卡驱动后在这里重新选择。",
		                                     "Vulkan could not start on this machine (the GPU or driver does not support it), so this session used the OpenGL renderer. To try Vulkan again, update your GPU driver and select it here." },
		{ "知道了##RendererNotice",          "Got it##RendererNotice" },
		{ "调试输出",                        "Debug output" },
		{ "当前是窗口化全屏：若显示器按自身刷新率输出，它与 57.5Hz 不能整除，会出现轻微的帧节奏不均 —— 在「常用」页改用独占全屏可以消除。\n但如果驱动已为「窗口和全屏模式」启用 G-SYNC/FreeSync，显示器会跟随模拟器，这种情况本程序无法检测：请看显示器自己的实时刷新率（约 57-58Hz 即已生效，此时本条提示可忽略）。",
		                                     "Borderless fullscreen: if the display is running at its own refresh rate, that rate does not divide evenly into 57.5 Hz and frames repeat unevenly - an exclusive sync mode on the Common tab removes this.\nBut if the driver is set to apply G-SYNC/FreeSync to windowed output, the display follows the emulator instead, and this program cannot detect that: check the display's own live refresh readout (about 57-58 Hz means it is in effect, and this notice can be ignored)." },

		// ---- Settings that belong to one renderer ------------------------------------------------
		// Shown beside an option the running renderer does not read, so the reason it is greyed out is
		// visible rather than left to be guessed. The two anti-aliasing systems are the case that
		// prompted this: MSAA (Vulkan) and Supersampling (OpenGL) look like two halves of one feature.
		{ "仅 Vulkan 渲染引擎有效，当前不是。",  "Only the Vulkan renderer uses this; it is not the one running." },
		{ "仅 OpenGL 渲染引擎有效，当前不是。",  "Only the OpenGL renderers use this; it is not the one running." },
		{ "仅 New 3D 引擎有效，当前不是。",      "Only the New 3D engine uses this; it is not the one running." },
		{ "仅 Legacy 3D 引擎有效，当前不是。",  "Only the Legacy 3D engine uses this; it is not the one running." },
		{ "当前渲染引擎是 OpenGL，背景图由 Vulkan 后端绘制，这里不起作用。",
		                                     "The running renderer is OpenGL; the backdrop is drawn by the Vulkan backend, so it has no effect here." },

		// ---- Game list --------------------------------------------------------------------------
		{ "双击游戏启动。直接运行本程序，或加上 -gui，都会打开这个窗口。",
		                                     "Double-click a game to start it. Running this program directly, or with -gui, opens this window." },
		{ "浏览",                            "Browse" },
		{ "搜索游戏",                        "Search games" },
		{ "请选择 ROM 目录。",               "Please choose a ROM directory." },
		{ "这个目录不存在。",                "That directory does not exist." },
		{ "目录里没有 .zip。",               "There are no .zip files in that directory." },
		{ "游戏",                            "Game" },
		{ "年份",                            "Year" },
		{ "找到 %d 个 ROM",                  "Found %d ROMs" },
		{ "  （未收录）",                    "  (unsupported)" },
		{ "Supermodel 的 Games.xml 里没有这个 ROM 集的定义，无法启动。\n同系列的其他版本可能已经在列表里了。",
		                                     "Games.xml has no definition for this ROM set, so it cannot be started.\nAnother version of the same game may already be listed." },

		// ---- Controls tab -----------------------------------------------------------------------
		{ "力反馈 / 灵敏度",                 "Force feedback / Sensitivity" },
		{ "校准摇杆",                        "Calibrate Sticks" },
		{ "这个输入系统没有初始化成功，换一个再打开这一页。",
		                                     "This input system failed to initialise; choose another one and reopen this page." },
		{ "分组",                            "Group" },
		{ "动作",                            "Action" },
		{ "双击一行来修改。选中左边的游戏后，这里只显示那款游戏用到的按键。",
		                                     "Double-click a row to change it. With a game selected on the left, only that game's bindings are listed." },
		{ "手柄编号：1=A/× 2=B/○ 3=X/□ 4=Y/△ 5=LB 6=RB 7=Back/Select 8=Start",
		                                     "Controller button numbers: 1=A/Cross 2=B/Circle 3=X/Square 4=Y/Triangle 5=LB 6=RB 7=Back/Select 8=Start" },
		{ "请按键。Esc 取消。",              "Press a key. Esc cancels." },
		{ "重设",                            "Rebind" },
		{ "追加",                            "Add" },
		{ "清空",                            "Clear" },
		{ "默认",                            "Default" },
		{ "完成",                            "Done" },

		// ---- Bottom bar and dialogs -------------------------------------------------------------
		{ "启动游戏",                        "Start Game" },
		{ "保存设置",                        "Save Settings" },
		{ "载入设置",                        "Load Settings" },
		{ "已保存",                          "Saved" },
		{ "恢复默认",                        "Restore Defaults" },
		{ "退出",                            "Quit" },
		{ "从文件载入设置？",                "Load settings from a file?" },
		{ "会覆盖当前设置。ROM 目录只在文件里的目录存在时才会替换。",
		                                     "This overwrites the current settings. The ROM directory is only replaced when the one in the file exists on this machine." },
		{ "选择文件",                        "Choose File" },
		{ "保存到哪个文件？",                "Where should the settings be saved?" },
		{ "默认位置：程序目录下的 Data\\Config\\Supermodel.ini",
		                                     "Default location: Data\\Config\\Supermodel.ini next to the program." },
		{ "保存到默认位置",                  "Save to Default" },
		{ "另存为...",                       "Save as..." },
		{ "设置另存为",                      "Save settings as" },
		{ "已保存到指定文件",                "Saved to the chosen file" },
		{ "已载入设置",                      "Settings loaded" },
		{ "设置已载入。",                    "Settings loaded." },
		{ "来源：",                          "From: " },
		{ "已应用 ",                         "Applied " },
		{ " 项设置。",                       " settings." },
		{ "ROM 目录：文件里没有这一项，保留当前目录：",
		                                     "ROM directory: not present in the file, keeping the current one: " },
		{ "ROM 目录：与当前相同（",          "ROM directory: same as the current one (" },
		{ "）。",                            ")." },
		{ "ROM 目录：已改用文件里的目录：",  "ROM directory: switched to the one in the file: " },
		{ "ROM 目录：文件里是 ",             "ROM directory: the file has " },
		{ "，本机不存在，保留当前目录：",     ", which does not exist here; keeping the current one: " },
		{ "这个文件里没有可用的设置。",      "That file has no usable settings in it." },
		{ "可能是文件无法打开，或者文件是空的。",
		                                     "It may not have been readable, or it may be empty." },
		{ "选择 Model 3 ROM 目录",           "Choose the Model 3 ROM directory" },
		{ "选择要载入的设置文件",            "Choose a settings file to load" },
		{ "Supermodel 设置 (*.ini)",         "Supermodel settings (*.ini)" },
		{ "所有文件 (*.*)",                  "All files (*.*)" },
		{ "先在左边选一个游戏。",            "Choose a game on the left first." },
		{ "确定",                            "OK" },
		{ "恢复默认设置？ROM 目录会保留。",  "Restore default settings? The ROM directory is kept." },
		{ "恢复",                            "Restore" },
		{ "退出前保存设置？",                "Save settings before quitting?" },
		{ "保存并退出",                      "Save and Quit" },
		{ "不保存",                          "Don't Save" },

		// ---- Trigger-axis pairing notice --------------------------------------------------------
		{ "    当前是 %s，需要 %s（现为 %s）", "    Currently %s, expected %s (is %s)" },
		{ "当前 %s 需要 %s。",               "Currently %s requires %s." },
		{ "刹车绑定的扳机形式与当前输入系统不符，刹车会没反应。",
		                                     "The brake is bound to a trigger axis that does not match the current input system, so the brake will not respond." },
		{ "修复扳机绑定##FixTrigger",        "Fix trigger binding##FixTrigger" },
		{ "或点「应用推荐键位」按当前系统重新配对。",
		                                     "Or press Apply Recommended Bindings to re-pair them for the current input system." },

		// ---- Core tab ---------------------------------------------------------------------------
		{ "0 = 按主板自动。Step 1.0 是 66 MHz，1.5 是 100 MHz，2.x 是 166 MHz。Daytona 2 是 2.1，保持 0 就是 166 MHz。",
		                                     "0 = automatic, from the board. Step 1.0 is 66 MHz, 1.5 is 100 MHz, 2.x is 166 MHz. Daytona 2 is 2.1, so leaving this at 0 gives 166 MHz." },

		// ---- Tooltip and help text ---------------------------------------------------------------
		// Long help strings, keyed on the exact text the code passes to Tr(). The keys below were
		// lifted out of Gui.cpp programmatically (Scripts/GenHelpTranslations.ps1) rather than retyped,
		// because a key that differs by one space simply falls through and shows Chinese.
				{ "开启后用一个键循环切换 4 个视角，不再占用 4 个键位。\n只影响有 4 视角的游戏（Daytona 2、Scud Race、Le Mans 24）。",
                                     "One key cycles through the four views instead of four separate keys.\nOnly affects games that have four views (Daytona 2, Scud Race, Le Mans 24)." },
				{ "显卡对多边形边缘做抗锯齿，贴图本身保持锐利。\n效果最好，有独显优先用它。",
                                     "Antialiases polygon edges on the GPU while leaving the textures sharp.\nThe best quality; prefer it on a discrete GPU." },
				{ "后处理抗锯齿，开销极低但整个画面会略软。\n适合集显；有独显优先用上面的 MSAA。",
                                     "A post-process antialiaser. Very cheap, but softens the whole picture slightly.\nGood for integrated graphics; with a discrete GPU prefer MSAA above." },
				{ "只按整数倍放大，放不满的部分留黑边。\n2D 层的文字会更均匀，代价是损失一些屏幕面积。\n倍率由面板的真实分辨率算出来，不是由上面的「分辨率」决定：\n384 x 倍率 必须不超过面板行数，所以 2160 行的 4K 屏只能到 5 倍（2480 x 1920）。\n勾选后「分辨率」会被覆盖，实际倍率见本页下面的读数。",
				                                 "Scales by whole numbers only, leaving black borders where it does not fill.\nMakes the 2D text even, at the cost of some screen area.\nThe multiple is derived from the panel's real resolution, not from the Resolution preset above:\n384 x multiple must not exceed the panel rows, so a 2160-row 4K panel reaches 5x (2480 x 1920) and no further.\nThis overrides Resolution; the active multiple is shown in the readout below." },
				{ "模拟街机 CRT 的扫描线和荧光粉点阵。\n4K 下最像真机；1080p 建议只开扫描线。",
                                     "Simulates the scanlines and phosphor mask of an arcade CRT.\nLooks most like the real thing at 4K; at 1080p use scanlines alone." },
				{ "扫描线和点阵都是周期图案，周期不成整数倍时会打拍子，\n出现彩色横带。这里列出的组合不会。",
                                     "Scanlines and mask are both periodic patterns; when their periods are not in a whole-number ratio they beat and produce colour bands. The presets listed here do not." },
				{ "画面高度上画多少条扫描线。0 = 按 Model 3 原始的 384 行。",
                                     "How many scanlines to draw across the picture height. 0 = the Model 3's native 384 lines." },
				{ "荧光粉的排列方式。世嘉街机用「槽孔式」，想还原原机选它。",
                                     "How the phosphor dots are arranged. Sega's cabinets used the slot mask; choose it to match the original." },
				{ "一个荧光粉点占几个像素。4K 用 2~3，1080p 用 1。",
                                     "How many pixels one phosphor dot covers. Use 2-3 at 4K, 1 at 1080p." },
				{ "红/绿/蓝荧光粉之间的明暗差异。调低更像普通显示器，调高颗粒感更强。",
                                     "How much the red, green and blue phosphors differ in brightness. Lower looks more like an ordinary monitor; higher is grainier." },
				{ "点阵和扫描线会挡光、让画面变暗，这里补回来。\n觉得太暗就往上调。",
                                     "The mask and scanlines block some of the light and darken the picture; this adds it back.\nRaise it if the picture looks too dark." },
				{ "模拟 CRT 玻璃面板的柔和光晕：亮部向外晕开，暗部不受影响。",
                                     "Simulates the soft halo that light scatters into across a CRT's glass face: bright areas bleed outwards, dark areas are unaffected." },
				{ "光晕铺开的范围。级数越多、光晕越大越淡。\n4K 用 5，1080p 用 4。",
                                     "How far the glow spreads. More levels means a wider, fainter glow.\nUse 5 at 4K, 4 at 1080p." },
		{ "亮度超过这个值的地方才会发光。这是控制辉光「干不干净」的关键。\n\n调低：草地、雪地、亮天空这类大面积的明亮区域也会一起发光，\n      整片画面会被整体抬亮，看起来发白发蒙。\n调高：只有真正的高光（车灯、金属反光、白线）才发光，画面更干净。\n\n草坪球场、雪地赛道这类游戏建议调到 60~70；\n夜晚或暗场景可以调低到 30 左右，让灯光晕得更明显。",
		                                     "Only areas brighter than this glow. This is what controls how clean the glow looks.\n\nLower: broad bright areas such as grass, snow and a bright sky glow too, lifting the whole picture\n       and giving it a washed-out, hazy look.\nHigher: only genuine highlights glow (headlights, metal glints, white lines) and the picture stays\n        clean.\n\nFor games with grass pitches or snow tracks, 60-70 is a good range. At night or in dark scenes\nlower it to around 30 so the lights bloom more visibly." },
				{ "不同输入系统下这个轴的名字不同，\n切换后原来的绑定会失效。\n当前 %s 需要 %s。",
                                     "The axis name differs between input systems, so switching invalidates the old binding.\n%s currently needs %s." },
		{ "普通的窗口化全屏，交给桌面合成器呈现。\n兼容性最好，但多了一层合成，画面节奏与显示器不同步，可能看到轻微抖动。",
		                                     "Ordinary windowed fullscreen, presented through the desktop compositor.\nBest compatibility, but one extra composition step means the frame cadence is not locked to the\ndisplay and slight judder can show." },
		{ "独占全屏 + G-SYNC：显示器以模拟器的帧率扫描，1:1 对应，消除抽帧抖动与撕裂。\n需要 G-SYNC/FreeSync 显示器，并且「垂直同步」要打开。不需要自定义分辨率。\n\n此时「画面」页的「刷新率」依然生效——它是模拟器呈现的节奏，显示器会跟着它走。\n如果你的显示器不支持 VRR，改用下面的固定街机刷新率。",
		                                     "Exclusive fullscreen plus G-SYNC: the display scans at the emulator's frame rate, 1:1, removing\nframe-pacing judder and tearing. Needs a G-SYNC/FreeSync display and V-Sync turned on. No custom\nresolution required.\n\nThe Refresh rate setting on the Video page still applies here - it is the cadence the emulator\npresents at, and the display follows it. If your display does not support VRR, use the fixed\narcade refresh rate below instead." },
		{ "让显示器以固定的 57.524Hz 扫描，与 Model 3 原始帧率 1:1 对应。\n需要先在显卡控制面板里创建一个 57.524Hz（或 57/58Hz）的自定义分辨率。\n没检测到该模式时这一项不可选，因为选了也只会回落到上面的 G-SYNC。\n\n这一项优先于「画面」页的「刷新率」——那里会显示成 57.5 Hz 并置灰。\n随发行版附带的 Scripts\\ArcadeDisplay.ps1 可以用 -Action List 查看当前是否已支持，\n-Action Run 会自动切换并在退出后恢复桌面。",
		                                     "Drives the display at a fixed 57.524Hz, matching the Model 3's original frame rate 1:1.\nYou must first create a custom 57.524 Hz (or 57/58 Hz) resolution in the GPU control panel.\nWith no such mode detected the option is unavailable, since choosing it would only fall back to G-SYNC above.\n\nThis takes priority over Refresh rate on the Video page, which is shown as 57.5 Hz and greyed out.\nThe Scripts\\ArcadeDisplay.ps1 shipped with the build can list whether one exists with -Action List;\n-Action Run switches automatically and restores the desktop on exit." },
		{ "分辨率变化后自动重新检测，也可以手动刷新一次。",
                                     "Re-detects automatically when the resolution changes, and can be refreshed by hand." },
		{ "进入游戏时默认使用的视角。\nDaytona 2 的默认第三人称跟车视角是 VR3（黄）。",
		                                     "The view the game starts in.\nThe default third-person chase view in Daytona 2 is VR3 (yellow)." },
	};

	/*
	 * Input group names and input labels, English -> Chinese.
	 *
	 * The reverse direction from kEnglish, and it exists for one reason: these strings originate in
	 * English. CInput::GetInputGroup() and every CInput label are English because the input system
	 * prints them to the console and into the log, where a translated name would be useless. The
	 * Controls tab is the one place they are read by a user, so the translation happens at display
	 * time rather than at the source.
	 *
	 * The group names are the switch in CInput::GetInputGroup(); the labels are the second argument
	 * of every AddSwitchInput() in Inputs.cpp.
	 */
	const std::pair<std::string_view, const char*> kInputChinese[] =
	{
		// ---- CInput::GetInputGroup() ------------------------------------------------------------
		{ "User Interface Controls",        "界面控制" },
		{ "Common Controls",                "通用控制" },
		{ "4-Way Joysticks",                "四方向摇杆" },
		{ "Fighting Game Buttons",          "格斗游戏按键" },
		{ "Spikeout Buttons",               "Spikeout 按键" },
		{ "Virtua Striker Buttons",         "VR 射手（足球）按键" },
		{ "Racing Game Steering Controls",  "赛车：转向" },
		{ "Racing Game Gear 4-Way Shift",   "赛车：四速换挡" },
		{ "Racing Game Gear Up/Down Shift", "赛车：上下换挡" },
		{ "Racing Game 4 VR View Buttons",  "赛车：四个视角按键" },
		{ "Racing Game View Change",        "赛车：切换视角" },
		{ "Racing Game Handbrake",          "赛车：手刹" },
		{ "Harley Davidson Controls",       "哈雷摩托控制" },
		{ "Virtual On Controls",            "电脑战机控制" },
		{ "Analog Joystick",                "模拟摇杆" },
		{ "Light Guns",                     "光枪" },
		{ "Analog Guns",                    "模拟光枪" },
		{ "Ski Controls",                   "滑雪控制" },
		{ "Magical Truck Controls",         "魔法卡车控制" },
		{ "Fishing Controls",               "钓鱼控制" },
		{ "Misc",                           "其他" },

		// ---- Interface --------------------------------------------------------------------------
		{ "Exit UI",                        "退出界面" },
		{ "Select",                         "选择" },
		{ "Pause",                          "暂停" },
		{ "Reset",                          "重置" },
		{ "Save State",                     "存档" },
		{ "Load State",                     "读档" },
		{ "Screenshot",                     "截图" },
		{ "Change Save Slot",               "切换存档位" },
		{ "Settings Overlay",               "设置浮层" },
		{ "Statistics Overlay",             "统计浮层" },
		{ "Toggle Fullscreen",              "切换全屏" },
		{ "Toggle Frame Limiting",          "切换帧率锁定" },
		{ "Dump Frame Timings",             "导出帧时间" },
		{ "Dump Input State",               "导出输入状态" },
		{ "Enter Debugger",                 "进入调试器" },
		{ "Clear NVRAM",                    "清空 NVRAM" },
		{ "Select Crosshairs",              "切换准星" },
		{ "Increase Sound Volume",          "调高音效" },
		{ "Decrease Sound Volume",          "调低音效" },
		{ "Increase Music Volume",          "调高音乐" },
		{ "Decrease Music Volume",          "调低音乐" },

		// ---- Common -----------------------------------------------------------------------------
		{ "P1 Start",                       "1P 开始" },
		{ "P2 Start",                       "2P 开始" },
		{ "P1 Coin",                        "1P 投币" },
		{ "P2 Coin",                        "2P 投币" },
		{ "Service A",                      "服务 A" },
		{ "Service B",                      "服务 B" },
		{ "Test A",                         "测试 A" },
		{ "Test B",                         "测试 B" },

		// ---- 4-way joystick ---------------------------------------------------------------------
		{ "P1 Joystick Up",                 "1P 摇杆上" },
		{ "P1 Joystick Down",               "1P 摇杆下" },
		{ "P1 Joystick Left",               "1P 摇杆左" },
		{ "P1 Joystick Right",              "1P 摇杆右" },
		{ "P2 Joystick Up",                 "2P 摇杆上" },
		{ "P2 Joystick Down",               "2P 摇杆下" },
		{ "P2 Joystick Left",               "2P 摇杆左" },
		{ "P2 Joystick Right",              "2P 摇杆右" },

		// ---- Fighting ---------------------------------------------------------------------------
		{ "P1 Punch",                       "1P 拳" },
		{ "P1 Kick",                        "1P 脚" },
		{ "P1 Guard",                       "1P 防御" },
		{ "P1 Escape",                      "1P 闪避" },
		{ "P2 Punch",                       "2P 拳" },
		{ "P2 Kick",                        "2P 脚" },
		{ "P2 Guard",                       "2P 防御" },
		{ "P2 Escape",                      "2P 闪避" },

		// ---- Spikeout ---------------------------------------------------------------------------
		{ "Shift",                          "闪身" },
		{ "Beat",                           "连击" },
		{ "Charge",                         "蓄力" },
		{ "Jump",                           "跳" },

		// ---- Virtua Striker ---------------------------------------------------------------------
		{ "P1 Short Pass",                  "1P 短传" },
		{ "P1 Long Pass",                   "1P 长传" },
		{ "P1 Shoot",                       "1P 射门" },
		{ "P2 Short Pass",                  "2P 短传" },
		{ "P2 Long Pass",                   "2P 长传" },
		{ "P2 Shoot",                       "2P 射门" },

		// ---- Racing -----------------------------------------------------------------------------
		{ "Steer Left",                     "左转" },
		{ "Steer Right",                    "右转" },
		{ "Full Steering",                  "转向（模拟轴）" },
		{ "Accelerator Pedal",              "油门" },
		{ "Brake Pedal/Front Brake",        "刹车 / 前刹" },
		{ "Gear Shift",                     "换挡（模拟轴）" },
		{ "Shift Up",                       "升档" },
		{ "Shift Down",                     "降档" },
		{ "Shift 1",                        "一档" },
		{ "Shift 2",                        "二档" },
		{ "Shift 3",                        "三档" },
		{ "Shift 4",                        "四档" },
		{ "Shift Neutral",                  "空档" },
		{ "VR1",                            "视角 1" },
		{ "VR2",                            "视角 2" },
		{ "VR3",                            "视角 3" },
		{ "VR4",                            "视角 4" },
		{ "View Change",                    "切换视角" },
		{ "Next View",                      "下一个视角" },
		{ "Hand Brake",                     "手刹" },
		{ "Rear Brake",                     "后刹" },
		{ "Music Selection",                "音乐选择" },

		// ---- Virtual On -------------------------------------------------------------------------
		{ "Left Joystick Up",               "左摇杆上" },
		{ "Left Joystick Down",             "左摇杆下" },
		{ "Left Joystick Left",             "左摇杆左" },
		{ "Left Joystick Right",            "左摇杆右" },
		{ "Right Joystick Up",              "右摇杆上" },
		{ "Right Joystick Down",            "右摇杆下" },
		{ "Right Joystick Left",            "右摇杆左" },
		{ "Right Joystick Right",           "右摇杆右" },
		{ "Macro Turn Left",                "宏：左转" },
		{ "Macro Turn Right",               "宏：右转" },
		{ "Macro Forward",                  "宏：前进" },
		{ "Macro Reverse",                  "宏：后退" },
		{ "Macro Strafe Left",              "宏：左平移" },
		{ "Macro Strafe Right",             "宏：右平移" },
		{ "Macro Jump",                     "宏：跳" },
		{ "Macro Crouch",                   "宏：蹲" },
		{ "Left Shot Trigger",              "左射击扳机" },
		{ "Right Shot Trigger",             "右射击扳机" },
		{ "Left Turbo",                     "左加速" },
		{ "Right Turbo",                    "右加速" },

		// ---- Analog joystick --------------------------------------------------------------------
		{ "Analog Up",                      "模拟上" },
		{ "Analog Down",                    "模拟下" },
		{ "Analog Left",                    "模拟左" },
		{ "Analog Right",                   "模拟右" },
		{ "Analog X-Axis",                  "模拟 X 轴" },
		{ "Analog Y-Axis",                  "模拟 Y 轴" },
		{ "Trigger Button 1",               "扳机 1" },
		{ "Trigger Button 2",               "扳机 2" },
		{ "Event Button 1",                 "事件按钮 1" },
		{ "Event Button 2",                 "事件按钮 2" },

		// ---- Light guns -------------------------------------------------------------------------
		{ "P1 Gun Up",                      "1P 光枪上" },
		{ "P1 Gun Down",                    "1P 光枪下" },
		{ "P1 Gun Left",                    "1P 光枪左" },
		{ "P1 Gun Right",                   "1P 光枪右" },
		{ "P1 Gun X-Axis",                  "1P 光枪 X 轴" },
		{ "P1 Gun Y-Axis",                  "1P 光枪 Y 轴" },
		{ "P2 Gun Up",                      "2P 光枪上" },
		{ "P2 Gun Down",                    "2P 光枪下" },
		{ "P2 Gun Left",                    "2P 光枪左" },
		{ "P2 Gun Right",                   "2P 光枪右" },
		{ "P2 Gun X-Axis",                  "2P 光枪 X 轴" },
		{ "P2 Gun Y-Axis",                  "2P 光枪 Y 轴" },
		{ "P1 Trigger",                     "1P 扳机" },
		{ "P2 Trigger",                     "2P 扳机" },
		{ "P1 Auto Trigger",                "1P 连发" },
		{ "P2 Auto Trigger",                "2P 连发" },
		{ "P1 Point Off-screen",            "1P 指向屏幕外" },
		{ "P2 Point Off-screen",            "2P 指向屏幕外" },

		// ---- Analog guns ------------------------------------------------------------------------
		{ "P1 Analog Gun Up",               "1P 模拟光枪上" },
		{ "P1 Analog Gun Down",             "1P 模拟光枪下" },
		{ "P1 Analog Gun Left",             "1P 模拟光枪左" },
		{ "P1 Analog Gun Right",            "1P 模拟光枪右" },
		{ "P1 Analog Gun X-Axis",           "1P 模拟光枪 X 轴" },
		{ "P1 Analog Gun Y-Axis",           "1P 模拟光枪 Y 轴" },
		{ "P1 Analog Gun Left Trigger",     "1P 模拟光枪左扳机" },
		{ "P1 Analog Gun Right Trigger",    "1P 模拟光枪右扳机" },
		{ "P2 Analog Gun Up",               "2P 模拟光枪上" },
		{ "P2 Analog Gun Down",             "2P 模拟光枪下" },
		{ "P2 Analog Gun Left",             "2P 模拟光枪左" },
		{ "P2 Analog Gun Right",            "2P 模拟光枪右" },
		{ "P2 Analog Gun X-Axis",           "2P 模拟光枪 X 轴" },
		{ "P2 Analog Gun Y-Axis",           "2P 模拟光枪 Y 轴" },
		{ "P2 Analog Gun Left Trigger",     "2P 模拟光枪左扳机" },
		{ "P2 Analog Gun Right Trigger",    "2P 模拟光枪右扳机" },

		// ---- Ski Champ --------------------------------------------------------------------------
		{ "Ski Champ Up",                   "滑雪上" },
		{ "Ski Champ Down",                 "滑雪下" },
		{ "Ski Champ Left",                 "滑雪左" },
		{ "Ski Champ Right",                "滑雪右" },
		{ "Ski Champ X-Axis",               "滑雪 X 轴" },
		{ "Ski Champ Y-Axis",               "滑雪 Y 轴" },
		{ "Ski Champ Poll Left",            "滑雪：左杖" },
		{ "Ski Champ Poll Right",           "滑雪：右杖" },
		{ "Ski Champ Select 1",             "滑雪：选项 1" },
		{ "Ski Champ Select 2",             "滑雪：选项 2" },
		{ "Ski Champ Select 3",             "滑雪：选项 3" },

		// ---- Magical Truck Adventure ------------------------------------------------------------
		{ "P1 Magical Lever Up",            "1P 拉杆上" },
		{ "P1 Magical Lever Down",          "1P 拉杆下" },
		{ "P1 Magical Lever",               "1P 拉杆（模拟轴）" },
		{ "P1 Magical Pedal",               "1P 踏板" },
		{ "P2 Magical Lever Up",            "2P 拉杆上" },
		{ "P2 Magical Lever Down",          "2P 拉杆下" },
		{ "P2 Magical Lever",               "2P 拉杆（模拟轴）" },
		{ "P2 Magical Pedal",               "2P 踏板" },

		// ---- Sega Bass Fishing ------------------------------------------------------------------
		{ "Rod Up",                         "鱼竿上" },
		{ "Rod Down",                       "鱼竿下" },
		{ "Rod Left",                       "鱼竿左" },
		{ "Rod Right",                      "鱼竿右" },
		{ "Rod X-Axis",                     "鱼竿 X 轴" },
		{ "Rod Y-Axis",                     "鱼竿 Y 轴" },
		{ "Stick Up",                       "摇竿上" },
		{ "Stick Down",                     "摇竿下" },
		{ "Stick Left",                     "摇竿左" },
		{ "Stick Right",                    "摇竿右" },
		{ "Stick X-Axis",                   "摇竿 X 轴" },
		{ "Stick Y-Axis",                   "摇竿 Y 轴" },
		{ "Cast",                           "抛竿" },
		{ "Select",                         "选择" },
		{ "Tension",                        "收线张力" },
		{ "Reel Speed",                     "收线速度" },
	};

	// One flat map is enough at this size; it is built once on first use.
	const std::unordered_map<std::string_view, const char*>& EnglishTable()
	{
		static const std::unordered_map<std::string_view, const char*> table(
			std::begin(kEnglish), std::end(kEnglish));
		return table;
	}

	const std::unordered_map<std::string_view, const char*>& InputChineseTable()
	{
		static const std::unordered_map<std::string_view, const char*> table(
			std::begin(kInputChinese), std::end(kInputChinese));
		return table;
	}

/*
 * Game titles, English -> Chinese.
 *
 * These are not official Sega translations: most of these games were never released in Chinese,
 * so the names here are the ones Chinese-speaking players and the Taiwan/Hong Kong arcade scene
 * actually use. The Virtua and Virtual-On names follow Sega's own Chinese branding (VR战士,
 * VR射手, 电脑战机); the rest are the common community names.
 *
 * Where a game shipped under different titles by region, the Chinese name is shared so the entries
 * read as one series with the original title in brackets - "Get Bass" is the Japanese title of what
 * the export market got as "Sega Bass Fishing", so both render as 世嘉钓鱼 rather than one of them
 * being half English.
 */
	const std::pair<std::string_view, const char*> kGameTitles[] =
	{
		{ "Cyber Troopers Virtual-On Oratorio Tangram", "电脑战机 Virtual-On 神谕探戈" },
		{ "Daytona USA 2 - Battle on the Edge",         "戴通纳 USA 2 - 边缘之战" },
		{ "Daytona USA 2 - Power Edition",              "戴通纳 USA 2 - 动力版" },
		{ "Dirt Devils",                                "泥地恶魔" },
		{ "Emergency Call Ambulance",                   "紧急救护车" },
		{ "Fighting Vipers 2",                          "格斗之蛇 2" },
		{ "Get Bass: Sega Bass Fishing",                "世嘉钓鱼 (Get Bass)" },
		{ "Harley-Davidson and L.A. Riders",            "哈雷戴维森与洛杉矶骑士" },
		{ "L.A. Machineguns: Rage of the Machines",     "洛杉矶机枪 机器之怒" },
		{ "Le Mans 24",                                 "勒芒 24 小时" },
		{ "Magical Truck Adventure",                    "魔法卡车大冒险" },
		{ "Scud Race",                                  "飞弹赛车" },
		{ "Scud Race Plus",                             "飞弹赛车 Plus" },
		{ "Sega Bass Fishing",                          "世嘉钓鱼" },
		{ "Sega Rally 2",                               "世嘉拉力赛 2" },
		{ "Ski Champ",                                  "滑雪冠军" },
		{ "Spikeout",                                   "街头格斗" },
		{ "Spikeout Final Edition",                     "街头格斗 最终版" },
		{ "Star Wars Trilogy Arcade",                   "星球大战三部曲 街机版" },
		{ "The Lost World",                             "失落的世界" },
		{ "The Ocean Hunter",                           "海洋猎人" },
		{ "Virtua Fighter 3",                           "VR 战士 3" },
		{ "Virtua Fighter 3 Team Battle",               "VR 战士 3 团队战" },
		{ "Virtua Striker 2",                           "VR 射手 2" },
		{ "Virtua Striker 2 '98",                       "VR 射手 2 '98" },
		{ "Virtua Striker 2 '99",                       "VR 射手 2 '99" },
		{ "Virtua Striker 2 '99.1",                     "VR 射手 2 '99.1" },
	};

	const std::unordered_map<std::string_view, const char*>& GameTitleTable()
	{
		static const std::unordered_map<std::string_view, const char*> table(
			std::begin(kGameTitles), std::end(kGameTitles));
		return table;
	}

	/*
	 * Version tokens. Only the region and revision words are translated; the step numbers and dates
	 * are left alone because they are version identifiers rather than words.
	 */
	const std::pair<std::string_view, const char*> kVersionTokens[] =
	{
		{ "Japan",                  "日本" },
		{ "Japan (test?)",          "日本（测试?）" },
		{ "USA",                    "美版" },
		{ "US location test?",      "美版场测?" },
		{ "Export",                 "出口版" },
		{ "Australia",              "澳洲" },
		{ "Deluxe",                 "豪华版" },
		{ "Standard",               "标准版" },
		{ "Upright",                "立式" },
		{ "Twin/DX",                "双机/豪华版" },
		{ "Revision A",             "A 版" },
		{ "Revision B",             "B 版" },
		{ "Revision C",             "C 版" },
		{ "Revision D",             "D 版" },
		{ "Prototype",              "原型版" },
		{ "Prototype Version A",    "原型版 A" },
		{ "Location Test",          "场测版" },
		{ "Ver 5.4g",               "5.4g 版" },
		{ "Ver. G?",                "G 版?" },
		{ "Step 1.5 Export",        "Step 1.5 出口版" },
		{ "Step 1.5 Japan",         "Step 1.5 日本" },
	};

	const std::unordered_map<std::string_view, const char*>& VersionTokenTable()
	{
		static const std::unordered_map<std::string_view, const char*> table(
			std::begin(kVersionTokens), std::end(kVersionTokens));
		return table;
	}
}

void SetUiLanguage(UiLanguage language)
{
	s_language = language;
}

UiLanguage GetUiLanguage()
{
	return s_language;
}

const char* UiLanguageName(UiLanguage language)
{
	// Each language is named in itself, which is what a language picker is expected to show.
	return language == UiLanguage::Chinese ? "中文" : "English";
}

const char* Tr(const char* chinese)
{
	if (chinese == nullptr || s_language == UiLanguage::Chinese)
		return chinese;

	const auto& table = EnglishTable();
	auto it = table.find(std::string_view(chinese));
	return it != table.end() ? it->second : chinese;
}

const char* TrInputText(const char* english)
{
	if (english == nullptr || s_language != UiLanguage::Chinese)
		return english;

	const auto& table = InputChineseTable();
	auto it = table.find(std::string_view(english));
	return it != table.end() ? it->second : english;
}

const char* TrGameTitle(const char* englishTitle)
{
	if (englishTitle == nullptr || s_language != UiLanguage::Chinese)
		return englishTitle;

	const auto& table = GameTitleTable();
	auto it = table.find(std::string_view(englishTitle));
	return it != table.end() ? it->second : englishTitle;
}

std::string TrGameVersion(const std::string& version)
{
	if (s_language != UiLanguage::Chinese || version.empty())
		return version;

	const auto& table = VersionTokenTable();
	std::string result;
	size_t start = 0;
	while (start <= version.size())
	{
		size_t comma = version.find(", ", start);
		std::string token = version.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

		auto it = table.find(std::string_view(token));
		if (!result.empty())
			result += ", ";
		result += it != table.end() ? it->second : token;

		if (comma == std::string::npos)
			break;
		start = comma + 2;
	}
	return result;
}
