cmake_minimum_required(VERSION 3.20)
project(fakk2-recomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Heavy Metal: FAKK2 Static Recompilation
# =============================================================================
# "One girl. One sword. One hell of a good time."
#
# Target: Unified x86-64 binary for Windows 11
# Engine: id Tech 3 + Ritual's UberTools
# Original: fakk2.exe v1.02 (2000-08-22, MSVC 6.0, 32-bit)
# =============================================================================

# -----------------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------------
option(FAKK_RENDERER_GL4   "Build with OpenGL 4.x renderer backend"  ON)
option(FAKK_RENDERER_VULKAN "Build with Vulkan renderer backend"     OFF)
option(FAKK_BUILD_TOOLS    "Build analysis and asset tools"          ON)
option(FAKK_USE_SDL2       "Use SDL2 for platform abstraction"       ON)

# -----------------------------------------------------------------------------
# Platform detection
# -----------------------------------------------------------------------------
if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DPLATFORM_LINUX)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
endif()

# Always 64-bit recomp
add_definitions(-DFAKK_RECOMP)
add_definitions(-DFAKK_64BIT)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
if(FAKK_USE_SDL2)
    find_package(SDL2 REQUIRED)
    add_definitions(-DUSE_SDL2)
endif()

find_package(OpenGL REQUIRED)

# -----------------------------------------------------------------------------
# Common types and headers (used by everything)
# -----------------------------------------------------------------------------
set(COMMON_SOURCES
    src/common/fakk_types.h
    src/common/q_shared.h
    src/common/q_shared.c
    src/common/q_math.h
    src/common/q_math.c
    src/common/qcommon.h
    src/common/qfiles.h
    src/common/str.h
    src/common/str.cpp
    src/common/pk3.h
    src/common/pk3.c
    src/common/alias.h
    src/common/alias.c
)

# -----------------------------------------------------------------------------
# Engine core
# -----------------------------------------------------------------------------
set(ENGINE_SOURCES
    src/engine/main.c
    src/engine/common.c
    src/engine/cvar.c
    src/engine/cmd.c
    src/engine/msg.c
    src/engine/files.c
    src/engine/net.c
    src/engine/sys_sdl.c
    src/engine/win32_compat.h
)

# -----------------------------------------------------------------------------
# Client subsystem
# -----------------------------------------------------------------------------
set(CLIENT_SOURCES
    src/client/cl_main.c
    src/client/cl_input.c
    src/client/cl_cgame.c
    src/client/keys.c
    src/client/console.c
)

# -----------------------------------------------------------------------------
# Server subsystem
# -----------------------------------------------------------------------------
set(SERVER_SOURCES
    src/server/sv_main.c
    src/server/sv_game.c
    src/server/sv_world.c
)

# -----------------------------------------------------------------------------
# Renderer
# -----------------------------------------------------------------------------
set(RENDERER_SOURCES
    src/renderer/r_main.c
    src/renderer/r_gl.c
    src/renderer/r_gl.h
    src/renderer/r_model.c
    src/renderer/r_bsp.c
    src/renderer/r_shader.c
    src/renderer/r_light.c
    src/renderer/r_sky.c
    src/renderer/r_surface.c
    src/renderer/tr_types.h
)

# -----------------------------------------------------------------------------
# Collision model
# -----------------------------------------------------------------------------
set(COLLISION_SOURCES
    src/collision/cm_local.h
    src/collision/cm_main.c
)

# -----------------------------------------------------------------------------
# Sound system (replacing Miles Sound System)
# -----------------------------------------------------------------------------
set(SOUND_SOURCES
    src/sound/snd_main.c
    src/sound/snd_sdl.c
    src/sound/snd_local.h
)

# -----------------------------------------------------------------------------
# TIKI model system
# -----------------------------------------------------------------------------
set(TIKI_SOURCES
    src/tiki/tiki_main.c
    src/tiki/tiki_parse.c
    src/tiki/tiki_anim.c
    src/tiki/tiki_skel.c
    src/tiki/tiki.h
)

# -----------------------------------------------------------------------------
# Morpheus scripting engine
# -----------------------------------------------------------------------------
set(SCRIPT_SOURCES
    src/script/script_main.c
    src/script/script_vm.c
    src/script/script.h
)

# -----------------------------------------------------------------------------
# Ghost particle system
# -----------------------------------------------------------------------------
set(GHOST_SOURCES
    src/ghost/ghost_main.c
    src/ghost/ghost.h
)

# -----------------------------------------------------------------------------
# Main executable -- all modules unified into single binary
# -----------------------------------------------------------------------------
add_executable(fakk2
    ${COMMON_SOURCES}
    ${ENGINE_SOURCES}
    ${CLIENT_SOURCES}
    ${SERVER_SOURCES}
    ${COLLISION_SOURCES}
    ${RENDERER_SOURCES}
    ${SOUND_SOURCES}
    ${TIKI_SOURCES}
    ${SCRIPT_SOURCES}
    ${GHOST_SOURCES}
)

target_include_directories(fakk2 PRIVATE
    src/common
    src/engine
    src/client
    src/server
    src/collision
    src/renderer
    src/sound
    src/tiki
    src/script
    src/ghost
)

if(FAKK_USE_SDL2)
    target_link_libraries(fakk2 PRIVATE SDL2::SDL2 SDL2::SDL2main)
endif()

target_link_libraries(fakk2 PRIVATE OpenGL::GL)

if(WIN32)
    target_link_libraries(fakk2 PRIVATE ws2_32 winmm)
endif()

# -----------------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------------
install(TARGETS fakk2 RUNTIME DESTINATION bin)

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== Heavy Metal: FAKK2 Recomp ===")
message(STATUS "  Version:    ${PROJECT_VERSION}")
message(STATUS "  Renderer:   ${FAKK_RENDERER_GL4} (GL4) / ${FAKK_RENDERER_VULKAN} (Vulkan)")
message(STATUS "  SDL2:       ${FAKK_USE_SDL2}")
message(STATUS "  Tools:      ${FAKK_BUILD_TOOLS}")
message(STATUS "================================")
message(STATUS "")
