#include "MMU.hpp"

#include "CPU.hpp"
#include "Chipset.hpp"
#include "Emulator.hpp"
#include "Gui/Hooks.h"
#include "Gui/Ui.hpp"
#include "Logger.hpp"
#include <cstring>

namespace casioemu {
	MMU::MMU(Emulator& _emulator) : emulator(_emulator) {
		segment_dispatch = new MemoryByte*[0x100];
		for (size_t ix = 0; ix != 0x100; ++ix)
			segment_dispatch[ix] = nullptr;
	}

	MMU::~MMU() {
		for (size_t ix = 0; ix != 0x100; ++ix)
			if (segment_dispatch[ix])
				delete[] segment_dispatch[ix];

		delete[] segment_dispatch;
	}

	void MMU::GenerateSegmentDispatch(size_t segment_index) {
		segment_dispatch[segment_index] = new MemoryByte[0x10000];
		for (size_t ix = 0; ix != 0x10000; ++ix) {
			segment_dispatch[segment_index][ix].region = nullptr;
		}
	}

	void MMU::SetupInternals() {
		me_mmu = this;
		real_hardware = emulator.ModelDefinition.real_hardware;
	}

	inline uint16_t le_read(uint8_t& a) {
		return *(uint16_t*)&a;
	}

	uint16_t MMU::ReadCode(size_t offset) {
		// if (offset >= (1 << 20))
		//	PANIC("offset doesn't fit 20 bits\n");
		// if (offset & 1)
		//	PANIC("offset has LSB set\n");

		size_t segment_index = offset >> 16;
		size_t segment_offset = offset & 0xFFFE;

		// if (emulator.hardware_id == HW_FX_5800P && segment_index > 7) {
		//	auto off = (segment_index & 7) << 16;
		//	return (((uint16_t)emulator.chipset.flash_data[off + segment_offset + 1]) << 8) | emulator.chipset.flash_data[off + segment_offset];
		// }
		//  Read from rom data?

		auto rom = emulator.chipset.rom_data.data();
		auto rom_size = emulator.chipset.rom_data.size();
		switch (emulator.hardware_id) {
		case HW_ES_PLUS:
			if (offset < rom_size)
				return le_read(rom[offset]);
			else
				return 0;
		case HW_TI:
		case HW_CLASSWIZ:
			if (emulator.chipset.SegmentAccess && segment_index == 5)
				segment_index = 0;
			if (segment_index < 4) {
				if (emulator.chipset.remap)
					return le_read(rom[offset + ((segment_index == 0 && segment_offset < 0x200) ? 0xFE00 : 0)]);
				else
					return (segment_index == 0 && segment_offset >= 0xFE00) ? 0xFFFF : le_read(rom[offset]);
			}
			return 0;
		case HW_CLASSWIZ_II:
			if (segment_index == 8)
				return le_read(rom[offset & 0x7ffff]);
			segment_index &= 7;
			if (segment_index == 7) {
				if (segment_offset >= 0x2000) {
					return 0xffff;
				}
				else {
					return le_read(rom[0x5E000 + segment_offset]);
				}
			}
			if (segment_index > 6)
				return 0xFFFF;
			if (segment_index == 5) {
				if (segment_offset >= 0xe000)
					return 0xFFFF;
			}
			if (emulator.chipset.remap)
				return le_read(rom[offset + ((segment_index == 0 && segment_offset < 0x200) ? 0xFE00 : 0)]);
			else
				return (segment_index == 0 && segment_offset >= 0xFE00) ? 0xFFFF : le_read(rom[offset]);
		case HW_FX_5800P:
			if (segment_index < 2)
				return le_read(rom[offset]);
			if (segment_index >= 8)
				return le_read(emulator.chipset.flash_data[offset & 0x7ffff]);
			return 0xFFFF;
		default:
			return 0;
		}
	}
	uint8_t MMU::ReadData(size_t offset, bool softwareRead) {
		// if (offset >= (1 << 24))
		//	PANIC("offset doesn't fit 24 bits\n");
#ifdef DBG
		if (offset == 0x60722) { // Debug printf

		}
		if (softwareRead) {
			MemoryEventArgs mea{};
			mea.offset = static_cast<uint32_t>(offset);
			RaiseEvent(on_memory_read, *this, mea);
			if (mea.handled)
				return mea.value;
		}
#endif

		/*
		things about accessing unmapped segment is actually far more complex on real hardware;
		the result seems to be also affected by the next instruction,
		and not every address of a certain segment gives the same result.
		the code here only provides a simple simulation which could deal with 2 qr code errors on fx991cncw:
		1.x=an in solver,exe,page down,exe twice,then catalog;
		2.1234567890123xan in solver,exe,page down,exe,catalog,up
		*/
		if (emulator.hardware_id == HW_CLASSWIZ_II && real_hardware) {
			offset = getRealOffset(offset);
			switch (offset) {
			case 0x1000001:
				emulator.chipset.cpu.CorruptByDSR();
				return 0;
			case 0x1000002:
				return 0;
			case 0x1000003:
				return 0xFF;
			default:
				break;
			}
		}

		size_t segment_index = offset >> 16;
		size_t segment_offset = offset & 0xFFFF;
		if (emulator.hardware_id == HW_FX_5800P) {
			if (offset == 0x100000) { // TODO: this is a hack!
				return 0xff;
			}
		}

		MemoryByte* segment = segment_dispatch[segment_index];
		if (!segment) {
			return 0;
		}

		MemoryByte& byte = segment[segment_offset];
		MMURegion* region = byte.region;

		if (!region || !region->read) {
			return 0;
		}
		return region->read(region, offset);
	}

	void MMU::WriteData(size_t offset, uint8_t data, bool softwareWrite) {
		// if (offset >= (1 << 24))
		//	PANIC("offset doesn't fit 24 bits\n");

#ifdef DBG
		if (offset == 0x60721) {
			std::cout << data;
			return;
		}
		if (softwareWrite) {
			MemoryEventArgs mea{};
			mea.offset = static_cast<uint32_t>(offset);
			mea.value = data;
			RaiseEvent(on_memory_write, *this, mea);
			if (mea.handled)
				return;
		}
#endif

		size_t segment_index = offset >> 16;
		size_t segment_offset = offset & 0xFFFF;

		MemoryByte* segment = segment_dispatch[segment_index];
		if (!segment) {
			return;
		}

		MemoryByte& byte = segment[segment_offset];
		MMURegion* region = byte.region;
		if (!region || !region->write) {
#ifdef DBG
			printf("[MMU][Warn] Unmapped write: %x <- %x\n", (uint32_t)offset, (uint32_t)data);
#endif
			return;
		}
		region->write(region, offset, data);
	}

	size_t MMU::getRealOffset(size_t offset) {
		size_t segment_index = offset >> 16;
		if (segment_index < 0x10)
			return offset;
		if (segment_index == 0xF0 || segment_index == 0x98)
			return 0x1000001;
		if ((segment_index & 0x07) > 5) {
			if (offset & 0x02 || !(offset & 0xFF)) {
				return 0x1000002;
			}
			else {
				return 0x1000003;
			}
		}
		return offset & 0x0FFFFF;
	}

	std::vector<MMURegion*> MMU::GetRegions() {
		return regions;
	}

	void MMU::RegisterRegion(MMURegion* region) {
		for (size_t ix = region->base; ix != region->base + region->size; ++ix) {
			if (segment_dispatch[ix >> 16][ix & 0xFFFF].region)
				PANIC("MMU region overlap at %06zX\n", ix);
			segment_dispatch[ix >> 16][ix & 0xFFFF].region = region;
		}
		regions.push_back(region);
	}

	void MMU::UnregisterRegion(MMURegion* region) {
		for (size_t ix = region->base; ix != region->base + region->size; ++ix) {
			if (!segment_dispatch[ix >> 16][ix & 0xFFFF].region)
				PANIC("MMU region double-hole at %06zX\n", ix);
			segment_dispatch[ix >> 16][ix & 0xFFFF].region = nullptr;
		}
		regions.erase(std::find(regions.begin(), regions.end(), region));
	}
} // namespace casioemu
