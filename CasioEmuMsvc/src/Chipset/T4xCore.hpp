#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <mutex>
#include <string>
#include <vector>

namespace casioemu {
	// T4x word-addressed CPU and MultiView machine. Memory units are explicit:
	// WRAM and registers contain nibbles; DRAM and ROM input contain bytes.
	class T4xCore {
	public:
		static constexpr unsigned Width = 96, Height = 31, BodyBytes = 372, LcdBytes = 384;
		static constexpr unsigned LcdControlRegister = 22, LcdContrastRegister = 37;
		struct DisplayState {
			std::array<uint8_t, LcdBytes> lcd;
			uint8_t control, contrast;
		};
		struct State {
			std::array<uint8_t, 64> reg{};
			std::array<uint8_t, 1024> work{};
			std::array<uint8_t, 4096> data{};
			std::array<uint8_t, LcdBytes> lcd{};
			uint16_t pc = 0;
			uint64_t cycles = 0, instructions = 0;
			uint32_t timer_remaining[2]{350000, 6000};
			uint32_t input_remaining = 0;
			uint8_t key = 255;
			uint8_t pending = 0;
			bool page_written = false, halted = false, timers = false, lcd_dirty = false;
			bool timer_enabled[2]{};
			bool ready = false, blocked_write = false;
		};
		bool LoadRom(const std::vector<uint8_t>& bytes);
		void Reset();
		unsigned Step(); // One instruction, PC and code addresses are WORD addresses.
		void RunBatch(unsigned instructions, uint32_t elapsed_us);
		void AdvanceTime(uint32_t elapsed_us);
		void Key(uint8_t code, bool pressed);
		State Snapshot() const;
		DisplayState ReadDisplay() const;
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
		void SaveState(std::ostream& out) const;
		void LoadState(std::istream& in);

	private:
		mutable std::mutex mutex;
		std::array<uint16_t, 65536> rom{};
		State state{};
		std::deque<uint8_t> keys;
		void ResetState();
		unsigned StepInstruction();
		void AdvanceTimeLocked(uint32_t elapsed_us);
		uint8_t& Work(unsigned bank, unsigned row, unsigned col);
		unsigned StackPointer() const;
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
		void ReadKey(unsigned index);
		bool KeyInterrupt(uint8_t code) const;
		void ServiceKey();
		void PublishLcd();
	};
} // namespace casioemu
