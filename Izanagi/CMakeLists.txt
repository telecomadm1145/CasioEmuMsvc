cmake_minimum_required(VERSION 3.10...3.22)

project(Izanagi)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/imgui
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext
    ${CMAKE_CURRENT_SOURCE_DIR}
)

file(GLOB SOURCES "*.cpp")
file(GLOB IMGUI_SOURCES "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/imgui/*.cpp")

add_executable(Izanagi ${SOURCES} ${IMGUI_SOURCES})

target_link_libraries(Izanagi PRIVATE
    SDL2::SDL2
    SDL2::image
)

if(UNIX AND NOT APPLE)
    target_link_libraries(Izanagi PRIVATE -lpthread -ldl -lrt -lm)
endif()
