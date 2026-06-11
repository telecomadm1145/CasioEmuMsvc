#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"
#include "Peripheral/Keyboard.hpp"
#include "Peripheral/Screen.hpp"

#include <SDL.h>
#include <emscripten.h>

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

bool low_perf_ext = false;
char* n_ram_buffer = nullptr;
casioemu::MMU* me_mmu = nullptr;
casioemu::Emulator* m_emu = nullptr;
uint32_t pc_cache = 0;

int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0.0f;
bool enable_screen_fading = false;
float screen_flashing_brightness_coeff = 1.5f;
int screen_buffer_select = 0;
bool audio_enable = false;

namespace {
	constexpr const char* kCoreDir = "/tmp/casioemu_core";
	constexpr const char* kRomPath = "/tmp/casioemu_core/rom.bin";

	std::unique_ptr<casioemu::Emulator> g_emulator;
	bool g_sdl_ready = false;
	uint32_t g_cpu_time = 0;

	void EnsureSdl() {
		if (!g_sdl_ready) {
			SDL_Init(SDL_INIT_TIMER);
			g_sdl_ready = true;
		}
	}

	int BitIndex(uint8_t value) {
		if (value == 0) return -1;
		for (int i = 0; i < 8; ++i) {
			if (value == (1u << i)) return i;
		}
		return -1;
	}

	int ParseModelNumber(const std::string& model_name) {
		if (model_name.size() < 5) return -1;
		int value = 0;
		for (size_t i = 2; i < 5; ++i) {
			if (model_name[i] < '0' || model_name[i] > '9') return -1;
			value = value * 10 + (model_name[i] - '0');
		}
		return value;
	}

	casioemu::HardwareId HardwareIdFromModelName(const std::string& model_name, bool real_hardware) {
		if (model_name.size() < 2) return casioemu::HW_CLASSWIZ;
		const std::string prefix = model_name.substr(0, 2);
		const int ver_num = ParseModelNumber(model_name);
		if (prefix == "EY" || prefix == "FY" || prefix == "EG") return casioemu::HW_CLASSWIZ_II;
		if (prefix == "CY") {
			if (ver_num >= 840) return casioemu::HW_ES_PLUS;
			if (ver_num >= 220 && ver_num < 230) return casioemu::HW_ES_PLUS;
			return casioemu::HW_CLASSWIZ;
		}
		if (prefix == "LY" || prefix == "GY" || prefix == "ES" || prefix == "FC") return casioemu::HW_ES_PLUS;
		return casioemu::HW_CLASSWIZ;
	}

	casioemu::ModelInfo MakeWebModel(const char* model_name_cstr, bool real_hardware, bool is_sample_rom, int pd_value) {
		const std::string model_name = model_name_cstr && model_name_cstr[0] ? model_name_cstr : (real_hardware ? "CY239R" : "CY");
		const auto hardware_id = HardwareIdFromModelName(model_name, real_hardware);
		casioemu::ModelInfo model{};
		model.csr_mask = hardware_id == casioemu::HW_ES_PLUS ? 0xff : 0x7;
		model.hardware_id = hardware_id;
		model.real_hardware = real_hardware;
		model.pd_value = static_cast<unsigned char>(pd_value & 0xff);
		model.interface_path = "";
		model.model_name = model_name;
		model.rom_path = kRomPath;
		model.enable_new_screen = false;
		model.is_sample_rom = is_sample_rom;
		model.legacy_ko = false;
		model.u16_mode = hardware_id != casioemu::HW_ES_PLUS;
		model.LARGE_model = true;
		model.ml620_mirroring = hardware_id != casioemu::HW_CLASSWIZ;
		model.ink_color = {0, 0, 0};

		for (int ko = 0; ko < 8; ++ko) {
			for (int ki = 0; ki < 8; ++ki) {
				casioemu::ButtonInfo button{};
				button.kiko = (ko << 4) | ki;
				button.keyname = "";
				model.buttons.push_back(button);
			}
		}
		casioemu::ButtonInfo on{};
		on.kiko = 0xFF;
		on.keyname = "";
		model.buttons.push_back(on);
		return model;
	}

	bool WriteRomFile(const uint8_t* rom, int len) {
		std::filesystem::create_directories(kCoreDir);
		std::ofstream out(kRomPath, std::ios::binary);
		if (!out) return false;
		out.write(reinterpret_cast<const char*>(rom), len);
		return out.good();
	}

	std::vector<uint8_t> NormalizeSimulatorRomForWeb(const uint8_t* rom, int len, casioemu::HardwareId hardware_id) {
		std::vector<uint8_t> normalized(rom, rom + len);
		if (hardware_id == casioemu::HW_CLASSWIZ_II && normalized.size() >= 0x72000) {
			std::copy_n(normalized.begin() + 0x70000, 0x2000, normalized.begin() + 0x5E000);
		}
		return normalized;
	}

	uint16_t ReadRegister(int reg_type) {
		auto& cpu = g_emulator->chipset.cpu;
		if (reg_type >= 0 && reg_type <= 15) return cpu.reg_r[reg_type].raw;
		switch (reg_type) {
		case 16: return cpu.reg_pc.raw;
		case 17: return cpu.reg_lr.raw;
		case 18: return cpu.reg_elr[1].raw;
		case 19: return cpu.reg_elr[2].raw;
		case 20: return cpu.reg_elr[3].raw;
		case 21: return cpu.reg_csr.raw;
		case 22: return cpu.reg_lcsr.raw;
		case 23: return cpu.reg_ecsr[1].raw;
		case 24: return cpu.reg_ecsr[2].raw;
		case 25: return cpu.reg_ecsr[3].raw;
		case 26: return cpu.reg_psw.raw;
		case 27: return cpu.reg_epsw[1].raw;
		case 28: return cpu.reg_epsw[2].raw;
		case 29: return cpu.reg_epsw[3].raw;
		case 30: return cpu.reg_sp.raw;
		case 31: return cpu.reg_ea.raw;
		case 32: return (cpu.reg_psw.raw & casioemu::CPU::PSW_C) != 0;
		case 33: return (cpu.reg_psw.raw & casioemu::CPU::PSW_Z) != 0;
		case 34: return (cpu.reg_psw.raw & casioemu::CPU::PSW_S) != 0;
		case 35: return (cpu.reg_psw.raw & casioemu::CPU::PSW_OV) != 0;
		case 36: return (cpu.reg_psw.raw & casioemu::CPU::PSW_MIE) != 0;
		case 37: return (cpu.reg_psw.raw & casioemu::CPU::PSW_HC) != 0;
		case 38: return cpu.GetExceptionLevel();
		case 39: return cpu.reg_psw.raw;
		default: return 0;
		}
	}

	void WriteFlag(uint8_t mask, int value) {
		auto& psw = g_emulator->chipset.cpu.reg_psw.raw;
		if (value) psw |= mask;
		else psw &= ~mask;
	}

	void WriteRegister(int reg_type, uint16_t value) {
		auto& cpu = g_emulator->chipset.cpu;
		if (reg_type >= 0 && reg_type <= 15) {
			cpu.reg_r[reg_type] = static_cast<uint8_t>(value);
			return;
		}
		switch (reg_type) {
		case 16: cpu.reg_pc = value; break;
		case 17: cpu.reg_lr = value; break;
		case 18: cpu.reg_elr[1] = value; break;
		case 19: cpu.reg_elr[2] = value; break;
		case 20: cpu.reg_elr[3] = value; break;
		case 21: cpu.reg_csr = value; break;
		case 22: cpu.reg_lcsr = value; break;
		case 23: cpu.reg_ecsr[1] = value; break;
		case 24: cpu.reg_ecsr[2] = value; break;
		case 25: cpu.reg_ecsr[3] = value; break;
		case 26: cpu.reg_psw = static_cast<uint8_t>(value); break;
		case 27: cpu.reg_epsw[1] = static_cast<uint8_t>(value); break;
		case 28: cpu.reg_epsw[2] = static_cast<uint8_t>(value); break;
		case 29: cpu.reg_epsw[3] = static_cast<uint8_t>(value); break;
		case 30: cpu.reg_sp = value; break;
		case 31: cpu.reg_ea = value; break;
		case 32: WriteFlag(casioemu::CPU::PSW_C, value); break;
		case 33: WriteFlag(casioemu::CPU::PSW_Z, value); break;
		case 34: WriteFlag(casioemu::CPU::PSW_S, value); break;
		case 35: WriteFlag(casioemu::CPU::PSW_OV, value); break;
		case 36: WriteFlag(casioemu::CPU::PSW_MIE, value); break;
		case 37: WriteFlag(casioemu::CPU::PSW_HC, value); break;
		case 39: cpu.reg_psw = static_cast<uint8_t>(value); break;
		default: break;
		}
	}

	casioemu::MMURegion* FindRegion(uint32_t addr) {
		for (auto* region : g_emulator->chipset.mmu.GetRegions()) {
			if (addr >= region->base && addr < region->base + region->size) return region;
		}
		return nullptr;
	}

	uint32_t NextRegionBase(uint32_t addr, uint32_t end) {
		uint32_t next = end;
		for (auto* region : g_emulator->chipset.mmu.GetRegions()) {
			if (region->base > addr && region->base < next) next = static_cast<uint32_t>(region->base);
		}
		return next;
	}

	bool IsRawMemoryRegion(const casioemu::MMURegion* region) {
		return region && (
			region->description == "BatteryBackedRAM" ||
			region->description == "BatteryBackedRAM/2" ||
			region->description == "Segment4");
	}


	int ReadDataBulk(uint32_t addr, int len, uint8_t* out) {
		if (g_emulator->chipset.cpu.reg_dsr) return -1;
		const uint32_t end = addr + static_cast<uint32_t>(len);
		uint32_t cur = addr;
		int out_pos = 0;
		while (cur < end) {
			auto* region = FindRegion(cur);
			if (!region || !region->read) {
				const uint32_t next = NextRegionBase(cur, end);
				const uint32_t chunk = std::max<uint32_t>(1, next - cur);
				std::memset(out + out_pos, 0, chunk);
				cur += chunk;
				out_pos += static_cast<int>(chunk);
				continue;
			}

			const uint32_t region_end = static_cast<uint32_t>(std::min<size_t>(region->base + region->size, end));
			const uint32_t chunk = region_end - cur;
			if (IsRawMemoryRegion(region)) {
				std::memcpy(out + out_pos, static_cast<uint8_t*>(region->userdata) + (cur - region->base), chunk);
			}
			else {
				for (uint32_t i = 0; i < chunk; ++i) {
					out[out_pos + i] = region->read(region, cur + i);
				}
			}
			cur += chunk;
			out_pos += static_cast<int>(chunk);
		}
		return 0;
	}
}

extern "C" {

int casioemu_core_init_real_rom(const uint8_t* rom, int len, const char* model_name, int pd_value) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		g_emulator.reset();
		if (!WriteRomFile(rom, len)) return -2;
		auto model = MakeWebModel(model_name, true, false, pd_value);
		g_emulator = std::make_unique<casioemu::Emulator>(model, false, true);
		m_emu = g_emulator.get();
		low_perf_ext = true;
		g_cpu_time = SDL_GetTicks();
		return 0;
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][Error] %s\n", ex.what());
		g_emulator.reset();
		m_emu = nullptr;
		return -3;
	}
}

int casioemu_core_init_sim_rom(const uint8_t* rom, int len, const char* model_name, int is_sample_rom, int pd_value) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		g_emulator.reset();
		const std::string model_name_str = model_name && model_name[0] ? model_name : "CY";
		const auto hardware_id = HardwareIdFromModelName(model_name_str, false);
		auto normalized_rom = NormalizeSimulatorRomForWeb(rom, len, hardware_id);
		if (!WriteRomFile(normalized_rom.data(), static_cast<int>(normalized_rom.size()))) return -2;
		auto model = MakeWebModel(model_name, false, is_sample_rom != 0, pd_value);
		g_emulator = std::make_unique<casioemu::Emulator>(model, false, true);
		m_emu = g_emulator.get();
		low_perf_ext = true;
		g_cpu_time = SDL_GetTicks();
		return 0;
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][Error] %s\n", ex.what());
		g_emulator.reset();
		m_emu = nullptr;
		return -3;
	}
}

void casioemu_core_shutdown() {
	g_emulator.reset();
	m_emu = nullptr;
	me_mmu = nullptr;
	n_ram_buffer = nullptr;
}

int casioemu_core_reset() {
	if (!g_emulator) return 1;
	g_emulator->chipset.Reset();
	return 0;
}

int casioemu_core_step(int cycles) {
	if (!g_emulator || cycles <= 0) return 1;
	for (int i = 0; i < cycles; ++i) {
		if (!g_emulator->Paused) g_emulator->Tick();
	}
	g_emulator->chipset.EmulatorTick();
	g_cpu_time = SDL_GetTicks();
	return 0;
}

int casioemu_core_is_running() {
	return g_emulator && g_emulator->Running();
}

uint32_t casioemu_core_cpu_time() {
	return g_cpu_time;
}

int casioemu_core_key_mask(int ki_mask, int ko_mask, int pressed) {
	if (!g_emulator) return 1;
	auto keyboard = g_emulator->chipset.QueryInterface<casioemu::IKeyboardAutomation>();
	if (!keyboard) return 2;
	if (ki_mask == 0 && ko_mask == 0) {
		keyboard->PressCode(0xFF, pressed != 0);
		return 0;
	}
	int ki = BitIndex(static_cast<uint8_t>(ki_mask));
	int ko = BitIndex(static_cast<uint8_t>(ko_mask));
	if (ki < 0 || ko < 0) return 3;
	keyboard->Key(ki, ko, pressed != 0);
	return 0;
}


int casioemu_core_read_data(uint32_t addr, int len, uint8_t* out) {
	if (!g_emulator || !out || len < 0) return 1;
	if (len > 64 && ReadDataBulk(addr, len, out) == 0) return 0;
	for (int i = 0; i < len; ++i) {
		out[i] = g_emulator->chipset.mmu.ReadData(addr + i, false);
	}
	return 0;
}

int casioemu_core_write_data(uint32_t addr, int len, const uint8_t* in) {
	if (!g_emulator || !in || len < 0) return 1;
	for (int i = 0; i < len; ++i) {
		g_emulator->chipset.mmu.WriteData(addr + i, in[i], false);
	}
	return 0;
}

int casioemu_core_read_code(uint32_t addr, int len, uint8_t* out) {
	if (!g_emulator || !out || len < 0) return 1;
	auto& rom = g_emulator->chipset.rom_data;
	for (int i = 0; i < len; ++i) {
		uint32_t offset = addr + i;
		out[i] = offset < rom.size() ? rom[offset] : 0xFF;
	}
	return 0;
}

int casioemu_core_write_code(uint32_t addr, int len, const uint8_t* in) {
	if (!g_emulator || !in || len < 0) return 1;
	auto& rom = g_emulator->chipset.rom_data;
	if (addr + static_cast<uint32_t>(len) > rom.size()) return 2;
	for (int i = 0; i < len; ++i) {
		rom[addr + i] = in[i];
	}
	return 0;
}

int casioemu_core_read_reg(int reg_type, uint16_t* out) {
	if (!g_emulator || !out) return 1;
	*out = ReadRegister(reg_type);
	return 0;
}

int casioemu_core_write_reg(int reg_type, uint16_t value) {
	if (!g_emulator) return 1;
	WriteRegister(reg_type, value);
	return 0;
}

int casioemu_core_set_solar_voltage(double voltage) {
	if (!g_emulator) return 1;
	if (!std::isfinite(voltage)) return 2;
	g_emulator->SolarPanelVoltage = static_cast<float>(voltage);
	return 0;
}

int casioemu_core_set_battery_voltage(double voltage) {
	if (!g_emulator) return 1;
	if (!std::isfinite(voltage)) return 2;
	g_emulator->BatteryVoltage = static_cast<float>(voltage);
	return 0;
}

int casioemu_core_save_state(uint8_t* out, int max_len) {
	if (!g_emulator || !out || max_len <= 0) return -1;
	std::ostringstream os(std::ios::binary);
	g_emulator->chipset.SaveStateAll(os);
	const auto state = os.str();
	if (static_cast<int>(state.size()) > max_len) return -static_cast<int>(state.size());
	std::memcpy(out, state.data(), state.size());
	return static_cast<int>(state.size());
}

int casioemu_core_load_state(const uint8_t* in, int len) {
	if (!g_emulator || !in || len <= 0) return 1;
	std::string state(reinterpret_cast<const char*>(in), len);
	std::istringstream is(state, std::ios::binary);
	g_emulator->chipset.LoadStateAll(is);
	return 0;
}

}

int main() {
	return 0;
}


