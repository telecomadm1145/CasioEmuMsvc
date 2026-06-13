#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"
#include "Models.h"
#include "Peripheral/Keyboard.hpp"
#include "Peripheral/Screen.hpp"
#include "Snapshot.h"

#include <SDL.h>
#include <emscripten.h>

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

bool low_perf_ext = false;
char* n_ram_buffer = nullptr;
casioemu::MMU* me_mmu = nullptr;
casioemu::Emulator* m_emu = nullptr;
uint32_t pc_cache = 0;

int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0.0f;
bool enable_screen_fading = false;
float screen_flashing_brightness_coeff = 1.5f;
bool screen_residual_enabled = true;
float screen_residual_alpha_scale = 1.0f;
int screen_buffer_select = 0;
bool audio_enable = false;

namespace {
	constexpr const char* kCoreDir = "/tmp/casioemu_core";
	constexpr const char* kRomPath = "/tmp/casioemu_core/rom.bin";

	std::unique_ptr<casioemu::Emulator> g_emulator;
	bool g_sdl_ready = false;
	uint32_t g_cpu_time = 0;
	Uint64 g_last_cpu_tick = 0;
	Uint64 g_last_emu_tick = 0;
	bool g_main_loop_running = false;
	casioemu::IScreenFrameProvider* g_screen_provider = nullptr;
	std::vector<uint8_t> g_frame_rgba;
	std::vector<uint8_t> g_source_frame_rgba;
	std::vector<uint8_t> g_status_alpha;
	std::vector<uint8_t> g_snapshot_buffer;
	int g_frame_width = 0;
	int g_frame_height = 0;
	uint8_t g_display_r = 0;
	uint8_t g_display_g = 0;
	uint8_t g_display_b = 0;

	void EnsureSdl() {
		if (!g_sdl_ready) {
			SDL_Init(SDL_INIT_TIMER);
			g_sdl_ready = true;
		}
	}

	void PumpEmulationClock() {
		if (!g_emulator || !g_emulator->Running()) return;
		const Uint64 now = SDL_GetTicks64();
		if (g_last_cpu_tick == 0) {
			g_last_cpu_tick = now;
			g_last_emu_tick = now;
		}

		int catchup_steps = 0;
		while (now >= g_last_cpu_tick + g_emulator->timer_interval && catchup_steps < 4) {
			g_emulator->TimerCallback();
			g_last_cpu_tick += g_emulator->timer_interval;
			++catchup_steps;
		}
		if (catchup_steps == 4 && now > g_last_cpu_tick + 250) {
			g_last_cpu_tick = now;
		}

		if (now >= g_last_emu_tick + 25) {
			g_emulator->chipset.EmulatorTick();
			g_last_emu_tick = now;
		}
		g_cpu_time = static_cast<uint32_t>(SDL_GetTicks());
	}

	void CoreFrame() {
		PumpEmulationClock();
	}

	void ResetClock() {
		g_last_cpu_tick = SDL_GetTicks64();
		g_last_emu_tick = g_last_cpu_tick;
		g_cpu_time = static_cast<uint32_t>(SDL_GetTicks());
	}

	void RefreshScreenProvider() {
		g_screen_provider = nullptr;
		if (!g_emulator) return;
		g_screen_provider = g_emulator->chipset.QueryInterface<casioemu::IScreenFrameProvider>();
		if (g_screen_provider) {
			g_frame_width = g_screen_provider->GetFrameWidth();
			g_frame_height = g_screen_provider->GetFrameHeight();
			g_frame_rgba.resize(static_cast<size_t>(g_frame_width) * static_cast<size_t>(g_frame_height) * 4);
			g_source_frame_rgba.resize(static_cast<size_t>(g_frame_width) * static_cast<size_t>(g_frame_height) * 4);
			g_status_alpha.resize(static_cast<size_t>(g_frame_width));
		}
		else {
			g_frame_width = 0;
			g_frame_height = 0;
			g_frame_rgba.clear();
			g_source_frame_rgba.clear();
			g_status_alpha.clear();
		}
	}

	void StopMainLoop() {
		if (g_main_loop_running) {
			emscripten_cancel_main_loop();
			g_main_loop_running = false;
		}
	}

	casioemu::HardwareId HardwareIdFromCoreType(int core_type) {
		if (core_type < casioemu::HW_MIN || core_type > casioemu::HW_MAX) {
			printf("[CasioEmuCore][Error] Invalid hardware_id from core type: %d\n", core_type);
			std::abort();
		}
		return static_cast<casioemu::HardwareId>(core_type);
	}

	casioemu::ModelInfo MakeWebModel(bool real_hardware, bool is_sample_rom, int pd_value, int model_type) {
	const auto hardware_id = HardwareIdFromCoreType(model_type);
		casioemu::ModelInfo model{};
	model.csr_mask = hardware_id == casioemu::HW_ES_PLUS ? 0x1 : 0xf;
		model.hardware_id = hardware_id;
		model.real_hardware = real_hardware;
		model.pd_value = static_cast<unsigned char>(pd_value & 0xff);
		model.interface_path = "";
		model.model_name = "CasioEmuCore";
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

	bool UserRamRange(uint32_t& addr, int& len) {
		if (!g_emulator) return false;
		switch (g_emulator->hardware_id) {
		case casioemu::HW_CLASSWIZ_II:
			addr = 0x9000;
			len = 0x4000;
			return true;
		case casioemu::HW_ES_PLUS:
		case casioemu::HW_EPS6800:
			addr = 0x8000;
			len = 0x2000;
			return true;
		default:
			addr = 0xD000;
			len = 0x2000;
			return true;
		}
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

int casioemu_core_init_real_rom(const uint8_t* rom, int len, int pd_value, int model_type) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		StopMainLoop();
		g_emulator.reset();
		if (!WriteRomFile(rom, len)) return -2;
		auto model = MakeWebModel(true, false, pd_value, model_type);
		g_emulator = std::make_unique<casioemu::Emulator>(model, false, true);
		m_emu = g_emulator.get();
		low_perf_ext = true;
		RefreshScreenProvider();
		ResetClock();
		return 0;
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][Error] %s\n", ex.what());
		g_emulator.reset();
		m_emu = nullptr;
		return -3;
	}
}

int casioemu_core_init_sim_rom(const uint8_t* rom, int len, int is_sample_rom, int pd_value, int model_type) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		StopMainLoop();
		g_emulator.reset();
		const auto hardware_id = HardwareIdFromCoreType(model_type);
		auto normalized_rom = NormalizeSimulatorRomForWeb(rom, len, hardware_id);
		if (!WriteRomFile(normalized_rom.data(), static_cast<int>(normalized_rom.size()))) return -2;
		auto model = MakeWebModel(false, is_sample_rom != 0, pd_value, model_type);
		g_emulator = std::make_unique<casioemu::Emulator>(model, false, true);
		m_emu = g_emulator.get();
		low_perf_ext = true;
		RefreshScreenProvider();
		ResetClock();
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
	StopMainLoop();
	g_emulator.reset();
	m_emu = nullptr;
	me_mmu = nullptr;
	n_ram_buffer = nullptr;
	g_screen_provider = nullptr;
	g_frame_rgba.clear();
	g_source_frame_rgba.clear();
	g_status_alpha.clear();
	g_snapshot_buffer.clear();
	g_frame_width = 0;
	g_frame_height = 0;
}

int casioemu_core_start() {
	if (!g_emulator) return 1;
	ResetClock();
	if (!g_main_loop_running) {
		emscripten_set_main_loop(CoreFrame, 0, 0);
		g_main_loop_running = true;
	}
	return 0;
}

void casioemu_core_stop() {
	StopMainLoop();
}

int casioemu_core_reset() {
	if (!g_emulator) return 1;
	g_emulator->chipset.Reset();
	RefreshScreenProvider();
	ResetClock();
	return 0;
}

int casioemu_core_is_running() {
	return g_emulator && g_emulator->Running();
}

uint32_t casioemu_core_cpu_time() {
	return g_cpu_time;
}

int casioemu_core_button_event(int kiko, int pressed) {
	if (!g_emulator) return 1;
	auto keyboard = g_emulator->chipset.QueryInterface<casioemu::IKeyboardAutomation>();
	if (!keyboard) return 2;
	if (kiko < 0 || kiko > 0xFF) return 3;
	keyboard->PressCode(static_cast<uint8_t>(kiko), pressed != 0);
	return 0;
}

int casioemu_core_sdl_key_event(int keycode, int pressed) {
	if (!g_emulator) return 1;
	auto keyboard = g_emulator->chipset.QueryInterface<casioemu::IKeyboardAutomation>();
	if (!keyboard) return 2;
	keyboard->HandleKeycode(keycode, pressed != 0);
	return 0;
}

int casioemu_core_bind_sdl_key(int keycode, int kiko) {
	if (!g_emulator) return 1;
	auto keyboard = g_emulator->chipset.QueryInterface<casioemu::IKeyboardAutomation>();
	if (!keyboard) return 2;
	if (kiko < 0 || kiko > 0xFF) return 3;
	keyboard->BindKeycode(keycode, static_cast<uint8_t>(kiko));
	return 0;
}

int casioemu_core_set_display_color(int r, int g, int b) {
	g_display_r = static_cast<uint8_t>(std::clamp(r, 0, 255));
	g_display_g = static_cast<uint8_t>(std::clamp(g, 0, 255));
	g_display_b = static_cast<uint8_t>(std::clamp(b, 0, 255));
	return 0;
}

int casioemu_core_set_screen_shadow(int enabled, double alpha_scale) {
	screen_residual_enabled = enabled != 0;
	if (std::isfinite(alpha_scale)) {
		screen_residual_alpha_scale = static_cast<float>(std::clamp(alpha_scale, 0.0, 1.0));
	}
	return 0;
}

int casioemu_core_update_frame() {
	if (!g_emulator || !g_screen_provider) return 1;
	g_screen_provider->UpdateFrameAlpha();
	g_frame_width = g_screen_provider->GetFrameWidth();
	g_frame_height = g_screen_provider->GetFrameHeight();
	g_frame_rgba.resize(static_cast<size_t>(g_frame_width) * static_cast<size_t>(g_frame_height) * 4);
	g_source_frame_rgba.resize(static_cast<size_t>(g_frame_width) * static_cast<size_t>(g_frame_height) * 4);
	g_status_alpha.resize(static_cast<size_t>(g_frame_width));
	const auto& color = g_emulator->ModelDefinition.ink_color;
	g_screen_provider->WriteFrameRgba(g_source_frame_rgba.data(), color.r, color.g, color.b);
	const int display_height = std::max(0, g_frame_height - 1);
	const int source_start_row = 1;
	g_frame_rgba.resize(static_cast<size_t>(g_frame_width) * static_cast<size_t>(display_height) * 4);
	for (int y = 0; y < display_height; ++y) {
		const auto* src = g_source_frame_rgba.data() + (static_cast<size_t>(source_start_row + y) * g_frame_width * 4);
		auto* dst = g_frame_rgba.data() + (static_cast<size_t>(y) * g_frame_width * 4);
		for (int x = 0; x < g_frame_width; ++x) {
			dst[x * 4] = g_display_r;
			dst[x * 4 + 1] = g_display_g;
			dst[x * 4 + 2] = g_display_b;
			dst[x * 4 + 3] = src[x * 4 + 3];
		}
	}
	if (!g_status_alpha.empty() && !g_source_frame_rgba.empty()) {
		for (int x = 0; x < g_frame_width; ++x) {
			g_status_alpha[x] = g_source_frame_rgba[x * 4 + 3];
		}
	}
	g_frame_height = display_height;
	return 0;
}

uint32_t casioemu_core_frame_ptr() {
	return g_frame_rgba.empty() ? 0 : reinterpret_cast<uint32_t>(g_frame_rgba.data());
}

int casioemu_core_frame_width() {
	return g_frame_width;
}

int casioemu_core_frame_height() {
	return g_frame_height;
}

uint32_t casioemu_core_status_alpha_ptr() {
	return g_status_alpha.empty() ? 0 : reinterpret_cast<uint32_t>(g_status_alpha.data());
}

int casioemu_core_status_alpha_len() {
	return static_cast<int>(g_status_alpha.size());
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

int casioemu_core_user_ram_size() {
	uint32_t addr = 0;
	int len = 0;
	return UserRamRange(addr, len) ? len : 0;
}

int casioemu_core_save_user_ram(uint8_t* out, int max_len) {
	if (!g_emulator || !out || max_len < 0) return -1;
	uint32_t addr = 0;
	int len = 0;
	if (!UserRamRange(addr, len)) return -2;
	if (max_len < len) return -len;
	return ReadDataBulk(addr, len, out);
}

int casioemu_core_load_user_ram(const uint8_t* in, int len) {
	if (!g_emulator || !in || len < 0) return 1;
	uint32_t addr = 0;
	int ram_len = 0;
	if (!UserRamRange(addr, ram_len)) return 2;
	const int copy_len = std::min(len, ram_len);
	for (int i = 0; i < copy_len; ++i) {
		g_emulator->chipset.mmu.WriteData(addr + i, in[i], false);
	}
	return 0;
}

uint32_t casioemu_core_snapshot_ptr() {
	return g_snapshot_buffer.empty() ? 0 : reinterpret_cast<uint32_t>(g_snapshot_buffer.data());
}

int casioemu_core_snapshot_len() {
	return static_cast<int>(g_snapshot_buffer.size());
}

int casioemu_core_save_snapshot() {
	if (!g_emulator) return 1;
	try {
		const std::filesystem::path snapshot_path = std::filesystem::path(kCoreDir) / "state.snapshot";
		std::filesystem::create_directories(kCoreDir);
		SnapshotManager manager;
		const uint32_t id = manager.SaveSnapshot(*g_emulator, 0, "WebCalcEmu");
		manager.ExportNode(snapshot_path, id);
		std::ifstream in(snapshot_path, std::ios::binary);
		if (!in) return 2;
		g_snapshot_buffer.assign(
			std::istreambuf_iterator<char>(in),
			std::istreambuf_iterator<char>());
		return g_snapshot_buffer.empty() ? 3 : 0;
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][Snapshot][Error] %s\n", ex.what());
		g_snapshot_buffer.clear();
		return 4;
	}
}

int casioemu_core_load_snapshot(const uint8_t* in, int len) {
	if (!g_emulator || !in || len <= 0) return 1;
	try {
		const std::filesystem::path snapshot_path = std::filesystem::path(kCoreDir) / "state.snapshot";
		std::filesystem::create_directories(kCoreDir);
		{
			std::ofstream out(snapshot_path, std::ios::binary);
			if (!out) return 2;
			out.write(reinterpret_cast<const char*>(in), len);
			if (!out.good()) return 3;
		}
		SnapshotManager manager;
		manager.ImportFromFile(snapshot_path);
		if (manager.Nodes.empty()) return 4;
		manager.LoadSnapshot(*g_emulator, manager.Nodes.front().Id);
		RefreshScreenProvider();
		ResetClock();
		return 0;
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][Snapshot][Error] %s\n", ex.what());
		return 5;
	}
}

}

static uint32_t rom_info_addr(int hw) {
	switch (hw) {
		case 3: return 0x1fff4; // HW_ES_PLUS
		case 4: return 0x3ffee; // HW_CLASSWIZ
		case 5: return 0x5ffee; // HW_CLASSWIZ_II
		default: return 0;
	}
}

extern "C" const char* casioemu_core_rom_version() {
	static char buffer[32] = {};
	if (!g_emulator) return "";
	const auto hw = g_emulator->ModelDefinition.hardware_id;
	const uint32_t addr = rom_info_addr(hw);
	if (!addr) return "";

	std::ifstream rom(kRomPath, std::ios::binary);
	if (!rom) return "";

	char name[7] = {};
	char ver[3] = {};
	unsigned char sum[2] = {};
	rom.seekg(addr); rom.read(name, 6);
	rom.seekg(addr + 6); rom.read(ver, 2);
	rom.seekg(addr + 8); rom.read(reinterpret_cast<char*>(sum), 2);
	snprintf(buffer, sizeof(buffer), "%.6s %.2s (%02X)", name, ver, sum[1] * 0x100 + sum[0]);
	return buffer;
}

int main() {
	return 0;
}
