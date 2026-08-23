/*
 * EPS6800 machine adapter for CasioEmuMsvc.
 *
 * The implementation is deliberately kept behind the historical ePSCPU
 * name so the debugger and plugin surfaces can migrate without exposing the
 * C core internals throughout the application.
 */
#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct machine_state;

namespace casioemu {
	class MMU;

	enum class Eps6800RomFormat : uint8_t {
		PackedLittleEndian,
		UnpackedNibbles
	};

	const char* Eps6800RomFormatName(Eps6800RomFormat format);

	enum class EpsVariant : uint8_t {
		Eps6800,
		Eps6009
	};

	struct Eps6800LcdControl {
		uint8_t lcdarh{};
		uint8_t lcdcon{};
		uint8_t contrast{};
		bool display_on{};
		bool blanked{};

		bool visible() const { return display_on && !blanked; }
	};

	enum class Eps6800DebugStopReason : uint8_t {
		None,
		Step,
		StepOver,
		StepOut,
		RunToAddress,
		ExecutionBreakpoint,
		MemoryBreakpoint,
		Hook,
		Break
	};

	struct Eps6800DebugStop {
		Eps6800DebugStopReason reason{Eps6800DebugStopReason::None};
		uint32_t program_counter{}; // 16-bit instruction-word address
		uint64_t instruction_count{};
		uint64_t cycle_count{};
		uint32_t memory_address{};
		uint8_t memory_value{};
		bool memory_write{};

		bool stopped() const { return reason != Eps6800DebugStopReason::None; }
	};

	struct Eps6800MemoryBreakpointHit {
		uint32_t program_counter{};
		uint32_t address{};
		uint8_t value{};
		bool write{};
		uint64_t instruction_count{};
	};

	struct Eps6800ExecutionBreakpoint {
		uint32_t address{};
		bool enabled{true};
		uint64_t skip_count{};
		uint64_t hit_count{};
	};

	struct Eps6800MemoryBreakpoint {
		uint32_t address{}; // 0x00-0x7f SFR, 0x80-0x207f banked RAM
		bool write{};
		bool enabled{true};
		bool break_when_hit{true};
		bool compare_data{};
		uint8_t data{};
		uint8_t mask{0xff};
		uint64_t skip_count{};
		uint64_t hit_count{};
	};

	struct Eps6800DebugSnapshot {
		uint32_t program_counter{}; // 16-bit instruction-word address
		std::array<uint8_t, 0x80> registers{};
		std::array<uint8_t, 27> wbk_registers{};
		std::array<uint32_t, 32> stack{};
		uint8_t stack_pointer{};
		uint64_t instruction_count{};
		uint64_t cycle_count{};
	};

	struct Eps6800TraceEntry {
		uint32_t program_counter{};
		uint32_t instruction{};
		uint32_t next_program_counter{};
		uint8_t stack_pointer{};
		uint8_t accumulator{};
		uint8_t status{};
		uint64_t instruction_count{};
		uint64_t cycle_count{};
	};

	class ePSCPU {
	private:
		class StateMutex {
			std::mutex mutex_;
			std::condition_variable condition_;
			uint64_t next_ticket_{};
			uint64_t serving_ticket_{};
			std::thread::id owner_{};
			uint32_t recursion_{};

		public:
			void lock() {
				std::unique_lock lock(mutex_);
				const auto caller = std::this_thread::get_id();
				if (owner_ == caller) {
					++recursion_;
					return;
				}
				const auto ticket = next_ticket_++;
				condition_.wait(lock, [this, ticket] {
					return serving_ticket_ == ticket && recursion_ == 0;
				});
				owner_ = caller;
				recursion_ = 1;
			}

			void unlock() {
				bool notify = false;
				{
					const std::lock_guard lock(mutex_);
					if (--recursion_ == 0) {
						owner_ = {};
						++serving_ticket_;
						notify = true;
					}
				}
				if (notify)
					condition_.notify_all();
			}
		};

		enum class DebugRunMode : uint8_t {
			Continue,
			StepInto,
			StepOver,
			StepOut,
			RunToAddress
		};

		machine_state* state_;
		mutable StateMutex state_mutex_;
		DebugRunMode debug_run_mode_{DebugRunMode::Continue};
		uint32_t debug_target_pc_{};
		uint8_t debug_target_stack_pointer_{};
		bool honor_execution_breakpoints_{true};
		bool honor_memory_breakpoints_{true};
		std::unordered_map<uint32_t, Eps6800ExecutionBreakpoint> execution_breakpoints_;
		std::vector<Eps6800MemoryBreakpoint> memory_breakpoints_;
		uint64_t memory_breakpoint_version_{};
		std::deque<Eps6800MemoryBreakpointHit> memory_breakpoint_hits_;
		bool memory_break_pending_{};
		uint32_t pending_memory_address_{};
		uint8_t pending_memory_value_{};
		bool pending_memory_write_{};
		Eps6800DebugStop last_debug_stop_{};
		uint64_t instruction_count_{};
		uint64_t cycle_count_{};
		uint32_t timer_cycle_divisor_{1};
		uint32_t timer_cycle_phase_{};
		bool ice_timer_scheduling_{};
		std::chrono::steady_clock::time_point idle_timer_checkpoint_{};
		bool trace_enabled_{};
		size_t trace_capacity_{4096};
		std::deque<Eps6800TraceEntry> trace_buffer_;
		Eps6800RomFormat rom_format_{Eps6800RomFormat::PackedLittleEndian};
		size_t rom_word_count_{};
		EpsVariant variant_{EpsVariant::Eps6800};
		std::atomic_bool break_requested_{};
		std::function<bool(uint32_t, uint32_t, uint8_t)> instruction_hook_;
		std::function<void(uint32_t, uint32_t, bool, uint32_t, const std::string&)> function_hook_;
		std::function<bool(uint32_t, uint8_t&, bool)> memory_hook_;
		std::function<void(uint8_t)> interrupt_hook_;

		bool RunInstructionLocked(bool tick_timer);
		bool ConsumeBreakRequestLocked();
		bool ShouldStopLocked(uint32_t pc_after, uint8_t stack_pointer_after,
			Eps6800DebugStopReason& reason);
		void RecordStopLocked(Eps6800DebugStopReason reason, uint32_t pc);
		void RecordTraceLocked(uint32_t pc_before, uint32_t instruction, uint32_t pc_after);
		static bool MemoryAccessThunk(void* user, uint32_t address, uint8_t* value, bool write, bool before);
		bool OnMemoryAccessLocked(uint32_t address, uint8_t& value, bool write, bool before);
		std::string BacktraceLocked() const;

	public:
		explicit ePSCPU(EpsVariant variant = EpsVariant::Eps6800);
		~ePSCPU();

		ePSCPU(const ePSCPU&) = delete;
		ePSCPU& operator=(const ePSCPU&) = delete;

		bool LoadRom(const std::vector<unsigned char>& rom, Eps6800RomFormat format);
		Eps6800RomFormat RomFormat() const;
		size_t RomWordCount() const;
		bool WriteRomImageWord(std::vector<unsigned char>& rom, uint32_t word_address, uint16_t value) const;
		void SetDebugHooks(
			std::function<bool(uint32_t, uint32_t, uint8_t)> instruction,
			std::function<void(uint32_t, uint32_t, bool, uint32_t, const std::string&)> function,
			std::function<bool(uint32_t, uint8_t&, bool)> memory,
			std::function<void(uint8_t)> interrupt);
		void Reset();
		void ClearRamAndReset();
		void SetTimerCycleDivisor(uint32_t divisor);
		void SetIceTimerScheduling(bool enabled);
		void SetPortCInput(uint8_t mask, uint8_t value);
		void Next();
		bool RunFrame(uint32_t idle_timer_cycles = 0);

		void KeyDown(uint8_t matrix_index);
		void RestoreKeyDown(uint8_t matrix_index);
		void KeyUp(uint8_t matrix_index);
		void OnDown();
		void OnUp();

		size_t CopyLcd(uint8_t* output, size_t size, Eps6800LcdControl* control = nullptr) const;
		size_t LcdRawSize() const;
		uint8_t ReadByte(uint8_t address);
		void WriteByte(uint8_t address, uint8_t value);
		uint8_t ReadDebugMemory(uint32_t linear_address) const;
		bool WriteDebugMemory(uint32_t linear_address, uint8_t value);
		uint16_t ReadCodeWord(uint32_t word_address) const;
		bool WriteCodeWord(uint32_t word_address, uint16_t value);
		uint8_t ReadLcdMemory(size_t address) const;
		bool WriteLcdMemory(size_t address, uint8_t value);
		Eps6800DebugSnapshot DebugSnapshot() const;
		std::string GetBacktrace() const;
		std::vector<uint8_t> ExportRam() const;
		bool ImportRam(const std::vector<uint8_t>& data);

		void RequestContinue(bool honor_breakpoints = true);
		void RequestStepInto();
		void RequestStepOver();
		bool RequestStepOut();
		void RequestRunToAddress(uint32_t word_address);
		void CancelDebugRun();
		void RequestBreak();
		void AddExecutionBreakpoint(uint32_t word_address);
		bool ConfigureExecutionBreakpoint(const Eps6800ExecutionBreakpoint& breakpoint);
		void RemoveExecutionBreakpoint(uint32_t word_address);
		void ClearExecutionBreakpoints();
		std::vector<uint32_t> ExecutionBreakpoints() const;
		std::vector<Eps6800ExecutionBreakpoint> ExecutionBreakpointDetails() const;
		bool AddMemoryBreakpoint(const Eps6800MemoryBreakpoint& breakpoint);
		bool RemoveMemoryBreakpoint(uint32_t address, bool write);
		void ClearMemoryBreakpoints();
		std::vector<Eps6800MemoryBreakpoint> MemoryBreakpoints() const;
		uint64_t MemoryBreakpointsVersion() const;
		std::vector<Eps6800MemoryBreakpointHit> MemoryBreakpointHits(uint32_t address, bool write) const;
		void ClearMemoryBreakpointHits();
		Eps6800DebugStop LastDebugStop() const;

		void EnableTrace(bool enabled);
		void ClearTrace();
		void SetTraceCapacity(size_t capacity);
		std::vector<Eps6800TraceEntry> TraceBuffer() const;

		void SaveState(std::ostream& stream) const;
		void LoadState(std::istream& stream);

		uint32_t ProgramCounter() const;
		// Legacy byte-address accessor retained while non-debugger callers migrate.
		uint32_t PC() const;
		void SetPC(uint32_t word_address);

	};
} // namespace casioemu
