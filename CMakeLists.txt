cmake_minimum_required(VERSION 3.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CheckCSourceCompiles)
include(CheckIncludeFiles)
include(CheckFunctionExists)
include(ExternalProject)
include(SourceGroupByDir)

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
option(BUILD_TOOLS "Build tools" ON)
option(BUILD_TESTS "Build tests" ON)

if(WIN32)
  set(PLATFORM_WINDOWS TRUE)
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

set(ARCH_X64 TRUE)

if(BUILD_LIBRETRO)
  # building the libretro core requires PIC code for all object files
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

#--------------------------------------------------
# config file
#--------------------------------------------------

check_include_files(strings.h HAVE_STRINGS_H)
check_function_exists(strcasecmp HAVE_STRCASECMP)
check_function_exists(strnlen HAVE_STRNLEN)
check_function_exists(strnstr HAVE_STRNSTR)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/core/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/src/core/config.h)
list(APPEND REDREAM_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/src)

#--------------------------------------------------
# dynamic libs
#--------------------------------------------------

# opengl
find_package(OpenGL REQUIRED)
list(APPEND REDREAM_INCLUDE_DIRS ${OPENGL_INCLUDE_DIR})
list(APPEND REDREAM_LIBS ${OPENGL_LIBRARIES})

#--------------------------------------------------
# static libs
#--------------------------------------------------

# sdl2
if(NOT BUILD_LIBRETRO)
set(DIRECTX OFF CACHE BOOL "")
set(RENDER_D3D OFF CACHE BOOL "")
set(SDL_ATOMIC OFF CACHE BOOL "")
set(SDL_CPUINFO OFF CACHE BOOL "")
set(SDL_FILESYSTEM OFF CACHE BOOL "")
set(SDL_HAPTIC OFF CACHE BOOL "")
set(SDL_POWER OFF CACHE BOOL "")
set(SDL_RENDER OFF CACHE BOOL "")
set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")
if(APPLE)
  set(SDL_FRAMEWORK_CARBON 1)
endif()
add_subdirectory(deps/sdl2-2.0.5 EXCLUDE_FROM_ALL)
list(APPEND REDREAM_INCLUDE_DIRS deps/sdl2-2.0.5/include)
endif()

# capstone
add_subdirectory(deps/capstone EXCLUDE_FROM_ALL)
set(CAPSTONE_ARM_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_ARM64_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_MIPS_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_PPC_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_SPARC_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_SYSZ_SUPPORT OFF CACHE BOOL "")
set(CAPSTONE_XCORE_SUPPORT OFF CACHE BOOL "")
list(APPEND REDREAM_INCLUDE_DIRS deps/capstone/include)
list(APPEND REDREAM_LIBS capstone-static)

# dirent
list(APPEND REDREAM_INCLUDE_DIRS deps/dirent-1.21)

# gdb_server
list(APPEND REDREAM_INCLUDE_DIRS deps/gdb_server)

# glew
set(BUILD_UTILS OFF CACHE BOOL "")
add_subdirectory(deps/glew-1.13.0/build/cmake EXCLUDE_FROM_ALL)
list(APPEND REDREAM_INCLUDE_DIRS deps/glew-1.13.0/include)
list(APPEND REDREAM_LIBS glew_s)
list(APPEND REDREAM_DEFS GLEW_STATIC)

# inih
add_library(inih STATIC deps/inih/ini.c)
list(APPEND REDREAM_INCLUDE_DIRS deps/inih)
list(APPEND REDREAM_LIBS inih)

# libsoundio
set(BUILD_DYNAMIC_LIBS OFF CACHE BOOL "")
set(BUILD_EXAMPLE_PROGRAMS OFF CACHE BOOL "")
set(BUILD_TEST OFF CACHE BOOL "")
add_subdirectory(deps/libsoundio EXCLUDE_FROM_ALL)
list(APPEND REDREAM_INCLUDE_DIRS deps/libsoundio)
list(APPEND REDREAM_LIBS libsoundio_static)
list(APPEND REDREAM_DEFS SOUNDIO_STATIC_LIBRARY)

# microprofile
list(APPEND REDREAM_INCLUDE_DIRS deps/microprofile)

# nuklear
list(APPEND REDREAM_INCLUDE_DIRS deps/nuklear)

# xbyak
list(APPEND REDREAM_INCLUDE_DIRS deps/xbyak-4.901)


#--------------------------------------------------
# format
#--------------------------------------------------

find_package(ClangFormat)

if(CLANG_FORMAT_FOUND)
file(GLOB_RECURSE CLANG_FORMAT_ARGS "src/*.c" "src/*.cc" "src/*.h" "test/*.c" "test/*.cc" "test/*.h" "tools/*.c" "tools/*.cc" "tools/*.h")
add_custom_target(format ${CLANG_FORMAT_EXECUTABLE} -i ${CLANG_FORMAT_ARGS})
endif()


#--------------------------------------------------
# relib
# intermediate "object library" to speed up recc / retest compiles
#--------------------------------------------------

set(REDREAM_SOURCES
  src/core/assert.c
  src/core/interval_tree.c
  src/core/list.c
  src/core/log.c
  src/core/md5.c
  src/core/option.c
  src/core/profiler.cc
  src/core/ringbuf.cc
  src/core/rb_tree.c
  src/core/sort.c
  src/core/string.c
  src/hw/aica/aica.c
  src/hw/arm7/arm7.c
  src/hw/gdrom/disc.c
  src/hw/gdrom/gdrom.c
  src/hw/holly/holly.c
  src/hw/maple/controller.c
  src/hw/maple/maple.c
  src/hw/maple/vmu.c
  src/hw/pvr/pvr.c
  src/hw/pvr/ta.c
  src/hw/pvr/tr.c
  src/hw/pvr/trace.c
  src/hw/rom/boot.c
  src/hw/rom/flash.c
  src/hw/sh4/sh4.c
  src/hw/sh4/sh4_ccn.c
  src/hw/sh4/sh4_dbg.c
  src/hw/sh4/sh4_dmac.c
  src/hw/sh4/sh4_intc.c
  src/hw/sh4/sh4_tmu.c
  src/hw/debugger.c
  src/hw/dreamcast.c
  src/hw/memory.c
  src/hw/scheduler.c
  src/jit/backend/x64/x64_backend.cc
  src/jit/backend/x64/x64_disassembler.c
  src/jit/backend/x64/x64_dispatch.cc
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
  src/jit/passes/conversion_elimination_pass.c
  src/jit/passes/dead_code_elimination_pass.c
  src/jit/passes/expression_simplification_pass.c
  src/jit/passes/load_store_elimination_pass.c
  src/jit/passes/register_allocation_pass.c
  src/jit/emit_stats.c
  src/jit/jit.c
  src/jit/pass_stats.c
  src/render/gl_backend.c
  src/render/microprofile.cc
  src/render/nuklear.c
  src/sys/exception_handler.c
  src/sys/filesystem.c
  src/sys/memory.c
  src/emulator.c
  src/keycode.c
  src/tracer.c
)

if(PLATFORM_WINDOWS)
  list(APPEND REDREAM_DEFS PLATFORM_WINDOWS=1)
  list(APPEND REDREAM_SOURCES src/sys/exception_handler_win.c)
  list(APPEND REDREAM_SOURCES src/sys/filesystem_win.c)
  list(APPEND REDREAM_SOURCES src/sys/memory_win.c)
  list(APPEND REDREAM_SOURCES src/sys/thread_win.c)
  list(APPEND REDREAM_SOURCES src/sys/time_win.c)
elseif(PLATFORM_DARWIN)
  list(APPEND REDREAM_DEFS PLATFORM_DARWIN=1)
  list(APPEND REDREAM_SOURCES src/sys/exception_handler_mac.c)
  list(APPEND REDREAM_SOURCES src/sys/filesystem_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/memory_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/thread_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/time_mac.c)
elseif(PLATFORM_LINUX)
  list(APPEND REDREAM_DEFS PLATFORM_LINUX=1)
  list(APPEND REDREAM_SOURCES src/sys/exception_handler_linux.c)
  list(APPEND REDREAM_SOURCES src/sys/filesystem_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/memory_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/thread_posix.c)
  list(APPEND REDREAM_SOURCES src/sys/time_linux.c)
endif()

# assign source groups for visual studio projects
source_group_by_dir(REDREAM_SOURCES)

if(COMPILER_GCC OR COMPILER_CLANG)
  list(APPEND REDREAM_FLAGS -fms-extensions -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-strict-aliasing -D_GNU_SOURCE)

  if(COMPILER_GCC)
    # some flavors of gcc require this to be defined for the PR* macros in inttypes.h
    list(APPEND REDREAM_FLAGS ${REDREAM_FLAGS} -D__STDC_FORMAT_MACROS)

    # nuklear triggers this due to some of its asserts
    list(APPEND REDREAM_FLAGS -Wno-unused-but-set-variable)
  elseif(COMPILER_CLANG)
    # clang will warn on '{0}' as -Wmissing-field-initializers even in C code
    # https://llvm.org/bugs/show_bug.cgi?id=21689
    list(APPEND REDREAM_FLAGS -Wno-missing-field-initializers -Wno-missing-braces -Wno-microsoft-anon-tag)
  endif()

  # used by shm_open / shm_unlink on linux
  if(PLATFORM_LINUX)
    list(APPEND REDREAM_LIBS rt)
  endif()
elseif(COMPILER_MSVC)
  list(APPEND REDREAM_FLAGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX /GR- /W3 /WX /wd4100 /wd4127 /wd4505 /wd4512 /wd4800 /wd4351)

  list(APPEND REDREAM_LIBS userenv ws2_32)
endif()

add_library(relib OBJECT ${REDREAM_SOURCES})
target_include_directories(relib SYSTEM PUBLIC ${REDREAM_INCLUDE_DIRS})
target_include_directories(relib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(relib PRIVATE ${REDREAM_DEFS})
target_compile_options(relib PRIVATE ${REDREAM_FLAGS})

#--------------------------------------------------
# redream_libretro
#--------------------------------------------------

if(BUILD_LIBRETRO)

add_library(redream_libretro SHARED $<TARGET_OBJECTS:relib> src/retro_host.c)
set_target_properties(redream_libretro PROPERTIES PREFIX "")
set_target_properties(redream_libretro PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_include_directories(redream_libretro SYSTEM PUBLIC ${REDREAM_INCLUDE_DIRS} deps/libretro/include)
target_include_directories(redream_libretro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(redream_libretro ${REDREAM_LIBS})
target_compile_definitions(redream_libretro PRIVATE ${REDREAM_DEFS})
target_compile_options(redream_libretro PRIVATE ${REDREAM_FLAGS})

# early out, libretro core build shouldn't also build regular binaries as it
# requires PIC code
return()

else()

if(CMAKE_POSITION_INDEPENDENT_CODE)
  message(FATAL "Only the libretro core build should use PIC code")
endif()

endif()

#--------------------------------------------------
# redream
#--------------------------------------------------

add_executable(redream $<TARGET_OBJECTS:relib> src/sdl_host.c)
target_include_directories(redream SYSTEM PUBLIC ${REDREAM_INCLUDE_DIRS})
target_include_directories(redream PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(redream ${REDREAM_LIBS} SDL2main SDL2-static)
target_compile_definitions(redream PRIVATE ${REDREAM_DEFS})
target_compile_options(redream PRIVATE ${REDREAM_FLAGS})

#--------------------------------------------------
# recc
#--------------------------------------------------

if(BUILD_TOOLS)

add_executable(recc $<TARGET_OBJECTS:relib> src/null_host.c tools/recc/main.c)
target_include_directories(recc SYSTEM PUBLIC ${REDREAM_INCLUDE_DIRS})
target_include_directories(recc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(recc ${REDREAM_LIBS})
target_compile_definitions(recc PRIVATE ${REDREAM_DEFS} MICROPROFILE_ENABLED=0)
target_compile_options(recc PRIVATE ${REDREAM_FLAGS})

add_executable(retrace $<TARGET_OBJECTS:relib>
  src/null_host.c
  tools/retrace/depth.c
  tools/retrace/main.c)
target_include_directories(retrace SYSTEM PUBLIC ${REDREAM_INCLUDE_DIRS})
target_include_directories(retrace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retrace ${REDREAM_LIBS})
target_compile_definitions(retrace PRIVATE ${REDREAM_DEFS} MICROPROFILE_ENABLED=0)
target_compile_options(retrace PRIVATE ${REDREAM_FLAGS})

endif()

#--------------------------------------------------
# retest
#--------------------------------------------------

if(BUILD_TESTS)

# compile master .inc file from .s files in test/asm
set(TEST_ASM
  test/asm/add.s
  test/asm/addc.s
  test/asm/addv.s
  test/asm/and.s
  test/asm/bf.s
  test/asm/bra.s
  test/asm/braf.s
  test/asm/bsr.s
  test/asm/bsrf.s
  test/asm/bt.s
  test/asm/cmp.s
  test/asm/div0.s
  test/asm/div1s.s
  test/asm/div1u.s
  test/asm/dmul.s
  test/asm/dt.s
  test/asm/ext.s
  test/asm/fabs.s
  test/asm/fadd.s
  test/asm/fcmpeq.s
  test/asm/fcmpgt.s
  test/asm/fcnv.s
  test/asm/fdiv.s
  test/asm/fipr.s
  test/asm/fld.s
  test/asm/float.s
  test/asm/fmac.s
  test/asm/fmov.s
  test/asm/fmov_load.s
  test/asm/fmov_index_load.s
  test/asm/fmov_store.s
  test/asm/fmov_index_store.s
  test/asm/fmov_save.s
  test/asm/fmov_restore.s
  test/asm/fmul.s
  test/asm/fneg.s
  test/asm/frchg.s
  test/asm/fsca.s
  test/asm/fschg.s
  test/asm/fsrra.s
  test/asm/fsqrt.s
  test/asm/fsub.s
  test/asm/ftrc.s
  test/asm/ftrv.s
  test/asm/jmp.s
  test/asm/jsr.s
  test/asm/ldc.s
  test/asm/ldcl.s
  test/asm/lds.s
  test/asm/ldsl.s
  test/asm/mova.s
  test/asm/movb.s
  test/asm/movl.s
  test/asm/movt.s
  test/asm/movw.s
  test/asm/mul.s
  test/asm/neg.s
  test/asm/negc.s
  test/asm/not.s
  test/asm/or.s
  test/asm/rot.s
  test/asm/sha.s
  test/asm/shl.s
  test/asm/sub.s
  test/asm/subc.s
  test/asm/subv.s
  test/asm/swap.s
  test/asm/tst.s
  test/asm/xor.s
  )
set(asm_inc ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sh4.inc)

# compile tests into include file if the sh4 toolchain is available
find_package(PythonInterp)
find_program(SH_AS NAMES sh-elf-as)
find_program(SH_LD NAMES sh-elf-ld)
find_program(SH_NM NAMES sh-elf-nm)
find_program(SH_OBJCOPY NAMES sh-elf-objcopy)
if(NOT PYTHONINTERP_FOUND)
  message(WARNING "Could not find python interpreter, won't be able to generate tests")
elseif(NOT SH_AS)
  message(WARNING "Could not find sh-elf-as, won't be able to generate tests")
elseif(NOT SH_LD)
  message(WARNING "Could not find sh-elf-ld, won't be able to generate tests")
elseif(NOT SH_NM)
  message(WARNING "Could not find sh-elf-nm, won't be able to generate tests")
elseif(NOT SH_OBJCOPY)
  message(WARNING "Could not find sh-elf-objcopy, won't be able to generate tests")
else()
  add_custom_command(OUTPUT ${asm_inc}
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sh4.py -as ${SH_AS} -ld ${SH_LD} -nm ${SH_NM} -objcopy ${SH_OBJCOPY} -o ${asm_inc} ${TEST_ASM}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sh4.py ${TEST_ASM}
    COMMENT "Assembling ${asm_inc} for ${TEST_ASM}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM)
endif()

# build test binary
set(RETEST_SOURCES
  src/null_host.c
  test/test_dead_code_elimination.c
  test/test_interval_tree.c
  test/test_list.c
  test/test_load_store_elimination.c
  test/test_sh4.c
  ${asm_inc}
  test/retest.c)

add_executable(retest $<TARGET_OBJECTS:relib> ${RETEST_SOURCES})
target_include_directories(retest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/test ${REDREAM_INCLUDE_DIRS})
target_link_libraries(retest ${REDREAM_LIBS})
target_compile_definitions(retest PRIVATE ${REDREAM_DEFS})
target_compile_options(retest PRIVATE ${REDREAM_FLAGS})

endif()
