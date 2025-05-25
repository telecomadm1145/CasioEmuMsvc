cmake_minimum_required(VERSION 3.6)

project(MY_APP)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fms-extensions")
# set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(#${CMAKE_SOURCE_DIR}/SDL_image/include
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Dcl
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Chipset
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Containers
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/imgui
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Peripheral
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/StartupUi)
        
find_library(SDL2 SDL2)

find_library(SDL2_image SDL2_image)

add_library(main SHARED)

# Search for all .cpp files in the CasioEmuMsvc directory 
file(GLOB_RECURSE SOURCES ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/*.cpp)

target_sources(main PRIVATE ${SOURCES})

target_link_libraries(main SDL2 SDL2_image)