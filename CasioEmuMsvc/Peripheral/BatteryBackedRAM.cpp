#include "BatteryBackedRAM.hpp"
#include "Emulator.hpp"
#include "MMURegion.hpp"
#include "Peripheral.hpp"
#include "Ui.hpp"
#include <Models.h>
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace casioemu {
	inline void fillRandomData(unsigned char* buf, size_t size) {
		std::srand(static_cast<unsigned int>(SDL_GetPerformanceCounter())); // 使用当前时间作为随机种子
		std::generate(buf, buf + size, []() {
			return static_cast<unsigned char>(std::rand() % 256); // 生成0到255之间的随机数
		});
	}

	constexpr uint32_t SAVE_INTERVAL_MS = 10 * 1000; // 10s

	class BatteryBackedRAM : public Peripheral, public IRam {
		MMURegion region{}, region_2{}, region_5{};
		uint8_t* ram_buffer{};
		uint8_t* pram_buffer{};
		size_t ram_size{};
		bool ram_file_requested{};

	public:
		using Peripheral::Peripheral;

		// 定时器回调函数
		static Uint32 SaveRamCallback(Uint32 interval, void* param) {
			static_cast<BatteryBackedRAM*>(param)->SaveRAMImage();
			return interval; // 继续触发定时器
		}

		void Initialise() override;
		void Uninitialise() override;
		void SaveRAMImage();
		void LoadRAMImage();

		void* GetRam() override { return ram_buffer; }
		void* GetPRam() override { return pram_buffer; }
		void* QueryInterface(const char* name) override {
			return strcmp(name, typeid(IRam).name()) == 0 ? static_cast<IRam*>(this) : nullptr;
		}
	};

	void BatteryBackedRAM::Initialise() {
		bool real_hardware = emulator.ModelDefinition.real_hardware;
		ram_size = GetRamSize(emulator.hardware_id) + (real_hardware ? 0 : 0x100);

		ram_buffer = new uint8_t[ram_size];
		fillRandomData(ram_buffer, ram_size);

		LoadRAMImage();

		region.Setup(
			GetRamBaseAddr(emulator.hardware_id), GetRamSize(emulator.hardware_id),
			"BatteryBackedRAM", ram_buffer,
			[](MMURegion* r, size_t o) { return static_cast<uint8_t*>(r->userdata)[o - r->base]; },
			[](MMURegion* r, size_t o, uint8_t d) { static_cast<uint8_t*>(r->userdata)[o - r->base] = d; },
			emulator);

		if (emulator.hardware_id == HW_FX_5800P) {
			pram_buffer = new uint8_t[0x8000];
			fillRandomData(pram_buffer, 0x8000);
			region_5.Setup(
				0x40000, 0x8000, "Segment4", pram_buffer,
				[](MMURegion* r, size_t o) { return static_cast<uint8_t*>(r->userdata)[o - r->base]; },
				[](MMURegion* r, size_t o, uint8_t d) { static_cast<uint8_t*>(r->userdata)[o - r->base] = d; },
				emulator);
		}

		if (!real_hardware) {
			region_2.Setup(
				emulator.hardware_id == HW_ES_PLUS	  ? 0x9800
				: emulator.hardware_id == HW_CLASSWIZ ? 0x49800
													  : 0x89800,
				0x0100, "BatteryBackedRAM/2", ram_buffer + ram_size - 0x100,
				[](MMURegion* r, size_t o) { return static_cast<uint8_t*>(r->userdata)[o - r->base]; },
				[](MMURegion* r, size_t o, uint8_t d) { static_cast<uint8_t*>(r->userdata)[o - r->base] = d; },
				emulator);
		}

		SDL_AddTimer(SAVE_INTERVAL_MS, SaveRamCallback, this);
		n_ram_buffer = (char*)ram_buffer;
	}

	void BatteryBackedRAM::SaveRAMImage() {
		auto ram_path = emulator.GetModelFilePath("ram.dmp");
		std::ofstream ram_out(ram_path, std::ofstream::binary);
		if (ram_out) {
			ram_out.write(reinterpret_cast<const char*>(ram_buffer), ram_size);
			logger::Info("[BatteryBackedRAM][Info] RAM image saved to ram.dmp\n");
		}
		else {
			logger::Info("[BatteryBackedRAM][Error] Failed to save RAM image to ram.dmp\n");
		}

		if (pram_buffer) {
			auto pram_path = emulator.GetModelFilePath("pram.dmp");
			std::ofstream pram_out(pram_path, std::ofstream::binary);
			if (pram_out) {
				pram_out.write(reinterpret_cast<const char*>(pram_buffer), 0x8000);
				logger::Info("[BatteryBackedRAM][Info] PRAM image saved to pram.dmp\n");
			}
			else {
				logger::Info("[BatteryBackedRAM][Error] Failed to save PRAM image to pram.dmp\n");
			}
		}
	}

	void BatteryBackedRAM::LoadRAMImage() {
		auto ram_path = emulator.GetModelFilePath("ram.dmp");
		std::ifstream ram_in(ram_path, std::ifstream::binary);
		if (ram_in) {
			ram_in.read(reinterpret_cast<char*>(ram_buffer), ram_size);
			logger::Info("[BatteryBackedRAM][Info] RAM image loaded from ram.dmp\n");
		}
		else {
			logger::Info("[BatteryBackedRAM][Warn] Can't read RAM image from ram.dmp\n");
		}

		if (pram_buffer) {
			auto pram_path = emulator.GetModelFilePath("pram.dmp");
			std::ifstream pram_in(pram_path, std::ifstream::binary);
			if (pram_in) {
				pram_in.read(reinterpret_cast<char*>(pram_buffer), 0x8000);
				logger::Info("[BatteryBackedRAM][Info] PRAM image loaded from pram.dmp\n");
			}
			else {
				logger::Info("[BatteryBackedRAM][Warn] Can't read PRAM image from pram.dmp\n");
			}
		}
	}

	void BatteryBackedRAM::Uninitialise() {
		SaveRAMImage();
		delete[] ram_buffer;
		delete[] pram_buffer;
	}

	Peripheral* CreateBatteryBackedRAM(Emulator& emu) {
		return new BatteryBackedRAM(emu);
	}

} // namespace casioemu
