#pragma once
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#ifdef __GNUG__
# define FUNCTION_NAME __PRETTY_FUNCTION__
#else
# define FUNCTION_NAME __func__
#endif
#ifndef PANIC
#define PANIC(...) ( \
        std::fprintf(stderr, "%s:%i: in %s: ", __FILE__, __LINE__, FUNCTION_NAME), \
        std::fprintf(stderr, __VA_ARGS__), \
        std::exit(1) \
        )
#endif
extern lua_State* ls;
extern lua_Integer lsr;