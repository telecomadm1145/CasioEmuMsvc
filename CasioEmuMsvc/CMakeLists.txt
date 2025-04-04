cmake_minimum_required(VERSION 3.6)

project(MY_APP)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fms-extensions")
# set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(#${CMAKE_SOURCE_DIR}/SDL_image/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/Ext
        ${CMAKE_SOURCE_DIR}/src/Dcl
        ${CMAKE_SOURCE_DIR}/src/Chipset
        ${CMAKE_SOURCE_DIR}/src/Containers
        ${CMAKE_SOURCE_DIR}/src/Gui
        ${CMAKE_SOURCE_DIR}/src/Gui/imgui
        ${CMAKE_SOURCE_DIR}/src/Peripheral
        ${CMAKE_SOURCE_DIR}/src/StartupUi)

find_library(SDL2 SDL2)

find_library(SDL2_image SDL2_image)

add_library(main SHARED)

# 搜索 src 目录下的所有 .cpp 文件 
file(GLOB_RECURSE SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

target_sources(main PRIVATE ${SOURCES})

target_link_libraries(main SDL2 SDL2_image)


