#include "T4xCore.hpp"
#include <algorithm>
#include <cstdio>
#include <istream>
#include <ostream>
#include <stdexcept>

namespace casioemu {
	T4xCore::T4xCore(unsigned dram_banks) : dram_banks(dram_banks) {
		if (dram_banks == 0 || dram_banks > 14)
			throw std::invalid_argument("T4x DRAM must contain 1..14 banks");
	}
	bool T4xCore::IsDataBankMapped(unsigned bank) const {
		return bank < dram_banks || bank >= 14; // Banks 14/15 contain LCD RAM.
	}
	bool T4xCore::LoadRom(const std::vector<uint8_t>& bytes) {
		const std::lock_guard lock(mutex);
		if (bytes.empty() || bytes.size() > rom.size() * 2 || bytes.size() % 2)
			return false;
		rom.fill(0);
		for (size_t i = 0; i < bytes.size() / 2; ++i)
			rom[i] = bytes[2 * i] | (bytes[2 * i + 1] << 8);
		ResetState();
		return true;
	}
	void T4xCore::Reset() {
		const std::lock_guard lock(mutex);
		ResetState();
	}
	void T4xCore::ResetState() {
		state = State{};
		keys.clear();
	}
	T4xCore::State T4xCore::Snapshot() const {
		const std::lock_guard lock(mutex);
		return state;
	}
	T4xCore::DisplayState T4xCore::ReadDisplay() const {
		const std::lock_guard lock(mutex);
		return {state.lcd, state.reg[LcdControlRegister], state.reg[LcdContrastRegister]};
	}
	uint16_t T4xCore::ProgramCounter() const {
		const std::lock_guard lock(mutex);
		return state.pc;
	}
	void T4xCore::Restore(const State& value) {
		const std::lock_guard lock(mutex);
		state = value;
		keys.clear();
	}
	uint16_t T4xCore::CodeWord(uint16_t address) const {
		const std::lock_guard lock(mutex);
		return rom[address];
	}
	void T4xCore::WriteCodeWord(uint16_t address, uint16_t value) {
		const std::lock_guard lock(mutex);
		rom[address] = value;
	}
	uint8_t T4xCore::ReadCodeByte(unsigned address) const {
		const std::lock_guard lock(mutex);
		return static_cast<uint8_t>(rom.at(address / 2) >> ((address & 1) * 8));
	}
	void T4xCore::WriteCodeByte(unsigned address, uint8_t value) {
		const std::lock_guard lock(mutex);
		auto& word = rom.at(address / 2);
		const unsigned shift = (address & 1) * 8;
		word = (word & ~(0xFFu << shift)) | (unsigned(value) << shift);
	}
	void T4xCore::SetPc(uint16_t address) {
		const std::lock_guard lock(mutex);
		state.pc = address;
		state.halted = false;
	}
	uint8_t T4xCore::ReadDebugMemory(unsigned address) const {
		if (address < 64)
			return ReadMemory(0, address);
		if (address >= 0x100 && address < 0x500)
			return ReadMemory(1, address - 0x100);
		if (address >= 0x1000 && address < 0x2000)
			return ReadMemory(2, address - 0x1000);
		return 0;
	}
	void T4xCore::WriteDebugMemory(unsigned address, uint8_t value) {
		if (address < 64)
			WriteMemory(0, address, value);
		else if (address >= 0x100 && address < 0x500)
			WriteMemory(1, address - 0x100, value);
		else if (address >= 0x1000 && address < 0x2000)
			WriteMemory(2, address - 0x1000, value);
	}
	uint8_t& T4xCore::Work(unsigned bank, unsigned row, unsigned col) {
		return state.work[(bank * 16 + row) * 16 + col];
	}
	unsigned T4xCore::StackPointer() const {
		return ((state.reg[9] & 7) * 16 + (state.reg[8] & 14)) / 2;
	}
	void T4xCore::StackPointer(unsigned value) {
		value &= 63;
		state.reg[8] = (value * 2) & 15;
		state.reg[9] = value >> 3;
	}
	void T4xCore::PushByte(uint8_t value) {
		unsigned sp = (StackPointer() - 1) & 63;
		Work(0, 8 + sp / 8, sp % 8 * 2) = value & 15;
		Work(0, 8 + sp / 8, sp % 8 * 2 + 1) = value >> 4;
		StackPointer(sp);
	}
	uint8_t T4xCore::PopByte() {
		unsigned sp = StackPointer();
		uint8_t value = Work(0, 8 + sp / 8, sp % 8 * 2) | (Work(0, 8 + sp / 8, sp % 8 * 2 + 1) << 4);
		StackPointer(sp + 1);
		return value;
	}
	void T4xCore::PushPc() {
		if (state.halted) {
			++state.pc;
			state.halted = false;
		}
		PushByte(state.pc & 255);
		PushByte(state.pc >> 8);
	}
	void T4xCore::DataNext() {
		auto& r = state.reg;
		if (!(r[23] & 1))
			return;
		unsigned address = ((r[27] << 8) | (r[29] << 4) | r[28]);
		address = (address + ((r[23] & 2) ? -1 : 1)) & 4095;
		r[27] = address >> 8;
		r[29] = (address >> 4) & 15;
		r[28] = address & 15;
	}
	void T4xCore::ReadKey(unsigned index) {
		auto& r = state.reg;
		unsigned row = state.key >> 4, col = state.key & 15;
		unsigned rows = r[12] | (r[13] << 4);
		unsigned bits = 0;
		if (state.key != 255 && row > 0 && row <= 7 && (rows & (1 << (row - 1))))
			bits = 1 << ((col ? col : 8) - 1);
		r[index] = (bits >> ((index - 16) * 4)) & 15;
	}
	bool T4xCore::ReadPair(unsigned index, uint8_t& value) {
		auto& r = state.reg;
		if (index >= 63)
			throw std::runtime_error("T4x register pair exceeds register file");
		if (index == 48)
			return false; // Official core treats DMA count as write-only here.
		if (index == 16) {
			ReadKey(16);
			ReadKey(17);
		}
		if (index != 30) {
			value = r[index] | (r[index + 1] << 4);
			return true;
		}
		unsigned bank = r[27];
		if (!IsDataBankMapped(bank))
			return false;
		bool enabled = bank < 14 ? (r[24] & 1) : (r[24] & 2);
		if (enabled) {
			value = state.data[(bank << 8) | (r[29] << 4) | r[28]];
			r[30] = value & 15;
			r[31] = value >> 4;
		}
		DataNext();
		return enabled;
	}
	void T4xCore::LoadPair(unsigned index, uint8_t value, bool pop) {
		auto& r = state.reg;
		if (index >= 63)
			throw std::runtime_error("T4x register pair exceeds register file");
		if (index != 30) {
			r[index] = value & 15;
			r[index + 1] = value >> 4;
			RegisterEffects(index, true, pop, false);
			return;
		}
		unsigned bank = r[27];
		if (!IsDataBankMapped(bank))
			return;
		if (bank < 14 ? (r[24] & 1) : (r[24] & 2)) {
			r[30] = value & 15;
			r[31] = value >> 4;
			state.data[(bank << 8) | (r[29] << 4) | r[28]] = value;
		}
		else
			state.blocked_write = true;
		DataNext();
	}
	void T4xCore::RegisterEffects(unsigned index, bool pair, bool pop, bool immediate) {
		auto& r = state.reg;
		switch (index) {
		case 8:
			r[8] &= 14;
			if (pair)
				r[9] &= 7;
			break;
		case 9:
			r[9] &= 7;
			break;
		case 10:
			state.timer_enabled[0] = r[10] & 4;
			state.timer_enabled[1] = r[10] & 8;
			break;
		case 42:
			r[40] &= 13;
			if (pair)
				r[40] &= 11;
			break;
		case 43:
			r[40] &= 11;
			break;
		case 44:
			if ((r[44] & 1) && !state.timers) {
				state.timer_remaining[0] = 350000;
				state.timer_remaining[1] = 6000;
			}
			state.timers = r[44] & 1;
			break;
		case 22:
			// POP uses the opposite edge in the reference core. Preserve that distinction.
			if (pop ? (r[22] & 2) : !(r[22] & 2))
				state.lcd_dirty = true;
			break;
		case 24:
			if (r[24] & 1) {
				state.key = 255;
				if (immediate)
					r[47] &= 14;
			}
			if (immediate)
				state.ready = true;
			break;
		case 58:
			if (r[58] & 1)
				Dma();
			break;
		}
	}
	void T4xCore::Dma() {
		auto& r = state.reg;
		unsigned src = (r[51] << 8) | (r[53] << 4) | r[52];
		unsigned dst = (r[59] << 8) | (r[61] << 4) | r[60];
		unsigned count = ((r[50] & 7) << 8) | (r[49] << 4) | r[48];
		unsigned shift = r[56] & 7, carry = 0;
		for (unsigned i = 0; i < count; ++i, ++src, ++dst) {
			unsigned bank = src >> 8;
			if (!IsDataBankMapped(bank) || src >= 4096 || dst >= 4096)
				break;
			unsigned byte = state.data[src], previous = carry;
			carry = 0;
			// JS shifts are modulo 32; this reproduces its serial DMA shifter.
			for (unsigned b = 0; b < shift; ++b)
				carry += byte & (1u << ((b - 1) & 31));
			carry *= 1u << (8 - shift);
			state.data[dst] = (byte >> shift) + previous;
		}
		if ((r[11] & 2) && (r[58] & 8)) {
			state.pending |= 16;
			r[41] |= 1;
		}
		r[58] &= 14;
	}
	void T4xCore::Interrupts() {
		auto& r = state.reg;
		const bool enabled[6] = {bool(r[10] & 2), state.timer_enabled[0], state.timer_enabled[1], true, true, true};
		for (unsigned i = 0; i < 6; ++i)
			if ((state.pending & (1 << i)) && enabled[i]) {
				PushPc();
				state.pc = rom[i + 1] & 4095;
				state.pending &= ~(1 << i);
				r[40 + i / 4] &= ~(1 << (i % 4));
			}
	}
	unsigned T4xCore::Step() {
		const std::lock_guard lock(mutex);
		return StepInstruction();
	}
	unsigned T4xCore::StepInstruction() {
		auto& r = state.reg;
		uint16_t ins = rom[state.pc];
		unsigned op = (ins & 0xFE00) >> 10, variant = (ins >> 8) & 3;
		unsigned lo = ins & 15, mid = (ins >> 4) & 15;
		unsigned src = (ins >> 4) & 7, dst = (ins >> 7) & 7, hi = (ins >> 4) & 63;
		unsigned bank = r[0] >> 2, row = r[3], col = r[2];
		unsigned cost = 1;
		const uint8_t previous_flags = r[0];
		bool old_page = state.page_written;
		state.page_written = false;
		auto flags = [&](int value, bool carry) {
			r[0] = (previous_flags & 12) | (value == 0 ? 2 : 0) | (carry ? 1 : 0);
		};
		auto zero = [&](int value) { r[0] = (previous_flags & 13) | (value == 0 ? 2 : 0); };
		auto arithmetic = [&](int value) {
			bool carry = value < 0 || value >= 16;
			r[dst] = value & 15;
			flags(value & 15, carry);
			state.page_written = dst == 7;
		};
		switch (op) {
		case 0:
			if (variant == 2) {
				cost = 2;
				state.pc += 1 + ((ins & 7) ? r[ins & 7] : 0);
			}
			else if (variant == 3) {
				cost = 3;
				uint16_t upper = PopByte();
				state.pc = (upper << 8) | PopByte();
			}
			else if (variant == 0 && ((ins >> 6) & 3) == 2) {
				state.halted = true;
			}
			else if (variant == 0 && ((ins >> 6) & 3) == 0)
				++state.pc;
			else
				throw std::runtime_error("Unsupported T4x control instruction");
			break;
		case 1:
			if (variant < 2) {
				cost = 2;
				uint16_t address = (r[7] << 12) | (r[6] << 8) | (r[5] << 4) | r[4];
				unsigned value = (rom[address] >> (variant * 8)) & 255;
				Work(bank, row, col & 14) = value & 15;
				Work(bank, row, col | 1) = value >> 4;
			}
			else if (variant == 2) {
				Work(bank, row, col) = lo;
				r[2] = (r[2] + mid) & 15;
			}
			else {
				Work(bank, row, col & 14) = lo;
				Work(bank, row, (col & 14) + 1) = mid;
			}
			++state.pc;
			break;
		case 2:
			cost = 2;
			if (variant == 0 || variant == 2) {
				int result = Work(bank, row, col) + (variant == 0 ? int(lo) : -int(lo));
				Work(bank, row, col) = result & 15;
				flags(result & 15, result < 0 || result >= 16);
			}
			++state.pc;
			break;
		case 3: {
			cost = variant == 3 ? 0 : 2; // Reference CMP does not increment its cycle counter.
			auto& m = Work(bank, row, col);
			if (variant == 0) {
				m &= lo;
				zero(m);
			}
			if (variant == 1) {
				m |= lo;
				zero(m);
			}
			if (variant == 2) {
				m ^= lo;
				zero(m);
			}
			if (variant == 3) {
				int result = m - lo;
				flags(result, result < 0);
			}
			++state.pc;
			break;
		}
		case 5:
			cost = variant == 2 ? 4 : variant == 0 ? 2
												   : 0;
			if (variant == 0)
				Work(bank, row, col) = Work(bank, mid, lo);
			else if (variant == 2)
				std::swap(Work(bank, row, col), Work(bank, mid, lo));
			++state.pc;
			break;
		case 6:
		case 7: {
			cost = op == 6 ? 3 : variant == 2 ? 4
											  : 2;
			if (op == 6 || variant == 3)
				r[0] |= 2;
			do {
				auto& a = Work(r[0] >> 2, r[3], op == 7 && variant < 2 ? (r[2] + lo) & 15 : r[2]);
				auto& b = Work(r[0] >> 2, mid, r[2]);
				if (op == 7 && variant < 2)
					a = b;
				else if (op == 7 && variant == 2)
					std::swap(a, b);
				else {
					int carry = r[0] & 1;
					bool sub = op == 7 || variant >= 2;
					int result = a + (sub ? -int(b) - carry : int(b) + carry);
					int radix = op == 6 && (variant & 1) ? 10 : 16;
					bool overflow = sub ? result < 0 : result >= radix;
					if (overflow && !(op == 7 && variant == 3))
						result = sub ? result + radix : result % radix;
					if (!(op == 7 && variant == 3))
						a = result;
					// Block operations accumulate Z rather than resetting it for each digit.
					r[0] = (r[0] & 12) | ((r[0] & 2) && result == 0 ? 2 : 0) | (overflow ? 1 : 0);
				}
				r[2] = (r[2] + (op == 7 && variant == 1 ? -1 : 1)) & 15;
				r[1] = (r[1] - 1) & 15;
			} while (r[1]);
			++state.pc;
			break;
		}
		case 8:
		case 9:
		case 12:
		case 13: {
			int value = op < 10 ? Work(bank, row, col) : (src ? r[src] : 0);
			unsigned count = (ins & 3) + 1;
			for (unsigned i = 0; i < count; ++i) {
				bool carry;
				if (op & 1) {
					carry = value & 8;
					value = (value << 1) & 15;
				}
				else {
					carry = value & 1;
					value >>= 1;
				}
				r[0] = (r[0] & 14) | static_cast<unsigned>(carry);
			}
			uint8_t shifted_flags = r[0];
			r[dst] = value;
			r[0] = shifted_flags;
			state.page_written = dst == 7;
			++state.pc;
			break;
		}
		case 10:
		case 31: {
			uint8_t value;
			if (ReadPair(hi, value)) {
				if (op == 10)
					PushByte(value);
				else {
					Work(bank, row, col & 14) = value & 15;
					Work(bank, row, (col & 14) + 1) = value >> 4;
				}
			}
			else if (op == 10)
				StackPointer(StackPointer() - 1);
			++state.pc;
			break;
		}
		case 11:
		case 30: {
			unsigned saved_sp = StackPointer();
			uint8_t value = op == 11 ? PopByte() : Work(bank, row, col & 14) | (Work(bank, row, (col & 14) + 1) << 4);
			LoadPair(hi, value, op == 11);
			if (op == 11)
				StackPointer(saved_sp + 1);
			state.page_written = hi == 6;
			++state.pc;
			break;
		}
		case 14:
			r[hi] = Work(bank, row, col);
			RegisterEffects(hi, false, false, false);
			state.page_written = hi == 7;
			++state.pc;
			break;
		case 15:
			if (hi == 16 || hi == 17)
				ReadKey(hi);
			Work(bank, row, col) = r[hi];
			++state.pc;
			break;
		case 16:
		case 17:
		case 18:
		case 19:
		case 21:
		case 23: {
			int a = src ? r[src] : 0;
			int b = (op == 16 || op == 18) ? lo : ((ins & 7) ? r[ins & 7] : 0);
			int c = (op == 21 || op == 23) ? r[0] & 1 : 0;
			arithmetic(op == 18 || op == 19 || op == 23 ? a - b - c : a + b + c);
			++state.pc;
			break;
		}
		case 20:
			r[hi] = lo;
			RegisterEffects(hi, false, false, true);
			state.page_written = hi == 7;
			++state.pc;
			break;
		case 22: {
			unsigned index = variant * 2;
			r[index] = lo;
			r[index + 1] = mid;
			state.page_written = index == 6;
			++state.pc;
			break;
		}
		case 24:
		case 25:
		case 26:
		case 27:
		case 28:
		case 29: {
			int a = src ? r[src] : 0, b = (op & 1) ? ((ins & 7) ? r[ins & 7] : 0) : lo;
			int value = op < 26 ? a & b : op < 28 ? a | b
												  : a ^ b;
			r[dst] = value;
			zero(value);
			state.page_written = dst == 7;
			++state.pc;
			break;
		}
		case 32:
		case 33:
		case 34:
		case 35:
			Work(bank, (ins >> 8) & 15, mid) = lo;
			++state.pc;
			break;
		case 36:
		case 37:
		case 38:
		case 39:
			cost = 0;
			++state.pc;
			PushPc();
			state.pc = (state.pc & 0xF000) | (ins & 4095);
			break;
		case 40:
		case 41:
		case 42:
		case 43:
			cost = 2;
			++state.pc;
			PushPc();
			state.pc = ins & 4095;
			break;
		case 44:
		case 45:
		case 46:
		case 47:
			state.pc = (r[0] & 2) ? (state.pc & 0xF000) | (ins & 4095) : state.pc + 1;
			break;
		case 48:
		case 49:
		case 50:
		case 51:
			state.pc = !(r[0] & 2) ? (state.pc & 0xF000) | (ins & 4095) : state.pc + 1;
			break;
		case 52:
		case 53:
		case 54:
		case 55:
			state.pc = (r[0] & 1) ? (state.pc & 0xF000) | (ins & 4095) : state.pc + 1;
			break;
		case 56:
		case 57:
		case 58:
		case 59:
			state.pc = !(r[0] & 1) ? (state.pc & 0xF000) | (ins & 4095) : state.pc + 1;
			break;
		case 60:
		case 61:
		case 62:
		case 63:
			state.pc = ((old_page ? r[7] << 12 : state.pc & 0xF000) | (ins & 4095));
			break;
		default:
			throw std::runtime_error("Unsupported T4x opcode");
		}
		state.cycles += cost;
		++state.instructions;
		Interrupts();
		if (!(r[22] & 2))
			Work(2, 8, 10) = 15;
		return cost;
	}
	bool T4xCore::KeyInterrupt(uint8_t code) const {
		const auto& r = state.reg;
		if (((state.pending & 2) && state.timer_enabled[0]) || ((state.pending & 4) && state.timer_enabled[1]))
			return false;
		unsigned row = code >> 4, col = code & 15;
		if (row >= 1 && row <= 6 && !((r[12] | (r[13] << 4)) & (1 << (row - 1))))
			return false;
		if (!col)
			return true;
		return col <= 8 && (r[11] & 1) && ((r[32] | (r[33] << 4)) & (1 << (col - 1)));
	}
	void T4xCore::ServiceKey() {
		auto& r = state.reg;
		if (keys.empty() || !(r[11] & 1) || (r[40] & 8))
			return;
		uint8_t code = keys.front();
		state.key = code;
		if (!code) {
			state.key = 255;
			r[47] |= 1;
			if (r[47] & 8)
				state.pending |= 1;
		}
		if (KeyInterrupt(code)) {
			state.pending |= 8;
			r[40] |= 8;
			keys.pop_front();
		}
		else {
			keys.pop_front();
			if (code != 45)
				keys.push_back(code);
		}
	}
	void T4xCore::Key(uint8_t code, bool pressed) {
		const std::lock_guard lock(mutex);
		if (!pressed && code == 0) {
			state.reg[47] &= 14;
			return;
		}
		// ON is a separate wake contact. The browser wrapper gates every key on
		// matrix IRQ enable, which is cleared by OFF and prevents it waking again.
		// Raise the existing ON interrupt directly while the matrix is disabled.
		if (pressed && code == 0 && !(state.reg[11] & 1)) {
			state.reg[47] |= 1;
			if (state.reg[47] & 8)
				state.pending |= 1;
			return;
		}
		// MultiView firmware clears its latched key through register 24. Releasing
		// the host key must not erase a press before the firmware scans it.
		if (pressed && keys.size() < 64) {
			keys.push_back(code);
			ServiceKey();
			state.input_remaining = 100000;
		}
	}
	void T4xCore::PublishLcd() {
		if (!state.lcd_dirty)
			return;
		std::copy_n(state.data.begin() + 14 * 256, LcdBytes, state.lcd.begin());
		state.lcd_dirty = false;
	}
	void T4xCore::AdvanceTime(uint32_t elapsed_us) {
		const std::lock_guard lock(mutex);
		AdvanceTimeLocked(elapsed_us);
	}
	void T4xCore::AdvanceTimeLocked(uint32_t elapsed_us) {
		if (state.timers)
			for (unsigned i = 0; i < 2; ++i) {
				if (elapsed_us >= state.timer_remaining[i]) {
					state.timer_remaining[i] = i ? 6000 : 350000;
					if (state.reg[42 + i] & 4) {
						state.pending |= 2 << i;
						state.reg[40] |= 2 << i;
					}
				}
				else
					state.timer_remaining[i] -= elapsed_us;
			}
		if (elapsed_us >= state.input_remaining) {
			state.input_remaining = 100000;
			ServiceKey();
		}
		else
			state.input_remaining -= elapsed_us;
		PublishLcd();
	}
	void T4xCore::RunBatch(unsigned count, uint32_t elapsed_us) {
		const std::lock_guard lock(mutex);
		for (unsigned i = 0; i < count; ++i)
			StepInstruction();
		AdvanceTimeLocked(elapsed_us);
	}
	uint8_t T4xCore::ReadMemory(unsigned space, unsigned address) const {
		const std::lock_guard lock(mutex);
		if (space == 0)
			return state.reg.at(address);
		if (space == 1)
			return state.work.at(address);
		return state.data.at(address);
	}
	void T4xCore::WriteMemory(unsigned space, unsigned address, uint8_t value) {
		const std::lock_guard lock(mutex);
		if (space == 0) {
			state.reg.at(address) = value & 15;
			RegisterEffects(address, false, false, false);
		}
		else if (space == 1)
			state.work.at(address) = value & 15;
		else
			state.data.at(address) = value;
	}
	std::string T4xCore::Disassemble(uint16_t address) const {
		uint16_t ins = CodeWord(address);
		unsigned op = (ins & 0xFE00) >> 10;
		static constexpr const char* names[32] = {"CTRL", "LDB/LD", "ADD M", "LOGIC M", "INVALID", "LD/SWAP M", "BLOCK ALU", "BLOCK MEM", "SHR M", "SHL M", "PUSH", "POP", "SHR", "SHL", "LD REG,M", "LD M,REG", "ADD #", "ADD", "SUB #", "SUB", "LD #", "ADC", "LD PAIR,#", "SBC", "AND #", "AND", "OR #", "OR", "XOR #", "XOR", "LD PAIR,M", "LD M,PAIR"};
		const char* name = op < 32 ? names[op] : op < 36 ? "LD WRAM,#"
											 : op < 40	 ? "CALL PAGE"
											 : op < 44	 ? "CALL"
											 : op < 48	 ? "JPZ"
											 : op < 52	 ? "JPNZ"
											 : op < 56	 ? "JPC"
											 : op < 60	 ? "JPNC"
														 : "JP";
		char text[96];
		std::snprintf(text, sizeof(text), "%04X  %04X  %s  %03X", address, ins, name, ins & 0x3FF);
		return text;
	}
	void T4xCore::SaveState(std::ostream& out) const {
		const std::lock_guard lock(mutex);
		out.write("T4X1", 4);
		// Fields are written separately: no compiler padding in the snapshot format.
		auto write = [&](const auto& v) { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
		write(state.reg);
		write(state.work);
		write(state.data);
		write(state.lcd);
		write(state.pc);
		write(state.cycles);
		write(state.instructions);
		write(state.timer_remaining);
		write(state.input_remaining);
		write(state.key);
		write(state.pending);
		for (bool b : {state.page_written, state.halted, state.timers, state.lcd_dirty, state.timer_enabled[0], state.timer_enabled[1], state.ready, state.blocked_write}) {
			uint8_t v = b;
			write(v);
		}
		uint8_t count = static_cast<uint8_t>(keys.size());
		write(count);
		for (auto code : keys)
			write(code);
	}
	void T4xCore::LoadState(std::istream& in) {
		const std::lock_guard lock(mutex);
		State next;
		char magic[4];
		in.read(magic, 4);
		if (!in || std::string(magic, 4) != "T4X1")
			throw std::runtime_error("Invalid T4x snapshot");
		auto read = [&](auto& v) { in.read(reinterpret_cast<char*>(&v), sizeof(v)); };
		read(next.reg);
		read(next.work);
		read(next.data);
		read(next.lcd);
		read(next.pc);
		read(next.cycles);
		read(next.instructions);
		read(next.timer_remaining);
		read(next.input_remaining);
		read(next.key);
		read(next.pending);
		bool* fields[] = {&next.page_written, &next.halted, &next.timers, &next.lcd_dirty, &next.timer_enabled[0], &next.timer_enabled[1], &next.ready, &next.blocked_write};
		for (auto field : fields) {
			uint8_t value = 0;
			read(value);
			*field = value != 0;
		}
		uint8_t count = 0;
		read(count);
		std::deque<uint8_t> queued;
		if (count > 64)
			throw std::runtime_error("Invalid T4x key queue");
		while (count--) {
			uint8_t code = 0;
			read(code);
			queued.push_back(code);
		}
		if (!in || std::any_of(next.reg.begin(), next.reg.end(), [](uint8_t n) { return n > 15; }) ||
			std::any_of(next.work.begin(), next.work.end(), [](uint8_t n) { return n > 15; }) ||
			next.timer_remaining[0] > 350000 || next.timer_remaining[1] > 6000)
			throw std::runtime_error("Invalid T4x machine state");
		state = next;
		keys = std::move(queued);
	}
} // namespace casioemu
