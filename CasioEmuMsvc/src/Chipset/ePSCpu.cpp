/*
 * EPS6800 machine adapter for CasioEmuMsvc.
 *
 * The machine implementation in EPS6800Core is derived from eps-emu-0601
 * and is licensed under GPL-3.0-or-later, matching this project.
 */
#include "ePSCpu.h"

#include "EPS6800Core/eps6800.h"
#include "EPS6800Core/machine.h"
#include "EPS6800Core/machine_debug.h"
#include "EPS6800Core/machine_internal.h"
#include "EPS6800Core/machine_io.h"
#include "EPS6800Core/machine_rom.h"
#include "EPS6800Core/machine_snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
	constexpr uint32_t kSnapshotMagic = 0x31535045; // "EPS1", little-endian
	constexpr uint32_t kMaximumSnapshotSize = 1024 * 1024;
	constexpr size_t kPackedBytesPerWord = 2;
	constexpr size_t kUnpackedBytesPerWord = 4;
	constexpr size_t kMaximumRomWords = 96 * 1024;

	bool ConvertUnpackedNibbles(const std::vector<unsigned char>& source,
		std::vector<unsigned char>& packed) {
		if (source.empty() || source.size() % kUnpackedBytesPerWord != 0 ||
			source.size() / kUnpackedBytesPerWord > kMaximumRomWords)
			return false;
		packed.resize(source.size() / 2);
		for (size_t source_offset = 0, packed_offset = 0;
			source_offset < source.size(); source_offset += kUnpackedBytesPerWord,
			packed_offset += kPackedBytesPerWord) {
			const auto nibble0 = source[source_offset];
			const auto nibble1 = source[source_offset + 1];
			const auto nibble2 = source[source_offset + 2];
			const auto nibble3 = source[source_offset + 3];
			if (nibble0 > 0x0f || nibble1 > 0x0f || nibble2 > 0x0f || nibble3 > 0x0f)
				return false;
			packed[packed_offset] = static_cast<unsigned char>((nibble2 << 4) | nibble3);
			packed[packed_offset + 1] = static_cast<unsigned char>((nibble0 << 4) | nibble1);
		}
		return true;
	}

	machine_state* CreateMachineOrThrow() {
		auto* state = machine_state_create();
		if (!state)
			throw std::runtime_error("Failed to create EPS6800 machine state");
		return state;
	}

	bool IsEpsCallInstruction(uint16_t word) {
		return (word & 0xf000u) == 0x3000u || // S0CALL
			(word & 0xe000u) == 0xe000u || // SCALL
			(word & 0xfff0u) == 0x0030u; // LCALL
	}

	bool IsEpsReturnInstruction(uint16_t word) {
		return word == 0x2bfeu || word == 0x2bffu;
	}

	enum eps_variant ToCoreVariant(casioemu::EpsVariant variant) {
		return variant == casioemu::EpsVariant::Eps6009 ? EPS_VARIANT_6009 : EPS_VARIANT_6800;
	}

	uint8_t EpsInstructionWords(uint16_t word, casioemu::EpsVariant variant) {
		if ((word & 0xfff0u) == 0x0020u || (word & 0xfff0u) == 0x0030u)
			return 2;
		const uint8_t high = static_cast<uint8_t>(word >> 8);
		(void)variant;
		if ((high >= 0x50 && high <= 0x51) ||
			(high >= 0x55 && high <= 0x67) ||
			(high >= 0x47 && high <= 0x49))
			return 2;
		return 1;
	}

	uint8_t EpsInstructionCycles(uint16_t word, casioemu::EpsVariant variant) {
		const uint8_t high = static_cast<uint8_t>(word >> 8);
		if ((word & 0xfff0u) == 0x0020u || (word & 0xfff0u) == 0x0030u ||
			(high >= 0x2c && high <= 0x2f) ||
			(high >= 0x50 && high <= 0x51) ||
			(high >= 0x55 && high <= 0x67) ||
			(high >= 0x47 && high <= 0x49))
			return 2;
		return 1;
	}

}

namespace casioemu {
	const char* Eps6800RomFormatName(Eps6800RomFormat format) {
		switch (format) {
		case Eps6800RomFormat::PackedLittleEndian:
			return "packed-little-endian";
		case Eps6800RomFormat::UnpackedNibbles:
			return "unpacked-nibbles";
		}
		return "unknown";
	}

	ePSCPU::ePSCPU(EpsVariant variant)
		: state_(CreateMachineOrThrow()), variant_(variant) {
		machine_state_set_variant(state_, ToCoreVariant(variant_));
		machine_state_debug_set_memory_access_callback(state_, &ePSCPU::MemoryAccessThunk, this);
	}

	ePSCPU::~ePSCPU() {
		machine_state_destroy(state_);
	}

	bool ePSCPU::LoadRom(const std::vector<unsigned char>& rom, Eps6800RomFormat format) {
		const std::lock_guard lock(state_mutex_);
		const unsigned char* data = rom.data();
		size_t size = rom.size();
		std::vector<unsigned char> packed;
		if (format == Eps6800RomFormat::UnpackedNibbles) {
			if (!ConvertUnpackedNibbles(rom, packed))
				return false;
			data = packed.data();
			size = packed.size();
		}
		else if (rom.empty() || rom.size() % kPackedBytesPerWord != 0 ||
			rom.size() / kPackedBytesPerWord > kMaximumRomWords) {
			return false;
		}
		if (!machine_state_load_rom_image(state_, data, size))
			return false;
		rom_format_ = format;
		rom_word_count_ = size / kPackedBytesPerWord;
		return true;
	}

	Eps6800RomFormat ePSCPU::RomFormat() const {
		const std::lock_guard lock(state_mutex_);
		return rom_format_;
	}

	void ePSCPU::SetDebugHooks(
		std::function<bool(uint32_t, uint32_t, uint8_t)> instruction,
		std::function<void(uint32_t, uint32_t, bool, uint32_t, const std::string&)> function,
		std::function<bool(uint32_t, uint8_t&, bool)> memory,
		std::function<void(uint8_t)> interrupt) {
		const std::lock_guard lock(state_mutex_);
		instruction_hook_ = std::move(instruction);
		function_hook_ = std::move(function);
		memory_hook_ = std::move(memory);
		interrupt_hook_ = std::move(interrupt);
	}

	bool ePSCPU::WriteRomImageWord(std::vector<unsigned char>& rom, uint32_t word_address,
		uint16_t value) const {
		const std::lock_guard lock(state_mutex_);
		if (rom_format_ == Eps6800RomFormat::UnpackedNibbles) {
			const size_t offset = static_cast<size_t>(word_address) * kUnpackedBytesPerWord;
			if (offset + 3 >= rom.size())
				return false;
			rom[offset] = static_cast<unsigned char>((value >> 12) & 0x0f);
			rom[offset + 1] = static_cast<unsigned char>((value >> 8) & 0x0f);
			rom[offset + 2] = static_cast<unsigned char>((value >> 4) & 0x0f);
			rom[offset + 3] = static_cast<unsigned char>(value & 0x0f);
			return true;
		}
		const size_t offset = static_cast<size_t>(word_address) * kPackedBytesPerWord;
		if (offset + 1 >= rom.size())
			return false;
		rom[offset] = static_cast<unsigned char>(value);
		rom[offset + 1] = static_cast<unsigned char>(value >> 8);
		return true;
	}

	void ePSCPU::Reset() {
		const std::lock_guard lock(state_mutex_);
		break_requested_.store(false, std::memory_order_relaxed);
		machine_state_reset(state_);
		debug_run_mode_ = DebugRunMode::Continue;
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		last_debug_stop_ = {};
		instruction_count_ = 0;
		cycle_count_ = 0;
		timer_cycle_phase_ = 0;
		idle_timer_checkpoint_ = std::chrono::steady_clock::now();
		trace_buffer_.clear();
		memory_break_pending_ = false;
	}

	void ePSCPU::ClearRamAndReset() {
		const std::lock_guard lock(state_mutex_);
		std::fill(std::begin(state_->mmio.ram), std::end(state_->mmio.ram), 0);
		std::fill(std::begin(state_->mmio.ram_wbk), std::end(state_->mmio.ram_wbk), 0);
		std::fill(&state_->mmio.regs[0x13], &state_->mmio.regs[0x20], 0);
		std::fill(&state_->mmio.regs[0x40], &state_->mmio.regs[0x80], 0);
		machine_state_reset(state_);
		debug_run_mode_ = DebugRunMode::Continue;
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		last_debug_stop_ = {};
		instruction_count_ = 0;
		cycle_count_ = 0;
		timer_cycle_phase_ = 0;
		idle_timer_checkpoint_ = std::chrono::steady_clock::now();
		trace_buffer_.clear();
		memory_break_pending_ = false;
	}

	void ePSCPU::SetTimerCycleDivisor(uint32_t divisor) {
		const std::lock_guard lock(state_mutex_);
		timer_cycle_divisor_ = std::max(1u, divisor);
		timer_cycle_phase_ = 0;
	}

	void ePSCPU::SetIceTimerScheduling(bool enabled) {
		const std::lock_guard lock(state_mutex_);
		ice_timer_scheduling_ = enabled;
		idle_timer_checkpoint_ = std::chrono::steady_clock::now();
	}

	void ePSCPU::SetPortBInput(uint8_t mask, uint8_t value) {
		const std::lock_guard lock(state_mutex_);
		machine_state_set_portb_input(state_, mask, value);
	}

	void ePSCPU::SetPortCInput(uint8_t mask, uint8_t value) {
		const std::lock_guard lock(state_mutex_);
		machine_state_set_portc_input(state_, mask, value);
	}

	void ePSCPU::Next() {
		const std::lock_guard lock(state_mutex_);
		RunInstructionLocked(true);
	}

	bool ePSCPU::RunFrame(uint32_t idle_timer_cycles) {
		const std::lock_guard lock(state_mutex_);
		if (ConsumeBreakRequestLocked())
			return true;
		constexpr uint32_t kLegacyActiveInstructions = 2000;
		constexpr uint32_t kFrameInstructions = 4000;
		bool stopped = false;
		if (state_->cpu.mode == CPU_MODE_SLEEP) {
			machine_state_advance_cycles_split(state_, kFrameInstructions, false, false);
			return false;
		}
		if (!ice_timer_scheduling_) {
			for (uint32_t i = 0; i < kFrameInstructions; ++i) {
				if (RunInstructionLocked(i < kLegacyActiveInstructions)) {
					stopped = true;
					break;
				}
			}
			return stopped;
		}
		const auto now = std::chrono::steady_clock::now();
		if (state_->cpu.mode == CPU_MODE_IDLE) {
			machine_state_advance_cycles_split(state_, kFrameInstructions, false, false);
			if (idle_timer_cycles != 0) {
				// Some EPS6800 models need Timer1 paced from the low-speed
				// oscillator while the CPU is idle, not from key wakeups.
				machine_state_tick_idle_timer1(state_, idle_timer_cycles);
			}
			else {
				constexpr auto kIdleTimerPeriod = std::chrono::milliseconds(20);
				/* Timer1 keeps running from its low-speed source while the
				 * CPU is idle, and the F-715SG firmware relies on its
				 * overflow cadence for the periodic LCD refresh and the
				 * ~7-minute auto power-off counter (0x3F increments every
				 * 4 overflows, threshold 0xD2).  Calibrate the wall-clock
				 * delivery to a 0.5 s overflow period (1280 timer cycles),
				 * i.e. 2560 cycles/s; the historical EPS6800 checkpoint
				 * delivered one cycle per period and effectively stalled
				 * Timer1 in idle. */
				constexpr uint32_t kEps6009IdleTimer1CyclesPerTick = 51;
				const auto elapsed = now - idle_timer_checkpoint_;
				const auto ticks = static_cast<uint32_t>(elapsed / kIdleTimerPeriod);
				if (ticks != 0) {
					idle_timer_checkpoint_ += kIdleTimerPeriod * ticks;
					machine_state_tick_idle_timer1(state_, ticks *
						(variant_ == EpsVariant::Eps6009 ? kEps6009IdleTimer1CyclesPerTick : 1));
				}
			}
			return false;
		}
		idle_timer_checkpoint_ = now;
		for (uint32_t i = 0; i < kFrameInstructions; ++i) {
			if (RunInstructionLocked(true)) {
				stopped = true;
				break;
			}
		}
		return stopped;
	}

	bool ePSCPU::RunInstructionLocked(bool tick_timer) {
		if (ConsumeBreakRequestLocked())
			return true;
		memory_break_pending_ = false;
		const uint32_t pc_before = state_->cpu.pc;
		const uint8_t stack_pointer_before = state_->mmio.regs[REG_STKPTR] & 0x1f;
		const uint8_t interrupt_pending = state_->cpu.int_pending;
		const uint32_t instruction = machine_state_debug_fetch_instruction(state_, pc_before);
		const uint16_t word = static_cast<uint16_t>(instruction >> 16);
		const uint8_t base_cycles = EpsInstructionCycles(word, variant_);
		bool advance_timer = false;
		if (tick_timer && ++timer_cycle_phase_ >= timer_cycle_divisor_) {
			timer_cycle_phase_ = 0;
			advance_timer = true;
		}
		/* Run one instruction and pace the timers with this instruction's
		 * weighted cycle count (1 or 2), so 2-cycle instructions advance the
		 * timers twice — matching the reference ice.dll model. The keyboard
		 * debounce counters keep the per-instruction cadence. */
		machine_state_advance_instruction_cycles(state_, base_cycles, tick_timer, advance_timer);
		++instruction_count_;

		const uint32_t pc_after = state_->cpu.pc;
		uint8_t elapsed_cycles = base_cycles;
		if (elapsed_cycles == 1 && pc_after != pc_before + EpsInstructionWords(word, variant_))
			elapsed_cycles = 2; // ePS6800 control-flow / PC-write penalty.
		cycle_count_ += elapsed_cycles;
		const uint8_t stack_pointer_after = state_->mmio.regs[REG_STKPTR] & 0x1f;
		RecordTraceLocked(pc_before, instruction, pc_after);
		if (function_hook_ && IsEpsCallInstruction(word)) {
			function_hook_(pc_after, pc_before + EpsInstructionWords(word, variant_), true,
				state_->mmio.regs[REG_ACC], BacktraceLocked());
		}
		else if (function_hook_ && IsEpsReturnInstruction(word)) {
			function_hook_(pc_after, pc_after, false, state_->mmio.regs[REG_ACC], BacktraceLocked());
		}
		if (interrupt_hook_ && interrupt_pending && stack_pointer_after > stack_pointer_before) {
			interrupt_hook_((interrupt_pending & INT_LEVEL4_TIMINT) ? 4 : 1);
		}

		Eps6800DebugStopReason reason = Eps6800DebugStopReason::None;
		if (instruction_hook_ && instruction_hook_(pc_before, pc_after, stack_pointer_after))
			reason = Eps6800DebugStopReason::Hook;
		if (reason == Eps6800DebugStopReason::None && !ShouldStopLocked(pc_after, stack_pointer_after, reason))
			return false;
		RecordStopLocked(reason, pc_after);
		debug_run_mode_ = DebugRunMode::Continue;
		return true;
	}

	size_t ePSCPU::RomWordCount() const {
		const std::lock_guard lock(state_mutex_);
		return rom_word_count_;
	}

	bool ePSCPU::ConsumeBreakRequestLocked() {
		if (!break_requested_.exchange(false, std::memory_order_acquire))
			return false;
		RecordStopLocked(Eps6800DebugStopReason::Break, state_->cpu.pc);
		debug_run_mode_ = DebugRunMode::Continue;
		return true;
	}

	bool ePSCPU::ShouldStopLocked(uint32_t pc_after, uint8_t stack_pointer_after,
		Eps6800DebugStopReason& reason) {
		if (memory_break_pending_) {
			reason = Eps6800DebugStopReason::MemoryBreakpoint;
			return true;
		}
		switch (debug_run_mode_) {
		case DebugRunMode::StepInto:
			reason = Eps6800DebugStopReason::Step;
			return true;
		case DebugRunMode::StepOver:
			if (pc_after == debug_target_pc_ && stack_pointer_after <= debug_target_stack_pointer_) {
				reason = Eps6800DebugStopReason::StepOver;
				return true;
			}
			break;
		case DebugRunMode::StepOut:
			if (pc_after == debug_target_pc_ && stack_pointer_after <= debug_target_stack_pointer_) {
				reason = Eps6800DebugStopReason::StepOut;
				return true;
			}
			break;
		case DebugRunMode::RunToAddress:
			if (pc_after == debug_target_pc_) {
				reason = Eps6800DebugStopReason::RunToAddress;
				return true;
			}
			break;
		case DebugRunMode::Continue:
			break;
		}

		if (honor_execution_breakpoints_) {
			if (auto it = execution_breakpoints_.find(pc_after);
				it != execution_breakpoints_.end() && it->second.enabled) {
				++it->second.hit_count;
				if (it->second.hit_count > it->second.skip_count) {
					reason = Eps6800DebugStopReason::ExecutionBreakpoint;
					return true;
				}
			}
		}
		return false;
	}

	void ePSCPU::RecordStopLocked(Eps6800DebugStopReason reason, uint32_t pc) {
		last_debug_stop_.reason = reason;
		last_debug_stop_.program_counter = pc;
		last_debug_stop_.instruction_count = instruction_count_;
		last_debug_stop_.cycle_count = cycle_count_;
		if (reason == Eps6800DebugStopReason::MemoryBreakpoint) {
			last_debug_stop_.memory_address = pending_memory_address_;
			last_debug_stop_.memory_value = pending_memory_value_;
			last_debug_stop_.memory_write = pending_memory_write_;
		}
	}

	bool ePSCPU::MemoryAccessThunk(void* user, uint32_t address, uint8_t* value, bool write, bool before) {
		return user && value
			? static_cast<ePSCPU*>(user)->OnMemoryAccessLocked(address, *value, write, before)
			: false;
	}

	bool ePSCPU::OnMemoryAccessLocked(uint32_t address, uint8_t& value, bool write, bool before) {
		if (before)
			return memory_hook_ ? memory_hook_(address, value, write) : false;
		for (auto& breakpoint : memory_breakpoints_) {
			if (!breakpoint.enabled || breakpoint.address != address || breakpoint.write != write)
				continue;
			if (breakpoint.compare_data && ((value & breakpoint.mask) != (breakpoint.data & breakpoint.mask)))
				continue;
			++breakpoint.hit_count;
			memory_breakpoint_hits_.push_back({state_->cpu.pc, address, value, write, instruction_count_ + 1});
			if (memory_breakpoint_hits_.size() > 4096)
				memory_breakpoint_hits_.pop_front();
			if (breakpoint.break_when_hit && honor_memory_breakpoints_ &&
				breakpoint.hit_count > breakpoint.skip_count && !memory_break_pending_) {
				memory_break_pending_ = true;
				pending_memory_address_ = address;
				pending_memory_value_ = value;
				pending_memory_write_ = write;
			}
		}
		return false;
	}

	void ePSCPU::RecordTraceLocked(uint32_t pc_before, uint32_t instruction, uint32_t pc_after) {
		if (!trace_enabled_ || trace_capacity_ == 0)
			return;
		Eps6800TraceEntry entry{};
		entry.program_counter = pc_before;
		entry.instruction = instruction;
		entry.next_program_counter = pc_after;
		entry.stack_pointer = state_->mmio.regs[REG_STKPTR] & 0x1f;
		entry.accumulator = state_->mmio.regs[REG_ACC];
		entry.status = state_->cpu.status;
		entry.instruction_count = instruction_count_;
		entry.cycle_count = cycle_count_;
		if (trace_buffer_.size() >= trace_capacity_)
			trace_buffer_.pop_front();
		trace_buffer_.push_back(entry);
	}

	void ePSCPU::KeyDown(uint8_t matrix_index) {
		const std::lock_guard lock(state_mutex_);
		machine_state_keydown(state_, matrix_index);
	}

	void ePSCPU::RestoreKeyDown(uint8_t matrix_index) {
		const std::lock_guard lock(state_mutex_);
		machine_state_restore_keydown(state_, matrix_index);
	}

	void ePSCPU::KeyUp(uint8_t matrix_index) {
		const std::lock_guard lock(state_mutex_);
		machine_state_keyup(state_, matrix_index);
	}

	void ePSCPU::OnDown() {
		const std::lock_guard lock(state_mutex_);
		machine_state_ondown(state_);
	}

	void ePSCPU::OnUp() {
		const std::lock_guard lock(state_mutex_);
		machine_state_onup(state_);
	}

	size_t ePSCPU::CopyLcd(uint8_t* output, size_t size, Eps6800LcdControl* control) const {
		const std::lock_guard lock(state_mutex_);
		machine_lcd_control raw_control{};
		const auto copied = machine_state_lcd_copy_display(
			state_, output, size, control ? &raw_control : nullptr);
		if (control) {
			control->lcdarh = raw_control.lcdarh;
			control->lcdcon = raw_control.lcdcon;
			control->contrast = variant_ == EpsVariant::Eps6009 ? 0x0f : static_cast<uint8_t>(
				(raw_control.lcdarh & MASK_LCD_CONTRAST) >> SHIFT_LCD_CONTRAST);
			control->display_on = (raw_control.lcdcon & BIT_LCD_ON) != 0;
			control->blanked = (raw_control.lcdcon & BIT_LCD_BLANK) != 0;
		}
		return copied;
	}

	size_t ePSCPU::LcdRawSize() const {
		return eps_lcd_raw_size(ToCoreVariant(variant_));
	}

	uint8_t ePSCPU::ReadByte(uint8_t address) {
		const std::lock_guard lock(state_mutex_);
		return machine_state_debug_read_byte(state_, address);
	}

	void ePSCPU::WriteByte(uint8_t address, uint8_t value) {
		const std::lock_guard lock(state_mutex_);
		machine_state_debug_write_byte(state_, address, value);
	}

	uint8_t ePSCPU::ReadDebugMemory(uint32_t linear_address) const {
		const std::lock_guard lock(state_mutex_);
		return machine_state_debug_peek_memory(state_, linear_address);
	}

	bool ePSCPU::WriteDebugMemory(uint32_t linear_address, uint8_t value) {
		const std::lock_guard lock(state_mutex_);
		return machine_state_debug_write_memory(state_, linear_address, value);
	}

	uint16_t ePSCPU::ReadCodeWord(uint32_t word_address) const {
		const std::lock_guard lock(state_mutex_);
		return machine_state_debug_read_rom_word(state_, word_address);
	}

	bool ePSCPU::WriteCodeWord(uint32_t word_address, uint16_t value) {
		const std::lock_guard lock(state_mutex_);
		return machine_state_debug_write_rom_word(state_, word_address, value);
	}

	uint8_t ePSCPU::ReadLcdMemory(size_t address) const {
		const std::lock_guard lock(state_mutex_);
		return address < LcdRawSize() ? state_->lcd.fb[address] : 0xff;
	}

	bool ePSCPU::WriteLcdMemory(size_t address, uint8_t value) {
		const std::lock_guard lock(state_mutex_);
		if (address >= LcdRawSize())
			return false;
		state_->lcd.fb[address] = value;
		return true;
	}

	Eps6800DebugSnapshot ePSCPU::DebugSnapshot() const {
		const std::lock_guard lock(state_mutex_);
		machine_debug_snapshot raw{};
		machine_state_debug_get_snapshot(state_, &raw);
		Eps6800DebugSnapshot result{};
		result.program_counter = raw.pc;
		std::copy(std::begin(raw.registers), std::end(raw.registers), result.registers.begin());
		std::copy(std::begin(raw.wbk_registers), std::end(raw.wbk_registers), result.wbk_registers.begin());
		std::copy(std::begin(raw.stack), std::end(raw.stack), result.stack.begin());
		result.stack_pointer = raw.stack_pointer;
		result.instruction_count = instruction_count_;
		result.cycle_count = cycle_count_;
		return result;
	}

	std::string ePSCPU::BacktraceLocked() const {
		std::ostringstream stream;
		const uint8_t depth = state_->mmio.regs[REG_STKPTR] & 0x1f;
		stream << "PC=" << std::hex << state_->cpu.pc;
		for (uint8_t i = depth; i > 0; --i)
			stream << " <- " << state_->cpu.stack[i - 1];
		return stream.str();
	}

	std::string ePSCPU::GetBacktrace() const {
		const std::lock_guard lock(state_mutex_);
		return BacktraceLocked();
	}

	std::vector<uint8_t> ePSCPU::ExportRam() const {
		const std::lock_guard lock(state_mutex_);
		std::vector<uint8_t> data;
		data.reserve(sizeof(state_->mmio.ram) + sizeof(state_->mmio.ram_wbk) + 0x0d + 0x40);
		data.insert(data.end(), std::begin(state_->mmio.ram), std::end(state_->mmio.ram));
		data.insert(data.end(), std::begin(state_->mmio.ram_wbk), std::end(state_->mmio.ram_wbk));
		data.insert(data.end(), &state_->mmio.regs[0x13], &state_->mmio.regs[0x20]);
		data.insert(data.end(), &state_->mmio.regs[0x40], &state_->mmio.regs[0x80]);
		return data;
	}

	bool ePSCPU::ImportRam(const std::vector<uint8_t>& data) {
		const std::lock_guard lock(state_mutex_);
		const size_t bank_ram_size = sizeof(state_->mmio.ram);
		const size_t legacy_persistent_ram_size = bank_ram_size + sizeof(state_->mmio.ram_wbk);
		const size_t persistent_ram_size = legacy_persistent_ram_size + 0x0d + 0x40;
		// Older builds wrote only the banked 8 KiB. Keep those images usable while
		// including the WBK window in all newly written images.
		if (data.size() != bank_ram_size && data.size() != legacy_persistent_ram_size &&
			data.size() != persistent_ram_size)
			return false;
		std::copy_n(data.begin(), bank_ram_size, std::begin(state_->mmio.ram));
		if (data.size() >= legacy_persistent_ram_size)
			std::copy_n(data.begin() + bank_ram_size, sizeof(state_->mmio.ram_wbk),
				std::begin(state_->mmio.ram_wbk));
		if (data.size() == persistent_ram_size) {
			auto source = data.begin() + legacy_persistent_ram_size;
			std::copy_n(source, 0x0d, &state_->mmio.regs[0x13]);
			std::copy_n(source + 0x0d, 0x40, &state_->mmio.regs[0x40]);
		}
		return true;
	}

	void ePSCPU::RequestContinue(bool honor_breakpoints) {
		const std::lock_guard lock(state_mutex_);
		debug_run_mode_ = DebugRunMode::Continue;
		honor_execution_breakpoints_ = honor_breakpoints;
		honor_memory_breakpoints_ = honor_breakpoints;
		last_debug_stop_ = {};
	}

	void ePSCPU::RequestStepInto() {
		const std::lock_guard lock(state_mutex_);
		debug_run_mode_ = DebugRunMode::StepInto;
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		last_debug_stop_ = {};
	}

	void ePSCPU::RequestStepOver() {
		const std::lock_guard lock(state_mutex_);
		const uint32_t pc = state_->cpu.pc;
		const uint16_t word = machine_state_debug_read_rom_word(state_, pc);
		last_debug_stop_ = {};
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		if (!IsEpsCallInstruction(word)) {
			debug_run_mode_ = DebugRunMode::StepInto;
			return;
		}
		debug_run_mode_ = DebugRunMode::StepOver;
		debug_target_pc_ = pc + EpsInstructionWords(word, variant_);
		debug_target_stack_pointer_ = state_->mmio.regs[REG_STKPTR] & 0x1f;
	}

	bool ePSCPU::RequestStepOut() {
		const std::lock_guard lock(state_mutex_);
		const uint8_t stack_pointer = state_->mmio.regs[REG_STKPTR] & 0x1f;
		if (stack_pointer == 0)
			return false;
		debug_run_mode_ = DebugRunMode::StepOut;
		debug_target_stack_pointer_ = stack_pointer - 1;
		debug_target_pc_ = state_->cpu.stack[stack_pointer - 1];
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		last_debug_stop_ = {};
		return true;
	}

	void ePSCPU::RequestRunToAddress(uint32_t word_address) {
		const std::lock_guard lock(state_mutex_);
		debug_run_mode_ = DebugRunMode::RunToAddress;
		debug_target_pc_ = word_address;
		honor_execution_breakpoints_ = true;
		honor_memory_breakpoints_ = true;
		last_debug_stop_ = {};
	}

	void ePSCPU::CancelDebugRun() {
		const std::lock_guard lock(state_mutex_);
		debug_run_mode_ = DebugRunMode::Continue;
		last_debug_stop_ = {};
	}

	void ePSCPU::RequestBreak() {
		break_requested_.store(true, std::memory_order_release);
	}

	void ePSCPU::AddExecutionBreakpoint(uint32_t word_address) {
		const std::lock_guard lock(state_mutex_);
		if (word_address >= rom_word_count_)
			return;
		execution_breakpoints_.try_emplace(word_address,
			Eps6800ExecutionBreakpoint{.address = word_address});
	}

	bool ePSCPU::ConfigureExecutionBreakpoint(const Eps6800ExecutionBreakpoint& breakpoint) {
		const std::lock_guard lock(state_mutex_);
		if (breakpoint.address >= rom_word_count_)
			return false;
		execution_breakpoints_[breakpoint.address] = breakpoint;
		return true;
	}

	void ePSCPU::RemoveExecutionBreakpoint(uint32_t word_address) {
		const std::lock_guard lock(state_mutex_);
		execution_breakpoints_.erase(word_address);
	}

	void ePSCPU::ClearExecutionBreakpoints() {
		const std::lock_guard lock(state_mutex_);
		execution_breakpoints_.clear();
	}

	std::vector<uint32_t> ePSCPU::ExecutionBreakpoints() const {
		const std::lock_guard lock(state_mutex_);
		std::vector<uint32_t> result;
		result.reserve(execution_breakpoints_.size());
		for (const auto& [address, breakpoint] : execution_breakpoints_)
			result.push_back(address);
		std::sort(result.begin(), result.end());
		return result;
	}

	std::vector<Eps6800ExecutionBreakpoint> ePSCPU::ExecutionBreakpointDetails() const {
		const std::lock_guard lock(state_mutex_);
		std::vector<Eps6800ExecutionBreakpoint> result;
		result.reserve(execution_breakpoints_.size());
		for (const auto& [address, breakpoint] : execution_breakpoints_)
			result.push_back(breakpoint);
		std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.address < rhs.address;
		});
		return result;
	}

	bool ePSCPU::AddMemoryBreakpoint(const Eps6800MemoryBreakpoint& breakpoint) {
		if (breakpoint.address >= 0x2080u)
			return false;
		const std::lock_guard lock(state_mutex_);
		auto it = std::find_if(memory_breakpoints_.begin(), memory_breakpoints_.end(), [&](const auto& item) {
			return item.address == breakpoint.address && item.write == breakpoint.write;
		});
		if (it == memory_breakpoints_.end()) {
			memory_breakpoints_.push_back(breakpoint);
		}
		else {
			/* Preserve the running hit/skip progress when an existing
			 * breakpoint is reconfigured. */
			const uint64_t preserved_hit_count = it->hit_count;
			*it = breakpoint;
			it->hit_count = preserved_hit_count;
		}
		++memory_breakpoint_version_;
		return true;
	}

	bool ePSCPU::RemoveMemoryBreakpoint(uint32_t address, bool write) {
		const std::lock_guard lock(state_mutex_);
		auto it = std::find_if(memory_breakpoints_.begin(), memory_breakpoints_.end(), [&](const auto& item) {
			return item.address == address && item.write == write;
		});
		if (it == memory_breakpoints_.end())
			return false;
		memory_breakpoints_.erase(it);
		++memory_breakpoint_version_;
		return true;
	}

	void ePSCPU::ClearMemoryBreakpoints() {
		const std::lock_guard lock(state_mutex_);
		memory_breakpoints_.clear();
		memory_break_pending_ = false;
		++memory_breakpoint_version_;
	}

	std::vector<Eps6800MemoryBreakpoint> ePSCPU::MemoryBreakpoints() const {
		const std::lock_guard lock(state_mutex_);
		return memory_breakpoints_;
	}

	uint64_t ePSCPU::MemoryBreakpointsVersion() const {
		const std::lock_guard lock(state_mutex_);
		return memory_breakpoint_version_;
	}

	std::vector<Eps6800MemoryBreakpointHit> ePSCPU::MemoryBreakpointHits(uint32_t address, bool write) const {
		const std::lock_guard lock(state_mutex_);
		std::vector<Eps6800MemoryBreakpointHit> result;
		for (const auto& hit : memory_breakpoint_hits_) {
			if (hit.address == address && hit.write == write)
				result.push_back(hit);
		}
		return result;
	}

	void ePSCPU::ClearMemoryBreakpointHits() {
		const std::lock_guard lock(state_mutex_);
		memory_breakpoint_hits_.clear();
	}

	Eps6800DebugStop ePSCPU::LastDebugStop() const {
		const std::lock_guard lock(state_mutex_);
		return last_debug_stop_;
	}

	void ePSCPU::EnableTrace(bool enabled) {
		const std::lock_guard lock(state_mutex_);
		trace_enabled_ = enabled;
	}

	void ePSCPU::ClearTrace() {
		const std::lock_guard lock(state_mutex_);
		trace_buffer_.clear();
	}

	void ePSCPU::SetTraceCapacity(size_t capacity) {
		const std::lock_guard lock(state_mutex_);
		trace_capacity_ = capacity;
		while (trace_buffer_.size() > trace_capacity_)
			trace_buffer_.pop_front();
	}

	std::vector<Eps6800TraceEntry> ePSCPU::TraceBuffer() const {
		const std::lock_guard lock(state_mutex_);
		return {trace_buffer_.begin(), trace_buffer_.end()};
	}

	uint32_t ePSCPU::PC() const {
		return ProgramCounter() << 1;
	}

	uint32_t ePSCPU::ProgramCounter() const {
		const std::lock_guard lock(state_mutex_);
		return state_->cpu.pc;
	}

	void ePSCPU::SetPC(uint32_t word_address) {
		const std::lock_guard lock(state_mutex_);
		state_->cpu.pc = word_address & 0x00ffffffu;
		state_->mmio.regs[REG_PCL] = static_cast<uint8_t>(state_->cpu.pc);
		state_->mmio.regs[REG_PCM] = static_cast<uint8_t>(state_->cpu.pc >> 8);
		state_->mmio.regs[REG_PCH] = static_cast<uint8_t>(state_->cpu.pc >> 16);
	}

	void ePSCPU::SaveState(std::ostream& stream) const {
		size_t size = 0;
		machine_snapshot* snapshot = nullptr;
		{
			const std::lock_guard lock(state_mutex_);
			snapshot = machine_state_save_snapshot(state_, &size);
		}
		if (!snapshot || size > std::numeric_limits<uint32_t>::max()) {
			machine_snapshot_free(snapshot);
			throw std::runtime_error("Failed to capture EPS6800 snapshot");
		}

		const void* data = machine_snapshot_data(snapshot, &size);
		const uint32_t encoded_size = static_cast<uint32_t>(size);
		stream.write(reinterpret_cast<const char*>(&kSnapshotMagic), sizeof(kSnapshotMagic));
		stream.write(reinterpret_cast<const char*>(&encoded_size), sizeof(encoded_size));
		stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
		machine_snapshot_free(snapshot);
		if (!stream)
			throw std::runtime_error("Failed to write EPS6800 snapshot");
	}

	void ePSCPU::LoadState(std::istream& stream) {
		uint32_t magic = 0;
		uint32_t size = 0;
		stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		stream.read(reinterpret_cast<char*>(&size), sizeof(size));
		if (!stream || magic != kSnapshotMagic || size == 0 || size > kMaximumSnapshotSize)
			throw std::runtime_error("Invalid EPS6800 snapshot header");

		std::vector<uint8_t> data(size);
		stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!stream)
			throw std::runtime_error("Truncated EPS6800 snapshot");

		machine_snapshot* snapshot = machine_snapshot_from_data(data.data(), data.size());
		if (!snapshot)
			throw std::runtime_error("Invalid EPS6800 snapshot payload");
		{
			const std::lock_guard lock(state_mutex_);
			machine_state_load_snapshot(state_, snapshot);
			timer_cycle_phase_ = 0;
			idle_timer_checkpoint_ = std::chrono::steady_clock::now();
			machine_state_debug_set_memory_access_callback(state_, &ePSCPU::MemoryAccessThunk, this);
		}
		machine_snapshot_free(snapshot);
	}
} // namespace casioemu
