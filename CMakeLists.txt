cmake_minimum_required(VERSION 3.10...3.22)

project(GAME)

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