#include "PowerSupply.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"

#include <cmath>

namespace casioemu {
	class PowerSupply : public Peripheral {
		MMURegion region_BLDCON0, region_BLDCON1, region_BLDCON2, region_SPIndicator;
		uint8_t threshold, data_BLDCON2, data_SPIndicator;
		bool BLDMode, BLDFlag, BLDControl;

		bool isTestRoutineRunning;
		size_t TestTimer;
		bool CurrentTestMode, CurrentRepMode, HasResult;
		float CurrentThresh;

		size_t DelayTicks;

		size_t BENDINT = 12;
		size_t BLOWINT = 13;

		// Currently most values for BatteryLevelDetector are just simulation and not tested on real hardware.
		float ThreshVoltage[32] = {1.10f, 1.15f, 1.20f, 1.25f, 1.30f, 1.35f, 1.40f, 1.45f, 1.50f, 1.60f, 1.70f, 1.80f, 1.90f, 2.00f, 2.10f, 2.20f,
			2.30f, 2.40f, 2.50f, 2.60f, 2.70f, 2.80f, 2.90f, 3.00f, 3.10f, 3.20f, 3.30f, 3.40f, 3.50f, 3.60f, 3.80f, 4.00f};
		const float StandardError = 0.02f;

		const float SolarPanelThresh = 1.0f;

		const size_t TestRoutineTicks = 131072;
		const size_t InitTicks = 16384;

	public:
		using Peripheral::Peripheral;

		void StartTest(bool TestMode, bool AutoRep, float thresh);
		void TestTick();
		void StopTest();

		void Initialise();
		void Tick();
		void Reset();
	};
	void PowerSupply::Initialise() {
		clock_type = CLOCK_UNDEFINED;

		BLDMode = 0;
		BLDControl = 0;
		threshold = 0;
		data_BLDCON2 = 0;
		data_SPIndicator = 0;
		isTestRoutineRunning = false;

		BLDFlag = emulator.BatteryVoltage >= ThreshVoltage[0] ? 1 : 0;

		region_BLDCON0.Setup(0xF0D0, 1, "BatteryLevelDetector/BLDCON0", &threshold, MMURegion::DefaultRead<uint8_t, 0x1F>, MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);
		region_BLDCON1.Setup(
			0xF0D1, 1, "BatteryLevelDetector/BLDCON1", this, [](MMURegion* region, size_t) {
            PowerSupply *powersupply = (PowerSupply*)region->userdata;
            uint8_t ReadData = (powersupply->BLDMode << 2) | (powersupply->BLDFlag << 1) | static_cast<uint8_t>(powersupply->BLDControl); // msvc C4805
            return ReadData; }, [](MMURegion* region, size_t, uint8_t data) {
            PowerSupply *powersupply = (PowerSupply*)region->userdata;
            powersupply->BLDMode = (data & 0x04) >> 2;
            if(data & 0x01) {
                powersupply->BLDControl = 1;
                powersupply->StartTest(powersupply->BLDMode, false, powersupply->ThreshVoltage[powersupply->threshold]);
            } else {
                powersupply->BLDControl = 0;
                powersupply->isTestRoutineRunning = false;
            } }, emulator);
		region_BLDCON2.Setup(0xF0D2, 1, "BatteryLevelDetector/BLDCON2", &data_BLDCON2, MMURegion::DefaultRead<uint8_t, 0x37>, MMURegion::DefaultWrite<uint8_t, 0x37>, emulator);

		region_SPIndicator.Setup(
			0xF310, 1, "SolarPanelIndicator/0xF310*1", this, [](MMURegion* region, size_t) {
            PowerSupply *powersupply = (PowerSupply*)region->userdata;
            bool isSPAvailable = powersupply->emulator.SolarPanelVoltage >= powersupply->SolarPanelThresh ? 1 : 0;
            return (uint8_t)((powersupply->data_SPIndicator & 0x0F) | (isSPAvailable << 4)); }, [](MMURegion* region, size_t, uint8_t data) {
            PowerSupply *powersupply = (PowerSupply*)region->userdata;
            powersupply->data_SPIndicator = (data & 0x0F); }, emulator);
	}

	void PowerSupply::StartTest(bool TestMode, bool AutoRep, float thresh) {
		if (isTestRoutineRunning)
			return;

		isTestRoutineRunning = true;
		CurrentTestMode = TestMode;
		CurrentRepMode = AutoRep;
		CurrentThresh = thresh;
		HasResult = false;
		TestTimer = 0;
	}

	void PowerSupply::TestTick() {
		TestTimer++;
		if (TestTimer >= TestRoutineTicks) {
			StopTest();
			return;
		}
		if (TestTimer < InitTicks || HasResult)
			return;

		float BatteryVoltage = emulator.BatteryVoltage;
		if (BatteryVoltage < CurrentThresh * (1 - StandardError)) {
			BLDFlag = 0;
			HasResult = true;
			if (data_BLDCON2 & 0x20)
				emulator.chipset.MaskableInterrupts[BLOWINT].TryRaise();
			return;
		}
		if (BatteryVoltage > CurrentThresh * (1 + StandardError)) {
			BLDFlag = 1;
			HasResult = true;
			return;
		}
		float RelVolt = (BatteryVoltage - CurrentThresh) / StandardError;
		if (CurrentTestMode) {
			if (TestTimer >= (0.5 - RelVolt / 2) * (TestRoutineTicks - InitTicks) + InitTicks) {
				BLDFlag = 1;
				HasResult = true;
			}
		}
		else {
			if (RelVolt > 0) {
				if (TestTimer >= (0.5 - RelVolt / 2) * (TestRoutineTicks - InitTicks) + InitTicks) {
					BLDFlag = 1;
					HasResult = true;
				}
			}
			else {
				if (TestTimer >= (0.5 + RelVolt / 2) * (TestRoutineTicks - InitTicks) + InitTicks) {
					BLDFlag = 0;
					HasResult = true;
					if (data_BLDCON2 & 0x20)
						emulator.chipset.MaskableInterrupts[BLOWINT].TryRaise();
					return;
				}
			}
		}
	}

	void PowerSupply::StopTest() {
		if (!isTestRoutineRunning)
			return;

		isTestRoutineRunning = false;
		if (!(data_BLDCON2 & 0x07)) {
			if (data_BLDCON2 & 0x10)
				emulator.chipset.MaskableInterrupts[BENDINT].TryRaise();
			BLDControl = 0;
			return;
		}
		if (CurrentRepMode) {
			StartTest(BLDMode, (data_BLDCON2 & 0x07) == 0x07 ? true : false, ThreshVoltage[threshold]);
			return;
		}
		if (data_BLDCON2 & 0x10)
			emulator.chipset.MaskableInterrupts[BENDINT].TryRaise();
		TestTimer = 0;
		DelayTicks = std::pow(2, -(data_BLDCON2 & 0x07)) * emulator.GetCyclesPerSecond();
	}

	void PowerSupply::Tick() {
		if (isTestRoutineRunning) {
			TestTick();
		}
		else if (BLDControl) {
			TestTimer++;
			if (TestTimer >= DelayTicks)
				StartTest(BLDMode, (data_BLDCON2 & 0x07) == 0x07 ? true : false, ThreshVoltage[threshold]);
		}
	}

	void PowerSupply::Reset() {
		BLDMode = 0;
		BLDControl = 0;
		threshold = 0;
		data_BLDCON2 = 0;
		data_SPIndicator = 0;
		isTestRoutineRunning = false;
		BLDFlag = emulator.BatteryVoltage >= ThreshVoltage[0] ? 1 : 0;
	}
	Peripheral* CreatePowerSupply(Emulator& emu) {
		return new PowerSupply(emu);
	}
} // namespace casioemu