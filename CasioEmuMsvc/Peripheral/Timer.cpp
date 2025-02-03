#include "Timer.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

#include <cmath>

namespace casioemu {
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
			}
			return;
		}
		if (data_control) {
			if (++ext_to_int_counter >= TimerFreqDiv) {
				ext_to_int_counter = 0;
                if (++data_counter >= v) {
					data_counter = 0;
					emulator.chipset.MaskableInterrupts[TM0INT].TryRaise();
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
	template <typename ReadFunc, typename WriteFunc>
		requires requires(ReadFunc read, WriteFunc write, MMURegion* reg, size_t off, uint8_t dat) {
			{ read(reg, off) } -> std::same_as<uint8_t>;
			{ write(reg, off, dat) } -> std::same_as<void>;
		}
	inline void SetupCpp(MMURegion& reg, size_t _base, size_t _size, std::string _description, ReadFunc _read, WriteFunc _write, Emulator& _emulator) {

		struct Bind {
			WriteFunc on_write;
			ReadFunc on_read;
		};

		reg.Setup(
			_base, _size, _description, new Bind{_write, _read}, [](MMURegion* reg, size_t off) -> uint8_t { return ((Bind*)reg->userdata)->on_read(reg, off); }, [](MMURegion* reg, size_t off, uint8_t dat) { ((Bind*)reg->userdata)->on_write(reg, off, dat); }, _emulator);
	}

	// ML62Q1000
	class Timer16Bit : public Peripheral {
	public:
		class TimerUnit {
		public:
			TimerUnit(int i) : i(i) {}
			int i;
			MMURegion tm_data{}, tm_counter{}, tm_mode{}, tm_int_stat{}, tm_int_clr{};
			uint16_t tm_data_d{}, tm_counter_d{}, tm_mode_d{}, tm_int_stat_d{}, tm_int_clr_d{};
			
			int tm_cnt = 0;

			bool started = false;
			const int int_map[8] = {27,28,35,36,43,44,51,52};
			void Initialise(Emulator& emulator) {
				tm_data.Setup(0xF300 + i * 2, 2, "16BitTimer/Data", &tm_data_d, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);
				tm_counter.Setup(0xF310 + i * 2, 2, "16BitTimer/Counter", &tm_counter_d, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);
				tm_mode.Setup(0xF320 + i * 2, 2, "16BitTimer/Mode", &tm_mode_d, MMURegion::DefaultRead<uint16_t>, MMURegion::DefaultWrite<uint16_t>, emulator);
				tm_int_stat.Setup(0xF330 + i * 2, 2, "16BitTimer/InterruptStatus", &tm_int_stat_d, MMURegion::DefaultRead<uint16_t>, MMURegion::IgnoreWrite, emulator);
				tm_int_clr.Setup(0xF340 + i * 2, 2, "16BitTimer/InterruptClear", &tm_int_clr_d, MMURegion::DefaultRead<uint16_t>, MMURegion::IgnoreWrite, emulator);
			}
			void Tick(Emulator& emulator) {
				if (!started)
					return;
				auto divider = (tm_mode_d >> 3) & 0b111;
				if (++tm_cnt > (1 << divider)) {
					tm_cnt = 0;
					tm_counter_d++;
					// Triggered!
					emulator.chipset.RaiseMaskable(int_map[i]);
				}
			}
		};
		TimerUnit Units[8]{0,1,2,3,4,5,6,7};
		MMURegion TMStart{};
		uint16_t a{};
		using Peripheral::Peripheral;
		void Initialise() override {
			for (auto& unit : Units)
				unit.Initialise(emulator);
			TMStart.Setup(0xF350, 2, "Timer/StartReg",&a, MMURegion::DefaultRead<uint16_t>,MMURegion::DefaultWrite<uint16_t>,emulator);
		}
		int bug{};
		void Tick() override {
			for (auto& unit : Units)
				unit.Tick(emulator);
			
			//if (bug++ > 0x8000)
			//{
			//	bug = 0;
			//	emulator.chipset.data_LTBR++;
			//}
		}
	};
	Peripheral* CreateTimer(Emulator& emu) {
		if (emu.hardware_id == HW_TI) {
			return new Timer16Bit(emu);
		}
		return new Timer(emu);
	}
} // namespace casioemu
