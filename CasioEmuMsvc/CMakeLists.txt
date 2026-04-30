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

if(NOT ANDROID AND NOT IOS)
    set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "Disable Sentry shared lib" FORCE)
    set(SENTRY_BUILD_RUNTIMESTATIC ${BUILD_EXECUTABLE} CACHE BOOL "Static runtime for Sentry (MSVC /MT)" FORCE)
    set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Disable Sentry tests" FORCE)
    set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Disable Sentry examples" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/sentry-native sentry-native-build)
endif()

# Collect project include directories into a variable; will be applied to the target only (not globally)
set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Ext
    ${CMAKE_CURRENT_SOURCE_DIR}/Ext/cam
    ${CMAKE_CURRENT_SOURCE_DIR}/Dcl
    ${CMAKE_CURRENT_SOURCE_DIR}/Chipset
    ${CMAKE_CURRENT_SOURCE_DIR}/Containers
    ${CMAKE_CURRENT_SOURCE_DIR}/Gui
    ${CMAKE_CURRENT_SOURCE_DIR}/Gui/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/GDB
    ${CMAKE_CURRENT_SOURCE_DIR}/Rop
    ${CMAKE_CURRENT_SOURCE_DIR}/Peripheral
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/StartupUi
)

# find_library(SDL2 SDL2)

# find_library(SDL2_image SDL2_image)

# add_library(main SHARED)

# Search for all .cpp files in the CasioEmuMsvc directory 
# file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Use individual directories instead of all to avoid asmjit's file testing
file(GLOB MAIN_SOURCES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB MAIN_SOURCES_ROOT_C "${CMAKE_SOURCE_DIR}/CasioEmuMsvc/*.c")
file(GLOB MAIN_SOURCES_CHIPSET "${CMAKE_CURRENT_SOURCE_DIR}/Chipset/*.cpp")
file(GLOB MAIN_SOURCES_CRASH "${CMAKE_CURRENT_SOURCE_DIR}/CrashHandler/*.cpp")
file(GLOB MAIN_SOURCES_EXT "${CMAKE_CURRENT_SOURCE_DIR}/Ext/*.cpp")
file(GLOB MAIN_SOURCES_EXT_CAM "${CMAKE_CURRENT_SOURCE_DIR}/Ext/cam/*.cc")
file(GLOB MAIN_SOURCES_GDB "${CMAKE_CURRENT_SOURCE_DIR}/GDB/*.cpp")
file(GLOB MAIN_SOURCES_ROP "${CMAKE_CURRENT_SOURCE_DIR}/Rop/*.cpp")
file(GLOB MAIN_SOURCES_GUI "${CMAKE_CURRENT_SOURCE_DIR}/Gui/*.cpp")
file(GLOB MAIN_SOURCES_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/Gui/imgui/*.cpp")
file(GLOB MAIN_SOURCES_JIT "${CMAKE_CURRENT_SOURCE_DIR}/JitU8/*.cpp")
file(GLOB MAIN_SOURCES_PERIPHERAL "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/*.cpp")
file(GLOB MAIN_SOURCES_PLUGIN "${CMAKE_CURRENT_SOURCE_DIR}/Plugin/*.cpp")
file(GLOB MAIN_SOURCES_STARTUP "${CMAKE_CURRENT_SOURCE_DIR}/StartupUi/*.cpp")

set(MAIN_SOURCES
    ${MAIN_SOURCES_ROOT}
    ${MAIN_SOURCES_ROOT_C}
    ${MAIN_SOURCES_CHIPSET}
    ${MAIN_SOURCES_CRASH}
    ${MAIN_SOURCES_EXT}
    ${MAIN_SOURCES_EXT_CAM}
    ${MAIN_SOURCES_GDB}
    ${MAIN_SOURCES_GUI}
    ${MAIN_SOURCES_ROP}
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
    
    if(WIN32)
        set(WIN_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Resource.rc)
    endif()

    add_executable(${TARGET_NAME} ${MAIN_SOURCES} ${WIN_RESOURCES})
else()
    message(STATUS "Building shared library")
    set(TARGET_NAME "main")
    add_library(${TARGET_NAME} SHARED ${MAIN_SOURCES})
endif()

# Apply include dirs to target (scoped) to reduce global includes and compiler parsing
target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDE_DIRS})

target_compile_definitions(${TARGET_NAME} PRIVATE ASMJIT_STATIC)

if(WIN32)
    #target_link_options(${TARGET_NAME} PRIVATE "-mconsole")
    if(NOT MSVC)
        target_link_options(${TARGET_NAME} PRIVATE
            "-static-libgcc"
            "-static-libstdc++"
            "-static"
        )
    endif()
    target_compile_definitions(${TARGET_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    # Ensure UTF-8 and increase compiler heap and object limits for large translation units
    target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /Zm200 /bigobj)
endif()

if(BUILD_EXECUTABLE)
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image-static
        #CpuFeatures::cpu_features
        asmjit::asmjit
    )
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            SDL2::SDL2main
            winmm
            dwmapi
            dbghelp
            imm32
        )
        set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "")
    else()
        
        target_link_libraries(${TARGET_NAME} PRIVATE
            -Wl,--start-group
            -lpthread
            -ldl
            -lrt
            -lm
            -Wl,--end-group
        )
    endif()
else()
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        #CpuFeatures::cpu_features
        asmjit::asmjit
    )
    if(ANDROID)
        target_link_libraries(${TARGET_NAME} PRIVATE log)
    endif()
endif()

if(NOT ANDROID AND NOT IOS)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/discord-rpc/include)
    target_link_libraries(${TARGET_NAME} PRIVATE discord-rpc sentry::sentry)
endif()

file(GLOB LOCALE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/locales/*.lc")

if(ANDROID)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/app/src/main/assets/locales)
    foreach(FILE_PATH ${LOCALE_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FILE_PATH}
            ${CMAKE_SOURCE_DIR}/app/src/main/assets/locales/)
    endforeach()
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/roms.db
        ${CMAKE_SOURCE_DIR}/app/src/main/assets/roms.db || ${CMAKE_COMMAND} -E true)
elseif(BUILD_EXECUTABLE)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET_NAME}>/locales)
    foreach(FILE_PATH ${LOCALE_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FILE_PATH}
            $<TARGET_FILE_DIR:${TARGET_NAME}>/locales/)
    endforeach()
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/roms.db
        $<TARGET_FILE_DIR:${TARGET_NAME}>/roms.db || ${CMAKE_COMMAND} -E true)
endif()


if(NOT WIN32 AND NOT ANDROID AND NOT IOS)
    if(BUILD_EXECUTABLE)
        target_link_options(${TARGET_NAME} PRIVATE "-static-libgcc" "-static-libstdc++")
    endif()
endif()

if(NOT ANDROID AND NOT IOS)
    if(TARGET crashpad_handler)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:crashpad_handler>
            $<TARGET_FILE_DIR:${TARGET_NAME}>/
        )
    endif()
endif()
