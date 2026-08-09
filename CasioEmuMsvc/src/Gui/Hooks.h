#pragma once
#include "Chipset/Chipset.hpp"
#include <functional>
#include <string>

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
struct EpsFunctionEventArgs {
	FunctionEventArgs function{};
	uint32_t accumulator{};
	std::string backtrace;
};

extern std::function<void(casioemu::CPU&, InstructionEventArgs&)> on_instruction;

extern std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_call_function;
extern std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_function_return;

extern std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_read;
extern std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_write;

extern std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_brk;
extern std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_interrupt;

extern std::function<void(casioemu::Chipset&)> on_reset;

// EPS6800-neutral hooks avoid pretending that the EPS core is the nX-U8 CPU/MMU.
extern std::function<void(InstructionEventArgs&)> on_eps_instruction;
extern std::function<void(const EpsFunctionEventArgs&)> on_eps_call_function;
extern std::function<void(const EpsFunctionEventArgs&)> on_eps_function_return;
extern std::function<void(MemoryEventArgs&)> on_eps_memory_read;
extern std::function<void(MemoryEventArgs&)> on_eps_memory_write;
extern std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_eps_interrupt;

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
