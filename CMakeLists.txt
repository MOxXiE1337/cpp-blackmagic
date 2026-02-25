cmake_minimum_required (VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("cpp-blackmagic")

option(BUILD_EXAMPLES "Build cpp-blackmagic examples." ON)
option(BUILD_TESTS "Build cpp-blackmagic benchmark/test executables (compile only)." ON)

option(BUILD_WINDOWS_X86 "Build Win32 (x86) variant." OFF)
option(BUILD_WINDOWS_X86_64 "Build Win64 (x64) variant." OFF)

option(BUILD_LINUX_X86 "Build Linux (x86) variant." OFF)
option(BUILD_LINUX_X86_64 "Build Linux (x86_64) variant." OFF)
option(BUILD_LINUX_ARM "Build Linux (arm32) variant." OFF)
option(BUILD_LINUX_ARM64 "Build Linux (arm64) variant." OFF)

option(BUILD_ANDROID_X86 "Build Android (x86) variant." OFF)
option(BUILD_ANDROID_X86_64 "Build Android (x86_64) variant." OFF)
option(BUILD_ANDROID_ARM "Build Android (arm32) variant." OFF)
option(BUILD_ANDROID_ARM64 "Build Android (arm64) variant." OFF)

add_subdirectory(cpp-blackmagic)

if (BUILD_EXAMPLES)
  add_subdirectory(cpp-blackmagic/examples)
endif ()

if (BUILD_TESTS)
  add_subdirectory(cpp-blackmagic/tests)
endif ()
