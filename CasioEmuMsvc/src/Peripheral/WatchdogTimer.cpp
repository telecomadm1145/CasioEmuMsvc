#include "WatchdogTimer.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "Binary.h"

#include <cmath>

namespace casioemu {
	class WatchdogTimer : public Peripheral {
		MMURegion region_WDTCON, region_WDTMOD;
		uint8_t data_WDTCON, data_WDTMOD;

		bool data_WDP;

		size_t WDT_counter;
		bool overflow_count;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();
		void SaveState(std::ostream& os) override {
			if (emulator.hardware_id != HW_TI_MATH_PRINT) return;
			Binary::Write(os, data_WDTCON);
			Binary::Write(os, data_WDTMOD);
			Binary::Write(os, data_WDP);
			Binary::Write(os, WDT_counter);
			Binary::Write(os, overflow_count);
		}
		void LoadState(std::istream& is) override {
			if (emulator.hardware_id != HW_TI_MATH_PRINT) return;
			Binary::Read(is, data_WDTCON);
			Binary::Read(is, data_WDTMOD);
			Binary::Read(is, data_WDP);
			Binary::Read(is, WDT_counter);
			Binary::Read(is, overflow_count);
		}
	};
	void WatchdogTimer::Initialise() {
		// Watchdog timer is normally disabled in casio calculators, but in some models parts of its function is reserved.
		clock_type = emulator.hardware_id == HW_TI_MATH_PRINT ? CLOCK_LSCLK :
			emulator.chipset.WDT_enabled ? CLOCK_HSCLK : CLOCK_STOPPED;

		data_WDTCON = 0;
		data_WDTMOD = emulator.hardware_id == HW_TI_MATH_PRINT ? 0x82 : 2;

		data_WDP = false;

		WDT_counter = 0;
		overflow_count = false;

		region_WDTCON.Setup(
			0xF00E, 1, "WatchdogTimer/WDTCON", this, [](MMURegion* region, size_t) {
            WatchdogTimer* wdt = (WatchdogTimer*)region->userdata;
            return (uint8_t)wdt->data_WDP; }, [](MMURegion* region, size_t, uint8_t data) {
            WatchdogTimer* wdt = (WatchdogTimer*)region->userdata;
            if(wdt->data_WDP && wdt->data_WDTCON == 0x5A && data == 0xA5) {
                wdt->WDT_counter = 0;
                wdt->overflow_count = false;
            }
            wdt->data_WDP = !wdt->data_WDP;
            wdt->data_WDTCON = data; }, emulator);

		if (emulator.hardware_id == HW_TI_MATH_PRINT) {
			region_WDTMOD.Setup(0xF00F, 1, "WatchdogTimer/WDTMOD", &data_WDTMOD, MMURegion::DefaultRead<uint8_t, 0x83>, MMURegion::DefaultWrite<uint8_t, 0x83>, emulator);
		}
		else if (emulator.hardware_id == HW_CLASSWIZ_II) {
			region_WDTMOD.Setup(0xF00F, 1, "WatchdogTimer/WDTMOD", &data_WDTMOD, MMURegion::DefaultRead<uint8_t, 0x03>, MMURegion::DefaultWrite<uint8_t, 0x03>, emulator);
		}
	}

	void WatchdogTimer::Tick() {
		if (emulator.hardware_id == HW_TI_MATH_PRINT) {
			if (emulator.chipset.run_mode == Chipset::RM_DEEP_HALT && !(data_WDTMOD & 0x80)) return;
			// TI WDT uses LSCLK even when the CPU/high-speed clock stops.
			if (++WDT_counter < (4096u << (2 * (data_WDTMOD & 3)))) return;
			if (overflow_count) {
				emulator.chipset.Reset();
				return;
			}
			emulator.chipset.RequestNonmaskable();
			data_WDTCON = 0;
			data_WDP = false;
			WDT_counter = 0;
			overflow_count = true;
			return;
		}
		// Accept 256Hz output
		if (emulator.chipset.HSCLK_output & 0x20) {
			if (++WDT_counter >= 32 * std::pow(4, data_WDTMOD)) {
				if (!overflow_count) {
					emulator.chipset.RequestNonmaskable();
					data_WDTCON = 0;
					data_WDP = false;
					WDT_counter = 0;
					overflow_count = true;
				}
				else {
					emulator.chipset.Reset();
				}
			}
		}
	}

	void WatchdogTimer::Reset() {
		data_WDTCON = 0;
		data_WDTMOD = emulator.hardware_id == HW_TI_MATH_PRINT ? 0x82 : 2;

		data_WDP = false;

		WDT_counter = 0;
		overflow_count = false;
	}
	Peripheral* CreateWatchdog(Emulator& emu) {
		return new WatchdogTimer(emu);
	}
} // namespace casioemu
