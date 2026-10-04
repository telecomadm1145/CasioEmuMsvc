#include "T4xCore.hpp"
#include <algorithm>
#include <cstdio>

namespace casioemu {

void T4xCore::MemoryAccess(uint32_t address, uint8_t value, bool write) {
	if (!tracking_memory) return;
	for (auto& bp : memory_breakpoints) {
		if (!bp.enabled || bp.address != address || bp.write != write ||
			(bp.compare_data && (value & bp.mask) != (bp.data & bp.mask))) continue;
		if (++bp.hit_count <= bp.skip_count) continue;
		memory_hits.push_back({instruction_pc, address, value, write, state.instructions + 1});
		if (memory_hits.size() > 4096) memory_hits.pop_front();
		if (bp.break_when_hit && !debug_stop.stopped()) {
			Stop(DebugStopReason::MemoryBreakpoint);
			debug_stop.memory_address = address;
			debug_stop.memory_value = value;
			debug_stop.memory_write = write;
		}
	}
}
void T4xCore::Stop(DebugStopReason reason) {
	debug_stop = {reason, state.pc, state.instructions, state.cycles};
	run_mode = DebugStopReason::None;
}
void T4xCore::PrepareDebugRun(DebugStopReason mode) {
	debug_stop = {};
	tracking_memory = false;
	run_mode = mode;
	honor_breakpoints = true;
	skip_execution_breakpoint = true;
}
bool T4xCore::BeforeInstruction() {
	if (debug_stop.stopped()) return false;
	if (honor_breakpoints && !skip_execution_breakpoint) {
		auto bp = execution_breakpoints.find(state.pc);
		if (bp != execution_breakpoints.end() && bp->second.enabled &&
			++bp->second.hit_count > bp->second.skip_count) {
			Stop(DebugStopReason::ExecutionBreakpoint);
			return false;
		}
	}
	skip_execution_breakpoint = false;
	instruction_pc = state.pc;
	tracking_memory = honor_breakpoints && !memory_breakpoints.empty();
	return true;
}
void T4xCore::AfterInstruction() {
	tracking_memory = false;
	if (debug_stop.stopped()) {
		// Watchpoints stop at the completed instruction, including its side effects.
		debug_stop.program_counter = state.pc;
		debug_stop.instruction_count = state.instructions;
		debug_stop.cycle_count = state.cycles;
		return;
	}
	if (run_mode == DebugStopReason::Step ||
		(run_mode == DebugStopReason::RunToAddress && state.pc == run_target) ||
		((run_mode == DebugStopReason::StepOver || run_mode == DebugStopReason::StepOut) &&
			state.pc == run_target && StackPointer() == run_sp)) Stop(run_mode);
}
void T4xCore::RequestContinue(bool honor) {
	const std::lock_guard lock(mutex);
	const bool skip = debug_stop.reason == DebugStopReason::ExecutionBreakpoint;
	PrepareDebugRun(DebugStopReason::None);
	honor_breakpoints = honor;
	skip_execution_breakpoint = skip;
}
void T4xCore::RequestStepInto() {
	const std::lock_guard lock(mutex);
	PrepareDebugRun(DebugStopReason::Step);
}
void T4xCore::RequestStepOver() {
	const std::lock_guard lock(mutex);
	unsigned op = rom[state.pc] >> 10;
	PrepareDebugRun(op >= 36 && op < 44 ? DebugStopReason::StepOver : DebugStopReason::Step);
	run_target = state.pc + 1;
	run_sp = StackPointer();
}
bool T4xCore::RequestStepOut() {
	const std::lock_guard lock(mutex);
	if (stack.empty()) return false;
	PrepareDebugRun(DebugStopReason::StepOut);
	run_target = stack.back().lr;
	run_sp = (stack.back().sp + 2) & 63;
	return true;
}
void T4xCore::RequestRunToAddress(uint16_t address) {
	const std::lock_guard lock(mutex);
	PrepareDebugRun(DebugStopReason::RunToAddress);
	run_target = address;
}
void T4xCore::CancelDebugRun() {
	const std::lock_guard lock(mutex);
	run_mode = DebugStopReason::None;
}
DebugStop T4xCore::LastDebugStop() const {
	const std::lock_guard lock(mutex); return debug_stop;
}
void T4xCore::AddExecutionBreakpoint(uint32_t address) {
	const std::lock_guard lock(mutex);
	if (address < rom.size()) execution_breakpoints.try_emplace(address, ExecutionBreakpoint{address});
}
bool T4xCore::ConfigureExecutionBreakpoint(const ExecutionBreakpoint& bp) {
	const std::lock_guard lock(mutex);
	if (bp.address >= rom.size()) return false;
	auto [it, inserted] = execution_breakpoints.try_emplace(bp.address, bp);
	if (!inserted) {
		it->second.enabled = bp.enabled;
		it->second.skip_count = bp.skip_count;
	}
	return true;
}
void T4xCore::RemoveExecutionBreakpoint(uint32_t address) {
	const std::lock_guard lock(mutex); execution_breakpoints.erase(address);
}
void T4xCore::ClearExecutionBreakpoints() {
	const std::lock_guard lock(mutex); execution_breakpoints.clear();
}
std::vector<uint32_t> T4xCore::ExecutionBreakpoints() const {
	const std::lock_guard lock(mutex);
	std::vector<uint32_t> result;
	for (const auto& [address, bp] : execution_breakpoints) result.push_back(address);
	return result;
}
std::vector<ExecutionBreakpoint> T4xCore::ExecutionBreakpointDetails() const {
	const std::lock_guard lock(mutex);
	std::vector<ExecutionBreakpoint> result;
	for (const auto& [address, bp] : execution_breakpoints) result.push_back(bp);
	return result;
}
bool T4xCore::AddMemoryBreakpoint(const MemoryBreakpoint& bp) {
	if (!(bp.address < 64 || (bp.address >= 0x100 && bp.address < 0x500) ||
		(bp.address >= 0x1000 && bp.address < 0x2000))) return false;
	const std::lock_guard lock(mutex);
	auto found = std::find_if(memory_breakpoints.begin(), memory_breakpoints.end(), [&](const auto& v) {
		return v.address == bp.address && v.write == bp.write;
	});
	if (found == memory_breakpoints.end()) memory_breakpoints.push_back(bp);
	else { auto hits = found->hit_count; *found = bp; found->hit_count = hits; }
	++memory_breakpoints_version;
	return true;
}
bool T4xCore::RemoveMemoryBreakpoint(uint32_t address, bool write) {
	const std::lock_guard lock(mutex);
	auto old_size = memory_breakpoints.size();
	std::erase_if(memory_breakpoints, [&](const auto& bp) { return bp.address == address && bp.write == write; });
	++memory_breakpoints_version;
	return old_size != memory_breakpoints.size();
}
void T4xCore::ClearMemoryBreakpoints() {
	const std::lock_guard lock(mutex);
	memory_breakpoints.clear(); memory_hits.clear(); ++memory_breakpoints_version;
}
std::vector<MemoryBreakpoint> T4xCore::MemoryBreakpoints() const {
	const std::lock_guard lock(mutex); return memory_breakpoints;
}
uint64_t T4xCore::MemoryBreakpointsVersion() const {
	const std::lock_guard lock(mutex); return memory_breakpoints_version;
}
std::vector<MemoryBreakpointHit> T4xCore::MemoryBreakpointHits(uint32_t address, bool write) const {
	const std::lock_guard lock(mutex);
	std::vector<MemoryBreakpointHit> result;
	for (const auto& hit : memory_hits) if (hit.address == address && hit.write == write) result.push_back(hit);
	return result;
}
std::string T4xCore::BacktraceLocked() const {
	char text[96];
	std::snprintf(text, sizeof(text), "PC %04X (word)\n", state.pc);
	std::string result = text;
	for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
		std::snprintf(text, sizeof(text), "%s %04X from %04X, return %04X, SP %02X\n",
			it->interrupt ? "IRQ" : "CALL", it->pc, it->caller, it->lr, it->sp);
		result += text;
	}
	return result;
}
std::string T4xCore::GetBacktrace() const {
	const std::lock_guard lock(mutex); return BacktraceLocked();
}
std::vector<T4xCore::StackFrame> T4xCore::StackFrames() const {
	const std::lock_guard lock(mutex); return stack;
}
void T4xCore::RecordCall(uint16_t caller, uint16_t lr, bool interrupt) {
	StackFrame frame{state.pc, lr, caller, uint8_t(RawStackPointer()), interrupt};
	stack.push_back(frame);
	if (!interrupt && function_events_enabled) function_events.push_back({frame, true,
		uint32_t(state.reg[0]) | uint32_t(state.reg[1]) << 8 | uint32_t(state.reg[2]) << 16 |
		uint32_t(state.reg[3]) << 24, BacktraceLocked()});
}
void T4xCore::RecordReturn() {
	auto found = std::find_if(stack.rbegin(), stack.rend(), [&](const auto& f) {
		return f.lr == state.pc && ((f.sp + 2) & 63) == RawStackPointer();
	});
	if (found == stack.rend()) { stack.clear(); return; }
	const auto frame = *found;
	stack.erase(found.base() - 1, stack.end());
	if (!frame.interrupt && function_events_enabled) function_events.push_back({frame, false, 0, BacktraceLocked()});
}
void T4xCore::EnableFunctionEvents(bool enabled) {
	const std::lock_guard lock(mutex); function_events_enabled = enabled;
}
std::vector<T4xCore::FunctionEvent> T4xCore::TakeFunctionEvents() {
	const std::lock_guard lock(mutex);
	std::vector<FunctionEvent> result; result.swap(function_events); return result;
}

// Mnemonics describe the observed T4x instruction behavior. M is the nibble
// at WRAM[R0.bank:R3:R2]; Pn denotes the pair Rn/R(n+1), low nibble first.
std::string T4xCore::Disassemble(uint16_t address) const {
	uint16_t ins = CodeWord(address);
	unsigned op = ins >> 10, v = (ins >> 8) & 3, lo = ins & 15, mid = (ins >> 4) & 15;
	unsigned src = (ins >> 4) & 7, dst = (ins >> 7) & 7, hi = (ins >> 4) & 63;
	char operand[160];
	std::string body;
	auto format = [&](const char* fmt, auto... args) {
		std::snprintf(operand, sizeof(operand), fmt, args...); body = operand;
	};
	std::string a = src ? "R" + std::to_string(src) : "#0";
	std::string b = (ins & 7) ? "R" + std::to_string(ins & 7) : "#0";
	switch (op) {
	case 0:
		if (v == 2) format("SKIP %s", (ins & 7) ? b.c_str() : "#0");
		else if (v == 3) body = "RET";
		else if (v == 0 && ((ins >> 6) & 3) == 2) body = "HALT";
		else if (v == 0 && ((ins >> 6) & 3) == 0) body = "NOP";
		break;
	case 1:
		if (v < 2) format("LDB M.pair, ROM[P6:P4].%s", v ? "high" : "low");
		else if (v == 2) format("LD M, #%X, R2+=#%X", lo, mid);
		else format("LD M.pair, #%02X", ins & 255);
		break;
	case 2: if (v == 0 || v == 2) format("%s M, #%X", v ? "SUB" : "ADD", lo); else body = "NOP"; break;
	case 3: format("%s M, #%X", std::array{"AND", "OR", "XOR", "CMP"}[v], lo); break;
	case 5: if (v == 0 || v == 2) format("%s M, WRAM[bank:%X:%X]", v ? "SWAP" : "LD", mid, lo); else body = "NOP"; break;
	case 6: format("%s M, WRAM[bank:%X:R2], count=R1", std::array{"ADCB", "ADCDB", "SBCB", "SBCDB"}[v], mid); break;
	case 7:
		if (v < 2) format("LDBLOCK [R3:(R2+%X)&F], [%X:R2], R1, %s", lo, mid, v ? "DEC" : "INC");
		else format("%s M, WRAM[bank:%X:R2], count=R1", v == 2 ? "SWAPBLOCK" : "CMPBLOCK", mid);
		break;
	case 8: case 9: format("%s R%u, M, #%u", op == 8 ? "SHR" : "SHL", dst, (ins & 3) + 1); break;
	case 10: case 11: format("%s P%u", op == 10 ? "PUSH" : "POP", hi); break;
	case 12: case 13: format("%s R%u, %s, #%u", op == 12 ? "SHR" : "SHL", dst, a.c_str(), (ins & 3) + 1); break;
	case 14: format("LD R%u, M", hi); break;
	case 15: format("LD M, R%u", hi); break;
	case 16: case 18: format("%s R%u, %s, #%X", op == 16 ? "ADD" : "SUB", dst, a.c_str(), lo); break;
	case 17: case 19: case 21: case 23: format("%s R%u, %s, %s", op == 17 ? "ADD" : op == 19 ? "SUB" : op == 21 ? "ADC" : "SBC", dst, a.c_str(), b.c_str()); break;
	case 20: format("LD R%u, #%X", hi, lo); break;
	case 22: format("LD P%u, #%02X", v * 2, ins & 255); break;
	case 24: case 25: case 26: case 27: case 28: case 29:
		if (op & 1) format("%s R%u, %s, %s", op < 26 ? "AND" : op < 28 ? "OR" : "XOR", dst, a.c_str(), b.c_str());
		else format("%s R%u, %s, #%X", op < 26 ? "AND" : op < 28 ? "OR" : "XOR", dst, a.c_str(), lo);
		break;
	case 30: format("LD P%u, M.pair", hi); break;
	case 31: format("LD M.pair, P%u", hi); break;
	default:
		if (op >= 32 && op < 36) format("LD WRAM[bank:%X:%X], #%X", (ins >> 8) & 15, mid, lo);
		else if (op >= 36 && op < 40) format("CALL %04X", ((uint16_t(address + 1)) & 0xF000) | (ins & 4095));
		else if (op >= 40 && op < 44) format("CALL %04X", ins & 4095);
		else if (op >= 44 && op < 60) format("%s %04X", std::array{"JPZ", "JPNZ", "JPC", "JPNC"}[(op - 44) / 4], (address & 0xF000) | (ins & 4095));
		else if (op >= 60) format("JP page:%03X ; page=R7 after page write, otherwise PC", ins & 4095);
	}
	if (body.empty()) format("DW #%04X ; unsupported", ins);
	char prefix[20];
	std::snprintf(prefix, sizeof(prefix), "%04X  %04X   ", address, ins); // 13-column CodeViewer prefix.
	return std::string(prefix) + body;
}
bool T4xCore::BranchTarget(uint16_t address, uint16_t& target) const {
	uint16_t ins = CodeWord(address);
	unsigned op = ins >> 10;
	if (op < 36 || op >= 60) return false; // JP's page latch is runtime-dependent.
	target = (ins & 4095) | (op < 40 ? uint16_t(address + 1) & 0xF000 : op < 44 ? 0 : address & 0xF000);
	return true;
}

} // namespace casioemu
