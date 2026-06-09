#include "Chipset/Chipset.hpp"
#include "Chipset/MMURegion.hpp"
#include "Emulator.hpp"
#include "Peripheral.hpp"
#include <cstdint>
namespace casioemu {
	class Flash : public Peripheral {
		MMURegion region_flash_addr, region_flash_data,
			flash_control, flash_acceptor,
			flash_segment;
		uint16_t data_flash_addr = 0, data_flash_data = 0;
		uint8_t data_flash_control = 0,
				data_flash_segment = 0;
		int flashing_status = 0;
		void Initialise() override;

	public:
		Flash(Emulator& emulator) : Peripheral(emulator) {
		}
	};
	Peripheral* CreateFlash(Emulator& emu) {
		return new Flash(emu);
	}
} // namespace casioemu
void casioemu::Flash::Initialise() {
	region_flash_addr.Setup(0xF0E0, 2, "Flash/FLASHA", &data_flash_addr, casioemu::MMURegion::DefaultRead<uint16_t>, casioemu::MMURegion::DefaultWrite<uint16_t>, emulator);
	region_flash_data.Setup(
		0xF0E2, 2, "Flash/FLASHD", this,
		[](MMURegion* region, size_t offset) -> uint8_t {
			auto flash = (Flash*)region->userdata;
			if (offset == 0xf0e3) {
				return uint8_t(flash->data_flash_data >> 8);
			}
			else {
				return uint8_t(flash->data_flash_data & 0xff);
			}
		},
		[](MMURegion* region, size_t offset, uint8_t data) {
			auto flash = (Flash*)region->userdata;
			if (offset == 0xf0e3) {
				flash->data_flash_data = flash->data_flash_data & 0xff | (data << 8);
			}
			else {
				flash->data_flash_data = flash->data_flash_data & 0xff00 | data;
			}
			if (offset == 0xf0e3 && flash->flashing_status == 2) {
				auto index = (flash->data_flash_segment << 16) | flash->data_flash_addr;
				if (index <= region->emulator->chipset.rom_data.size() - 2) {
					*((uint8_t*)&region->emulator->chipset.rom_data[index]) = flash->data_flash_data & 0xff;
					*((uint8_t*)&region->emulator->chipset.rom_data[index + 1]) = flash->data_flash_data >> 8;
				}
				flash->flashing_status = 0;
			}
		},
		emulator);
	flash_acceptor.Setup(
		0xF0E5, 1, "Flash/FLASHACP", this, casioemu::MMURegion::IgnoreRead<0>, [](MMURegion* region, size_t offset, uint8_t data) {
			Flash* flash = (Flash*)region->userdata;
			switch (flash->flashing_status) {
			case 0:
				if (data == 0xFA)
					flash->flashing_status = 1;
				break;
			case 1:
				if (data == 0xF5)
					flash->flashing_status = 2;
				else
					flash->flashing_status = 0;
				break;
			default:
				return;
			}
		},
		emulator);
	flash_segment.Setup(0xF0E6, 1, "Flash/FLASHSEG", &data_flash_segment, casioemu::MMURegion::DefaultRead<uint8_t, 0x1F>, casioemu::MMURegion::DefaultWrite<uint8_t, 0x1F>, emulator);
}