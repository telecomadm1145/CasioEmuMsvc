project(LuaLibrary)

file(GLOB LUA_SOURCES "src/*.c")
add_library(lua STATIC ${LUA_SOURCES})

if(WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_WINDOWS)
endif()
target_include_directories(lua PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(UNIX)
    target_link_libraries(lua PUBLIC m dl)
endif()