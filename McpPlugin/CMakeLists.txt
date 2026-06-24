cmake_minimum_required(VERSION 3.10...3.22)

project(CasioEmuMsvcMcpPlugin LANGUAGES CXX)

add_library(CasioEmuMsvcMcpPlugin SHARED
    dllmain.cpp
    pch.cpp
)

set_target_properties(CasioEmuMsvcMcpPlugin PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    PREFIX ""
    OUTPUT_NAME "CasioEmuMsvc.Plugin.McpPlugin"
)

target_compile_definitions(CasioEmuMsvcMcpPlugin PRIVATE
    CASIOEMU_PLUGIN_API
    CASIOEMU_HEADLESS_PLUGIN
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(CasioEmuMsvcMcpPlugin PRIVATE /utf-8 /bigobj)

target_include_directories(CasioEmuMsvcMcpPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/src
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/src/Gui/imgui
    ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/src/Plugin
)

target_link_libraries(CasioEmuMsvcMcpPlugin PRIVATE ws2_32)

add_dependencies(CasioEmuMsvcMcpPlugin CasioEmuMsvc)

add_custom_command(TARGET CasioEmuMsvcMcpPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:CasioEmuMsvcMcpPlugin>"
        "$<TARGET_FILE_DIR:CasioEmuMsvc>"
    COMMENT "Copying MCP plugin next to CasioEmuMsvc"
)
