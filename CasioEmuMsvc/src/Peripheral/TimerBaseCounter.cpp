#include "TimerBaseCounter.hpp"

#include "Chipset/Chipset.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "Binary.h"

namespace casioemu {
	class TimerBaseCounter : public Peripheral {
		size_t L256SINT = 5;
		size_t L1024SINT = 6;
		size_t L4096SINT = 7;
		size_t L16384SINT = 8;

		uint8_t current_output;
		bool LTBR_reset_tick;

		size_t LTBRCounter;
		const size_t LTBROutputCount = 128;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();
		void ResetLSCLK();
	};
	void TimerBaseCounter::Initialise() {
		clock_type = CLOCK_LSCLK;

		LTBRCounter = 0;
		current_output = 0;
		LTBR_reset_tick = false;
	}

	void TimerBaseCounter::Reset() {
		LTBRCounter = 0;
		current_output = 0;
		LTBR_reset_tick = false;
	}

	void TimerBaseCounter::Tick() {
		if (LTBR_reset_tick) {
			LTBR_reset_tick = false;
			return;
		}

		emulator.chipset.LSCLK_output = 0;

		if (++LTBRCounter >= LTBROutputCount) {
			LTBRCounter = 0;
			emulator.chipset.data_LTBR++;
			current_output = emulator.chipset.LSCLK_output = (emulator.chipset.data_LTBR - 1) & (~emulator.chipset.data_LTBR);

			if (current_output & 0x01)
				emulator.chipset.MaskableInterrupts[L256SINT].TryRaise();
			if (current_output & 0x04)
				emulator.chipset.MaskableInterrupts[L1024SINT].TryRaise();
			if (current_output & 0x10)
				emulator.chipset.MaskableInterrupts[L4096SINT].TryRaise();
			if (current_output & 0x40)
				emulator.chipset.MaskableInterrupts[L16384SINT].TryRaise();
		}
	}

	void TimerBaseCounter::ResetLSCLK() {
		LTBRCounter = 0;
		emulator.chipset.LSCLK_output = 0xFF;
		LTBR_reset_tick = true;

		emulator.chipset.MaskableInterrupts[L256SINT].TryRaise();
		emulator.chipset.MaskableInterrupts[L1024SINT].TryRaise();
		emulator.chipset.MaskableInterrupts[L4096SINT].TryRaise();
		emulator.chipset.MaskableInterrupts[L16384SINT].TryRaise();
	}
	class TBC2 : public Peripheral {
		size_t LTB0INT = 55; // See Chipset.cpp
		size_t LTB1INT = 56;
		size_t LTB2INT = 57;

		uint8_t current_output;
		bool LTBR_reset_tick;

		size_t LTBRCounter;
		const size_t LTBROutputCount = 128;

		MMURegion reg_LTBINT;

		int LTB0S = 0;
		int LTB1S = 3;
		int LTB2S = 6;

	public:
		using Peripheral::Peripheral;

		void Initialise() {
			clock_type = CLOCK_LSCLK;

			reg_LTBINT.Setup(
				0xF064, 2, "TimerBaseCounter/LTBINT", this,
				[](MMURegion* sender, size_t offset) -> uint8_t {
					auto pthis = (TBC2*)sender->userdata;
					if (offset == 0xF064) {
						return (pthis->LTB1S << 4) | pthis->LTB0S;
					}
					else {
						return pthis->LTB2S;
					}
				},
				[](MMURegion* sender, size_t offset, uint8_t data) {
					auto pthis = (TBC2*)sender->userdata;
					if (offset == 0xF064) {
						pthis->LTB1S = (data & 0x70) >> 4;
						pthis->LTB0S = (data & 0x7);
					}
					else {
						pthis->LTB2S = data & 0x7;
					}
				},
				emulator);
			LTBRCounter = 0;
			current_output = 0;
			LTBR_reset_tick = false;
		}
		void Reset() {
			LTBRCounter = 0;
			current_output = 0;
			LTBR_reset_tick = false;
			LTB0S = 0;
			LTB1S = 3;
			LTB2S = 6;
		}
		void Tick() {
			if (LTBR_reset_tick) {
				LTBR_reset_tick = false;
				return;
			}

			emulator.chipset.LSCLK_output = 0;

			if (++LTBRCounter >= LTBROutputCount) {
				LTBRCounter = 0;
				emulator.chipset.data_LTBR++;
				current_output = emulator.chipset.LSCLK_output = (emulator.chipset.data_LTBR - 1) & (~emulator.chipset.data_LTBR);
				if (current_output & (1 << LTB0S))
					emulator.chipset.MaskableInterrupts[LTB0INT].TryRaise();
				if (current_output & (1 << LTB1S))
					emulator.chipset.MaskableInterrupts[LTB1INT].TryRaise();
				if (current_output & (1 << LTB2S))
					emulator.chipset.MaskableInterrupts[LTB2INT].TryRaise();
			}
		}
		void ResetLSCLK() {
			LTBRCounter = 0;
			emulator.chipset.LSCLK_output = 0xFF;
			LTBR_reset_tick = true;

			emulator.chipset.MaskableInterrupts[LTB0INT].TryRaise();
			emulator.chipset.MaskableInterrupts[LTB1INT].TryRaise();
			emulator.chipset.MaskableInterrupts[LTB2INT].TryRaise();
		}
		void SaveState(std::ostream& os) override {
			Binary::Write(os, LTBRCounter);
			Binary::Write(os, current_output);
			Binary::Write(os, LTBR_reset_tick);
			Binary::Write(os, LTB0S);
			Binary::Write(os, LTB1S);
			Binary::Write(os, LTB2S);
		}
		void LoadState(std::istream& is) override {
			Binary::Read(is, LTBRCounter);
			Binary::Read(is, current_output);
			Binary::Read(is, LTBR_reset_tick);
			Binary::Read(is, LTB0S);
			Binary::Read(is, LTB1S);
			Binary::Read(is, LTB2S);
		}
	};
	Peripheral* CreateTimerBaseCounter(Emulator& emu) {
		if (emu.hardware_id == HW_TI_MATH_PRINT) {
			return new TBC2(emu);
		}
		return new TimerBaseCounter(emu);
	}

} // namespace casioemu
