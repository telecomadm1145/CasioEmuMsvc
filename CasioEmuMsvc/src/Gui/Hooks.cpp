#include "Hooks.h"

std::function<void(casioemu::CPU&, InstructionEventArgs&)> on_instruction;

std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_call_function;
std::function<void(casioemu::CPU&, const FunctionEventArgs&)> on_function_return;

std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_read;
std::function<void(casioemu::MMU&, MemoryEventArgs&)> on_memory_write;

std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_brk;
std::function<void(casioemu::Chipset&, InterruptEventArgs&)> on_interrupt;

std::function<void(casioemu::Chipset&)> on_reset;