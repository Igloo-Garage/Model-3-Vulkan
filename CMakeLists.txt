# =============================================================================
#  Model 3-Vulkan
#
#  Stage 2: dual-backend CMake build.
#
#  Two executables, physically separate source sets:
#
#    Model3-Vulkan           SDL3 + Vulkan 1.3 only (no GL objects at all)
#    Supermodel-LegacyOG      SDL2 + OpenGL only (no Vulkan objects at all)
#
#  The MSVC/Supermodel.vcxproj build produces the LegacyOG executable as well.
#
#  Design rules:
#    * supermodel_core is backend-free: it does not link SDL2 or SDL3.
#    * Each core archive carries exactly one renderer family.  The Vulkan
#      renderer, volk and the Vulkan headers exist only on the Pure-Vulkan
#      target's include path; the OpenGL target cannot even include
#      <vulkan/vulkan.h>.
#    * Every OSD/platform source is compiled once per executable, with exactly
#      one of SUPERMODEL_PLATFORM_SDL2 / SUPERMODEL_PLATFORM_SDL3 defined, and
#      at most one of SUPERMODEL_OPENGL_ONLY / SUPERMODEL_PURE_VULKAN.
#    * The two executables are separate processes with separate static copies
#      of the core.  SDL2 and SDL3 must never share a link line: their exported
#      symbol names collide and their ABIs do not match.
# =============================================================================

cmake_minimum_required(VERSION 3.24)

project(Model3-Vulkan LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------

option(USE_SDL2 "Build the SDL2 compatibility executable" ON)
option(USE_SDL3 "Build the SDL3 low-latency executable" OFF)
option(USE_SDL3_NET "Link SDL3_net into the SDL3 executable" OFF)
option(SUPERMODEL_ENABLE_DEBUGGER "Build the console debugger" OFF)
option(SUPERMODEL_LTO "Enable IPO/LTO in Release" OFF)

#
# Model3-Vulkan is the Pure-Vulkan build: no OpenGL sources, no GLEW and
# no GL/GLU on its link line.  The OSD units that used to be OpenGL-only now
# contain compile-time Vulkan branches (SUPERMODEL_PURE_VULKAN):
#
#   * Src/OSD/SDL/Gui.cpp       - settings window (CVulkanContext + ImGui Vulkan)
#   * Src/OSD/SDL/Main.cpp      - renderer selection and Vulkan screen setup
#   * Src/OSD/SDL/Overlay.cpp   - Vulkan half only
#   * Src/OSD/SDL/Crosshair.cpp - not compiled; Vulkan2D draws the crosshair
#
# Turning the option OFF is the escape hatch that builds the earlier dual-renderer
# SDL3 variant while keeping the GL sources available.
#
option(SUPERMODEL_SDL3_PURE_VULKAN
    "Build Model3-Vulkan without any OpenGL sources, headers or libraries" ON)

if(NOT USE_SDL2 AND NOT USE_SDL3)
    message(FATAL_ERROR "Enable at least one of USE_SDL2 or USE_SDL3")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The existing MSVC project uses the static CRT (/MT).  Keep both executables
# and the vendored libraries on the same runtime or the link fails with a
# mismatched RuntimeLibrary.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(SUPERMODEL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH
    "Directory for the generated executables")

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# Only the targets that still compile an OpenGL renderer need GL.  A
# Pure-Vulkan SDL3 build must not even discover the package, so that a missing
# GL SDK cannot influence its configuration.
if(USE_SDL2 OR NOT SUPERMODEL_SDL3_PURE_VULKAN)
    find_package(OpenGL REQUIRED)
endif()
find_package(Threads REQUIRED)

# zlib is vendored.  Do not use its CMakeLists: it renames the checked-in
# zconf.h when configured out of source, which would break the MSVC project.
# The source list matches MSVC/ZLib/ZLib.vcxproj.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_library(supermodel_zlib STATIC
    Libraries/zlib-1.3.1/adler32.c
    Libraries/zlib-1.3.1/compress.c
    Libraries/zlib-1.3.1/crc32.c
    Libraries/zlib-1.3.1/deflate.c
    Libraries/zlib-1.3.1/gzclose.c
    Libraries/zlib-1.3.1/gzlib.c
    Libraries/zlib-1.3.1/gzread.c
    Libraries/zlib-1.3.1/gzwrite.c
    Libraries/zlib-1.3.1/infback.c
    Libraries/zlib-1.3.1/inffast.c
    Libraries/zlib-1.3.1/inflate.c
    Libraries/zlib-1.3.1/inftrees.c
    Libraries/zlib-1.3.1/trees.c
    Libraries/zlib-1.3.1/uncompr.c
    Libraries/zlib-1.3.1/zutil.c)

target_include_directories(supermodel_zlib
    PUBLIC Libraries/zlib-1.3.1)

target_compile_definitions(supermodel_zlib PRIVATE
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE)

if(USE_SDL2)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
    set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)

    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/SDL2-2.30.11"
        "${CMAKE_BINARY_DIR}/third_party/SDL2"
        EXCLUDE_FROM_ALL)

    set(SDL2NET_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL2NET_SAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/SDL2_net-2.4.0"
        "${CMAKE_BINARY_DIR}/third_party/SDL2_net"
        EXCLUDE_FROM_ALL)
endif()

if(USE_SDL3)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/SDL3/CMakeLists.txt")
        message(FATAL_ERROR
            "USE_SDL3=ON but Libraries/SDL3 does not exist. "
            "Vendor SDL3 before enabling this target.")
    endif()

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)
    #
    # Vulkan video support must be compiled into the SDL3 the Vulkan executable
    # links.  Stated explicitly and forced because SDL2 and SDL3 share one CMake
    # cache when both targets are enabled: a value left there by another
    # configuration would otherwise disable Vulkan in this build, and the failure
    # only shows up at run time as "Vulkan support is either not configured in SDL".
    #
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)

    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/SDL3"
        "${CMAKE_BINARY_DIR}/third_party/SDL3"
        EXCLUDE_FROM_ALL)
endif()

# -----------------------------------------------------------------------------
# Source lists
#
# These mirror MSVC/Supermodel.vcxproj.  Files the MSVC project marks
# ExcludedFromBuild (m68kdasm.c, m68k_in.c, ppc_ops.c, ppc603.c, Make68K.c,
# SCSPLFO.cpp) are deliberately absent.
# -----------------------------------------------------------------------------

set(SUPERMODEL_CORE_SOURCES
    Src/BlockFile.cpp
    Src/GameLoader.cpp
    Src/ROMSet.cpp

    Src/CPU/68K/68K.cpp
    Src/CPU/68K/Musashi/m68kcpu.c
    Src/CPU/68K/Musashi/m68kopac.c
    Src/CPU/68K/Musashi/m68kopdm.c
    Src/CPU/68K/Musashi/m68kopnz.c
    Src/CPU/68K/Musashi/m68kops.c
    Src/CPU/PowerPC/ppc.cpp
    Src/CPU/PowerPC/PPCDisasm.cpp
    Src/CPU/Z80/Z80.cpp

    # CPU-only New3D units shared by the OpenGL and Vulkan renderers.  They
    # live in the New3D folder but include no GL header, so both cores need
    # them and the OpenGL-only list below must not claim them.
    Src/Graphics/New3D/Mat4.cpp
    Src/Graphics/New3D/Model.cpp
    Src/Graphics/New3D/PolyHeader.cpp
    Src/Graphics/New3D/R3DFloat.cpp
    Src/Graphics/New3D/Vec.cpp

    Src/Inputs/Input.cpp
    Src/Inputs/Inputs.cpp
    Src/Inputs/InputSource.cpp
    Src/Inputs/InputSystem.cpp
    Src/Inputs/InputTypes.cpp
    Src/Inputs/MultiInputSource.cpp

    Src/Model3/53C810.cpp
    Src/Model3/53C810Disasm.cpp
    Src/Model3/93C46.cpp
    Src/Model3/Crypto.cpp
    Src/Model3/DSB.cpp
    Src/Model3/IRQ.cpp
    Src/Model3/JTAG.cpp
    Src/Model3/Model3.cpp
    Src/Model3/MPC10x.cpp
    Src/Model3/PCI.cpp
    Src/Model3/Real3D.cpp
    Src/Model3/RTC72421.cpp
    Src/Model3/SoundBoard.cpp
    Src/Model3/TileGen.cpp
    Src/Model3/DriveBoard/BillBoard.cpp
    Src/Model3/DriveBoard/DriveBoard.cpp
    Src/Model3/DriveBoard/JoystickBoard.cpp
    Src/Model3/DriveBoard/SkiBoard.cpp
    Src/Model3/DriveBoard/WheelBoard.cpp
    Src/Model3/DriveBoard/Z80CTC.cpp

    Src/Sound/SCSP.cpp
    Src/Sound/SCSPDSP.cpp
    Src/Sound/MPEG/MpegAudio.cpp

    Src/Util/ByteSwap.cpp
    Src/Util/ConfigBuilders.cpp
    Src/Util/Format.cpp
    Src/Util/NewConfig.cpp

    Src/OSD/Logger.cpp
    Src/OSD/Outputs.cpp

    Src/Pkgs/ioapi.c
    Src/Pkgs/tinyxml2.cpp
    Src/Pkgs/unzip.c

    Src/Pkgs/imgui/imgui.cpp
    Src/Pkgs/imgui/imgui_draw.cpp
    Src/Pkgs/imgui/imgui_tables.cpp
    Src/Pkgs/imgui/imgui_widgets.cpp
)

#
# Vulkan renderer, the volk loader and the ImGui Vulkan backend.  Compiled into
# the Pure-Vulkan core only: an OpenGL executable must not contain these
# objects, must not compile against <vulkan/vulkan.h> and must not link a
# Vulkan loader.  (Vulkan is loaded through volk at run time, so there is no
# vulkan-1.lib on any link line in either configuration.)
#
set(SUPERMODEL_CORE_VULKAN_SOURCES
    Src/Graphics/Vulkan3D/HDTextures.cpp
    Src/Graphics/Vulkan3D/MotionInterpolation.cpp
    Src/Graphics/Vulkan3D/R3DSceneBuilder.cpp
    Src/Graphics/Vulkan3D/UiBlur.cpp
    Src/Graphics/Vulkan3D/Vulkan2D.cpp
    Src/Graphics/Vulkan3D/Vulkan3D.cpp
    Src/Graphics/Vulkan3D/VulkanMemory.cpp
    Src/Graphics/Vulkan3D/VulkanRingBuffer.cpp
    Src/Graphics/Vulkan3D/VulkanTextureCache.cpp
    Src/Pkgs/volk_impl.cpp
    Src/Pkgs/imgui/imgui_impl_vulkan.cpp
)

#
# Everything that needs an OpenGL context (and therefore <GL/glew.h>): the two
# upstream renderers, the 2D/FBO/shader plumbing they share, the GLEW loader and
# the ImGui OpenGL3 backend.  A Pure-Vulkan executable never compiles these and
# never links OpenGL::GL / OpenGL::GLU.
#
set(SUPERMODEL_CORE_GL_SOURCES
    Src/Graphics/FBO.cpp
    Src/Graphics/Shader.cpp
    Src/Graphics/SuperAA.cpp
    Src/Graphics/Render2D.cpp
    Src/Graphics/Legacy3D/Error.cpp
    Src/Graphics/Legacy3D/Legacy3D.cpp
    Src/Graphics/Legacy3D/Models.cpp
    Src/Graphics/Legacy3D/TextureRefs.cpp
    Src/Graphics/New3D/GLSLShader.cpp
    Src/Graphics/New3D/New3D.cpp
    Src/Graphics/New3D/R3DFrameBuffers.cpp
    Src/Graphics/New3D/R3DScrollFog.cpp
    Src/Graphics/New3D/R3DShader.cpp
    Src/Graphics/New3D/TextureBank.cpp
    Src/Graphics/New3D/VBO.cpp
    Src/Pkgs/glew.c
    Src/Pkgs/imgui/imgui_impl_opengl3.cpp
)

# supermodel_add_core() below makes the difference between the cores explicit:
# one call site decides which renderer sources and libraries an archive gets.

set(SUPERMODEL_OSD_SOURCES
    # VulkanContext.cpp talks to PlatformVulkan.h, so it is compiled once per
    # backend rather than into the backend-free core.
    Src/Graphics/Vulkan3D/VulkanContext.cpp
    Src/OSD/Platform/ImguiPlatform.cpp
    Src/OSD/SDL/Audio.cpp
    Src/OSD/SDL/Crosshair.cpp
    Src/OSD/SDL/FontProbe.cpp
    Src/OSD/SDL/Gui.cpp
    Src/OSD/SDL/Localization.cpp
    Src/OSD/SDL/Main.cpp
    Src/OSD/SDL/NetOutputs.cpp
    Src/OSD/SDL/Overlay.cpp
    Src/OSD/SDL/SDLInputSystem.cpp
    Src/OSD/SDL/StatsOsd.cpp
    Src/OSD/SDL/Thread.cpp
    Src/OSD/DisplayPacer.cpp

    Src/Network/NetBoard.cpp
    Src/Network/SimNetBoard.cpp
)

set(SUPERMODEL_NET_SOURCES
    Src/Network/NativeSocket.cpp
    Src/Network/TCPReceive.cpp
    Src/Network/TCPSend.cpp
)

if(SUPERMODEL_ENABLE_DEBUGGER)
    list(APPEND SUPERMODEL_CORE_SOURCES
        Src/Debugger/AddressTable.cpp
        Src/Debugger/Breakpoint.cpp
        Src/Debugger/CodeAnalyser.cpp
        Src/Debugger/ConsoleDebugger.cpp
        Src/Debugger/CPUDebug.cpp
        Src/Debugger/CPU/68KDebug.cpp
        Src/Debugger/CPU/Musashi68KDebug.cpp
        Src/Debugger/CPU/PPCDebug.cpp
        Src/Debugger/CPU/Turbo68KDebug.cpp
        Src/Debugger/CPU/Z80Debug.cpp
        Src/Debugger/Debugger.cpp
        Src/Debugger/DebuggerIO.cpp
        Src/Debugger/Exception.cpp
        Src/Debugger/Interrupt.cpp
        Src/Debugger/Label.cpp
        Src/Debugger/Register.cpp
        Src/Debugger/SupermodelDebugger.cpp
        Src/Debugger/Watch.cpp
    )
endif()

set(SUPERMODEL_INCLUDE_DIRS
    Src
    Src/OSD
    Src/OSD/SDL
    Src/OSD/Windows
    Src/Pkgs
    Libraries/zlib-1.3.1
)

# libwebp decoder subset, vendored under Src/Pkgs/webp.
#
# Windows decodes cover art through WIC, which only knows WebP when the "Webp Image Extensions"
# package is installed - and a machine without it silently shows no covers. The bundled decoder is
# the fallback for exactly that case, so it is compiled into the Windows targets and nowhere else:
# Linux links the system libwebp and macOS uses ImageIO (see DecodeBoxart() in Gui.cpp).
set(SUPERMODEL_WEBP_SOURCES)
if(WIN32)
    list(APPEND SUPERMODEL_WEBP_SOURCES
        Src/Pkgs/webp/src/dec/alpha_dec.c
        Src/Pkgs/webp/src/dec/buffer_dec.c
        Src/Pkgs/webp/src/dec/frame_dec.c
        Src/Pkgs/webp/src/dec/idec_dec.c
        Src/Pkgs/webp/src/dec/io_dec.c
        Src/Pkgs/webp/src/dec/quant_dec.c
        Src/Pkgs/webp/src/dec/tree_dec.c
        Src/Pkgs/webp/src/dec/vp8_dec.c
        Src/Pkgs/webp/src/dec/vp8l_dec.c
        Src/Pkgs/webp/src/dec/webp_dec.c
        Src/Pkgs/webp/src/dsp/alpha_processing.c
        Src/Pkgs/webp/src/dsp/cpu.c
        Src/Pkgs/webp/src/dsp/dec.c
        Src/Pkgs/webp/src/dsp/dec_clip_tables.c
        Src/Pkgs/webp/src/dsp/filters.c
        Src/Pkgs/webp/src/dsp/lossless.c
        Src/Pkgs/webp/src/dsp/rescaler.c
        Src/Pkgs/webp/src/dsp/upsampling.c
        Src/Pkgs/webp/src/dsp/yuv.c
        # The generic dsp files declare VP8DspInitSSE2() and friends unconditionally; the
        # definitions live in these files and are selected through WEBP_USE_SSE2/SSE41.
        Src/Pkgs/webp/src/dsp/alpha_processing_sse2.c
        Src/Pkgs/webp/src/dsp/alpha_processing_sse41.c
        Src/Pkgs/webp/src/dsp/dec_sse2.c
        Src/Pkgs/webp/src/dsp/dec_sse41.c
        Src/Pkgs/webp/src/dsp/filters_sse2.c
        Src/Pkgs/webp/src/dsp/lossless_sse2.c
        Src/Pkgs/webp/src/dsp/lossless_sse41.c
        Src/Pkgs/webp/src/dsp/rescaler_sse2.c
        Src/Pkgs/webp/src/dsp/upsampling_sse2.c
        Src/Pkgs/webp/src/dsp/upsampling_sse41.c
        Src/Pkgs/webp/src/dsp/yuv_sse2.c
        Src/Pkgs/webp/src/dsp/yuv_sse41.c
        Src/Pkgs/webp/src/utils/bit_reader_utils.c
        Src/Pkgs/webp/src/utils/color_cache_utils.c
        Src/Pkgs/webp/src/utils/filters_utils.c
        Src/Pkgs/webp/src/utils/huffman_utils.c
        Src/Pkgs/webp/src/utils/palette.c
        Src/Pkgs/webp/src/utils/quant_levels_dec_utils.c
        Src/Pkgs/webp/src/utils/rescaler_utils.c
        Src/Pkgs/webp/src/utils/random_utils.c
        Src/Pkgs/webp/src/utils/thread_utils.c
        Src/Pkgs/webp/src/utils/utils.c
    )
    # "src/..." is how libwebp's own sources spell their includes, so the libwebp root has to be on
    # the include path; the second entry exposes the public header as <webp/decode.h>.
    list(APPEND SUPERMODEL_INCLUDE_DIRS Src/Pkgs/webp Src/Pkgs/webp/src)
endif()

# Libraries/Vulkan-Headers/include is attached to the Vulkan cores only (see
# supermodel_add_core).  Keeping it off the shared include list means an OpenGL
# translation unit cannot reach <vulkan/vulkan.h> even by accident.

# Model3.cpp and the rest of the core are backend-free: nothing in it includes an SDL header.
# The SDL include paths are attached to each executable instead (see
# supermodel_add_platform_target), so an SDL2 header can never be picked up by the SDL3 target.

# Definitions that both the core and the OSD layer need.  INLINE has to be a
# definition because Musashi's m68kconf.h defaults to the GCC spelling.
add_library(supermodel_common INTERFACE)
target_compile_definitions(supermodel_common INTERFACE
    GLEW_STATIC
    "INLINE=static __inline"
    _MBCS
    _CRT_SECURE_NO_WARNINGS)

# Chinese literals in these files need /utf-8 with MSVC.
if(MSVC)
    set_source_files_properties(
        Src/OSD/SDL/Gui.cpp
        Src/OSD/SDL/Localization.cpp
        Src/OSD/SDL/Main.cpp
        Src/OSD/SDL/Overlay.cpp
        PROPERTIES COMPILE_OPTIONS "/utf-8")

    # The generated Musashi opcode files compute in 64-bit and truncate to the 68K's 32-bit
    # registers on purpose, so C4244 is silenced for them alone. See the note in
    # MSVC/Supermodel.vcxproj; the two builds have to agree or one of them starts printing hundreds
    # of warnings again.
    set_source_files_properties(
        Src/CPU/68K/Musashi/m68kopac.c
        Src/CPU/68K/Musashi/m68kopdm.c
        Src/CPU/68K/Musashi/m68kopnz.c
        Src/CPU/68K/Musashi/m68kops.c
        PROPERTIES COMPILE_OPTIONS "/wd4244")
endif()

if(WIN32)
    # The MSVC project embeds SupermodelManifest.xml through the .rc file and
    # disables the compiler-generated manifest.  CMake's Ninja/VS link rules
    # always embed a manifest of their own, so build a CMake-specific .rc with
    # only the icon and feed the manifest through /MANIFESTINPUT.  That gives
    # the same executable without a duplicate MANIFEST resource.
    set(SUPERMODEL_CMAKE_RC "${CMAKE_BINARY_DIR}/SupermodelResources.cmake.rc")
    file(WRITE "${SUPERMODEL_CMAKE_RC}"
        "#include \"winuser.h\"\n"
        "#include \"${CMAKE_SOURCE_DIR}/Src/OSD/Windows/SplashResource.h\"\n"
        "#define IDI_SUPERMODEL 101\n"
        "IDI_SUPERMODEL ICON \"${CMAKE_SOURCE_DIR}/output.ico\"\n"
        "IDR_SPLASH RCDATA \"${CMAKE_SOURCE_DIR}/Igloo-Garage Presents.webp\"\n")
    set_source_files_properties("${SUPERMODEL_CMAKE_RC}" PROPERTIES GENERATED TRUE)
endif()

# -----------------------------------------------------------------------------
# Backend-free core
# -----------------------------------------------------------------------------

#
# Builds one core library.  mode selects which renderer family (if any) the
# archive contains:
#
#   OPENGL  base + OpenGL renderers, links OpenGL::GL/GLU, defines GLEW_STATIC
#   VULKAN  base + Vulkan renderers, Vulkan headers, defines SUPERMODEL_PURE_VULKAN
#   BOTH    base + both renderer families.  Only the SDL3 escape hatch
#           (SUPERMODEL_SDL3_PURE_VULKAN=OFF) uses this: it is the shape the
#           code had before the split and exists so that option keeps working.
#
# The upstream PowerPC/Real3D/SCSP code is the same object code in all cases:
# nothing above the renderer layer knows which graphics API is present.
#
function(supermodel_add_core target mode)
    set(sources ${SUPERMODEL_CORE_SOURCES})
    # Empty everywhere except Windows; see the note where the list is built.
    list(APPEND sources ${SUPERMODEL_WEBP_SOURCES})
    if(mode STREQUAL "OPENGL")
        list(APPEND sources ${SUPERMODEL_CORE_GL_SOURCES})
    elseif(mode STREQUAL "VULKAN")
        list(APPEND sources ${SUPERMODEL_CORE_VULKAN_SOURCES})
    elseif(mode STREQUAL "BOTH")
        list(APPEND sources ${SUPERMODEL_CORE_GL_SOURCES})
        list(APPEND sources ${SUPERMODEL_CORE_VULKAN_SOURCES})
    else()
        message(FATAL_ERROR "supermodel_add_core: unknown mode '${mode}'")
    endif()

    add_library(${target} STATIC ${sources})

    target_include_directories(${target}
        PUBLIC ${SUPERMODEL_INCLUDE_DIRS})

    if(mode STREQUAL "VULKAN")
        target_include_directories(${target}
            PUBLIC Libraries/Vulkan-Headers/include)
        target_compile_definitions(${target}
            PUBLIC SUPERMODEL_PURE_VULKAN=1)
    else()
        if(mode STREQUAL "BOTH")
            target_include_directories(${target}
                PUBLIC Libraries/Vulkan-Headers/include)
        endif()
        target_compile_definitions(${target}
            PRIVATE GLEW_STATIC)
        target_link_libraries(${target} PUBLIC OpenGL::GL)
        if(TARGET OpenGL::GLU)
            target_link_libraries(${target} PUBLIC OpenGL::GLU)
        elseif(WIN32)
            target_link_libraries(${target} PUBLIC glu32)
        endif()
    endif()

    target_link_libraries(${target}
        PUBLIC
            supermodel_common
            supermodel_zlib
            Threads::Threads
            ${CMAKE_DL_LIBS})

    if(WIN32)
        target_compile_definitions(${target} PUBLIC SUPERMODEL_WIN32)
        target_link_libraries(${target} PUBLIC
            wbemuuid dinput8 dxguid ws2_32 iphlpapi setupapi cfgmgr32
            winmm imm32 version psapi)
    endif()

    if(APPLE)
        target_compile_definitions(${target} PUBLIC SUPERMODEL_OSX)
    endif()
endfunction()

# The OpenGL core serves Supermodel-LegacyOG: OpenGL sources only, no Vulkan.
if(USE_SDL2)
    supermodel_add_core(supermodel_core OPENGL)
endif()

# The SDL3 escape hatch is the only target that contains both renderers.
if(USE_SDL3 AND NOT SUPERMODEL_SDL3_PURE_VULKAN)
    supermodel_add_core(supermodel_core_dual BOTH)
endif()

if(USE_SDL3 AND SUPERMODEL_SDL3_PURE_VULKAN)
    # A second archive with no GL objects in it at all.  The macro is PUBLIC so
    # the core units that have a GL-free branch (Model3.cpp's SuperAA draw)
    # take it, and so does the executable.
    supermodel_add_core(supermodel_core_vk VULKAN)
endif()

# -----------------------------------------------------------------------------
# Runtime data beside the executables
# -----------------------------------------------------------------------------

#
# Config/ and Assets/ are copied next to the executables so the build output runs
# where it is built, which is the same layout build.bat assembles.
#
# This used to be a POST_BUILD step on each executable. That is a race: with both
# backend targets enabled, ninja links them concurrently and runs both post-build
# steps at once, and two copy_directory calls into the same destination make the
# loser fail with "Error copying directory". It only shows up when both targets
# relink in the same build, which is exactly what a release build does.
#
# One target, depended on by both executables, cannot race. It has no OUTPUT and
# so runs on every build; copying ~6 MB is cheaper than the bookkeeping needed to
# make it conditional, and it also repairs a Data directory someone deleted.
#
add_custom_target(supermodel_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${SUPERMODEL_OUTPUT_DIR}/Data/Config"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Assets"
        "${SUPERMODEL_OUTPUT_DIR}/Data/NVRAM"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Saves"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Screenshots"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Analysis"
        "${SUPERMODEL_OUTPUT_DIR}/roms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Config"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Config"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Model3VulkanBezel.png"
        "${SUPERMODEL_OUTPUT_DIR}/Data/Assets/Model3VulkanBezel.png"
    COMMENT "Assembling runtime Data/ beside the executables"
    VERBATIM)

# -----------------------------------------------------------------------------
# Platform target factory
# -----------------------------------------------------------------------------

function(supermodel_add_platform_target backend)
    if(backend STREQUAL "SDL2")
        set(osd_sources ${SUPERMODEL_OSD_SOURCES})
        # The Vulkan context is a renderer unit: it talks to PlatformVulkan.h and
        # to volk, neither of which is part of the OpenGL-only executable.
        list(REMOVE_ITEM osd_sources Src/Graphics/Vulkan3D/VulkanContext.cpp)
        set(core_library supermodel_core)
    else()
        set(osd_sources ${SUPERMODEL_OSD_SOURCES})
        if(SUPERMODEL_SDL3_PURE_VULKAN)
            # Crosshair.cpp is a New3D/GL texture consumer.  Vulkan2D draws the crosshair
            # natively (CVulkan2D::SetCrosshair), so the whole translation unit is absent.
            list(REMOVE_ITEM osd_sources Src/OSD/SDL/Crosshair.cpp)
        endif()
    endif()

    if(backend STREQUAL "SDL2")
        # Supermodel-LegacyOG: SDL2 + OpenGL only.  The historical name of this
        # target was Supermodel-VK, back when one executable carried both
        # renderers and picked one at run time.
        set(target_name "Supermodel-LegacyOG")
        set(output_name "Supermodel-LegacyOG")
        set(backend_define SUPERMODEL_PLATFORM_SDL2)
        set(platform_sources
            Src/OSD/Platform/SDL2/PlatformSDL2.cpp
            Src/OSD/Platform/SDL2/PlatformInputSDL2.cpp)
        # PlatformVulkanSDL2.cpp is deliberately absent: no Vulkan surface
        # handshake exists in this executable.
        set(platform_libs SDL2::SDL2-static SDL2::SDL2main SDL2_net)
        set(network_sources ${SUPERMODEL_NET_SOURCES})
        set(core_library supermodel_core)
    elseif(backend STREQUAL "SDL3")
        set(target_name "Model3-Vulkan")
        set(output_name "Model3-Vulkan")
        set(backend_define SUPERMODEL_PLATFORM_SDL3)
        set(platform_sources
            Src/OSD/Platform/SDL3/PlatformSDL3.cpp
            Src/OSD/Platform/SDL3/PlatformInputSDL3.cpp
            Src/OSD/Platform/SDL3/PlatformVulkanSDL3.cpp
            # C++23 startup layer.  These files use std::expected, std::optional's
            # monadic operations, <print> and <stacktrace>, so they are compiled
            # only here and only at C++23.
            Src/OSD/SDL3/AppRuntime.cpp
            Src/OSD/SDL3/VulkanRequirement.cpp
            Src/OSD/SDL3/HostCoreBridge.cpp)
        if(SUPERMODEL_SDL3_PURE_VULKAN)
            # ImGui hosted on the settings window's Vulkan swapchain.
            list(APPEND platform_sources Src/OSD/SDL3/UiVulkanBackend.cpp)
        endif()
        set(platform_libs SDL3::SDL3)
        # TCPSend/TCPReceive use NativeSocket (WinSock2 / BSD sockets) on this backend, so no
        # SDL_net dependency is needed and the executable has a real network board.
        set(network_sources ${SUPERMODEL_NET_SOURCES})
        if(USE_SDL3_NET)
            list(APPEND platform_libs SDL3_net::SDL3_net)
        endif()
        if(SUPERMODEL_SDL3_PURE_VULKAN)
            set(core_library supermodel_core_vk)
        else()
            set(core_library supermodel_core_dual)
        endif()
    else()
        message(FATAL_ERROR "Unknown backend: ${backend}")
    endif()

    add_executable(${target_name}
        ${osd_sources}
        ${network_sources}
        ${platform_sources})

    if(backend STREQUAL "SDL2")
        target_include_directories(${target_name} PRIVATE
            Libraries/SDL2-2.30.11/include
            Libraries/SDL2_net-2.4.0/include)
    else()
        # Explicitly SDL3 only.  SDL2 is never on this target's include path,
        # so an accidental #include <SDL.h> fails instead of silently picking
        # up the SDL2 ABI (the two libraries export the same symbol names).
        # Vulkan headers are here because this executable always contains the
        # Vulkan renderer, whether or not the OpenGL half is also compiled in.
        target_include_directories(${target_name} PRIVATE
            Libraries/SDL3/include
            Libraries/Vulkan-Headers/include)

        # C++23 is a property of this executable target, not of the project:
        # the SDL2 target and supermodel_core stay at C++17, which keeps the
        # upstream emulation code untouched and merge-friendly.
        set_target_properties(${target_name} PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)
        target_compile_features(${target_name} PRIVATE cxx_std_23)
        target_compile_definitions(${target_name} PRIVATE SUPERMODEL_CXX23_TARGET=1)
    endif()

    target_compile_definitions(${target_name}
        PRIVATE ${backend_define})

    target_link_libraries(${target_name}
        PRIVATE ${core_library} supermodel_common ${platform_libs})

    if(backend STREQUAL "SDL3" AND WIN32)
        # NativeSocket.cpp uses WinSock2 directly; SDL3_net is not on this link line.
        target_link_libraries(${target_name} PRIVATE ws2_32)
    endif()

    if(WIN32)
        target_sources(${target_name} PRIVATE
            Src/OSD/Windows/FileSystemPath.cpp
            Src/OSD/Windows/SplashScreen.cpp
            Src/OSD/Windows/WinOutputs.cpp
            ${SUPERMODEL_CMAKE_RC})
        target_link_options(${target_name} PRIVATE
            "/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/Src/OSD/Windows/SupermodelManifest.xml")
        if(backend STREQUAL "SDL2")
            target_sources(${target_name} PRIVATE
                Src/OSD/Windows/DirectInputSystem.cpp)
        endif()

        # The MSVC project is a Windows-subsystem executable.  SDL2main
        # supplies WinMain; the console is attached explicitly in Main.cpp.
        set_target_properties(${target_name} PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(APPLE)
        target_sources(${target_name} PRIVATE
            Src/OSD/OSX/FileSystemPath.cpp
            Src/OSD/OSX/FileDialogs.mm)
        # ImageIO/CoreGraphics decode the WebP boxarts on macOS (see DecodeBoxart()).
        target_link_libraries(${target_name} PRIVATE
            "-framework Cocoa" "-framework IOKit"
            "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
    else()
        target_sources(${target_name} PRIVATE
            Src/OSD/Unix/FileSystemPath.cpp)
        # libwebp decodes the WebP boxarts on Linux (see DecodeBoxart()).
        find_library(WEBP_LIBRARY webp REQUIRED)
        target_link_libraries(${target_name} PRIVATE ${WEBP_LIBRARY})
    endif()

    # The C# Native AOT cold-path module is optional: the exe loads it at
    # runtime.  When it has been published for this platform, keep a copy next
    # to the executable so build-cmake/bin is directly runnable.
    if(backend STREQUAL "SDL3")
        if(WIN32)
            set(hostcore_name "Model3VulkanHostCore.dll")
        elseif(APPLE)
            set(hostcore_name "libModel3VulkanHostCore.dylib")
        else()
            set(hostcore_name "libModel3VulkanHostCore.so")
        endif()
        # The cold-path module is self-contained: the zstd codec (ZstdSharp.Port)
        # is compiled into the image, so only the managed image is deployed.
        set(hostcore_lib "${CMAKE_SOURCE_DIR}/HostCore/publish/${hostcore_name}")
        if(EXISTS "${hostcore_lib}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${hostcore_lib}" "$<TARGET_FILE_DIR:${target_name}>/${hostcore_name}"
                COMMENT "Deploying ${hostcore_name} beside the Vulkan executable")
        endif()
    endif()

    set_target_properties(${target_name} PROPERTIES
        OUTPUT_NAME ${output_name}
        RUNTIME_OUTPUT_DIRECTORY "${SUPERMODEL_OUTPUT_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # Keep the CMake output directly runnable: the program resolves Config and
    # Assets under <exe>/Data, the same layout build.bat assembles.  Done by a
    # shared target below rather than here - see the reason there.

    # One binary, one backend.  Catch a definition leak at compile time.
    if(backend STREQUAL "SDL2")
        target_compile_definitions(${target_name} PRIVATE SUPERMODEL_SDL2_TARGET=1)
        # This executable contains the OpenGL renderers and nothing else.  Every
        # Vulkan code path in the shared OSD units is compiled out by this macro.
        target_compile_definitions(${target_name} PRIVATE SUPERMODEL_OPENGL_ONLY=1)
    else()
        target_compile_definitions(${target_name} PRIVATE SUPERMODEL_SDL3_TARGET=1)
    endif()

    # Runtime data is assembled once, by supermodel_runtime_data, before either
    # executable links.
    add_dependencies(${target_name} supermodel_runtime_data)

    if(SUPERMODEL_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_message LANGUAGES C CXX)
        if(ipo_supported)
            set_property(TARGET ${target_name}
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            set_property(TARGET supermodel_core
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        else()
            message(STATUS "LTO unavailable: ${ipo_message}")
        endif()
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Optional host-core publication (Linux / macOS and manual CMake builds)
# -----------------------------------------------------------------------------
# The C# Native AOT module is what reads and writes the .zst save containers.
# Windows publishes it from build.bat.  On Linux and macOS the publish has to
# run on the target operating system (Native AOT does not cross-compile between
# operating systems), so it is a separate target rather than part of ALL:
#
#     cmake --build build-cmake --target hostcore
#
# It is skipped when the .NET SDK is absent; the emulator still builds without
# the module, it just cannot use .zst containers.
if(EXISTS "${CMAKE_SOURCE_DIR}/HostCore/Model3VulkanHostCore.csproj")
    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        if(WIN32)
            set(hostcore_rid "win-x64")
        elseif(APPLE)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                set(hostcore_rid "osx-arm64")
            else()
                set(hostcore_rid "osx-x64")
            endif()
        else()
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(hostcore_rid "linux-arm64")
            else()
                set(hostcore_rid "linux-x64")
            endif()
        endif()

        if(WIN32)
            set(hostcore_published "Model3VulkanHostCore.dll")
        elseif(APPLE)
            set(hostcore_published "libModel3VulkanHostCore.dylib")
        else()
            set(hostcore_published "libModel3VulkanHostCore.so")
        endif()

        add_custom_target(hostcore
            COMMAND ${DOTNET_EXECUTABLE} publish
                "${CMAKE_SOURCE_DIR}/HostCore/Model3VulkanHostCore.csproj"
                -c Release -r ${hostcore_rid}
                -o "${CMAKE_SOURCE_DIR}/HostCore/publish"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/HostCore/publish/${hostcore_published}"
                "${SUPERMODEL_OUTPUT_DIR}/${hostcore_published}"
            COMMENT "Publishing ${hostcore_published} (${hostcore_rid}) and copying it beside the executable"
            VERBATIM)
    endif()
endif()

if(USE_SDL2)
    supermodel_add_platform_target(SDL2)
endif()

if(USE_SDL3)
    supermodel_add_platform_target(SDL3)
endif()
