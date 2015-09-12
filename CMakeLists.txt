cmake_minimum_required(VERSION 2.8.12)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ExternalProject)
include(SourceGroupByDir)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE "Release")
endif()

project(dreavm)
set(DREAVM_VERSION_MAJOR 0)
set(DREAVM_VERSION_MINOR 1)
set(DREAVM_VERSION_PATCH 0)
set(DREAVM_VERSION ${DREAVM_VERSION_MAJOR}.${DREAVM_VERSION_MINOR}.${DREAVM_VERSION_PATCH})


#--------------------------------------------------
# dynamic libs
#--------------------------------------------------

# opengl
find_package(OpenGL REQUIRED)
list(APPEND DREAVM_INCLUDE_DIRS ${OPENGL_INCLUDE_DIR})
list(APPEND DREAVM_LIBS ${OPENGL_LIBRARIES})


#--------------------------------------------------
# static libs
#--------------------------------------------------

# sdl2
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
endif(APPLE)
add_subdirectory(deps/sdl2-2.0.3 EXCLUDE_FROM_ALL)
list(APPEND DREAVM_INCLUDE_DIRS deps/sdl2-2.0.3/include)
list(APPEND DREAVM_LIBS SDL2main SDL2-static)

# beaengine
add_library(beaengine STATIC deps/beaengine-4.1/beaengineSources/BeaEngine.c)
target_include_directories(beaengine PUBLIC deps/beaengine-4.1/include)
list(APPEND DREAVM_DEFS BEA_ENGINE_STATIC)
list(APPEND DREAVM_INCLUDE_DIRS deps/beaengine-4.1/include)
list(APPEND DREAVM_LIBS beaengine)

# eigen
list(APPEND DREAVM_DEFS EIGEN_DEFAULT_TO_ROW_MAJOR)
list(APPEND DREAVM_INCLUDE_DIRS deps/eigen-3.2.1)

# gflags
set(GFLAGS_NAMESPACE "google" CACHE STRING "")
add_subdirectory(deps/gflags-2.1.2 EXCLUDE_FROM_ALL)
list(APPEND DREAVM_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/deps/gflags-2.1.2/include)
list(APPEND DREAVM_LIBS gflags_nothreads-static)

# glew
set(BUILD_UTILS OFF CACHE BOOL "")
add_subdirectory(deps/glew-1.13.0/build/cmake EXCLUDE_FROM_ALL)
list(APPEND DREAVM_INCLUDE_DIRS deps/glew-1.13.0/include)
list(APPEND DREAVM_LIBS glew_s)

# json11
add_library(json11 STATIC deps/json11/json11.cpp)
list(APPEND DREAVM_INCLUDE_DIRS deps/json11)
list(APPEND DREAVM_LIBS json11)
target_compile_options(json11 PRIVATE -std=c++11)

# microprofile
list(APPEND DREAVM_INCLUDE_DIRS deps/microprofile)

# stb_truetype
list(APPEND DREAVM_INCLUDE_DIRS deps/stb_truetype-1.0.5)

# xbyak
list(APPEND DREAVM_INCLUDE_DIRS deps/xbyak-4.85)


#--------------------------------------------------
# format
#--------------------------------------------------
find_package(ClangFormat REQUIRED)

file(GLOB_RECURSE CLANG_FORMAT_ARGS "src/*.cc" "src/*.h" "test/*.cc" "test/*.h")

add_custom_target(format ${CLANG_FORMAT_EXECUTABLE} -style=Google -i ${CLANG_FORMAT_ARGS})


#--------------------------------------------------
# emulator
#--------------------------------------------------

set(DREAVM_SOURCES
  src/core/assert.cc
  src/core/filesystem.cc
  src/core/log.cc
  src/emu/emulator.cc
  src/emu/profiler.cc
  src/hw/aica/aica.cc
  src/hw/gdrom/disc.cc
  src/hw/gdrom/gdrom.cc
  src/hw/holly/holly.cc
  src/hw/holly/pvr2.cc
  src/hw/holly/tile_accelerator.cc
  src/hw/holly/tile_renderer.cc
  src/hw/holly/tile_texture_cache.cc
  src/hw/maple/maple.cc
  src/hw/maple/maple_controller.cc
  src/hw/sh4/sh4.cc
  src/hw/dreamcast.cc
  src/hw/scheduler.cc
  src/jit/backend/interpreter/interpreter_backend.cc
  src/jit/backend/interpreter/interpreter_block.cc
  src/jit/backend/interpreter/interpreter_callbacks.cc
  src/jit/backend/x64/x64_backend.cc
  src/jit/backend/x64/x64_block.cc
  src/jit/backend/x64/x64_emitter.cc
  src/jit/frontend/sh4/sh4_builder.cc
  src/jit/frontend/sh4/sh4_context.cc
  src/jit/frontend/sh4/sh4_emit.cc
  src/jit/frontend/sh4/sh4_frontend.cc
  src/jit/frontend/sh4/sh4_instr.cc
  src/jit/ir/ir_builder.cc
  src/jit/ir/passes/constant_propagation_pass.cc
  src/jit/ir/passes/context_promotion_pass.cc
  src/jit/ir/passes/control_flow_analysis_pass.cc
  src/jit/ir/passes/pass_runner.cc
  src/jit/ir/passes/register_allocation_pass.cc
  src/jit/ir/passes/validate_pass.cc
  src/jit/runtime.cc
  src/renderer/gl_backend.cc
  src/renderer/gl_shader.cc
  src/sigsegv/sigsegv_handler.cc
  src/system/keys.cc
  src/system/system.cc
  src/trace/trace.cc
  src/trace/trace_viewer.cc
  src/main.cc)

if(WIN32)
  list(APPEND DREAVM_SOURCES src/sigsegv/sigsegv_handler_win.cc)
elseif(APPLE)
  list(APPEND DREAVM_SOURCES src/sigsegv/sigsegv_handler_mac.cc)
else()
  list(APPEND DREAVM_SOURCES src/sigsegv/sigsegv_handler_linux.cc)
endif()


# assign source groups for visual studio projects
source_group_by_dir(DREAVM_SOURCES)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  set(DREAVM_COMPILE_FLAGS -std=c++11 -fno-operator-names -fno-rtti -Wall -Wextra -Werror -Wno-unused-parameter -Wno-strict-aliasing)

  string(TOLOWER ${CMAKE_BUILD_TYPE} BUILD_TYPE)

  if(BUILD_TYPE STREQUAL "debug")
    set(DREAVM_COMPILE_FLAGS ${DREAVM_COMPILE_FLAGS} -fno-omit-frame-pointer)
  endif()
else()
  set(DREAVM_COMPILE_FLAGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX /GR- /W3 /WX /wd4100 /wd4127 /wd4505 /wd4512 /wd4800 /wd4351)

  list(APPEND DREAVM_LIBS userenv)
endif()

add_executable(dreavm ${DREAVM_SOURCES})
target_include_directories(dreavm SYSTEM PUBLIC ${DREAVM_INCLUDE_DIRS})
target_include_directories(dreavm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dreavm ${DREAVM_LIBS})
target_compile_definitions(dreavm PRIVATE ${DREAVM_DEFS})
target_compile_options(dreavm PRIVATE ${DREAVM_COMPILE_FLAGS})


#--------------------------------------------------
# tests
#--------------------------------------------------

set(gtest_force_shared_crt ON CACHE BOOL "")
add_subdirectory(deps/gtest-1.7.0 EXCLUDE_FROM_ALL)

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
  test/asm/fdiv.s
  test/asm/fipr.s
  test/asm/fld.s
  test/asm/float.s
  test/asm/fmac.s
  test/asm/fmovd.s
  test/asm/fmovf.s
  test/asm/fmovsz.s
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
set(asm_inc ${CMAKE_CURRENT_SOURCE_DIR}/test/asm/sh4_test.inc)

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
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/sh4_test.py -as ${SH_AS} -ld ${SH_LD} -nm ${SH_NM} -objcopy ${SH_OBJCOPY} -o ${asm_inc} ${TEST_ASM}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/sh4_test.py ${TEST_ASM}
    COMMENT "Assembling ${asm_inc} for ${TEST_ASM}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM)
endif()

# build test binary
set(DREAVM_TEST_SOURCES
  ${DREAVM_SOURCES}
  test/sh4_test.cc
  test/test_interval_tree.cc
  test/test_intrusive_list.cc
  test/test_ring_buffer.cc
  test/test_sh4.cc
  ${asm_inc})
list(REMOVE_ITEM DREAVM_TEST_SOURCES src/main.cc)

# assign source groups for visual studio projects
source_group_by_dir(DREAVM_TEST_SOURCES)

add_executable(dreavm_test ${DREAVM_TEST_SOURCES})
target_include_directories(dreavm_test PUBLIC deps/gtest-1.7.0/include src/ test/ ${DREAVM_INCLUDE_DIRS})
target_link_libraries(dreavm_test gtest gtest_main ${DREAVM_LIBS})
target_compile_definitions(dreavm_test PRIVATE ${DREAVM_DEFS})
target_compile_options(dreavm_test PRIVATE ${DREAVM_COMPILE_FLAGS})
