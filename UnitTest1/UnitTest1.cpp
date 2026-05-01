#include "pch.h"
#include "CppUnitTest.h"
#include "Emulator.hpp"
#include "Chipset.hpp"
#include "MMU.hpp"


bool low_perf_ext = false;

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MMU
{
	TEST_CLASS(Fuzzing)
	{
	public:
		TEST_METHOD(Filling)
		{
			auto fakepath = R"(C:\Users\Administrator\Downloads\windows-x64-6649606\models\fx-999CN CW_test\rom.bin)";
			for (size_t i = casioemu::HardwareId::HW_MIN; i < casioemu::HardwareId::HW_MAX; i++)
			{
				for (size_t j = 0; j < 2; j++)
				{
					if (i == casioemu::HardwareId::HW_FX_5800P && !j)
						continue; // FX-5800P doesn't have a emulator rom.
					casioemu::ModelInfo mi{};
					mi.hardware_id = i;
					mi.rom_path = fakepath;
					mi.flash_path = fakepath;
					mi.real_hardware = j;
					casioemu::Emulator emu{ mi,true,true };
					for (size_t k = 0; k < 0x10'0000; k++)
					{
						emu.chipset.mmu.WriteData(k, 0xff);
					}
					for (size_t k = 0; k < 0x10'0000; k++)
					{
						emu.chipset.mmu.WriteData(k, 0);
					}
				}
			}
		}
	};
}
