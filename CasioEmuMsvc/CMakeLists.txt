cmake_minimum_required(VERSION 3.10...3.22)

project(CasioEmuMsvc)
#set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fms-extensions")
# set(CMAKE_CXX_EXTENSIONS OFF)
set(ASMJIT_STATIC ON CACHE BOOL "Build AsmJit as a static library")
set(ASMJIT_BUILD_TEST OFF CACHE BOOL "Disable AsmJit tests")
set(ASMJIT_BUILD_EXAMPLES OFF CACHE BOOL "Disable AsmJit examples")

message(STATUS "Attempting to add subdirectory: asmjit")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asmjit/CMakeLists.txt")
    message(STATUS "Found asmjit/CMakeLists.txt")
    add_subdirectory(asmjit)
    message(STATUS "Finished add_subdirectory(asmjit)")
else()
    message(FATAL_ERROR "Could not find asmjit/CMakeLists.txt at ${CMAKE_CURRENT_SOURCE_DIR}/asmjit")
endif()

if(TARGET asmjit::asmjit)
    message(STATUS "Target asmjit::asmjit FOUND after add_subdirectory")
else()
    message(WARNING "Target asmjit::asmjit NOT FOUND after add_subdirectory")
endif()

include_directories(#${CMAKE_SOURCE_DIR}/SDL_image/include
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext/cam
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Dcl
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Chipset
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Containers
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/imgui
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/GDB
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Peripheral
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Plugin
        ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/StartupUi)
        
# find_library(SDL2 SDL2)

# find_library(SDL2_image SDL2_image)

# add_library(main SHARED)

# Search for all .cpp files in the CasioEmuMsvc directory 
# file(GLOB_RECURSE SOURCES ${CMAKE_SOURCE_DIR}/CasioEmuMsvc/*.cpp)

# Use individual directories instead of all to avoid asmjit's file testing
file(GLOB MAIN_SOURCES_ROOT "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/*.cpp")
file(GLOB MAIN_SOURCES_ROOT_C "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/*.cpp")
file(GLOB MAIN_SOURCES_CHIPSET "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Chipset/*.cpp")
file(GLOB MAIN_SOURCES_CRASH "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/CrashHandler/*.cpp")
file(GLOB MAIN_SOURCES_EXT "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext/*.cpp")
file(GLOB MAIN_SOURCES_EXT_CAM "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Ext/cam/*.cc")
file(GLOB MAIN_SOURCES_GDB "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/GDB/*.cpp")
file(GLOB MAIN_SOURCES_GUI "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/*.cpp")
file(GLOB MAIN_SOURCES_IMGUI "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Gui/imgui/*.cpp")
file(GLOB MAIN_SOURCES_JIT "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/JitU8/*.cpp")
file(GLOB MAIN_SOURCES_PERIPHERAL "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Peripheral/*.cpp")
file(GLOB MAIN_SOURCES_PLUGIN "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/Plugin/*.cpp")
file(GLOB MAIN_SOURCES_STARTUP "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/StartupUi/*.cpp")

set(MAIN_SOURCES
    ${MAIN_SOURCES_ROOT}
    ${MAIN_SOURCES_ROOT_C}
    ${MAIN_SOURCES_CHIPSET}
    ${MAIN_SOURCES_CRASH}
    ${MAIN_SOURCES_EXT}
    ${MAIN_SOURCES_EXT_CAM}
    ${MAIN_SOURCES_GDB}
    ${MAIN_SOURCES_GUI}
    ${MAIN_SOURCES_IMGUI}
    ${MAIN_SOURCES_JIT}
    ${MAIN_SOURCES_PERIPHERAL}
    ${MAIN_SOURCES_PLUGIN}
    ${MAIN_SOURCES_STARTUP}
)

# target_sources(main PRIVATE ${MAIN_SOURCES})

if(BUILD_EXECUTABLE)
    message(STATUS "Building executable")
    set(TARGET_NAME "CasioEmuMsvc")
    add_executable(${TARGET_NAME} ${MAIN_SOURCES})
else()
    message(STATUS "Building shared library")
    set(TARGET_NAME "main")
    add_library(${TARGET_NAME} SHARED ${MAIN_SOURCES})
endif()

target_compile_definitions(${TARGET_NAME} PRIVATE ASMJIT_STATIC)

if(BUILD_EXECUTABLE)
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image-static
        CpuFeatures::cpu_features
        asmjit::asmjit
        -lpthread
        -ldl
        -lrt
        -lm
)
else()
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        CpuFeatures::cpu_features
        asmjit::asmjit
    )
    if(ANDROID)
        target_link_libraries(${TARGET_NAME} PRIVATE log)
    endif()
endif()

#if(BUILD_EXECUTABLE AND NOT ANDROID)
#    target_link_options(${TARGET_NAME} PRIVATE "-static")
#endif()
