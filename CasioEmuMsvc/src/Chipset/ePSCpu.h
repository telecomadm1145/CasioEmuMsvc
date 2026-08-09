/*
 * EPS6800 machine adapter for CasioEmuMsvc.
 *
 * The implementation is deliberately kept behind the historical ePSCPU
 * name so the debugger and plugin surfaces can migrate without exposing the
 * C core internals throughout the application.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

struct machine_state;

namespace casioemu {
	class MMU;

	class ePSCPU {
	private:
		machine_state* state_;

	public:
		ePSCPU();
		~ePSCPU();

		ePSCPU(const ePSCPU&) = delete;
		ePSCPU& operator=(const ePSCPU&) = delete;

		bool LoadRom(const std::vector<unsigned char>& rom);
		void Reset();
		void Next();
		void RunFrame();

		void KeyDown(uint8_t matrix_index);
		void KeyUp(uint8_t matrix_index);
		void OnDown();
		void OnUp();

		size_t CopyLcd(uint8_t* output, size_t size) const;
		size_t LcdRawSize() const;
		uint8_t ReadByte(uint8_t address);
		void WriteByte(uint8_t address, uint8_t value);

		void SaveState(std::ostream& stream) const;
		void LoadState(std::istream& stream);

		// Historical debugger convention: return a byte address. The EPS core
		// stores its PC and stack entries as 16-bit instruction-word addresses.
		uint32_t PC() const;
		void SetPC(uint32_t word_address);

		// Direct views used by CasioEmuMsvc's existing memory/register editors.
		uint8_t* regs;
		uint8_t* ram;
		uint8_t* wbk;
		uint8_t* vram;
		uint32_t* stack;

		uint8_t& FSR;
		uint8_t& BSR;
		uint8_t& FSR1;
		uint8_t& BSR1;
		uint8_t& STKPTR;
		uint8_t& ACC;
		uint8_t& STATUS;
		uint8_t& FSR2;
		uint8_t& BSR2;
		uint8_t& LCDARL;
		uint8_t& LCDARH;
	};
} // namespace casioemu
