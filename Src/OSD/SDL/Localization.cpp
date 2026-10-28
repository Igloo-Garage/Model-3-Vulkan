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
		{ "用游戏背景填满两侧",            "Fill the sides with the game background" },
		{ "把游戏底部的 2D 背景层横向拉满，填掉宽屏两侧的黑边。",
		                                     "Stretches the game's bottom 2D background across the window, filling the side bars in widescreen." },
		{ "关闭白闪",                        "Disable White Flash" },
		{ "读档和切场景时，街机硬件会把整屏刷白；开启后跳过这层白闪。\n只影响观感，不影响模拟结果。",
		                                     "The arcade hardware flashes the screen white during block culling, most visibly on loads and scene changes; this skips it.\nVisual only - the simulation is unaffected." },
		{ "锁定帧率",                        "Frame Limit" },
		{ "刷新率",                          "Refresh Rate" },
		{ "显示与运行",                      "Display & runtime" },
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
		{ "关闭（原始 1:1）",                "Off (raw 1:1)" },
		{ "轻度（保留转向手感）",            "Light (keeps the steering feel)" },
		{ "中度（推荐手柄）",                "Medium (recommended for pads)" },
		{ "强力（最平稳）",                  "Strong (smoothest)" },
		{ "锐利",                            "Sharp" },
		{ "双五次",                          "Biquintic" },
		{ "双线性",                          "Bilinear" },
		{ "双三次",                          "Bicubic" },
		{ "ARI/D93（日版）",                 "ARI/D93 (Japan)" },
		{ "BT.601 525/D65（美版）",          "BT.601 525/D65 (US)" },
		{ "BT.601 625/D65（欧版）",          "BT.601 625/D65 (EU)" },
		{ "57.524 Hz（真实机台）",           "57.524 Hz (arcade exact)" },
		{ "60 Hz（非 VRR / 60Hz 面板）",     "60 Hz (non-VRR / 60 Hz panel)" },
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
		{ "8x MSAA",                         "8x MSAA" },
		{ "宽屏",                            "Widescreen" },
		{ "滤镜和宽屏开关即时生效；抗锯齿等选项仍需要重新开一局。",
		                                     "Filters and the widescreen switch apply live; anti-aliasing and similar options still need a new game." },
		{ "只加宽水平视锥，不拉伸画面。\n部分游戏的 2D 背景不延伸，四角可能露黑。",
		                                     "Widens the horizontal frustum without stretching.\nSome games' 2D backgrounds do not extend, so the corners can go black." },
		{ "此开关即时生效，无需重新开局。",   "This switch applies immediately; no new game is needed." },
		{ "「宽屏背景」已开启：2D 背景仍会拉伸铺满窗口，所以取消宽屏后四周不是黑边。要黑边请关掉「宽屏背景」。",
		                                     "Widescreen Background is on: the 2D background is still stretched across the window, so turning Widescreen off leaves a filled border rather than black bars. Turn Widescreen Background off for black bars." },
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
		// ---- HD texture pack ----------------------------------------------------------------
		{ "高清纹理",                        "HD textures" },
		{ "加载高清纹理包##LoadHDTextures",  "Load HD texture pack##LoadHDTextures" },
		{ "导出当前游戏纹理##DumpHDTextures", "Dump this game's textures##DumpHDTextures" },
		{ "重新加载高清纹理##ReloadHDTextures", "Reload HD textures##ReloadHDTextures" },
		{ "读取 Data/Assets/HDTextures/<游戏>/ 里的替换贴图，按内容哈希匹配。\n支持 PNG、WebP 与 DDS（BC1/BC3/BC5/BC7，可压成 .dds.zst），\n分辨率可以是原图的 2x、4x、8x 等任意倍数，DDS 可自带 mip 链。\n找不到替换的贴图仍用原版。",
		 "Reads replacements from Data/Assets/HDTextures/<set>/, matched by content hash.\nPNG, WebP and DDS are supported (BC1/BC3/BC5/BC7, optionally .dds.zst)\nat any scale (2x, 4x, 8x ...); DDS may carry its own mip chain.\nTextures without a replacement use the original." },
		{ "把游戏实际使用的每块子贴图解码成 RGBA8 PNG，写到\nData/Assets/DumpTextures/<游戏>/，文件名带内容哈希。\n同一张内容只写一次；超分后放进 HDTextures 目录即可替换。",
		                                     "Decodes every sub-texture the game draws to an RGBA8 PNG under\nData/Assets/DumpTextures/<set>/, with its content hash in the name.\nEach distinct texture is written once; upscale it and drop it into HDTextures." },
		{ "（F4 切换替换/原版，F3 重扫；关闭面板后生效）",
										 "(F4 switches replacements on/off, F3 rescans; applied when the panel closes)" },
		{ "替换贴图：Data/Assets/HDTextures/<游戏>/（PNG / WebP / DDS）",
										 "Replacements: Data/Assets/HDTextures/<set>/ (PNG / WebP / DDS)" },
		{ "导出贴图：Data/Assets/DumpTextures/<游戏>/（PNG）",
										 "Dumps: Data/Assets/DumpTextures/<set>/ (PNG)" },
		{ "游戏中按 F4 在替换贴图与原版之间切换，F3 重新扫描。",
										 "Press F4 in a game to switch between replacements and the original artwork; F3 rescans." },

		// Section headings inside the combined force feedback / sensitivity page. These used to be
		// tab titles; they are kept as headings so the two groups stay distinguishable now that they
		// share a page.
		{ "力反馈",                          "Force feedback" },
		{ "灵敏度",                          "Sensitivity" },

		// ---- Section headings -------------------------------------------------------------------
		{ "抗锯齿",                          "Anti-aliasing" },
		{ "贴图过滤",                        "Texture filtering" },
		{ "贴图过滤##TexFilter",             "Texture filtering##TexFilter" },
		{ "半透明排序",                      "Transparency sorting" },
		{ "原版（双层，逐位兼容）",           "Original (two layers, bit-exact)" },
		{ "逐像素排序（OIT）",                "Per-pixel sorted (OIT)" },
		{ "街机真机（棋盘格，试验）",        "Arcade hardware (checkerboard, experimental)" },
		{ "决定重叠的半透明表面怎么合成。下一局生效。\nAlphaSortMode = 0：Original (two layers, bit-exact) —— 原版 OG 的 2-Subpass 快速混合。\nAlphaSortMode = 1：Per-pixel sorted (OIT) —— 4 槽位 A-Buffer，现代高画质 OIT。\nAlphaSortMode = 2：Arcade hardware (checkerboard) —— 试验模式，只为雪雾/尘土；无法还原 LOD 交叉淡化（瀑布、水幕会变成闪动的蓝块），这类场景用 0 或 1。",
		  "How overlapping translucent surfaces are combined. Next session.\nAlphaSortMode = 0: Original (two layers, bit-exact) - the original 2-subpass blend.\nAlphaSortMode = 1: Per-pixel sorted (OIT) - 4-slot A-buffer, the modern high-quality path.\nAlphaSortMode = 2: Arcade hardware (checkerboard) - experimental, for dense snow and dust only. It cannot reproduce LOD cross-fades, so waterfalls and water sheets turn into a flashing blue block; use 0 or 1 for those." },
		{ "硬件（按距离，原机行为）",         "Hardware (radial, as the real hardware)" },
		{ "锐利（屏幕空间导数）",             "Sharp (screen-space derivatives)" },
		{ "锐利 + 4x 各向异性",              "Sharp + 4x anisotropic" },
		{ "画面缩放",                        "Scaling" },
		{ "色彩与缩放",                      "Colour & Scaling" },
		{ "CRT 滤镜",                        "CRT Filter" },
		{ "玻璃辉光",                        "Glass Glow" },
		{ "光枪白边##SindenBorderSection",    "Light-gun Border##SindenBorderSection" },
		{ "Sinden 光枪白边",                 "Sinden light-gun border" },
		{ "白边宽度##SindenBorderWidth",     "Border width##SindenBorderWidth" },
		{ "白边亮度##SindenBorderBrightness", "Border brightness##SindenBorderBrightness" },
		{ "手柄",                            "Controllers" },
		{ "视角切换",                        "View Cycling" },
		{ "手柄转向",                        "Gamepad steering" },
		{ "摇杆转向平滑##GamepadSteeringSmoothing", "Stick steering smoothing##GamepadSteeringSmoothing" },

		// Steering smoothing help. Kept whole rather than split per line: a partial lookup would
		// fall back to Chinese mid-paragraph, the same reason the Sinden text above is one entry.
		{ "把手柄摇杆的阶跃输入压平，模拟方向盘阻尼。\n关闭：原始输入。\n轻度 / 中度 / 强力：约 35 / 50 / 75ms 平滑，依次更稳。\n只影响转向轴；真实方向盘建议关闭。",
		                                     "Smooths the step-like stick input to imitate a wheel's damping.\nOff: raw input.\nLight / Medium / Strong: about 35 / 50 / 75 ms of smoothing, progressively steadier.\nOnly affects the steering axis; leave it off with a real wheel." },

		// Sinden light-gun border help text. Kept whole rather than split per line: the translation
		// is the same sentence and a partial lookup would fall back to Chinese mid-paragraph.
		{ "在画面四周画白边，供 Sinden 光枪摄像头识别。\n画在最终输出上，不受 CRT/机柜背景影响；只对光枪游戏有意义。",
		  "Draws a white border the Sinden light gun's camera can see.\nIt is drawn on the final output, unaffected by CRT or bezel; only useful for light-gun games." },
		{ "边框像素宽度；1080p 建议 16-32，4K 建议 32-64。",
		  "Border width in output pixels; 16-32 at 1080p, 32-64 at 4K." },
		{ "边框亮度；100% 纯白，环境光强可调低，太低会难识别。",
		  "Border brightness; 100% is pure white. Lower it in a bright room, but too low and the camera loses the edge." },

		// ---- Device and resolution controls -----------------------------------------------------
		{ "显示设备##VulkanDevice",          "Display device##VulkanDevice" },
		{ "分辨率",                          "Resolution" },
		{ "自定义宽度##XResolution",         "Custom width##XResolution" },
		{ "自定义高度##YResolution",         "Custom height##YResolution" },
		{ "全屏##FullScreen",                "Fullscreen##FullScreen" },
		{ "拉伸##Stretch",                   "Stretch##Stretch" },
		{ "宽屏##WideScreen",                "Widescreen##WideScreen" },
		{ "垂直同步##VSync",                 "V-Sync##VSync" },
		{ "标题栏帧率##ShowFrameRate",       "FPS in title##ShowFrameRate" },
		{ "每秒把帧率和音频缓冲欠载/溢出计数写进标题栏；独占全屏时看不到。",
		                                     "Writes the frame rate and the audio buffer under/overflow counters to the title bar every second; invisible in exclusive fullscreen." },
		{ "独占全屏 · G-SYNC/VRR##ExclusiveFullscreen", "Exclusive fullscreen - G-SYNC/VRR##ExclusiveFullscreen" },
		{ "独占全屏 · 固定街机刷新率 57.524Hz##ExclusiveRefreshRate", "Exclusive fullscreen - arcade exact 57.524 Hz##ExclusiveRefreshRate" },
		{ "全屏刷新方式",                    "Fullscreen refresh mode" },

		// ---- Presentation cadence (fixed 57.524 Hz logic, interpolated present) ------------------
		{ "呈现节奏",                        "Presentation cadence" },
		{ "原始 57.5Hz（与机台一致）##PaceOriginal", "Original 57.5 Hz (arcade timing)##PaceOriginal" },
		{ "按真机的 57.524Hz 输出，一逻辑帧一画面，不插值。\n需要 57.5Hz 模式或 VRR 显示器，否则高刷屏会有规律顿挫。",
		                                     "Arcade-exact 57.524 Hz: one picture per logic step, no interpolation.\nNeeds a 57.5 Hz mode or a VRR display, or a high-refresh panel shows regular judder." },
		{ "跟随显示器（推荐）##PaceAuto",     "Follow the display (recommended)##PaceAuto" },
		{ "逻辑仍按 57.524Hz 推进，画面按显示器当前刷新率输出，\n亚帧用运动插值补平。60/75/90Hz 与高刷屏都适用。",
		                                     "Logic stays at 57.524 Hz; the picture is presented at the display's own rate,\nwith the sub-frame filled by motion interpolation. Works on 60/75/90 Hz and high-refresh panels." },
		{ "固定呈现频率##PaceFixed",            "Fixed present rate##PaceFixed" },
		{ "按下方指定频率输出，不切换显示器模式。\n60 Hz：最保守，任何屏都能跑。\n75 / 90 Hz：普通高刷或笔记本屏。\n120 / 144 / 165 Hz：主流电竞屏。\n240 / 480 Hz：需对应面板。\n低于实际刷新率只会重复帧；高了会被丢弃。",
		                                     "Presents at the rate below; it does not switch the monitor mode.\n60 Hz: the safe choice on any panel.\n75 / 90 Hz: everyday high-refresh and laptop panels.\n120 / 144 / 165 Hz: mainstream gaming monitors.\n240 / 480 Hz: requires a matching panel.\nBelow the panel rate this only repeats frames; above it, extra presents are dropped." },
		{ "呈现频率##FixedRefreshRate",     "Present rate##FixedRefreshRate" },
		{ "当前显示器最高支持 %d Hz，%d Hz 无法使用。",
		                                     "This display supports up to %d Hz, so %d Hz cannot be used." },
		{ "当前显示器最高支持 %d Hz；更高的档位已置灰。",
		                                     "This display supports up to %d Hz; higher entries are greyed out." },
		{ "60/75/90Hz 显示器用「跟随显示器」即可；高刷屏请先在 Windows 或显卡控制面板固定 120/144/240Hz。",
		                                     "On a 60/75/90 Hz display, Follow the display is all you need; on a high-refresh display, set 120/144/240 Hz in Windows or the GPU control panel first." },
		{ "G-SYNC：NVIDIA 控制面板 → 显示 → 设置 G-SYNC；「固定呈现频率」只改呈现目标，不切换模式。",
		                                     "G-SYNC: NVIDIA Control Panel > Display > Set up G-SYNC; Fixed present rate only changes the pacer target, not the monitor mode." },
		{ "运动插值##MotionInterpolation",    "Motion interpolation##MotionInterpolation" },
		{ "在两次逻辑帧之间对相机和动态物体插值，高刷屏每帧都是新画面。\n关闭＝低延迟优先，比插值少约 17ms 延迟，但画面会重复。",
		                                     "Interpolates the camera and moving instances between logic frames, so a high-refresh panel shows a new picture every present.\nOff means low latency first: about 17 ms fresher, but frames repeat." },
		{ "插值范围##InterpolationScope",     "Interpolation scope##InterpolationScope" },
		{ "自动",                            "Automatic" },
		{ "全部视口",                        "All viewports" },
		{ "仅主视口",                        "Main viewport only" },
		{ "自动：全屏与移动的小视口插值，固定的小视口离散更新。\n全部视口：所有 3D 视口都插值。\n仅主视口：只插值全屏视口。\n2D 图层（计时、字幕）永不插值。",
		                                     "Auto: the full-screen view and moving small viewports interpolate; fixed small viewports update discretely.\nAll: every 3D viewport interpolates.\nMain only: only the full-screen viewport interpolates.\n2D layers (timers, subtitles) never interpolate." },
		{ "显示器当前 %.0f Hz：每 1 次逻辑步呈现 %.2f 帧，亚帧权重 0.00-1.00。",
		                                     "Display is at %.0f Hz: %.2f presents per logic step, with a sub-frame weight of 0.00-1.00." },

		// ---- VRR multiples ----
		{ "VRR 倍频",                        "VRR multiple" },
		{ "向面板的整数倍频，每个仿真帧正好扫描 N 次。\n2x = 115.05Hz，需面板 120Hz 以上。\n3x = 172.57Hz，需 176Hz 以上。\n4x = 230.10Hz，需 235Hz 以上。\n8x = 460.19Hz，需 470Hz 以上。\n不够的档位置灰；都需 VRR 跟随 + 垂直同步。",
		                                     "Whole multiples of the arcade rate, N scans per emulated frame.\n2x = 115.05 Hz, needs a 120 Hz panel.\n3x = 172.57 Hz, needs 176 Hz.\n4x = 230.10 Hz, needs 235 Hz.\n8x = 460.19 Hz, needs 470 Hz.\nShort entries are greyed out; all need VRR and V-Sync." },
		{ "%.2f Hz：每个仿真帧 %d 次扫描。\n%s",
		                                     "%.2f Hz: %d scans per emulated frame.\n%s" },
		{ "本机面板支持。",                  "This display supports it." },
		{ "本机面板刷新率不够，此项不可选。", "This display does not scan fast enough for this option." },
		{ "面板 %.0f Hz：2x 可用（%.2f Hz，余量 %.1f%%），倍频下的节奏与面板无关。",
		                                     "Display is at %.0f Hz: 2x is available (%.2f Hz, %.1f%% headroom), and under a multiple the cadence no longer depends on the panel." },
		{ "面板 %.0f Hz：低于 120 Hz，无法使用整数倍频；可选择「跟随显示器」让插值接管。",
		                                     "Display is at %.0f Hz, below 120 Hz, so no multiple is available; choose Follow the display and let interpolation handle the cadence." },
		{ "输出显示器##DisplayNumber",       "Output display##DisplayNumber" },
		{ "主显示器",                        "Primary" },
		{ "副显示器",                        "Secondary" },
		{ "自动 · 跟随系统主显示器",         "Automatic - follow the system's main display" },
		{ "57/58Hz 可用",                    "57/58 Hz available" },
		{ "游戏窗口与独占全屏都出现在这台显示器上，下一局生效。\n「自动」跟随系统主显示器；末尾 #n 是 Windows 的显示器编号。",
		                                     "The game window and both exclusive fullscreen modes open on this display; next session.\nAuto follows the system primary; the #n suffix is Windows' display number." },
		{ "无边框全屏（默认）##SyncOff",     "Borderless fullscreen (default)##SyncOff" },
		{ "与设置界面共用窗口，来回切换最顺，帧率可能略低。\n驱动已为窗口启用 G-SYNC 时，本模式也能让面板跟随。",
		                                     "Shares the window with the settings UI, so switching is seamless; frame rate can be slightly lower.\nIf the driver has G-SYNC enabled for windowed mode, the panel can follow here too." },
		{ "独占全屏 · G-SYNC/VRR##SyncGSync", "Exclusive fullscreen - G-SYNC/VRR##SyncGSync" },
		{ "独占全屏 · 固定街机刷新率 57.524Hz##SyncFixedRate", "Exclusive fullscreen - fixed arcade 57.524 Hz##SyncFixedRate" },
		{ "（由「独占全屏 · 固定街机刷新率」决定）", "  (set by Exclusive fullscreen - fixed arcade rate)" },
		{ "（G-SYNC 下为呈现节奏，显示器跟随）", "  (under G-SYNC this is the present rate the display follows)" },
		{ "（G-SYNC 自动使用街机 57.5Hz）",   "  (G-SYNC paces at the arcade 57.5 Hz rate)" },
		{ "已按街机 57.5Hz 自动呈现（显示器时序范围包含 57.5Hz）。", "Pacing at the arcade 57.5 Hz rate automatically (the display's timing range includes it)." },
		{ "未确认显示器时序范围包含 57.5Hz，按「画面」页的刷新率呈现。", "The display's timing range does not confirm 57.5 Hz, so Refresh rate on the Video page is used." },
		{ "面板会跟随呈现节奏（%.2f Hz）扫描；节奏本身在「画面」页设置。",
		                                     "The panel follows the present rate (%.2f Hz); the rate itself is set on the Video tab." },
		{ "已检测到显示器的 57/58 Hz 模式，可以 1:1 输出。", "A 57/58 Hz display mode was detected; 1:1 output is available." },
				{ "多亮才开始发光；调高更干净，调低整片会发蒙。",
                                     "How bright a pixel must be to glow; higher is cleaner, lower washes out the whole picture." },
		{ "未检测到显示器的 57/58 Hz 模式，此项不可选。",
		                                     "No 57/58 Hz display mode was found, so this option is unavailable." },
		{ "已声明 VRR",
		                                     "(declares VRR" },
		{ "→ 57.5Hz 在时序范围内，可尝试创建自定义分辨率",
		                                     "-> 57.5 Hz is inside the timing range; a custom resolution is worth trying" },
		{ "→ 57.5Hz 不在时序范围内，创建自定义分辨率无用",
		                                     "-> 57.5 Hz is outside the timing range; creating a custom resolution would be wasted effort" },
		{ "把显示器锁到真机 57.5Hz，与帧率 1:1。\n少数显示器/驱动不接受该时序，黑屏就换回上一项。",
		                                     "Locks the display to the arcade 57.5 Hz, one scan per frame.\nA few displays or drivers reject the timing; if the screen goes black, go back to the option above." },
		{ "把显示器锁到真机 57.5Hz，与帧率 1:1。\n不可用＝没有 57/58Hz 固定模式；支持 VRR 的话用上一项即可。\n没有 VRR 时需先在驱动里创建 57.524Hz 自定义分辨率。",
		                                     "Locks the display to the arcade 57.5 Hz, one scan per frame.\nUnavailable means no fixed 57/58 Hz mode exists; with VRR, the option above does the same job.\nWithout VRR, create a 57.524 Hz custom resolution in the driver first." },
		{ "无法读取显示器 EDID，故不能判断 57.5Hz 是否在其时序范围内。",
		                                     "The display EDID could not be read, so whether 57.5 Hz is inside its timing range is unknown." },
		{ "独占全屏，显示器跟随模拟器，需要 VRR（EDID 范围含 57.5Hz）。\n若读不到 EDID 或不包含，仍按「画面」页的刷新率呈现。",
		                                     "Exclusive fullscreen; the display follows the emulator. Needs VRR (EDID range covers 57.5 Hz).\nWithout a usable EDID range the rate from the Video tab is used." },
		{ "独占全屏，显示器跟随呈现节奏，需要 VRR。\n面板的 VRR 范围必须包含当前呈现速率（见「画面」页）。",
		                                     "Exclusive fullscreen; the display follows the present cadence. Needs VRR.\nThe panel's VRR range must contain the current present rate (see the Video tab)." },
		{ "第 1 项：无边框，桌面合成器定节奏，最省事。\n第 2 项：显示器跟随模拟器，需要 VRR。\n第 3 项：把显示器锁到 57.5Hz，需已有 57/58Hz 模式。\n怎么选：有 VRR 用第 2 项，否则第 1 项。",
		                                     "Option 1: borderless; the desktop compositor sets the cadence. Simplest.\nOption 2: the display follows the emulator. Needs VRR.\nOption 3: locks the display to 57.5 Hz; needs an existing 57/58 Hz mode.\nWhich one: use option 2 with VRR, otherwise option 1." },
		{ "第 1 项：无边框，桌面合成器定节奏，最省事。\n第 2 项：显示器跟随模拟器，需要 VRR。\n第 3 项：把显示器锁到 57.5Hz，需先建自定义分辨率。\n怎么选：有 VRR 用第 2 项，否则第 1 项。",
		                                     "Option 1: borderless; the desktop compositor sets the cadence. Simplest.\nOption 2: the display follows the emulator. Needs VRR.\nOption 3: locks the display to 57.5 Hz; create a custom resolution first.\nWhich one: use option 2 with VRR, otherwise option 1." },
		{ "显示控制台窗口##ShowConsole",     "Show console window##ShowConsole" },
		{ "Vulkan 校验层##VkValidation",     "Vulkan validation layers##VkValidation" },
		{ "让 Vulkan 校验层逐帧检查绘制调用、描述符和同步，发现的问题写进 Supermodel.log。正常游玩请关闭（会明显变慢）；排查花屏、闪烁或崩溃时临时打开。",
		                                     "Checks every frame's draw calls, descriptors and synchronisation with the Vulkan validation layers and writes the findings to Supermodel.log. Leave it off for normal play (it is much slower); turn it on temporarily when investigating corruption, flicker or crashes." },

		// ---- Display device helper text ---------------------------------------------------------
		{ "自动（默认高性能显卡）",          "Auto (default high-performance GPU)" },
		{ "没有检测到可用的 Vulkan 显卡。",  "No usable Vulkan device was detected." },
		{ "自动（系统默认）",                "Auto (system default)" },
		{ "没有检测到可用的音频输出设备。",  "No usable audio output device was detected." },
		{ " [系统默认]",                     " [system default]" },
		{ "OpenGL 无法在程序内选择显卡（OpenGL 没有设备枚举接口）。\n本程序只能“请求”使用独立显卡，驱动可以不理会。\n\n要改用另一块显卡：\nWindows 设置 → 系统 → 显示 → 显卡（或“高级显示器设置”里的图形设置），\n为 Supermodel-LegacyOG.exe 指定“高性能”，然后重新启动本程序。\n\n注意：该设置是否对 OpenGL 生效由驱动决定，且本程序无法检测它是否被采纳，\n以上一行 GL_RENDERER 为准。",
		                                     "OpenGL offers no way to choose a GPU from within the program (it has no device enumeration interface).\nThis program can only *ask* for the discrete GPU, and the driver is free to ignore that.\n\nTo use a different GPU:\nWindows Settings > System > Display > Graphics, set Supermodel-LegacyOG.exe to \"High performance\",\nthen restart this program.\n\nNote: whether that setting affects OpenGL is up to the driver, and this program cannot detect whether it\nwas honoured - the GL_RENDERER line above is the authority." },
		{ "实际渲染卡：未知（未能读取 GL_RENDERER）",
		                                     "Rendering GPU: unknown (GL_RENDERER could not be read)" },
		{ "实际渲染卡：",                    "Rendering GPU: " },
		{ "要改用另一块显卡：Windows 设置 → 系统 → 显示 → 显卡（或“高级显示器设置”里的图形设置），\n为 Supermodel-LegacyOG.exe 指定“高性能”，然后重新启动本程序。\n注意：该设置是否对 OpenGL 生效由驱动决定，且本程序无法检测它是否被采纳，以上一行 GL_RENDERER 为准。",
		                                     "To use a different GPU: Windows Settings > System > Display > Graphics,\nset Supermodel-LegacyOG.exe to \"High performance\", then restart this program.\nNote: whether that setting affects OpenGL is up to the driver, and this program cannot detect whether it was\nhonoured - the GL_RENDERER line above is the authority." },

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
		{ "XInput：Xbox 手柄，左右扳机分开。",
		                                     "XInput: Xbox pads, with separate left/right triggers." },
		{ "DirectInput / Raw Input：旧手柄或特殊设备。",
		                                     "DirectInput / Raw Input: older pads and special devices." },
		{ "SDL：通用读取；SDL 手柄：按标准手柄映射，推荐。",
		                                     "SDL: generic reading; SDL Gamepad: standard pad mapping, recommended." },
		{ "按键在「按键」页改；切换系统后旧绑定会失效。",
		     "Bindings live on the Controls tab; changing the system invalidates the old ones." },
{ "SDL 手柄：按标准手柄映射，推荐；Xbox 手柄请选它（Windows 上经 XInput 读取，左右扳机分开）。",
		     "SDL Gamepad: standard mapping, recommended; choose it for Xbox pads (read through XInput on Windows, separate left/right triggers)." },
{ "SDL：通用读取，适合键盘、鼠标或非标准设备。",
		     "SDL: generic reading for keyboards, mice and non-standard devices." },
{ "Xbox 手柄选「SDL 手柄」；本版本不提供 XInput / DirectInput。",
		     "Xbox pads: choose SDL Gamepad. This build has no XInput/DirectInput backend." },
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
		{ "硬件预设",                        "Hardware presets" },
		{ "档位##QualityTier",                "Tier##QualityTier" },
		{ "超低配极速（496×384，核显）",      "Ultra low / fastest (496x384, integrated)" },
		{ "核显 / 低配（1080p，1x）",          "Integrated / low-end (1080p, 1x)" },
		{ "入门独显（1080p，2x）",             "Entry discrete (1080p, 2x)" },
		{ "中端独显（2K，2x）",                "Mid-range discrete (2K, 2x)" },
		{ "高端独显（4K，4x）",                "High-end discrete (4K, 4x)" },
		{ "自定义（按下面几项单独调）",        "Custom (set the options below individually)" },
		{ "一次配好分辨率、MSAA、半透明排序、贴图过滤等相关选项，下一局生效。\n超低配极速：496×384 原生 3D + 等比放大，关插值/后处理。\n核显 / 低配：1080p、1x、原版半透明与硬件贴图，最稳。\n入门独显：1080p + 2x MSAA。\n中端独显：2K + 2x MSAA + 逐像素排序 + 锐利贴图。\n高端独显：4K + 4x MSAA，同上最高画质。\n自定义：不套预设，只改下面各项。",
		                                     "Sets resolution, MSAA, transparency sorting, texture filtering and related options at once; next session.\nUltra low: native 496x384 3D + aspect-preserving upscale; no interpolation or post-processing.\nIntegrated / low-end: 1080p, 1x, original transparency and hardware filtering - the safest choice.\nEntry discrete: 1080p + 2x MSAA.\nMid-range: 2K + 2x MSAA + per-pixel sorting + sharp filtering.\nHigh-end: 4K + 4x MSAA, the same best-quality settings.\nCustom: apply nothing; set the options below individually." },
		{ "超低配的低开销靠「低分辨率渲染」（或「整数倍缩放」）；两者都关掉时全屏会按面板分辨率渲染，帧率会明显下降。",
		                                     "The ultra-low tier's low cost comes from Low-resolution rendering (or Integer scaling); with both off, fullscreen renders at the panel resolution and the frame rate drops noticeably." },
		{ "硬件 MSAA##MSAA",                 "Hardware MSAA##MSAA" },
		{ "四边形精确插值##QuadInterpolation", "Exact quad interpolation##QuadInterpolation" },
		{ "关闭后改用显卡三角形插值，跳过每像素 4 次顶点读取和二次求解。\n核显档位用；非平面四边形会有 0-2 对角线接缝。下一局生效。",
		                                     "Off uses the rasteriser's triangle interpolation, skipping four vertex reads and a quadratic solve per pixel.\nFor integrated GPUs; non-planar quads show a 0-2 diagonal seam. Next session." },
		{ "FXAA 后处理抗锯齿##PostFXAA",     "FXAA post-process anti-aliasing##PostFXAA" },
		{ "整数倍缩放（点对点）##IntegerScale", "Integer scaling (pixel-exact)##IntegerScale" },
		{ "关闭（按画面分辨率渲染）",        "Off (render at the picture's resolution)" },
		{ "496×384（原生，等比放大）",       "496x384 (native, aspect-preserving upscale)" },
		{ "992×768（2x 原生，等比放大）",     "992x768 (2x native, aspect-preserving upscale)" },
		{ "3D 场景按固定小分辨率渲染，再等比放大到画面矩形；2D HUD 仍按原分辨率，保持清晰。\n关闭：按画面分辨率渲染，最清晰也最慢。\n496×384：街机原生分辨率，与画面矩形同比例；1080p 全屏仍是 1395×1080，左右黑边保留。\n992×768：2 倍原生分辨率，比 496×384 清晰，填充开销仍远低于画面分辨率。\n开启后「整数倍缩放」与「宽屏」不生效。下一局生效。",
		                                     "Renders the 3D scene at a fixed small resolution and upscales it onto the picture rectangle; the 2D HUD keeps its own resolution and stays sharp.\nOff: render at the picture's resolution - sharpest, and slowest.\n496x384: the arcade's native resolution, the same aspect as the picture rectangle; at 1080p fullscreen the picture is still 1395x1080 with its side bars kept.\n992x768: twice native, sharper than 496x384, and still far cheaper to fill than the picture resolution.\nWhile on, Integer scaling and Widescreen have no effect. Next session." },
		{ "「低分辨率渲染」已开启，整数倍缩放被忽略。", "Low-resolution rendering is on; integer scaling is ignored." },
		{ "「低分辨率渲染」已开启，本局宽屏不生效。", "Low-resolution rendering is on; widescreen does not apply to this session." },
		{ "CRT 滤镜##PostCRT",               "CRT filter##PostCRT" },
		{ "CRT 滤镜##PostCRTSection",        "CRT Filter##PostCRTSection" },
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

		// ---- Statistics readout ------------------------------------------------------------------
		// Only the settings-window side needs translating. The readout's own labels are deliberately
		// left in English: CPU, GPU, VRAM and FPS are the same words in every language this window
		// speaks, and keeping them out of the table also keeps their format specifiers out of reach of
		// a translation that could reorder or drop one.
		{ "统计面板##ShowStatsOsd",           "Stats panel##ShowStatsOsd" },
		{ "琥珀经典",                        "Amber" },
		{ "冷灰暖黄",                        "Slate" },
		{ "高对比",                          "Contrast" },
		{ "极简描边",                        "Outline" },
		{ "在画面左上角显示引擎、刷新率、CPU/GPU 频率与占用、内存、帧率和 1% Low。\n游戏里按 ` 键开关；Vulkan 与 OpenGL 都支持。",
		                                     "Shows engine, refresh rate, CPU/GPU clocks and load, memory, frame rate and 1% low in the top-left corner.\nPress the ` key in game to toggle it; works in both renderers." },
		{ "统计面板的配色预设（先勾选「统计面板」）。\n琥珀经典：天青标签 + 琥珀数值，帧率按 57/50 fps 变色。\n冷灰暖黄：标签低调，数值暖黄。\n高对比：深底板 + 白标签，适合亮场景。\n极简描边：无底板，文字带黑描边。",
		                                     "Colour presets for the stats panel (enable the stats panel first).\nAmber: cyan labels with amber values, recoloured by the 57/50 fps thresholds.\nSlate: quieter labels with warm yellow values.\nContrast: dark backing and white labels, for bright scenes.\nOutline: no backing, black 1 px outline on the text." },

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
		{ "背景图只画在黑边上，不会盖住游戏画面。\n更改背景图或亮度需要重新开一局；在游戏内取消「宽屏」会立即显示。",
		                                     "The backdrop is drawn only on the black bars, never over the picture.\nChanging the image or brightness needs a new game; turning Widescreen off in the in-game panel shows it immediately." },

		// ---- In-game save-state menu ------------------------------------------------
		{ "即时存档##OverlaySaveStates",     "Save states##OverlaySaveStates" },
		{ "槽位",                            "Slot" },
		{ "状态",                            "Status" },
		{ "操作",                            "Actions" },
		{ "空",                              "Empty" },
		{ "保存",                            "Save" },
		{ "读取",                            "Load" },

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
		{ "管理存档",                        "Manage saves" },
		{ "管理存档...",                     "Manage saves..." },
		{ "存档目录：%s",                    "Save folder: %s" },
		{ "刷新",                            "Refresh" },
		{ "导入旧版 .st",                    "Import legacy .st" },
		{ "关闭窗口",                        "Close" },
		{ "无缩略图",                        "No thumbnail" },
		{ "改名",                            "Rename" },
		{ "删除",                            "Delete" },
		{ "确认删除",                        "Confirm delete" },
		{ "删除失败",                        "Delete failed" },
		{ "这个游戏还没有 .zst 存档。游戏内保存后，这里会显示缩略图卡片。",
		                                     "No .zst saves yet. Saves made in-game appear here as thumbnail cards." },
		{ "这个 set 还有 %d 个旧版 .st 存档（含 LegacyOG 的 Saves_Legacy；这里只列 .zst）；点「导入旧版 .st」即可转换。",
		                                     "This set still has %d legacy .st save(s), including Supermodel-LegacyOG.exe's Saves_Legacy folder. This window lists .zst only; use \"Import legacy .st\" to convert them." },
		{ "这个 set 没有 .zst 存档，但存档目录里还有其他 set 的存档：",
		                                     "This set has no .zst saves, but the save folder contains saves for other sets:" },
		{ "  %s（%d 个）",                 "  %s (%d)" },
		{ "如果你玩的是另一个区域版本，请关闭本窗口，在游戏列表里右键那个版本再打开「管理存档」。",
		                                     "If you played a different regional release, close this window and open Manage Saves from that set's row in the game list." },
		{ "……",                           "..." },
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
		{ "把鼠标移到游戏上，或单击选中，这里会显示封面和英文简介。",
		                                     "Hover a game or click to select it; its cover art and a short description appear here." },
		{ "无封面",                          "No cover art" },
		{ "没能从这个包里认出 Model 3 游戏（可能缺文件）。",
		                                     "No Model 3 game could be identified in this archive - files may be missing." },
		{ "这个游戏还没有简介。",            "No description is available for this game yet." },
		{ "  （未收录）",                    "  (unsupported)" },
		{ "没能从这个包里认出 Model 3 游戏（可能缺文件）。\n同系列的其他版本可能已经在列表里了。",
		                                     "No Model 3 game could be identified in this archive - files may be missing.\nAnother version of the same game may already be listed." },

		// ---- Controls tab -----------------------------------------------------------------------
		{ "力反馈 / 灵敏度",                 "FFB" },
		{ "先在「常用」页勾选「力反馈」，这一页的设置才会生效。",
		                                     "Tick \"Force feedback\" on the Common tab first; nothing on this page takes effect until it is on." },
		{ "校准摇杆",                        "Calibrate Sticks" },
		{ "摇杆/方向盘轴校准是命令行功能：用 -config-inputs 启动后按 B。手柄的轴由 SDL 归一化，通常不需要校准。",
		                                     "Stick and wheel axis calibration is a command-line feature: start with -config-inputs and press B. Gamepad axes are normalised by SDL and usually need none." },
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
		{ "当前是 %s，需要 %s（现为 %s）\n或点「应用推荐键位」按当前系统重新配对。",
		                                     "Currently %s, expected %s (is %s).\nOr press Apply Recommended Bindings to re-pair them for the current input system." },
		{ "刹车绑定的扳机形式与当前输入系统不符，刹车会没反应。",
		                                     "The brake is bound to a trigger axis that does not match the current input system, so the brake will not respond." },
		{ "修复扳机绑定##FixTrigger",        "Fix trigger binding##FixTrigger" },

		// ---- Core tab ---------------------------------------------------------------------------
		{ "0 = 按主板自动。Step 1.0 是 66 MHz，1.5 是 100 MHz，2.x 是 166 MHz。Daytona 2 是 2.1，保持 0 就是 166 MHz。",
		                                     "0 = automatic, from the board. Step 1.0 is 66 MHz, 1.5 is 100 MHz, 2.x is 166 MHz. Daytona 2 is 2.1, so leaving this at 0 gives 166 MHz." },

		// ---- Tooltip and help text ---------------------------------------------------------------
		// Long help strings, keyed on the exact text the code passes to Tr(). The keys below were
		// lifted out of Gui.cpp programmatically (Scripts/GenHelpTranslations.ps1) rather than retyped,
		// because a key that differs by one space simply falls through and shows Chinese.
				{ "开启后一个键循环切换 4 个视角；只影响有 4 视角的游戏（Daytona 2、Scud Race、Le Mans 24）。",
                                     "Cycles the four views with one key; only affects games with four views (Daytona 2, Scud Race, Le Mans 24)." },
				{ "显卡对多边形边缘做抗锯齿，贴图保持锐利。\n关闭：最快。\n2x / 4x：画质与开销的常用折中。\n8x：更平滑，4K 下开销大，且需驱动支持。",
                                     "Anti-aliases polygon edges while textures stay sharp.\nOff: fastest.\n2x / 4x: the usual quality/cost trade.\n8x: smoother, expensive at 4K and needs driver support." },
				{ "远处贴图用哪一级 mip。\n硬件：按原机径向公式估算，最忠实。\n锐利：按像素覆盖面积计算，倾斜路面/墙壁更清晰。\n锐利 + 4x 各向异性：再沿拉伸方向采 4 次样。\n后两项不是原机行为，可能轻微闪烁。",
                                     "Which mip level distant textures use.\nHardware: the arcade's radial formula, the most faithful.\nSharp: the pixel footprint, so slanted roads and walls stay clearer.\nSharp + 4x anisotropic: four extra taps along the stretched axis.\nThe last two are not arcade behaviour and may shimmer slightly." },
				{ "开销极低的后处理抗锯齿，画面会略软；有独显优先用 MSAA。",
                                     "Very cheap post-process anti-aliasing that softens the whole picture; prefer MSAA on a discrete GPU." },
				{ "只按整数倍放大，余量留黑边；文字更均匀，但损失屏幕面积。\n倍率按面板真实分辨率算（496×384 的整数倍），勾选后会覆盖「分辨率」。\n关闭后全屏按面板分辨率渲染（1080p 上约 1395×1080），核显帧率会明显下降。",
				                                 "Magnifies by whole numbers only, leaving black bars; text looks more even at the cost of screen area.\nThe factor comes from the panel's real resolution (multiples of 496x384) and overrides Resolution.\nOff renders fullscreen at the panel resolution (about 1395x1080 at 1080p), so integrated GPUs lose a lot of frame rate." },
				{ "模拟街机 CRT 的扫描线和荧光粉点阵。\n关闭：不处理。\n扫描线：只画横线。\n扫描线 + 荫罩：再加荧光粉点阵，最像真机；4K 效果最好。",
		                                     "Simulates an arcade CRT's scanlines and phosphor mask.\nOff: no processing.\nScanlines: horizontal lines only.\nScanlines + mask: adds the phosphor mask, closest to the arcade; best at 4K." },
		{ "把当时街机 CRT 的色温换算到 sRGB，只改颜色。\n关闭：不转换，保持原始颜色。\nARI/D93：日版机台的白点（偏冷）。\nPVM：专业监视器的白点。\nBT.601 525/D93、525/D65：美版 525 行制式（冷 / 标准白）。\nBT.601 625/D65：欧版 625 行制式。\n不确定就保持关闭；Vulkan 即时生效，OpenGL 下一局生效。",
		                                     "Converts the arcade CRT's white point to sRGB; colour only.\nOff: no conversion, original colours.\nARI/D93: Japanese cabinets, cooler white.\nPVM: professional monitor white.\nBT.601 525/D93, 525/D65: US 525-line standards (cool / standard white).\nBT.601 625/D65: European 625-line standard.\nKeep it off if unsure; instant in Vulkan, next session in OpenGL." },
		{ "2D 层（HUD、文字）放大用的滤镜。\n锐利：最近邻，最硬但有锯齿。\n双五次 / 双三次：平滑且保留细节。\n双线性：最平滑、最软。\nVulkan 即时生效；OpenGL 下一局生效。",
		                                     "Filter used to scale the 2D layers (HUD, text).\nSharp: nearest-neighbour, hardest edges but aliased.\nBicubic / quintic: smooth while keeping detail.\nBilinear: smoothest and softest.\nInstant in Vulkan, next session in OpenGL." },
		{ "OpenGL：这两项需要重新开一局生效。",
		                                     "OpenGL: these two take effect when a new game starts." },
				{ "扫描线与点阵的周期不成整数倍会打拍子（彩色横带），这些组合不会。\n只改扫描线/荫罩和亮度补偿，不改上面的 CRT 色彩。",
                                     "Scanlines and mask beat against each other unless their periods line up, producing coloured bands; these combinations do not.\nThey change scanlines/mask and gain only, not CRT colour." },
				{ "画面高度上画多少条扫描线。0 = 按 Model 3 原始的 384 行。",
                                     "How many scanlines to draw across the picture height. 0 = the Model 3's native 384 lines." },
				{ "荧光粉的排列方式。\n栅条式：特丽珑类显示器。\n槽孔式：世嘉街机，想还原原机选它。\n点阵式：普通彩显。",
                                     "How the phosphors are arranged.\nAperture grille: Trinitron-style monitors.\nSlot mask: Sega arcade monitors; pick this for the original look.\nDot mask: ordinary colour displays." },
				{ "一个荧光粉点占几个像素。4K 用 2~3，1080p 用 1。",
                                     "How many pixels one phosphor dot covers. Use 2-3 at 4K, 1 at 1080p." },
				{ "红/绿/蓝荧光粉之间的明暗差异。调低更像普通显示器，调高颗粒感更强。",
                                     "How much the red, green and blue phosphors differ in brightness. Lower looks more like an ordinary monitor; higher is grainier." },
				{ "点阵和扫描线会让画面变暗，这里补回来；太暗就往上调。",
                                     "The mask and scanlines darken the picture; this compensates. Raise it if the result looks dim." },
				{ "模拟 CRT 玻璃面板的柔和光晕：亮部向外晕开，暗部不受影响。",
                                     "Simulates the soft halo that light scatters into across a CRT's glass face: bright areas bleed outwards, dark areas are unaffected." },
				{ "光晕铺开的范围；级数越多越大越淡。4K 用 5，1080p 用 4。",
                                     "How far the glow spreads: more levels mean a larger, softer halo. Use 5 at 4K, 4 at 1080p." },
		{ "亮度超过这个值的地方才会发光。这是控制辉光「干不干净」的关键。\n\n调低：草地、雪地、亮天空这类大面积的明亮区域也会一起发光，\n      整片画面会被整体抬亮，看起来发白发蒙。\n调高：只有真正的高光（车灯、金属反光、白线）才发光，画面更干净。\n\n草坪球场、雪地赛道这类游戏建议调到 60~70；\n夜晚或暗场景可以调低到 30 左右，让灯光晕得更明显。",
		                                     "Only areas brighter than this glow. This is what controls how clean the glow looks.\n\nLower: broad bright areas such as grass, snow and a bright sky glow too, lifting the whole picture\n       and giving it a washed-out, hazy look.\nHigher: only genuine highlights glow (headlights, metal glints, white lines) and the picture stays\n        clean.\n\nFor games with grass pitches or snow tracks, 60-70 is a good range. At night or in dark scenes\nlower it to around 30 so the lights bloom more visibly." },
			{ "普通的窗口化全屏，交给桌面合成器呈现。\n兼容性最好，但多了一层合成，画面节奏与显示器不同步，可能看到轻微抖动。",
		                                     "Ordinary windowed fullscreen, presented through the desktop compositor.\nBest compatibility, but one extra composition step means the frame cadence is not locked to the\ndisplay and slight judder can show." },
		{ "独占全屏 + G-SYNC：显示器以模拟器的帧率扫描，1:1 对应，消除抽帧抖动与撕裂。\n需要 G-SYNC/FreeSync 显示器，并且「垂直同步」要打开。不需要自定义分辨率。\n\n此时「画面」页的「刷新率」依然生效——它是模拟器呈现的节奏，显示器会跟着它走。\n如果你的显示器不支持 VRR，改用下面的固定街机刷新率。",
		                                     "Exclusive fullscreen plus G-SYNC: the display scans at the emulator's frame rate, 1:1, removing\nframe-pacing judder and tearing. Needs a G-SYNC/FreeSync display and V-Sync turned on. No custom\nresolution required.\n\nThe Refresh rate setting on the Video page still applies here - it is the cadence the emulator\npresents at, and the display follows it. If your display does not support VRR, use the fixed\narcade refresh rate below instead." },
		{ "让显示器以固定的 57.524Hz 扫描，与 Model 3 原始帧率 1:1 对应。\n需要先在显卡控制面板里创建一个 57.524Hz（或 57/58Hz）的自定义分辨率。\n没检测到该模式时这一项不可选，因为选了也只会回落到上面的 G-SYNC。\n\n这一项优先于「画面」页的「刷新率」——那里会显示成 57.5 Hz 并置灰。\n随发行版附带的 Scripts\\ArcadeDisplay.ps1 可以用 -Action List 查看当前是否已支持，\n-Action Run 会自动切换并在退出后恢复桌面。",
		                                     "Drives the display at a fixed 57.524Hz, matching the Model 3's original frame rate 1:1.\nYou must first create a custom 57.524 Hz (or 57/58 Hz) resolution in the GPU control panel.\nWith no such mode detected the option is unavailable, since choosing it would only fall back to G-SYNC above.\n\nThis takes priority over Refresh rate on the Video page, which is shown as 57.5 Hz and greyed out.\nThe Scripts\\ArcadeDisplay.ps1 shipped with the build can list whether one exists with -Action List;\n-Action Run switches automatically and restores the desktop on exit." },
		{ "分辨率变化后自动重新检测，也可以手动刷新一次。",
                                     "Re-detects automatically when the resolution changes, and can be refreshed by hand." },
		{ "进入游戏时的默认视角；Daytona 2 的第三人称跟车视角是 VR3（黄）。",
		                                     "The view the game starts in; Daytona 2's third-person chase view is VR3 (yellow)." },
		{ "OpenGL 无法在程序内选择显卡，只能「请求」独显。\n要换卡：Windows 设置 → 系统 → 显示 → 图形，为该程序指定「高性能」。",
		                                     "OpenGL cannot enumerate adapters, so the program can only request the discrete GPU.\nTo change it: Windows Settings > System > Display > Graphics, set this program to High performance." },

		// ---- Interface appearance (UITheme / UiFrostedGlass) ------------------------------------
		{ "界面外观",                        "Appearance" },
		{ "界面主题##UITheme",               "Theme##UITheme" },
		{ "浅色（暖色护眼）",                "Light (eye-care)" },
		{ "深色",                            "Dark" },
		{ "原始界面（改动前）",              "Original interface" },
		{ "浅色：温润羊皮纸色底 + 陶土琥珀强调色，默认。\n"
		  "深色：暖岩灰玻璃底 + 琥珀强调色，晚上更不刺眼。\n"
		  "原始界面：毛玻璃重做之前的平面暖纸样式（蓝色强调色、不透底、无背景图）。\n"
		  "前两个主题都不使用纯黑与纯白；切换立即生效。",
		                                     "Light: warm alabaster panes with a terracotta accent, the default.\nDark: warm rock-grey glass with an amber accent, easier at night.\nOriginal: the flat warm-paper look from before the frosted-glass rework (blue accent, opaque, no backdrop).\nThe first two themes use neither pure black nor pure white. Switching applies immediately." },
		{ "毛玻璃背景##UiFrostedGlass",       "Frosted glass##UiFrostedGlass" },
		{ "在 Ko-fi 上支持开发",              "Support development on Ko-fi" },
		{ "面板背景改用模糊后的画面（双重 Kawase 模糊），带 1px 玻璃高光边。\n"
		  "关闭后改用不透明底色，适合核显或驱动不支持的情形。\n"
		  "原始界面本来就不透明，此开关对它无效。",
		                                     "Draws panel backgrounds from a blurred copy of the picture (dual Kawase blur) with a 1px glass rim.\nTurn it off for an opaque fill, which is the first thing to try on a weak GPU.\nThe original theme is opaque by design, so the switch does nothing there." },

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
