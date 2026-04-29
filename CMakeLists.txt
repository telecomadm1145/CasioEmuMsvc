cmake_minimum_required(VERSION 3.10...3.22)

project(GAME)

set(SDL_TEST OFF CACHE BOOL "Disable building the SDL_test library" FORCE)
option(BUILD_EXECUTABLE "Build a statically linked executable" OFF)

if(NOT DEFINED BUILD_EXECUTABLE)
    if(ANDROID OR IOS)
        set(BUILD_EXECUTABLE OFF)
        message(STATUS "Auto-setting BUILD_EXECUTABLE: OFF (Mobile platform)")
    else()
        set(BUILD_EXECUTABLE ON)
        message(STATUS "Auto-setting BUILD_EXECUTABLE: ON (Desktop platform)")
    endif()
endif()

set(BUILD_EXECUTABLE ${BUILD_EXECUTABLE} CACHE BOOL "Build the project as an executable")

if(BUILD_EXECUTABLE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    message(STATUS "Executable build enabled. Subprojects will be built as static libraries.")
else()
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
endif()

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(MSVC)
    if(BUILD_EXECUTABLE)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

# armeabi-v7a requires cpufeatures library
#include(AndroidNdkModules)
#android_ndk_import_module_cpufeatures()


# SDL sources are in a subfolder named "SDL"
add_subdirectory(SDL)

# Compilation of companion libraries
set(SDL2IMAGE_AVIF OFF CACHE BOOL "Disable AVIF" FORCE)
set(SDL2IMAGE_WEBP OFF CACHE BOOL "Disable WEBP" FORCE)
set(SDL2IMAGE_JXL OFF CACHE BOOL "Disable JXL" FORCE)
set(SDL2IMAGE_TIF OFF CACHE BOOL "Disable TIFF" FORCE)
add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

#set(TEMP_BUILD_EXECUTABLE ${BUILD_EXECUTABLE})
#set(BUILD_EXECUTABLE OFF CACHE BOOL "" FORCE)
#add_subdirectory(cpu_features)
#set(BUILD_EXECUTABLE ${TEMP_BUILD_EXECUTABLE} CACHE BOOL "" FORCE)

# add_subdirectory(lua)

if(NOT ANDROID AND NOT IOS)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable discord-rpc examples" FORCE)
    add_subdirectory(discord-rpc)
endif()

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(CasioEmuMsvc)