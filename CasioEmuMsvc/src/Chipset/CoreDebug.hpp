#pragma once
#include <cstdint>

namespace casioemu {

enum class DebugStopReason : uint8_t {
	None, Step, StepOver, StepOut, RunToAddress,
	ExecutionBreakpoint, MemoryBreakpoint, Hook, Break
};
struct DebugStop {
	DebugStopReason reason{DebugStopReason::None};
	uint32_t program_counter{};
	uint64_t instruction_count{}, cycle_count{};
	uint32_t memory_address{};
	uint8_t memory_value{};
	bool memory_write{};
	bool stopped() const { return reason != DebugStopReason::None; }
};
struct MemoryBreakpointHit {
	uint32_t program_counter{}, address{};
	uint8_t value{};
	bool write{};
	uint64_t instruction_count{};
};
struct ExecutionBreakpoint {
	uint32_t address{};
	bool enabled{true};
	uint64_t skip_count{}, hit_count{};
};
struct MemoryBreakpoint {
	uint32_t address{}; // Architecture-specific debugger address map.
	bool write{}, enabled{true}, break_when_hit{true}, compare_data{};
	uint8_t data{}, mask{0xff};
	uint64_t skip_count{}, hit_count{};
};

} // namespace casioemu
