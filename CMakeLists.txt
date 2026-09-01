cmake_minimum_required(VERSION 3.20)
project(fakk2-recomp VERSION 0.2.0 LANGUAGES C)

# =============================================================================
# Heavy Metal: FAKK2 -- Static Recompilation
# =============================================================================
# "One girl. One sword. One hell of a good time."
#
# Original: fakk2.exe v1.02 (2000-08-22, MSVC 6.0, 32-bit, image base 0x400000)
# This target builds the ~430K lines of C lifted out of it by run_lift.py.
#
# The pre-lift hand-written engine scaffold (src/engine, src/renderer, ...) is
# not built: it was a reimplementation, superseded by the lift. Its CMake lives
# in docs/CMakeLists.scaffold.txt.
# =============================================================================

# The lifted code keeps x86 machine state in 32-bit globals and stores host
# pointers into them, so the host process must be 32-bit.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build 32-bit (x86): cmake -A Win32")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# CONFIGURE_DEPENDS re-globs at build time, so a re-lift that emits more chunks
# is picked up without a manual reconfigure (otherwise: unresolved sub_*).
file(GLOB RECOMP_GENERATED_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/recomp_*.c")

add_executable(fakk2
    src/game/main.c
    src/game/imports.c
    src/game/imports_gen.c
    src/recomp/image_loader.c
    ${RECOMP_GENERATED_SOURCES}
)

target_include_directories(fakk2 PRIVATE src/game src/recomp src/recomp/gen)

set_source_files_properties(${RECOMP_GENERATED_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "RECOMP_GENERATED_CODE")

target_link_libraries(fakk2 PRIVATE kernel32 user32 gdi32 winmm advapi32 ole32)

if(MSVC)
    target_link_options(fakk2 PRIVATE
        /DYNAMICBASE:NO        # fixed base: FAKK2's VAs are absolute
        /BASE:0x70000000       # keep 0x00100000-0x00D40000 free for the image
        /STACK:0x80000         # host C frames only; the emulated stack is at 0x00100000
        /LARGEADDRESSAWARE:NO
    )
    target_compile_options(fakk2 PRIVATE
        /GS- /W3 /bigobj
        /wd4244 /wd4018 /wd4146 /wd4305 /wd4267 /wd4090 /wd4102 /wd4101
    )
endif()
