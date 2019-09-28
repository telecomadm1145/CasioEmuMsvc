#include "BatteryBackedRAM.hpp"

#include "../Chipset/MMU.hpp"
#include "../Emulator.hpp"
#include "../Chipset/Chipset.hpp"
#include "../Logger.hpp"

#include <fstream>
#include <cstring>

namespace casioemu
{
	void BatteryBackedRAM::Initialise()
	{
		bool real_hardware = emulator.GetModelInfo("real_hardware");
		ram_size = real_hardware ? 0xE00 : 0xF00;

		ram_buffer = new uint8_t[ram_size];
		for (size_t ix = 0; ix != ram_size; ++ix)
			ram_buffer[ix] = 0;

		ram_file_requested = false;
		if (emulator.argv_map.find("ram") != emulator.argv_map.end())
		{
			ram_file_requested = true;

			if (emulator.argv_map.find("clean_ram") == emulator.argv_map.end())
				LoadRAMImage();
		}

		region.Setup(0x8000, 0x0E00, "BatteryBackedRAM", ram_buffer, [](MMURegion *region, size_t offset) {
			return ((uint8_t *)region->userdata)[offset - region->base];
		}, [](MMURegion *region, size_t offset, uint8_t data) {
			((uint8_t *)region->userdata)[offset - region->base] = data;
		}, emulator);
		if (!real_hardware)
			region_2.Setup(0x9800, 0x0100, "BatteryBackedRAM/2", ram_buffer + 0xE00, [](MMURegion *region, size_t offset) {
				return ((uint8_t *)region->userdata)[offset - region->base];
			}, [](MMURegion *region, size_t offset, uint8_t data) {
				((uint8_t *)region->userdata)[offset - region->base] = data;
			}, emulator);
	}

	void BatteryBackedRAM::Uninitialise()
	{
		if (ram_file_requested && emulator.argv_map.find("preserve_ram") == emulator.argv_map.end())
			SaveRAMImage();

		delete[] ram_buffer;
	}

	void BatteryBackedRAM::SaveRAMImage()
	{
		std::ofstream ram_handle(emulator.argv_map["ram"], std::ofstream::binary);
		if (ram_handle.fail())
		{
			logger::Info("[BatteryBackedRAM] std::ofstream failed: %s\n", std::strerror(errno));
			return;
		}
		ram_handle.write((char *)ram_buffer, ram_size);
		if (ram_handle.fail())
		{
			logger::Info("[BatteryBackedRAM] std::ofstream failed: %s\n", std::strerror(errno));
			return;
		}
	}

	void BatteryBackedRAM::LoadRAMImage()
	{
		std::ifstream ram_handle(emulator.argv_map["ram"], std::ifstream::binary);
		if (ram_handle.fail())
		{
			logger::Info("[BatteryBackedRAM] std::ifstream failed: %s\n", std::strerror(errno));
			return;
		}
		ram_handle.read((char *)ram_buffer, ram_size);
		if (ram_handle.fail())
		{
			logger::Info("[BatteryBackedRAM] std::ifstream failed: %s\n", std::strerror(errno));
			return;
		}
	}
}

