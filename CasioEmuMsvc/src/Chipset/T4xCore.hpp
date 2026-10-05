#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include "CoreDebug.hpp"

namespace casioemu {
	// T4x word-addressed CPU and MultiView machine. Memory units are explicit:
	// WRAM and registers contain nibbles; DRAM and ROM input contain bytes.
	class T4xCore {
	public:
		static constexpr unsigned Width = 96, Height = 31, BodyBytes = 372, LcdBytes = 384;
		static constexpr unsigned LcdControlRegister = 22, LcdContrastRegister = 37;
		explicit T4xCore(unsigned dram_banks = 8);
		struct DisplayState {
			std::array<uint8_t, LcdBytes> lcd;
			uint8_t control, contrast;
			uint64_t elapsed_us;
			uint64_t generation;
			uint64_t steady_ns; // LCD presentation time; independent of CPU pause/speed.
		};
		struct State {
			std::array<uint8_t, 64> reg{};
			std::array<uint8_t, 1024> work{};
			std::array<uint8_t, 4096> data{};
			std::array<uint8_t, LcdBytes> lcd{};
			uint16_t pc = 0;
			uint64_t cycles = 0, instructions = 0;
			uint64_t elapsed_us = 0;
			uint32_t instruction_time_remainder = 0;
			uint32_t timer_remaining[2]{350000, 6000};
			uint32_t input_remaining = 0;
			uint8_t key = 255;
			uint8_t pending = 0;
			bool page_written = false, halted = false, timers = false, lcd_dirty = false;
			bool timer_enabled[2]{};
		};
		bool LoadRom(const std::vector<uint8_t>& bytes);
		void Reset();
		unsigned Step(); // One instruction, PC and code addresses are WORD addresses.
		static constexpr unsigned InstructionsPerMillisecond = 3001;
		// Normal execution and debugger ticks share the reference instruction rate.
		void RunBatch(unsigned instructions);
		void RunBatch(unsigned instructions, uint32_t elapsed_us);
		void AdvanceTime(uint32_t elapsed_us);
		void Key(uint8_t code, bool pressed);
		State Snapshot() const;
		DisplayState ReadDisplay() const;
		struct DisplayHistory { std::vector<DisplayState> changes; DisplayState current; };
		void EnableDisplayHistory();
		DisplayHistory ConsumeDisplayHistory();
		uint16_t ProgramCounter() const;
		void Restore(const State& state);
		uint16_t CodeWord(uint16_t address) const;
		void WriteCodeWord(uint16_t address, uint16_t value);
		uint8_t ReadCodeByte(unsigned address) const;
		void WriteCodeByte(unsigned address, uint8_t value);
		void SetPc(uint16_t address);
		// Debug address map: registers 0000..003F, WRAM 0100..04FF,
		// DRAM 1000..1FFF. One address holds one nibble in registers/WRAM.
		uint8_t ReadDebugMemory(unsigned address) const;
		void WriteDebugMemory(unsigned address, uint8_t value);
		uint8_t ReadMemory(unsigned space, unsigned address) const;
		void WriteMemory(unsigned space, unsigned address, uint8_t value);
		std::string Disassemble(uint16_t address) const;
		bool BranchTarget(uint16_t address, uint16_t& target) const;
		struct StackFrame {
			uint16_t pc{}, lr{}, caller{};
			uint8_t sp{};
			bool interrupt{};
		};
		struct FunctionEvent {
			StackFrame frame;
			bool call;
			uint32_t registers;
			std::string backtrace;
		};
		std::vector<StackFrame> StackFrames() const;
		std::string GetBacktrace() const;
		std::vector<FunctionEvent> TakeFunctionEvents();
		void EnableFunctionEvents(bool enabled);
		void RequestContinue(bool honor_breakpoints = true);
		void RequestStepInto();
		void RequestStepOver();
		bool RequestStepOut();
		void RequestRunToAddress(uint16_t address);
		void CancelDebugRun();
		void AddExecutionBreakpoint(uint32_t address);
		bool ConfigureExecutionBreakpoint(const ExecutionBreakpoint& breakpoint);
		void RemoveExecutionBreakpoint(uint32_t address);
		void ClearExecutionBreakpoints();
		std::vector<uint32_t> ExecutionBreakpoints() const;
		std::vector<ExecutionBreakpoint> ExecutionBreakpointDetails() const;
		bool AddMemoryBreakpoint(const MemoryBreakpoint& breakpoint);
		bool RemoveMemoryBreakpoint(uint32_t address, bool write);
		void ClearMemoryBreakpoints();
		std::vector<MemoryBreakpoint> MemoryBreakpoints() const;
		uint64_t MemoryBreakpointsVersion() const;
		std::vector<MemoryBreakpointHit> MemoryBreakpointHits(uint32_t address, bool write) const;
		DebugStop LastDebugStop() const;
		void SaveState(std::ostream& out) const;
		void LoadState(std::istream& in);

	private:
		mutable std::mutex mutex;
		const unsigned dram_banks;
		std::array<uint16_t, 65536> rom{};
		State state{};
		std::deque<uint8_t> keys;
		// CPU accesses use these views; debugger reads/writes deliberately bypass them.
		struct Cell {
			T4xCore* core;
			uint8_t* value;
			uint32_t address;
			operator uint8_t() const {
				if (core->tracking_memory) core->MemoryAccess(address, *value, false);
				return *value;
			}
			Cell& operator=(uint8_t v) {
				*value = v;
				if (core->tracking_memory) core->MemoryAccess(address, v, true);
				return *this;
			}
			Cell& operator=(const Cell& v) { return *this = uint8_t(v); }
			Cell& operator&=(unsigned v) { return *this = uint8_t(*this) & v; }
			Cell& operator|=(unsigned v) { return *this = uint8_t(*this) | v; }
		};
		struct MemoryView {
			T4xCore* core;
			uint8_t* bytes;
			uint32_t base;
			Cell operator[](unsigned i) const { return {core, bytes + i, base + i}; }
		};
		MemoryView registers{this, state.reg.data(), 0};
		MemoryView data_memory{this, state.data.data(), 0x1000};
		std::vector<StackFrame> stack;
		std::vector<FunctionEvent> function_events;
		bool function_events_enabled = false, display_history_enabled = false;
		std::deque<DisplayState> display_history;
		DisplayState last_display{};
		void CaptureDisplay();
		std::map<uint32_t, ExecutionBreakpoint> execution_breakpoints;
		std::vector<MemoryBreakpoint> memory_breakpoints;
		std::deque<MemoryBreakpointHit> memory_hits;
		uint64_t memory_breakpoints_version = 0, display_generation = 0;
		DebugStop debug_stop;
		DebugStopReason run_mode = DebugStopReason::None;
		uint16_t run_target = 0, instruction_pc = 0;
		unsigned run_sp = 0;
		bool honor_breakpoints = true, skip_execution_breakpoint = false, tracking_memory = false;
		void PrepareDebugRun(DebugStopReason mode);
		bool BeforeInstruction();
		void AfterInstruction();
		void Stop(DebugStopReason reason);
		void MemoryAccess(uint32_t address, uint8_t value, bool write);
		void RecordCall(uint16_t caller, uint16_t lr, bool interrupt);
		void RecordReturn();
		std::string BacktraceLocked() const;
		void ResetState();
		unsigned StepInstruction();
		void AdvanceTimeLocked(uint32_t elapsed_us);
		void AdvanceInstructionTime(unsigned instructions);
		unsigned RunInstructionsLocked(unsigned count);
		Cell Work(unsigned bank, unsigned row, unsigned col);
		unsigned StackPointer() const;
		unsigned RawStackPointer() const { return ((state.reg[9] & 7) * 16 + (state.reg[8] & 14)) / 2; }
		void StackPointer(unsigned value);
		void PushByte(uint8_t value);
		uint8_t PopByte();
		void PushPc();
		void Interrupts();
		void RegisterEffects(unsigned index, bool pair, bool pop, bool immediate);
		void LoadPair(unsigned index, uint8_t value, bool pop);
		bool ReadPair(unsigned index, uint8_t& value);
		void DataNext();
		void Dma();
		bool IsDataBankMapped(unsigned bank) const;
		void ReadKey(unsigned index);
		bool KeyInterrupt(uint8_t code) const;
		void ServiceKey();
		void PublishLcd();
	};
} // namespace casioemu
