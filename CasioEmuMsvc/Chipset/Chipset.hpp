#pragma once
#include "Config.hpp"

#include "InterruptSource.hpp"
#include "MMURegion.hpp"

#include "Peripheral/ExternalInterrupts.hpp"
#include "Peripheral/IOPorts.hpp"

#include <SDL.h>
#include <forward_list>
#include <string>
#include <vector>

namespace casioemu {
	class Emulator;
	class CPU;
	class MMU;
	class Peripheral;

	class Chipset {
		enum InterruptIndex {
			INT_CHECKFLAG,
			INT_RESET,
			INT_BREAK,
			INT_EMULATOR,
			INT_NONMASKABLE,
			INT_MASKABLE,
			INT_SOFTWARE = 64,
			INT_COUNT = 128
		};

	public:
		enum RunMode {
			RM_STOP,
			RM_HALT,
			RM_RUN
		};
		RunMode run_mode;

	private:
		std::forward_list<Peripheral*> peripherals;

		/**
		 * A bunch of internally used methods for encapsulation purposes.
		 */
		size_t pending_interrupt_count;
		bool interrupts_active[INT_COUNT];
		void AcceptInterrupt();
		void RaiseSoftware(size_t index);

		void ConstructPeripherals();
		void DestructPeripherals();

		void ConstructClockGenerator();
		void GenerateTickForClock();
		void ResetClockGenerator();
		void DestructClockGenerator();

		void ConstructInterruptSFR();
		void ResetInterruptSFR();
		void DestructInterruptSFR();
		MMURegion region_int_mask, region_int_pending;
		uint64_t data_int_mask, data_int_pending;
		static const size_t managed_interrupt_base = 4;

		MMURegion region_BLKCON;

		MMURegion region_FCON, region_FCON1, region_LTBR, region_HTBR, region_LTBADJ;
		int LSCLKFreq{};

		long long LSCLKTickCounter{}, HSCLKTickCounter, HSCLKTimeCounter, SYSCLKTickCounter, LSCLKTimeCounter, LSCLKThresh;
		int LSCLKFreqAddition{};

		bool real_hardware;

	public:
		void* QueryInterface(const char* name);
		Chipset(Emulator& emulator);
		void Setup(); // must be called after emulator.hardware_id is initialized
		~Chipset();

		Emulator& emulator;
		CPU& cpu;
		MMU& mmu;

		std::vector<unsigned char> rom_data;
		std::vector<unsigned char> flash_data;

		bool remap = false;

		InterruptSource* MaskableInterrupts;
		size_t EffectiveMICount;

		// Reserve these pointers to make it easy for other peripherals to input to pins.
		IOPorts* ioport;
		ExternalInterrupts* EXIhandle;

		bool WDT_enabled = false;

		uint8_t data_BLKCON, BLKCON_mask;
		uint8_t data_EXICON;

		uint8_t data_FCON, data_FCON1, data_LTBR, data_HTBR;
		uint16_t data_LTBADJ;

		// 0.5Hz-64Hz Low Speed Clock output.Corresponding bit is set to 1 on output and got reset on the next LSCLK tick.
		uint8_t LSCLK_output;

		// 64Hz-8kHz output.
		uint8_t HSCLK_output;

		int ClockDiv;
		bool LSCLKMode;

		bool LSCLKTick, HSCLKTick, SYSCLKTick;
		bool LTBCReset, HTBCReset;

		const int HTBROutputCount = 128;

		/*
		 * Pin levels.0 for L level, 1 for H level.
		 * The external interrupts are controlled by Keyboard and ExternalInterrupts.These values could still be accessed by other peripherals.
		 * P00 should be used as keyboard ki/ko.
		 * We dont know the name of the port controlled by F220h.
		 */
		bool Port0Inputlevel[3], Port1Inputlevel[7];			 // The actual pin level inputted to the chip
		bool Port0Outputlevel[3], Port1Outputlevel[7];			 // The pin level that could be directly read from the pins
		bool UserInput_level_Port0[3], UserInput_level_Port1[7]; // The voltage attached directly to the pins
		bool UserInput_state_Port0[3], UserInput_state_Port1[7]; // Marks if there is user input

		bool SegmentAccess;

		bool isMIBlocked;

		// TI things.
		bool tiDiagMode;
		int tiKey;

		/**
		 * This exists because the Emulator that owns this Chipset is not ready
		 * to supply a ROM path upon construction. It has to call `LoadROM` later
		 * in its constructor.
		 */
		void SetupInternals();

		/**
		 * See 1.3.7 in the nX-U8 manual.
		 */
		void Reset();
		void Break();
		void Halt();
		void Stop();
		bool GetRunningState();
		void RaiseEmulator();
		void RequestNonmaskable();
		void RaiseNonmaskable();
		void ResetNonmaskable();
		void RaiseMaskable(size_t index);
		void ResetMaskable(size_t index);
		void SetInterruptPendingSFR(size_t index, bool val);
		bool GetInterruptPendingSFR(size_t index);
		void InputToPort(int, int, bool);
		void RemovePortInput(int, int);

		void Tick();
		void EmulatorTick();
		void Frame();
		void UIEvent(SDL_Event& event);

		template <typename T>
		T* QueryInterface() {
			auto i = this->QueryInterface(typeid(T).name());
			if (i)
				return (T*)i;
			return nullptr;
		}

		friend class CPU;
	};
} // namespace casioemu
