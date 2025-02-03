#include "ROMWindow.hpp"

#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "Logger.hpp"
#include "ModelInfo.h"

#include <string>

namespace casioemu {
	class ROMWindow : public Peripheral {
		std::unique_ptr<MMURegion[]> regions;

	public:
		using Peripheral::Peripheral;

		void Initialise();
	};
	static void SetupROMRegion(MMURegion& region, size_t region_base, size_t size, size_t rom_base, bool strict_memory, Emulator& emulator, std::string description = {}) {
		uint8_t* data = emulator.chipset.rom_data.data();
		auto offset = (long long)rom_base - (long long)region_base;
		if (description.empty())
			description = "ROM/Segment" + std::to_string(region_base >> 16);

		MMURegion::WriteFunction write_function = strict_memory ? [](MMURegion* region, size_t address, uint8_t data) {
			// logger::Info("ROM::[region write lambda]: attempt to write %02hhX to %06zX\n", data, address);
			region->emulator->HandleMemoryError();
		}
																: [](MMURegion* region, size_t address, uint8_t data) {
																	  // printf("ROM::[region write lambda]: attempt to write %02hhX to %06zX\n", data, address);
																  };

		if (offset >= 0)
			region.Setup(
				region_base, size, description, data + offset, [](MMURegion* region, size_t address) {
					return ((uint8_t*)(region->userdata))[address];
				},
				write_function, emulator);
		else
			region.Setup(
				region_base, size, description, data + rom_base, [](MMURegion* region, size_t address) {
					return ((uint8_t*)(region->userdata))[address - region->base];
				},
				write_function, emulator);
	}

	void ROMWindow::Initialise() {
		bool strict_memory = emulator.argv_map.find("strict_memory") != emulator.argv_map.end();

		switch (emulator.hardware_id) { // Initializer list cannot be used with move-only type: https://stackoverflow.com/q/8468774
		case HW_ES_PLUS:
			regions.reset(new MMURegion[3]);
			emulator.chipset.rom_data.resize(0x20000, 0);
			SetupROMRegion(regions[0], 0x00000, 0x08000, 0x00000, strict_memory, emulator);
			SetupROMRegion(regions[1], 0x10000, 0x10000, 0x10000, strict_memory, emulator);
			SetupROMRegion(regions[2], 0x80000, 0x10000, 0x00000, strict_memory, emulator);
			break;

		case HW_CLASSWIZ:
			regions.reset(new MMURegion[5]);
			emulator.chipset.rom_data.resize(0x40000, 0);
			SetupROMRegion(regions[0], 0x00000, 0x0D000, 0x00000, strict_memory, emulator);
			SetupROMRegion(regions[1], 0x10000, 0x10000, 0x10000, strict_memory, emulator);
			SetupROMRegion(regions[2], 0x20000, 0x10000, 0x20000, strict_memory, emulator);
			SetupROMRegion(regions[3], 0x30000, 0x10000, 0x30000, strict_memory, emulator);
			SetupROMRegion(regions[4], 0x50000, 0x10000, 0x00000, strict_memory, emulator);
			break;

		case HW_TI:
		case HW_CLASSWIZ_II:
			regions.reset(new MMURegion[16]);
			emulator.chipset.rom_data.resize(0x60000, 0);
			SetupROMRegion(regions[0], 0x00000, 0x09000, 0x00000, strict_memory, emulator);
			SetupROMRegion(regions[1], 0x10000, 0x10000, 0x10000, strict_memory, emulator);
			SetupROMRegion(regions[2], 0x20000, 0x10000, 0x20000, strict_memory, emulator);
			SetupROMRegion(regions[3], 0x30000, 0x10000, 0x30000, strict_memory, emulator);
			SetupROMRegion(regions[4], 0x40000, 0x10000, 0x40000, strict_memory, emulator);
			SetupROMRegion(regions[5], 0x50000, 0x10000, 0x50000, strict_memory, emulator);
			if (emulator.ModelDefinition.real_hardware) {
				SetupROMRegion(regions[7], 0x70000, 0x2000, 0x5E000, strict_memory, emulator);
				SetupROMRegion(regions[8], 0x80000, 0x10000, 0x00000, strict_memory, emulator);
				SetupROMRegion(regions[9], 0x90000, 0x10000, 0x10000, strict_memory, emulator);
				SetupROMRegion(regions[10], 0xa0000, 0x10000, 0x20000, strict_memory, emulator);
				SetupROMRegion(regions[11], 0xb0000, 0x10000, 0x30000, strict_memory, emulator);
				SetupROMRegion(regions[12], 0xc0000, 0x10000, 0x40000, strict_memory, emulator);
				SetupROMRegion(regions[13], 0xd0000, 0x10000, 0x50000, strict_memory, emulator);
				SetupROMRegion(regions[15], 0xf0000, 0x2000, 0x5E000, strict_memory, emulator);
			}
			else {
				SetupROMRegion(regions[7], 0x70000, 0x2000, 0x70000, strict_memory, emulator);
				SetupROMRegion(regions[9], 0x90000, 0x10000, 0x10000, strict_memory, emulator);
				SetupROMRegion(regions[10], 0xa0000, 0x10000, 0x20000, strict_memory, emulator);
				SetupROMRegion(regions[11], 0xb0000, 0x10000, 0x30000, strict_memory, emulator);
				SetupROMRegion(regions[12], 0xc0000, 0x10000, 0x40000, strict_memory, emulator);
				SetupROMRegion(regions[13], 0xd0000, 0x10000, 0x50000, strict_memory, emulator);
				SetupROMRegion(regions[15], 0xf0000, 0x2000, 0x5E000, strict_memory, emulator);
			}

			break;
		case HW_FX_5800P:
			regions.reset(new MMURegion[2]);
			emulator.chipset.rom_data.resize(0x20000, 0);
			SetupROMRegion(regions[0], 0x00000, 0x8000, 0x00000, strict_memory, emulator);
			SetupROMRegion(regions[1], 0x10000, 0x10000, 0x10000, strict_memory, emulator);
			break;
		default:
			PANIC("Unknown Model type");
			break;
		}
	}
	Peripheral* CreateRomWindow(Emulator& emu) {
		return new ROMWindow(emu);
	}
} // namespace casioemu
