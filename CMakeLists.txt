cmake_minimum_required(VERSION 2.8.12)

include(ExternalProject)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

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
list(APPEND DREAVM_INCLUDE_DIRS deps/beaengine-4.1/include)
list(APPEND DREAVM_LIBS beaengine)
target_include_directories(beaengine PUBLIC deps/beaengine-4.1/include)

# eigen
list(APPEND DREAVM_DEFS EIGEN_DEFAULT_TO_ROW_MAJOR)
list(APPEND DREAVM_INCLUDE_DIRS deps/eigen-3.2.1)

# gflags / glog
set(GFLAGS_NAMESPACE "google" CACHE STRING "")
add_subdirectory(deps/gflags-2.1.2 EXCLUDE_FROM_ALL)
list(APPEND DREAVM_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/deps/gflags-2.1.2/include)

ExternalProject_Add(glog-external
  DEPENDS gflags
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/glog-0.3.3
  BINARY_DIR ${CMAKE_BINARY_DIR}/deps/glog-0.3.3
  CONFIGURE_COMMAND ${CMAKE_SOURCE_DIR}/deps/glog-0.3.3/configure CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} --disable-shared --with-gflags=${CMAKE_BINARY_DIR}/deps/gflags-2.1.2
  BUILD_COMMAND ${MAKE}
  INSTALL_COMMAND "")
add_library(glog UNKNOWN IMPORTED)
set_property(TARGET glog PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/deps/glog-0.3.3/.libs/libglog.a)
add_dependencies(glog glog-external)
list(APPEND DREAVM_INCLUDE_DIRS deps/glog-0.3.3/src ${CMAKE_BINARY_DIR}/deps/glog-0.3.3/src)

# glog depends on gflags, it must be linked first
list(APPEND DREAVM_LIBS glog gflags_nothreads-static)

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
  test/asm/dmuls.s
  test/asm/dmulu.s
  test/asm/dt.s
  test/asm/ext.s
  test/asm/fabsd.s
  test/asm/fabsf.s
  test/asm/faddd.s
  test/asm/faddf.s
  test/asm/fcmpeqd.s
  test/asm/fcmpeqf.s
  test/asm/fcmpgtd.s
  test/asm/fcmpgtf.s
  test/asm/fdivd.s
  test/asm/fdivf.s
  test/asm/fipr.s
  test/asm/fld.s
  test/asm/floatd.s
  test/asm/floatf.s
  test/asm/fmac.s
  test/asm/fmovd.s
  test/asm/fmovf.s
  test/asm/fmovsz.s
  test/asm/fmuld.s
  test/asm/fmulf.s
  test/asm/fnegd.s
  test/asm/fnegf.s
  test/asm/frchg.s
  test/asm/fsca.s
  test/asm/fschg.s
  test/asm/fsrra.s
  test/asm/fsqrtd.s
  test/asm/fsqrtf.s
  test/asm/fsubd.s
  test/asm/fsubf.s
  test/asm/ftrcd.s
  test/asm/ftrcf.s
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
add_custom_command(OUTPUT ${asm_inc}
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/sh4_test.py -o ${asm_inc} ${TEST_ASM}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/sh4_test.py ${TEST_ASM}
  COMMENT "Assembling ${asm_inc} for ${TEST_ASM}"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

# build gtest binary
set(DREAVM_TEST_SOURCES
  src/core/filesystem.cc
  src/cpu/backend/interpreter/interpreter_backend.cc
  src/cpu/backend/interpreter/interpreter_block.cc
  src/cpu/backend/interpreter/interpreter_callbacks.cc
  src/cpu/backend/x64/x64_backend.cc
  src/cpu/backend/x64/x64_block.cc
  src/cpu/backend/x64/x64_emitter.cc
  src/cpu/frontend/sh4/sh4_builder.cc
  src/cpu/frontend/sh4/sh4_emit.cc
  src/cpu/frontend/sh4/sh4_frontend.cc
  src/cpu/frontend/sh4/sh4_instr.cc
  src/cpu/ir/ir_builder.cc
  src/cpu/ir/passes/constant_propagation_pass.cc
  src/cpu/ir/passes/context_promotion_pass.cc
  src/cpu/ir/passes/control_flow_analysis_pass.cc
  src/cpu/ir/passes/pass_runner.cc
  src/cpu/ir/passes/register_allocation_pass.cc
  src/cpu/ir/passes/validate_pass.cc
  src/cpu/sh4.cc
  src/cpu/sh4_context.cc
  src/cpu/runtime.cc
  src/emu/profiler.cc
  src/emu/scheduler.cc
  test/sh4_test.cc
  test/test_intrusive_list.cc
  test/test_memory.cc
  test/test_ring_buffer.cc
  test/test_sh4.cc
  ${asm_inc})

add_executable(dreavm_gtest ${DREAVM_TEST_SOURCES})
target_include_directories(dreavm_gtest PUBLIC deps/gtest-1.7.0/include src/ test/ ${DREAVM_INCLUDE_DIRS})
target_link_libraries(dreavm_gtest gtest gtest_main ${DREAVM_LIBS})
if(NOT MSVC)
  target_compile_options(dreavm_gtest PRIVATE -std=c++11 -fno-operator-names -fno-rtti -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-parameter -Wno-unused-private-field -Wno-missing-field-initializers -Wno-strict-aliasing)
else()
  target_compile_options(dreavm_gtest PRIVATE -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()


#--------------------------------------------------
# emulator
#--------------------------------------------------

set(DREAVM_SOURCES
  src/core/filesystem.cc
  src/cpu/backend/interpreter/interpreter_backend.cc
  src/cpu/backend/interpreter/interpreter_block.cc
  src/cpu/backend/interpreter/interpreter_callbacks.cc
  src/cpu/backend/x64/x64_backend.cc
  src/cpu/backend/x64/x64_block.cc
  src/cpu/backend/x64/x64_emitter.cc
  src/cpu/frontend/sh4/sh4_builder.cc
  src/cpu/frontend/sh4/sh4_emit.cc
  src/cpu/frontend/sh4/sh4_frontend.cc
  src/cpu/frontend/sh4/sh4_instr.cc
  src/cpu/ir/ir_builder.cc
  src/cpu/ir/passes/constant_propagation_pass.cc
  src/cpu/ir/passes/context_promotion_pass.cc
  src/cpu/ir/passes/control_flow_analysis_pass.cc
  src/cpu/ir/passes/pass_runner.cc
  src/cpu/ir/passes/register_allocation_pass.cc
  src/cpu/ir/passes/validate_pass.cc
  src/cpu/sh4.cc
  src/cpu/sh4_context.cc
  src/cpu/runtime.cc
  src/emu/disc.cc
  src/emu/emulator.cc
  src/emu/profiler.cc
  src/emu/scheduler.cc
  src/holly/gdrom.cc
  src/holly/holly.cc
  src/holly/maple.cc
  src/holly/maple_controller.cc
  src/holly/pvr2.cc
  src/holly/tile_accelerator.cc
  src/holly/tile_renderer.cc
  src/renderer/gl_backend.cc
  src/renderer/gl_shader.cc
  src/system/keys.cc
  src/system/system.cc
  src/trace/trace.cc
  src/trace/trace_viewer.cc
  src/main.cc)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  set(DREAVM_COMPILE_FLAGS -std=c++11 -fno-operator-names -fno-rtti -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-local-typedef -Wno-unused-parameter -Wno-unused-private-field -Wno-missing-field-initializers -Wno-strict-aliasing)

  string(TOLOWER ${CMAKE_BUILD_TYPE} BUILD_TYPE)

  if(BUILD_TYPE STREQUAL "debug")
    set(DREAVM_COMPILE_FLAGS ${DREAVM_COMPILE_FLAGS} -fno-omit-frame-pointer)
  endif()
else()
  set(DREAVM_COMPILE_FLAGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX /W2 /WX /wd4100 /wd4127 /wd4505 /wd4512 /wd4800 /wd4351 )
endif()

add_executable(dreavm ${DREAVM_SOURCES})
target_include_directories(dreavm SYSTEM PUBLIC ${DREAVM_INCLUDE_DIRS})
target_include_directories(dreavm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dreavm ${DREAVM_LIBS})
target_compile_definitions(dreavm PRIVATE ${DREAVM_DEFS})
target_compile_options(dreavm PRIVATE ${DREAVM_COMPILE_FLAGS})
