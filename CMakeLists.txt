cmake_minimum_required(VERSION 3.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(ExternalProject)
include(SourceGroupByDir)
include(GetGitRevisionDescription)

project(redream)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

#--------------------------------------------------
# configuration options
#--------------------------------------------------

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
    "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

option(BUILD_LIBRETRO "Build libretro core" OFF)
option(BUILD_TOOLS "Build tools" OFF)
option(BUILD_TESTS "Build tests" OFF)

if(WIN32 OR MINGW)
  set(PLATFORM_WINDOWS TRUE)
elseif(ANDROID)
  set(PLATFORM_ANDROID TRUE)
elseif(APPLE)
  set(PLATFORM_DARWIN TRUE)
elseif(UNIX)
  set(PLATFORM_LINUX TRUE)
endif()

if("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  set(COMPILER_MSVC TRUE)
elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
  set(COMPILER_CLANG TRUE)
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(COMPILER_GCC TRUE)
endif()

if(PLATFORM_ANDROID)
  set(ARCH_A64 TRUE)
else()
  set(ARCH_X64 TRUE)
endif()

if(BUILD_LIBRETRO)
  # building the libretro core requires PIC code for all object files
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

#--------------------------------------------------
# version file
#--------------------------------------------------

git_describe(GIT_VERSION --tags)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/core/version.h @ONLY)


#--------------------------------------------------
# config file
#--------------------------------------------------

check_include_files(strings.h HAVE_STRINGS_H)
check_function_exists(strcasecmp HAVE_STRCASECMP)
check_function_exists(strnlen HAVE_STRNLEN)
check_function_exists(strnstr HAVE_STRNSTR)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/core/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/core/config.h)

list(APPEND RELIB_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/src)
list(APPEND RELIB_DEFS HAVE_CONFIG_H)


#--------------------------------------------------
# static libs
#--------------------------------------------------

# capstone
set(CAPSTONE_ARM_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_ARM64_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_MIPS_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_PPC_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_SPARC_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_SYSZ_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_XCORE_SUPPORT OFF CACHE BOOL "")
add_subdirectory(deps/capstone EXCLUDE_FROM_ALL)
list(APPEND RELIB_INCLUDES deps/capstone/include)
list(APPEND RELIB_LIBS capstone-static)

# chdr
add_subdirectory(deps/chdr EXCLUDE_FROM_ALL)
list(APPEND RELIB_INCLUDES deps/chdr/src)
list(APPEND RELIB_LIBS chdr-static)

# dirent
list(APPEND RELIB_INCLUDES deps/dirent)

# gdb_server
list(APPEND RELIB_INCLUDES deps/gdb_server)

# glad
add_library(glad-static STATIC deps/glad/src/glad.c)
target_include_directories(glad-static SYSTEM PUBLIC deps/glad/include)
list(APPEND RELIB_INCLUDES deps/glad/include)
list(APPEND RELIB_LIBS glad-static)

# inih
add_library(inih-static STATIC deps/inih/ini.c)
list(APPEND RELIB_INCLUDES deps/inih)
list(APPEND RELIB_LIBS inih-static)

# vixl
if(ARCH_A64)
  file(GLOB VIXL_SOURCES deps/vixl/src/*.cc deps/vixl/src/aarch64/*.cc)
  add_library(vixl-static STATIC ${VIXL_SOURCES})
  target_compile_definitions(vixl-static PRIVATE VIXL_CODE_BUFFER_STATIC)
  list(APPEND RELIB_INCLUDES deps/vixl/src)
  list(APPEND RELIB_LIBS vixl-static)
endif()

# xbyak
if(ARCH_X64)
  list(APPEND RELIB_INCLUDES deps/xbyak)
endif()

# zlib, reused from libchdr
list(APPEND RELIB_INCLUDES deps/chdr/deps/zlib-1.2.11)

#--------------------------------------------------
# optional libs
#--------------------------------------------------

# imgui
add_library(imgui-static STATIC
  deps/cimgui/imgui/imgui.cpp
  deps/cimgui/imgui/imgui_demo.cpp
  deps/cimgui/imgui/imgui_draw.cpp
  deps/cimgui/cimgui/cimgui.cpp
  deps/cimgui/cimgui/drawList.cpp
  deps/cimgui/cimgui/fontAtlas.cpp
  deps/cimgui/cimgui/listClipper.cpp)
list(APPEND RELIB_INCLUDES deps/cimgui)
list(APPEND IMGUI_LIBS imgui-static)

# sdl2
set(SDL_ATOMIC OFF CACHE BOOL "")
set(SDL_CPUINFO ON CACHE BOOL "")
set(SDL_FILESYSTEM OFF CACHE BOOL "")
set(SDL_POWER OFF CACHE BOOL "")
set(SDL_RENDER OFF CACHE BOOL "")
set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")

if(PLATFORM_DARWIN)
  set(SDL_FRAMEWORK_CARBON 1)
endif()

add_subdirectory(deps/sdl2 EXCLUDE_FROM_ALL)
list(APPEND RELIB_INCLUDES deps/sdl2/include)

if(MINGW)
  list(APPEND SDL_LIBS mingw32)
endif()
list(APPEND SDL_LIBS SDL2main SDL2-static)

#--------------------------------------------------
# format
#--------------------------------------------------

find_package(ClangFormat)

if(CLANG_FORMAT_FOUND)
file(GLOB_RECURSE CLANG_FORMAT_ARGS "src/*.c" "src/*.cc" "src/*.h" "test/*.c" "test/*.cc" "test/*.h" "tools/*.c" "tools/*.cc" "tools/*.h")
add_custom_target(format ${CLANG_FORMAT_EXECUTABLE} -i ${CLANG_FORMAT_ARGS})
endif()

#--------------------------------------------------
# redream sources, includes and libs, common to multiple projects
#--------------------------------------------------

set(RELIB_SOURCES
  src/core/assert.c
  src/core/bitmap.c
  src/core/exception_handler.c
  src/core/filesystem.c
  src/core/interval_tree.c
  src/core/list.c
  src/core/log.c
  src/core/md5.c
  src/core/memory.c
  src/core/option.c
  src/core/profiler.c
  src/core/ringbuf.cc
  src/core/rb_tree.c
  src/core/sort.c
  src/core/string.c
  src/file/trace.c
  src/guest/aica/aica.c
  src/guest/arm7/arm7.c
  src/guest/bios/bios.c
  src/guest/bios/flash.c
  src/guest/bios/scramble.c
  src/guest/bios/syscalls.c
  src/guest/gdrom/cdi.c
  src/guest/gdrom/chd.c
  src/guest/gdrom/disc.c
  src/guest/gdrom/gdi.c
  src/guest/gdrom/gdrom.c
  src/guest/holly/holly.c
  src/guest/maple/controller.c
  src/guest/maple/maple.c
  src/guest/maple/vmu.c
  src/guest/pvr/pvr.c
  src/guest/pvr/ta.c
  src/guest/pvr/tex.c
  src/guest/pvr/tr.c
  src/guest/rom/boot.c
  src/guest/rom/flash.c
  src/guest/serial/serial.c
  src/guest/sh4/sh4.c
  src/guest/sh4/sh4_ccn.c
  src/guest/sh4/sh4_dbg.c
  src/guest/sh4/sh4_dmac.c
  src/guest/sh4/sh4_intc.c
  src/guest/sh4/sh4_mem.c
  src/guest/sh4/sh4_mmu.c
  src/guest/sh4/sh4_tmu.c
  src/guest/sh4/sh4_scif.c
  src/guest/debugger.c
  src/guest/dreamcast.c
  src/guest/memory.c
  src/guest/scheduler.c
  src/host/keycode.c
  src/jit/backend/interp/interp_backend.c
  src/jit/frontend/armv3/armv3_context.c
  src/jit/frontend/armv3/armv3_disasm.c
  src/jit/frontend/armv3/armv3_fallback.c
  src/jit/frontend/armv3/armv3_frontend.c
  src/jit/frontend/sh4/sh4_disasm.c
  src/jit/frontend/sh4/sh4_fallback.c
  src/jit/frontend/sh4/sh4_frontend.c
  src/jit/frontend/sh4/sh4_translate.c
  src/jit/ir/ir.c
  src/jit/ir/ir_read.c
  src/jit/ir/ir_write.c
  src/jit/passes/constant_propagation_pass.c
  src/jit/passes/control_flow_analysis_pass.c
  #src/jit/passes/conversion_elimination_pass.c
  src/jit/passes/dead_code_elimination_pass.c
  src/jit/passes/expression_simplification_pass.c
  src/jit/passes/load_store_elimination_pass.c
  src/jit/passes/register_allocation_pass.c
  src/jit/jit.c
  src/jit/pass_stats.c
  src/render/gl_backend.c
  src/options.c
  src/stats.c)

if(PLATFORM_ANDROID)
  list(APPEND RELIB_DEFS PLATFORM_ANDROID=1)
  list(APPEND RELIB_SOURCES
    src/core/exception_handler_linux.c
    src/core/filesystem_posix.c
    src/core/memory_posix.c
    src/core/thread_posix.c
    src/core/time_linux.c)
elseif(PLATFORM_DARWIN)
  list(APPEND RELIB_DEFS PLATFORM_DARWIN=1)
  list(APPEND RELIB_SOURCES
    src/core/exception_handler_mac.c
    src/core/filesystem_posix.c
    src/core/memory_posix.c
    src/core/thread_posix.c
    src/core/time_mac.c)
elseif(PLATFORM_LINUX)
  list(APPEND RELIB_DEFS PLATFORM_LINUX=1)
  list(APPEND RELIB_SOURCES
    src/core/exception_handler_linux.c
    src/core/filesystem_posix.c
    src/core/memory_posix.c
    src/core/thread_posix.c
    src/core/time_linux.c)
elseif(PLATFORM_WINDOWS)
  list(APPEND RELIB_DEFS PLATFORM_WINDOWS=1)
  list(APPEND RELIB_SOURCES
    src/core/exception_handler_win.c
    src/core/filesystem_win.c
    src/core/memory_win.c
    src/core/time_win.c)
  if(MINGW)
    list(APPEND RELIB_SOURCES src/core/thread_posix.c)
  else()
    list(APPEND RELIB_SOURCES src/core/thread_win.c)
  endif()
endif()

if(ARCH_X64)
  list(APPEND RELIB_DEFS ARCH_X64=1)
  list(APPEND RELIB_SOURCES
    src/jit/backend/x64/x64_backend.cc
    src/jit/backend/x64/x64_disassembler.c
    src/jit/backend/x64/x64_dispatch.cc
    src/jit/backend/x64/x64_emitters.cc)
elseif(ARCH_A64)
  list(APPEND RELIB_DEFS ARCH_A64=1)
endif()

if(COMPILER_MSVC)
  list(APPEND RELIB_DEFS COMPILER_MSVC=1)

  list(APPEND RELIB_FLAGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX /GR- /W3 /WX /wd4100 /wd4127 /wd4505 /wd4512 /wd4800 /wd4351)
elseif(COMPILER_GCC OR COMPILER_CLANG)
  list(APPEND RELIB_FLAGS
    # used for unnamed structure fields
    -fms-extensions
    # enable most errors
    -Wall -Wextra -Werror
    # lax some of these common development warnings
    -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-strict-aliasing
    -D_GNU_SOURCE)

  if(COMPILER_GCC)
    list(APPEND RELIB_DEFS COMPILER_GCC=1)

    # -format-truncation is too aggressive with snprintfs that are purposefully supposed
    # to truncate on overflow
    list(APPEND RELIB_FLAGS -Wno-format-truncation)

    # some flavors of gcc require this to be defined for the PR* macros in inttypes.h
    list(APPEND RELIB_FLAGS -D__STDC_FORMAT_MACROS)
  elseif(COMPILER_CLANG)
    list(APPEND RELIB_DEFS COMPILER_CLANG=1)

    # clang will warn on '{0}' as -Wmissing-field-initializers even in C code
    # https://llvm.org/bugs/show_bug.cgi?id=21689
    list(APPEND RELIB_FLAGS -Wno-missing-field-initializers -Wno-missing-braces -Wno-microsoft-anon-tag)
  endif()

  if(NOT PLATFORM_ANDROID)
    # the android libc provides built-in support for pthreads, so no
    # additional linking or compile flags are necessary
    list(APPEND RELIB_FLAGS -pthread)
    list(APPEND RELIB_LIBS pthread)
  endif()
endif()

if(PLATFORM_ANDROID)
  # this is an awful hack for the gcc provided by the android-ndk-r15, which
  # complains about zero-initializing multi-dimensional arrays using foo = {0}
  list(APPEND RELIB_FLAGS -Wno-missing-field-initializers -Wno-missing-braces)
  list(APPEND RELIB_LIBS android log)
elseif(PLATFORM_LINUX)
  # used by glad / sdl
  list(APPEND RELIB_LIBS dl)
  # used by shm_open / shm_unlink on linux
  list(APPEND RELIB_LIBS rt)
elseif(PLATFORM_WINDOWS)
  list(APPEND RELIB_LIBS userenv ws2_32)
endif()

#--------------------------------------------------
# redream
#--------------------------------------------------

if(BUILD_LIBRETRO)
  set(REDREAM_SOURCES ${RELIB_SOURCES}
    src/host/retro_host.c
    src/emulator.c)
  set(REDREAM_INCLUDES ${RELIB_INCLUDES} deps/libretro/include)
  set(REDREAM_LIBS ${RELIB_LIBS})
  set(REDREAM_DEFS ${RELIB_DEFS})
  set(REDREAM_FLAGS ${RELIB_FLAGS})
else()
  set(REDREAM_SOURCES ${RELIB_SOURCES}
    src/host/sdl_host.c
    src/emulator.c
    src/imgui.cc
    src/tracer.c
    src/ui.c)
  set(REDREAM_INCLUDES ${RELIB_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})
  set(REDREAM_LIBS ${RELIB_LIBS} ${IMGUI_LIBS} ${SDL_LIBS})
  set(REDREAM_DEFS ${RELIB_DEFS} HAVE_GDBSERVER HAVE_IMGUI)
  set(REDREAM_FLAGS ${RELIB_FLAGS})
endif()

# fastmem can be troublesome when running under gdb or lldb. when doing so,
# SIGSEGV handling can be completely disabled with:
# handle SIGSEGV nostop noprint pass
# however, then legitimate SIGSEGV will also not be handled by the debugger.
# as of this writing, there is no way to configure the debugger to ignore the
# signal initially, letting us try to handle it, and then handling it in the
# case that we do not (e.g. because it was not a fastmem-related segfault).
# because of this, fastmem is default disabled for debug builds to cause less
# headaches
list(APPEND REDREAM_DEFS $<$<NOT:$<CONFIG:Debug>>:HAVE_FASTMEM>)

source_group_by_dir(REDREAM_SOURCES)

if(BUILD_LIBRETRO OR ANDROID)
  add_library(redream SHARED ${REDREAM_SOURCES})
else()
  add_executable(redream ${REDREAM_SOURCES})
endif()

target_include_directories(redream PUBLIC ${REDREAM_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(redream ${REDREAM_LIBS})
target_compile_definitions(redream PRIVATE ${REDREAM_DEFS})
target_compile_options(redream PRIVATE ${REDREAM_FLAGS})

#--------------------------------------------------
# tools
#--------------------------------------------------

if(BUILD_TOOLS)

if(ARCH_X64)

# recc
set(RECC_SOURCES
  ${RELIB_SOURCES}
  src/host/null_host.c
  tools/recc/main.c)
source_group_by_dir(RECC_SOURCES)

add_executable(recc ${RECC_SOURCES})
target_include_directories(recc PUBLIC ${RELIB_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(recc ${RELIB_LIBS})
target_compile_definitions(recc PRIVATE ${RELIB_DEFS})
target_compile_options(recc PRIVATE ${RELIB_FLAGS})
endif()

# reload
set(RELOAD_SOURCES
  ${RELIB_SOURCES}
  src/host/null_host.c
  tools/reload/main.c)
source_group_by_dir(RELOAD_SOURCES)

add_executable(reload ${RELOAD_SOURCES})
target_include_directories(reload PUBLIC ${RELIB_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(reload ${RELIB_LIBS})
target_compile_definitions(reload PRIVATE ${RELIB_DEFS})
target_compile_options(reload PRIVATE ${RELIB_FLAGS})

# retex
set(RETEX_SOURCES
  ${RELIB_SOURCES}
  src/host/null_host.c
  tools/retex/main.c)
source_group_by_dir(RETEX_SOURCES)

add_executable(retex ${RETEX_SOURCES})
target_include_directories(retex PUBLIC ${RELIB_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retex ${RELIB_LIBS})
target_compile_definitions(retex PRIVATE ${RELIB_DEFS})
target_compile_options(retex PRIVATE ${RELIB_FLAGS})

# retrace
set(RETRACE_SOURCES
  ${RELIB_SOURCES}
  src/host/null_host.c
  tools/retrace/depth.c
  tools/retrace/main.c)
source_group_by_dir(RETRACE_SOURCES)

add_executable(retrace ${RETRACE_SOURCES})
target_include_directories(retrace PUBLIC ${RELIB_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retrace ${RELIB_LIBS})
target_compile_definitions(retrace PRIVATE ${RELIB_DEFS})
target_compile_options(retrace PRIVATE ${RELIB_FLAGS})

endif()

#--------------------------------------------------
# retest
#--------------------------------------------------

if(BUILD_TESTS)

set(RETEST_SOURCES
  ${RELIB_SOURCES}
  src/host/null_host.c
  test/test_dead_code_elimination.c
  test/test_interval_tree.c
  test/test_list.c
  test/test_load_store_elimination.c
  test/retest.c)
source_group_by_dir(RETEST_SOURCES)

add_executable(retest ${RETEST_SOURCES})
target_include_directories(retest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/test ${RELIB_INCLUDES})
target_link_libraries(retest ${RELIB_LIBS})
target_compile_definitions(retest PRIVATE ${RELIB_DEFS})
target_compile_options(retest PRIVATE ${RELIB_FLAGS})

endif()
