#include "5800Flash.h"
#include "Chipset/MMU.hpp"
#include "Chipset/Chipset.hpp"
#include "Emulator.hpp"
#include <fstream>
#include <cstring>

namespace casioemu {

	constexpr const char* FLASH_SAVE_PATH = "flash.dmp";
	constexpr uint32_t SAVE_INTERVAL_MS = 10 * 1000; // 10s
	class Flash2 : public casioemu::Peripheral {
	public:
		MMURegion flash;
		int flash_mode = 0;

		static Uint32 SaveRamCallback(Uint32 interval, void* param) {
			static_cast<Flash2*>(param)->SaveFlashData();
			return interval;
		}
		Flash2(Emulator& emu) : casioemu::Peripheral(emu) {}

		void Initialise() override {
			LoadFlashData();
			flash.Setup(
				0x80000, 0x80000, "Flash/Fx5800PFlash", this,
				[](MMURegion* region, size_t offset) -> uint8_t {
					auto flash = static_cast<Flash2*>(region->userdata);
					auto fo = offset & 0x7ffff;
					if (flash->flash_mode == 6) {
						flash->flash_mode = 0;
						return 0x80;
					}
					return flash->emulator.chipset.flash_data[fo];
				},
				[](MMURegion* region, size_t offset, uint8_t data) {
					auto flash = static_cast<Flash2*>(region->userdata);
					auto fo = offset & 0x7ffff;
					switch (flash->flash_mode) {
					case 0:
						if (fo == 0xaaa && data == 0xaa) {
							flash->flash_mode = 1;
							return;
						}
						break;
					case 1:
						if (fo == 0x555 && data == 0x55) {
							flash->flash_mode = 2;
							return;
						}
						break;
					case 2:
						if (fo == 0xAAA && data == 0xA0) {
							flash->flash_mode = 3;
							return;
						}
						if (fo == 0xaaa && data == 0x80) {
							flash->flash_mode = 4;
							return;
						}
						break;
					case 3:
						flash->emulator.chipset.flash_data[fo] = data;
						flash->flash_mode = 0;
						return;
					case 4:
						if (fo == 0xAAA && data == 0xaa) {
							flash->flash_mode = 5;
							return;
						}
						break;
					case 5:
						if (fo == 0x555 && data == 0x55) {
							flash->flash_mode = 6;
							return;
						}
						break;
					case 6:
						if (fo == 0)
							memset(&flash->emulator.chipset.flash_data[fo], 0xff, 0x7fff);
						if (fo == 0x20000 || fo == 0x30000)
							memset(&flash->emulator.chipset.flash_data[fo], 0xff, 0xffff);
						return;
					case 7:
						if (fo == 0xaaa && data == 0xaa) {
							flash->flash_mode = 1;
							return;
						}
						break;
					}
					if (data == 0xf0) {
						flash->flash_mode = 0;
						return;
					}
					printf("[Flash][Warn] Unknown command: %05x = %02x\n", static_cast<int>(fo), data);
				},
				emulator);

					SDL_AddTimer(SAVE_INTERVAL_MS, SaveRamCallback, this);
		}

		void Uninitialise() override {
			SaveFlashData();
		}

		void SaveFlashData() {
			std::ofstream out_file(emulator.GetModelFilePath(FLASH_SAVE_PATH), std::ios::binary);
			if (out_file) {
				out_file.write((char*)emulator.chipset.flash_data.data(), 0x80000);
				logger::Info("[Flash2] Flash data saved to flash.dmp\n");
			}
			else {
				logger::Info("[Flash2] Failed to save flash data to flash.dmp\n");
			}
		}

		void LoadFlashData() {
			std::ifstream in_file(emulator.GetModelFilePath(FLASH_SAVE_PATH), std::ios::binary);
			if (in_file) {
				in_file.read((char*)emulator.chipset.flash_data.data(), 0x80000);
				logger::Info("[Flash2] Flash data loaded from flash.dmp\n");
			}
			else {
				logger::Info("[Flash2] Using default flash data\n");
			}
		}
	};

	Peripheral* CreateFx5800Flash(Emulator& emu) {
		return new Flash2(emu);
	}

} // namespace casioemu
