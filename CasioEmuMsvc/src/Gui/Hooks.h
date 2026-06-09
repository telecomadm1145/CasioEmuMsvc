#pragma once
#include "Chipset/Chipset.hpp"
#include <functional>

// this is the new cpp style hook library
// for script & ui

struct FunctionEventArgs {
	uint32_t pc{};
	uint32_t lr{};
};
struct MemoryEventArgs {
	uint32_t offset{};
	bool handled{};
	uint8_t value{};
};
struct InterruptEventArgs {
	uint8_t index{};
	bool handled{};
};
struct InstructionEventArgs {
	uint32_t pc_before;
	uint32_t pc_after;
	bool should_break{};
};

extern std::function<void(casioemu::CPU&, InstructionEventArgs&)> on_instruction;

extern std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_call_function;
extern std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_function_return;

extern std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_read;
extern std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_write;

extern std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_brk;
extern std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_interrupt;

extern std::function<void(casioemu::Chipset&)> on_reset;

#define RaiseEvent(func, ...) \
	if (func)                 \
		func(__VA_ARGS__);

template <class... TArgs>
inline void SetupHook(std::function<void(TArgs...)>& func, auto lambda) {
	if (!func) {
		func = lambda;
	}
	else {
		func = [orig = func, lambda](TArgs... args) {
			orig(args...);
			lambda(args...);
		};
	}
}