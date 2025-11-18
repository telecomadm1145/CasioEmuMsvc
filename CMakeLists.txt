cmake_minimum_required(VERSION 3.10...3.22)

project(GAME)

set(SDL_TEST OFF CACHE BOOL "Disable building the SDL_test library" FORCE)
option(BUILD_EXECUTABLE "Build a statically linked executable" OFF)

if(BUILD_EXECUTABLE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    message(STATUS "Executable build enabled. Subprojects will be built as static libraries.")
else()
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
endif()

# armeabi-v7a requires cpufeatures library
#include(AndroidNdkModules)
#android_ndk_import_module_cpufeatures()


# SDL sources are in a subfolder named "SDL"
add_subdirectory(SDL)

# Compilation of companion libraries
add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

add_subdirectory(cpu_features)

# add_subdirectory(lua)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(CasioEmuMsvc)