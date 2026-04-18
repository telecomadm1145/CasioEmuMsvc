cmake_minimum_required(VERSION 3.10...3.22)
project(ExamplePlugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

file(GLOB SOURCES "*.cpp")

set(IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/../CasioEmuMsvc/Gui/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/../CasioEmuMsvc/Gui/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/../CasioEmuMsvc/Gui/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/../CasioEmuMsvc/Gui/imgui/imgui_widgets.cpp
)

add_library(ExamplePlugin SHARED ${SOURCES} ${IMGUI_SOURCES})

target_include_directories(ExamplePlugin PRIVATE
    .
    ${CMAKE_SOURCE_DIR}/../CasioEmuMsvc/Gui/imgui
)

target_compile_definitions(ExamplePlugin PRIVATE _USRDLL CASIOEMU_PLUGIN_API)

if(WIN32)
    target_compile_definitions(ExamplePlugin PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(ExamplePlugin PRIVATE
    imm32
    winmm
    version
)

target_link_options(ExamplePlugin PRIVATE "-static-libgcc" "-static-libstdc++" "-static" "-Wl,--unresolved-symbols=ignore-all")