#include "Timer.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

#include <cmath>
#include <algorithm>
#include "Binary.h"
#include <iostream>

namespace casioemu {
	static void WakeSolarIIStopOnTimer(Emulator& emulator) {
		if (emulator.hardware_id != HW_SOLARII || emulator.chipset.run_mode != Chipset::RM_STOP)
			return;

		emulator.chipset.run_mode = Chipset::RM_RUN;
	}

	// ML61X
	class Timer : public Peripheral {
		MMURegion region_counter, region_interval, region_F024, region_control;
		uint16_t data_counter, data_interval;
		uint8_t data_F024, data_control;

		size_t TM0INT = 4;

		uint64_t ext_to_int_counter, ext_to_int_next, ext_to_int_int_done;

		size_t TimerFreqDiv;

		unsigned int cycles_per_second;
		static const uint64_t ext_to_int_frequency = 16384;

	public:
		using Peripheral::Peripheral;

		void Initialise();
		void Reset();
		void Tick();
		void Uninitialise();
		void SaveState(std::ostream& os) override {
			os.write(reinterpret_cast<const char*>(&data_counter), sizeof(data_counter));
			os.write(reinterpret_cast<const char*>(&data_interval), sizeof(data_interval));
			os.write(reinterpret_cast<const char*>(&data_F024), sizeof(data_F024));
			os.write(reinterpret_cast<const char*>(&data_control), sizeof(data_control));
			os.write(reinterpret_cast<const char*>(&ext_to_int_counter), sizeof(ext_to_int_counter));
		}
		void LoadState(std::istream& is) override {
			is.read(reinterpret_cast<char*>(&data_counter), sizeof(data_counter));
			is.read(reinterpret_cast<char*>(&data_interval), sizeof(data_interval));
			is.read(reinterpret_cast<char*>(&data_F024), sizeof(data_F024));
			is.read(reinterpret_cast<char*>(&data_control), sizeof(data_control));
			is.read(reinterpret_cast<char*>(&ext_to_int_counter), sizeof(ext_to_int_counter));
		}
	};
	void Timer::Initialise() {
		if (enabled)
			return;

		enabled = true;

		cycles_per_second = emulator.GetCyclesPerSecond();

		TimerFreqDiv = 1;
		if (emulator.ModelDefinition.real_hardware) {
			clock_type = CLOCK_LSCLK;
		}
		else {
			clock_type = CLOCK_EMUCLK;
		}

		block_bit = 3;

		ext_to_int_counter = 0;
		data_interval = 0;
		data_counter = 0;
		data_control = 0;
		data_F024 = 0;

		region_interval.Setup(
			0xF020, 2, "Timer/TM0D", &data_interval, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>,
			emulator);

		region_counter.Setup(
			0xF022, 2, "Timer/TM0C", &data_counter, MMURegion::DefaultRead<uint16_t>, [](MMURegion* region, size_t, uint8_t) {
				*((uint16_t*)region->userdata) = 0;
			},
			emulator);

		region_F024.Setup(
			0xF024, 1, "Timer/TM0CON0", this,
			[](MMURegion* region, size_t) {
				Timer* timer = (Timer*)region->userdata;
				return (uint8_t)(timer->data_F024 & 0x0F);
			},
			[](MMURegion* region, size_t, uint8_t data) {
				Timer* timer = (Timer*)region->userdata;
				timer->data_F024 = data & 0x0F;
				timer->TimerFreqDiv = std::pow(2, data & 0x07);
				if (timer->emulator.ModelDefinition.real_hardware) {
					if (data & 0x08)
						timer->clock_type = CLOCK_HSCLK;
					else
						timer->clock_type = CLOCK_LSCLK;
				}
			},
			emulator);

		region_control.Setup(
			0xF025, 1, "Timer/TM0CON1", this, [](MMURegion* region, size_t) {
			Timer *timer = (Timer *)region->userdata;
			return (uint8_t)(timer->data_control & 0x01); }, [](MMURegion* region, size_t, uint8_t data) {
			Timer *timer = (Timer *)region->userdata;
			timer->data_control = data & 0x01; }, emulator);
	}

	void Timer::Reset() {
		if (!enabled) {
			Initialise();
			return;
		}

		ext_to_int_counter = 0;

		data_interval = 0;
		data_counter = 0;
		data_control = 0;
		data_F024 = 0;
	}

	void Timer::Tick() {
        auto v = data_interval;
        if (!v) v = 1;
		if (clock_type == CLOCK_EMUCLK) {
            if (++ext_to_int_counter >= (v * TimerFreqDiv) / 32678.0 / 0.025 * 2) {
				ext_to_int_counter = 0;
				emulator.chipset.MaskableInterrupts[TM0INT].TryRaise();
				WakeSolarIIStopOnTimer(emulator);
			}
			return;
		}
		if (data_control) {
			if (++ext_to_int_counter >= TimerFreqDiv) {
				ext_to_int_counter = 0;
                if (++data_counter >= v) {
					data_counter = 0;
					emulator.chipset.MaskableInterrupts[TM0INT].TryRaise();
					WakeSolarIIStopOnTimer(emulator);
				}
			}
		}
	}

	void Timer::Uninitialise() {
		if (!enabled)
			return;

		enabled = false;

		clock_type = CLOCK_STOPPED;

		region_interval.Kill();
		region_counter.Kill();
		region_F024.Kill();
		region_control.Kill();
	}
	// ML620 timers: eight 8-bit channels, joined in pairs for 16-bit mode.
	class Timer16Bit : public Peripheral {
		MMURegion data_region, counter_region, control_region, start_region, stop_region, status_region;
		uint8_t data[8]{}, counter[8]{}, control[8]{}, status = 0;
		uint16_t prescaler[8]{};

		bool Paired(unsigned i) const { return control[i & ~1u] & 0x40; }
		void SetRunning(uint8_t bits, bool running) {
			for (unsigned i = 0; i < 8; ++i) {
				if (!(bits & (1 << i)) || ((i & 1) && Paired(i))) continue;
				if (running) status |= 1 << i;
				else status &= ~(1 << i);
			}
		}
	public:
		using Peripheral::Peripheral;
		void Initialise() override {
			// Each channel selects its own LSCLK or undivided OSCLK.
			clock_type = CLOCK_UNDEFINED;
			auto read_byte = [](MMURegion* r, size_t offset) { return static_cast<uint8_t*>(r->userdata)[offset - r->base]; };
			auto write_byte = [](MMURegion* r, size_t offset, uint8_t value) { static_cast<uint8_t*>(r->userdata)[offset - r->base] = value; };
			data_region.Setup(0xF300, 8, "Timer/Data", data, read_byte, write_byte, emulator);
			counter_region.Setup(0xF310, 8, "Timer/Counter", this,
				[](MMURegion* r, size_t offset) { return static_cast<Timer16Bit*>(r->userdata)->counter[offset - r->base]; },
				[](MMURegion* r, size_t offset, uint8_t) {
					auto* self = static_cast<Timer16Bit*>(r->userdata);
					unsigned i = static_cast<unsigned>(offset - r->base);
					if (self->Paired(i)) {
						i &= ~1u;
						self->counter[i + 1] = 0;
					}
					self->counter[i] = 0;
					self->prescaler[i] = 0;
				}, emulator);
			control_region.Setup(0xF320, 8, "Timer/Control", control, read_byte, write_byte, emulator);
			start_region.Setup(0xF330, 1, "Timer/Start", this, MMURegion::IgnoreRead<0>,
				[](MMURegion* r, size_t, uint8_t bits) { static_cast<Timer16Bit*>(r->userdata)->SetRunning(bits, true); }, emulator);
			stop_region.Setup(0xF332, 1, "Timer/Stop", this, MMURegion::IgnoreRead<0>,
				[](MMURegion* r, size_t, uint8_t bits) { static_cast<Timer16Bit*>(r->userdata)->SetRunning(bits, false); }, emulator);
			status_region.Setup(0xF334, 1, "Timer/Status", &status, MMURegion::DefaultRead<uint8_t>, MMURegion::IgnoreWrite, emulator);
			Reset();
		}
		void Reset() override {
			std::fill_n(data, 8, 0xFF);
			std::fill_n(counter, 8, 0);
			std::fill_n(control, 8, 0);
			std::fill_n(prescaler, 8, 0);
			status = 0;
		}
		void Tick() override {
			for (unsigned i = 0; i < 8; ++i) {
				if (!(status & (1 << i)) || ((i & 1) && Paired(i))) continue;
				const unsigned source = control[i] & 3;
				const bool low_speed = source == 0 || (source == 2 && i >= 2 && i <= 5) || (source == 3 && i >= 6);
				const bool tick = low_speed ? emulator.chipset.LSCLKTick :
					source == 1 ? emulator.chipset.OSCLKTick : false;
				// External timer input clocks are not modeled.
				if (!tick) continue;
				const unsigned div = (control[i] >> 3) & 7;
				if (++prescaler[i] < (1u << (div == 7 ? 0 : div))) continue;
				prescaler[i] = 0;
				const bool paired = Paired(i);
				const uint16_t count = counter[i] | (paired ? uint16_t(counter[i + 1]) << 8 : 0);
				uint16_t limit = data[i] | (paired ? uint16_t(data[i + 1]) << 8 : 0);
				if (!limit) limit = 1;
				const uint16_t next = count == limit ? 0 : count + 1;
				counter[i] = static_cast<uint8_t>(next);
				if (paired) counter[i + 1] = static_cast<uint8_t>(next >> 8);
				if (count == limit) {
					// IE/IRQ5 bits 0..7; paired mode interrupts on the odd channel.
					emulator.chipset.MaskableInterrupts[39 + i + (paired ? 1 : 0)].TryRaise();
					if (control[i] & 0x80) status &= ~(1 << i);
				}
			}
		}
		void SaveState(std::ostream& os) override {
			Binary::Write(os, data);
			Binary::Write(os, counter);
			Binary::Write(os, control);
			Binary::Write(os, status);
			Binary::Write(os, prescaler);
		}
		void LoadState(std::istream& is) override {
			Binary::Read(is, data);
			Binary::Read(is, counter);
			Binary::Read(is, control);
			Binary::Read(is, status);
			Binary::Read(is, prescaler);
		}
	};
	Peripheral* CreateTimer(Emulator& emu) {
		if (emu.hardware_id == HW_TI_MATH_PRINT) {
			return new Timer16Bit(emu);
		}
		return new Timer(emu);
	}
} // namespace casioemu
