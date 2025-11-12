cmake_minimum_required(VERSION 3.16)
project(luacvtcf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(lua-5.4.8)
add_executable(luacvtcf lua_config_convert.cpp)
target_link_libraries(luacvtcf PUBLIC lua)

include(GNUInstallDirs)
install(TARGETS luacvtcf
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)