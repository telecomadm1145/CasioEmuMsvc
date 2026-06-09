#include "RealTimeClock.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

namespace casioemu {
	class RealTimeClock : public Peripheral {
		MMURegion region_RTCSEC, region_RTCMIN, region_RTCHOUR, region_RTCWEEK, region_RTCDAY, region_RTCMON, region_RTCYEAR,
			region_RTCCON, region_AL0MIN, region_AL0HOUR, region_AL0WEEK, region_AL1MIN, region_AL1HOUR, region_AL1DAY, region_AL1MON;

		uint8_t RTCSEC, RTCMIN, RTCHOUR, RTCWEEK, RTCDAY, RTCMON, RTCYEAR, RTCCON, AL0MIN, AL0HOUR, AL0WEEK, AL1MIN, AL1HOUR, AL1DAY, AL1MON;

		size_t RTCINT = 14;
		size_t AL0INT = 15;
		size_t AL1INT = 16;

		bool RTCSEC_carry;

		const uint8_t day_count[0x12] = {0x31, 0x28, 0x31, 0x30, 0x31, 0x30, 0x31, 0x31, 0x30, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x30, 0x31};
		const uint8_t day_count_leap[0x12] = {0x31, 0x29, 0x31, 0x30, 0x31, 0x30, 0x31, 0x31, 0x30, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x30, 0x31};

		template <uint8_t RealTimeClock::*value_ptr, uint8_t mask = 0xFF>
		static uint8_t RTCRead(MMURegion* region, size_t) {
			RealTimeClock* self = (RealTimeClock*)region->userdata;
			return self->*value_ptr & mask;
		}

		template <uint8_t RealTimeClock::*value_ptr, uint8_t mask = 0xFF, uint8_t minimum_val = 0>
		static void RTCWrite(MMURegion* region, size_t, uint8_t data) {
			RealTimeClock* self = (RealTimeClock*)region->userdata;
			if (self->RTCCON & 1)
				return;
			data &= mask;
			if (data < minimum_val)
				data = mask;
			self->*value_ptr = data;
		}

	public:
		using Peripheral::Peripheral;

		void CheckValue();
		void RTCTick();
		bool AL0Check();
		bool AL1Check();

		void Initialise();
		void Reset();
		void Tick();
	};
	void RealTimeClock::Initialise() {
		clock_type = CLOCK_LSCLK;

		RTCSEC = RTCMIN = RTCDAY = RTCMON = RTCYEAR = RTCCON = AL0MIN = AL0HOUR = AL0WEEK = AL1MIN = AL1HOUR = AL1DAY = AL1MON = 0;
		RTCWEEK = 1;

		RTCSEC_carry = false;

		region_RTCSEC.Setup(0xF0C0, 1, "RealTimeClock/RTCSEC", this, RTCRead<&RealTimeClock::RTCSEC, 0x7F>, RTCWrite<&RealTimeClock::RTCSEC, 0x7F>, emulator);
		region_RTCMIN.Setup(0xF0C1, 1, "RealTimeClock/RTCMIN", this, RTCRead<&RealTimeClock::RTCMIN, 0x7F>, RTCWrite<&RealTimeClock::RTCMIN, 0x7F>, emulator);
		region_RTCHOUR.Setup(0xF0C2, 1, "RealTimeClock/RTCHOUR", this, RTCRead<&RealTimeClock::RTCHOUR, 0x3F>, RTCWrite<&RealTimeClock::RTCHOUR, 0x3F>, emulator);
		region_RTCWEEK.Setup(0xF0C3, 1, "RealTimeClock/RTCWEEK", this, RTCRead<&RealTimeClock::RTCWEEK, 0x07>, RTCWrite<&RealTimeClock::RTCWEEK, 0x07, 0x01>, emulator);
		region_RTCDAY.Setup(0xF0C4, 1, "RealTimeClock/RTCDAY", this, RTCRead<&RealTimeClock::RTCDAY, 0x3F>, RTCWrite<&RealTimeClock::RTCDAY, 0x3F>, emulator);
		region_RTCMON.Setup(0xF0C5, 1, "RealTimeClock/RTCMON", this, RTCRead<&RealTimeClock::RTCMON, 0x1F>, RTCWrite<&RealTimeClock::RTCMON, 0x1F>, emulator);
		region_RTCYEAR.Setup(0xF0C6, 1, "RealTimeClock/RTCYEAR", this, RTCRead<&RealTimeClock::RTCYEAR>, RTCWrite<&RealTimeClock::RTCYEAR>, emulator);
		region_RTCCON.Setup(
			0xF0C7, 1, "RealTimeClock/RTCCON", this, RTCRead<&RealTimeClock::RTCCON, 0x07>, [](MMURegion* region, size_t, uint8_t data) {
				RealTimeClock* self = (RealTimeClock*)region->userdata;
				self->RTCCON = data & 0x07;
				if (data & 1)
					self->CheckValue();
			},
			emulator);
		region_AL0MIN.Setup(0xF0C8, 1, "RealTimeClock/AL0MIN", &AL0MIN, MMURegion::DefaultRead<uint8_t, 0x7F>, MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
		region_AL0HOUR.Setup(0xF0C9, 1, "RealTimeClock/AL0HOUR", &AL0HOUR, MMURegion::DefaultRead<uint8_t, 0x3F>, MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);
		region_AL0WEEK.Setup(0xF0CA, 1, "RealTimeClock/AL0WEEK", &AL0WEEK, MMURegion::DefaultRead<uint8_t, 0x07>, MMURegion::DefaultWrite<uint8_t, 0x07>, emulator);
		region_AL1MIN.Setup(0xF0CB, 1, "RealTimeClock/AL1MIN", &AL1MIN, MMURegion::DefaultRead<uint8_t, 0x7F>, MMURegion::DefaultWrite<uint8_t, 0x7F>, emulator);
		region_AL1HOUR.Setup(0xF0CC, 1, "RealTimeClock/AL1HOUR", &AL1HOUR, MMURegion::DefaultRead<uint8_t, 0x3F>, MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);
		region_AL1DAY.Setup(0xF0CD, 1, "RealTimeClock/AL1DAY", &AL1DAY, MMURegion::DefaultRead<uint8_t, 0x3F>, MMURegion::DefaultWrite<uint8_t, 0x3F>, emulator);
		region_AL1MON.Setup(0xF0CE, 1, "RealTimeClock/AL1MON", &AL1MON, MMURegion::DefaultRead<uint8_t, 0x1F>, MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);
	}

	void RealTimeClock::CheckValue() {
		if ((RTCSEC & 0x0F) > 0x09)
			RTCSEC = (RTCSEC + 0x10) & 0xF0;
		if (RTCSEC >= 0x60) {
			RTCSEC = 0;
			if ((RTCMIN & 0x0F) <= 0x09)
				RTCMIN++;
		}

		if ((RTCMIN & 0x0F) > 0x09)
			RTCMIN = (RTCMIN + 0x10) & 0xF0;
		if (RTCMIN >= 0x60) {
			RTCMIN = 0;
			if ((RTCHOUR & 0x0F) <= 0x09)
				RTCHOUR++;
		}

		if ((RTCHOUR & 0x0F) > 0x09)
			RTCHOUR = (RTCHOUR + 0x10) & 0xF0;
		if (RTCHOUR >= 0x24) {
			RTCHOUR = 0;
			if (++RTCWEEK > 0x07)
				RTCWEEK = 0x01;
			if ((RTCDAY & 0x0F) <= 0x09)
				RTCDAY++;
		}

		bool isRTCMONValid = RTCMON && RTCMON <= 0x12 && (RTCMON & 0x0F) <= 0x09;

		if (!RTCDAY)
			RTCDAY = 1;

		if ((RTCDAY & 0x0F) > 0x09)
			RTCDAY = (RTCDAY + 0x10) & 0xF0;
		if (RTCDAY > (isRTCMONValid ? (RTCYEAR % 4 ? day_count[RTCMON - 1] : day_count_leap[RTCMON - 1]) : 0x31)) {
			RTCDAY = 1;
			if (isRTCMONValid)
				RTCMON++;
		}

		if (!RTCMON)
			RTCMON = 1;

		if ((RTCMON & 0x0F) > 0x09)
			RTCMON = (RTCMON + 0x10) & 0xF0;
		if (RTCMON > 0x12) {
			RTCMON = 1;
			if ((RTCYEAR & 0x0F) <= 0x09 && RTCYEAR < 0xA0)
				RTCYEAR++;
		}

		if ((RTCYEAR & 0x0F) > 0x09)
			RTCYEAR = (RTCYEAR + 0x10) & 0xF0;
		if (RTCYEAR >= 0xA0)
			RTCYEAR = 0;
	}

	void RealTimeClock::RTCTick() {
		if ((++RTCSEC & 0x0F) > 0x09)
			RTCSEC += 0x06;
		if (RTCSEC < 0x60)
			return;

		RTCSEC_carry = true;

		if ((RTCCON & 0x06) == 0x06)
			emulator.chipset.MaskableInterrupts[RTCINT].TryRaise();
		RTCSEC = 0;
		if ((++RTCMIN & 0x0F) > 0x09)
			RTCMIN += 0x06;
		if (RTCMIN < 0x60)
			return;

		RTCMIN = 0;
		if ((++RTCHOUR & 0x0F) > 0x09)
			RTCHOUR += 0x06;
		if (RTCHOUR < 0x24)
			return;

		RTCHOUR = 0;
		if (++RTCWEEK > 0x07)
			RTCWEEK = 0x01;
		if ((++RTCDAY & 0x0F) > 0x09)
			RTCDAY += 0x06;

		if (!(RTCMON && RTCMON <= 0x12 && (RTCMON & 0x0F) <= 0x09)) {
			logger::Info("RTCMON value 0x%02X invalid while running!\n", RTCMON);
			CheckValue();
		}

		if (RTCDAY <= (RTCYEAR % 4 ? day_count[RTCMON - 1] : day_count_leap[RTCMON - 1]))
			return;

		RTCDAY = 1;
		if ((++RTCMON & 0x0F) > 0x09)
			RTCMON += 0x06;
		if (RTCMON <= 0x12)
			return;

		RTCMON = 1;
		if ((++RTCYEAR & 0x0F) > 0x09)
			RTCYEAR += 0x06;
		if (RTCYEAR >= 0xA0)
			RTCYEAR = 0;
	}

	bool RealTimeClock::AL0Check() {
		if (RTCMIN != AL0MIN || RTCHOUR != AL0HOUR)
			return false;

		if ((!AL0WEEK) || RTCWEEK == AL0WEEK)
			return true;

		return false;
	}

	bool RealTimeClock::AL1Check() {
		if (RTCMIN != AL1MIN || RTCHOUR != AL1HOUR)
			return false;

		if (AL1DAY && RTCDAY != AL1DAY)
			return false;

		if (AL1MON && RTCMON != AL1MON)
			return false;

		return true;
	}

	void RealTimeClock::Tick() {
		RTCSEC_carry = false;

		// Accept 2Hz LSCLK output
		if (emulator.chipset.LSCLK_output & 0x20) {
			if ((RTCCON & 0x06) == 0x02)
				emulator.chipset.MaskableInterrupts[RTCINT].TryRaise();
		}

		// Accept 1Hz LSCLK output
		if (emulator.chipset.LSCLK_output & 0x40) {
			if ((RTCCON & 0x06) == 0x04)
				emulator.chipset.MaskableInterrupts[RTCCON].TryRaise();

			if (RTCCON & 1) {
				RTCTick();
				if (RTCSEC_carry) {
					if (AL0Check())
						emulator.chipset.MaskableInterrupts[AL0INT].TryRaise();
					if (AL1Check())
						emulator.chipset.MaskableInterrupts[AL1INT].TryRaise();
				}
			}
		}
	}

	void RealTimeClock::Reset() {
		// RTCCON = 0;
	}
	Peripheral* CreateRtc(Emulator& emu) {
		return new RealTimeClock(emu);
	}
} // namespace casioemu