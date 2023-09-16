add_requires("libsdl 2.26.4","libsdl_image 2.6.2","readline","lua 5.3")
add_requires("imgui",  {configs = {sdl2renderer = true}})
target("CasioEmuX")

    set_kind("binary")
    add_cxflags("--std=c++17 -g")
    add_files("src/*.cpp","src/*/*.cpp")
    add_packages("readline","libsdl","libsdl_image","lua")
    set_rundir("./")
    add_packages("imgui", {public = true})
    
    