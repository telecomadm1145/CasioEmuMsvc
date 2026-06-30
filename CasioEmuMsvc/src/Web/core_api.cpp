#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "Emulator.hpp"
#include "ModelInfo.h"
#include "Models.h"
#include "Peripheral/Keyboard.hpp"
#include "Peripheral/BatteryBackedRAM.hpp"
#include "Peripheral/Screen.hpp"
#include "Romu.h"
#include "Snapshot.h"

#include <SDL.h>
#include <SDL_image.h>
#include <emscripten.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>
#include <unistd.h>

#include "Gui/Localization.h"
#include "Gui/ThemeManager.h"
#include "Gui/WebDebuggerGui.h"
#include "Gui/imgui/imgui.h"
#include "Gui/imgui/imgui_internal.h"

bool low_perf_ext = false;
extern char* n_ram_buffer;
extern casioemu::MMU* me_mmu;
extern casioemu::Emulator* m_emu;
extern uint32_t pc_cache;

extern int screen_flashing_threshold;
extern float screen_fading_blending_coefficient;
extern bool enable_screen_fading;
extern float screen_flashing_brightness_coeff;
extern bool screen_residual_enabled;
extern float screen_residual_alpha_scale;
extern int screen_buffer_select;
extern bool audio_enable;

namespace {
	constexpr const char* kCoreDir = "/tmp/casioemu_core";
	constexpr const char* kRomPath = "/tmp/casioemu_core/rom.bin";
	constexpr const char* kFlashPath = "/tmp/casioemu_core/flash.bin";

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
	bool g_qr_active = false;
	int g_qr_version = 0;
	uint64_t g_qr_revision = 0;
	std::string g_qr_data;
	int g_frame_width = 0;
	int g_frame_height = 0;
	uint8_t g_display_r = 0;
	uint8_t g_display_g = 0;
	uint8_t g_display_b = 0;
	bool g_gui_attached = false;
	int g_gui_width = 0;
	int g_gui_height = 0;
	uint32_t g_gui_frame_counter = 0;
	std::vector<uint8_t> g_gui_frame_rgba;
	std::string g_model_id = "unknown";
	std::string g_gui_model_dir = "/persist/unknown";
	bool g_gui_imgui_ready = false;
	float g_gui_mouse_x = -FLT_MAX;
	float g_gui_mouse_y = -FLT_MAX;
	std::array<bool, 5> g_gui_mouse_down{};
	std::array<bool, 5> g_gui_mouse_release_pending{};
	std::array<int, 5> g_gui_mouse_hold_frames{};
	float g_gui_wheel_x = 0.0f;
	float g_gui_wheel_y = 0.0f;
	std::string g_gui_locale = "en_US";
	std::string g_gui_file_request_kind;
	std::string g_gui_file_request_path;
	std::string g_gui_file_request_name;
	std::string g_gui_file_result_path;
	int g_gui_file_result_code = 0;
	bool g_gui_file_result_pending = false;
	std::string g_gui_clipboard_text;
	std::string g_gui_clipboard_write_text;
	uint32_t g_gui_clipboard_write_revision = 0;
	uint32_t g_gui_clipboard_ack_revision = 0;
	bool g_gui_ime_visible = false;
	float g_gui_ime_x = 0.0f;
	float g_gui_ime_y = 0.0f;
	float g_gui_ime_line_height = 0.0f;
	std::vector<uint8_t> g_gui_background_rgba;
	int g_gui_background_width = 0;
	int g_gui_background_height = 0;
	bool g_gui_background_checked = false;

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
			g_status_alpha.resize(static_cast<size_t>(std::max(0, g_screen_provider->GetStatusAlphaCount())));
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

	void SyncStatusAlpha() {
		if (!g_screen_provider) return;
		const int count = g_screen_provider->GetStatusAlphaCount();
		if (count <= 0) {
			g_status_alpha.clear();
			return;
		}
		g_status_alpha.resize(static_cast<size_t>(count));
		g_screen_provider->WriteStatusAlpha(g_status_alpha.data(), count);
	}

	casioemu::HardwareId HardwareIdFromCoreType(int core_type) {
		if (core_type < casioemu::HW_MIN || core_type > casioemu::HW_MAX) {
			printf("[CasioEmuCore][Error] Invalid hardware_id from core type: %d\n", core_type);
			std::abort();
		}
		return static_cast<casioemu::HardwareId>(core_type);
	}

	void GuiShutdown();

	void GuiResetState() {
		GuiShutdown();
		g_gui_attached = false;
		g_gui_width = 0;
		g_gui_height = 0;
		g_gui_frame_counter = 0;
		g_gui_frame_rgba.clear();
		g_gui_mouse_x = -FLT_MAX;
		g_gui_mouse_y = -FLT_MAX;
		g_gui_mouse_down.fill(false);
		g_gui_mouse_release_pending.fill(false);
		g_gui_mouse_hold_frames.fill(0);
		g_gui_wheel_x = 0.0f;
		g_gui_wheel_y = 0.0f;
	}

	std::string SanitizeModelId(const char* model_id) {
		std::string sanitized;
		if (model_id) {
			for (const char* p = model_id; *p; ++p) {
				const unsigned char ch = static_cast<unsigned char>(*p);
				if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
					sanitized.push_back(static_cast<char>(ch));
				}
			}
		}
		return sanitized.empty() ? "unknown" : sanitized;
	}

	void SetCoreModelId(const char* model_id) {
		g_model_id = SanitizeModelId(model_id);
		g_gui_model_dir = std::string("/persist/") + g_model_id;
		std::filesystem::create_directories(g_gui_model_dir);
		if (g_emulator) {
			g_emulator->model_path = g_gui_model_dir;
		}
	}

	std::string CurrentWebModelDir() {
		return g_gui_model_dir.empty() ? std::string("/persist/unknown") : g_gui_model_dir;
	}

	void GuiDetachCanvas() {
		g_gui_attached = false;
		g_gui_width = 0;
		g_gui_height = 0;
		g_gui_frame_counter = 0;
		g_gui_frame_rgba.clear();
		g_gui_mouse_x = -FLT_MAX;
		g_gui_mouse_y = -FLT_MAX;
		g_gui_mouse_down.fill(false);
		g_gui_mouse_release_pending.fill(false);
		g_gui_mouse_hold_frames.fill(0);
		g_gui_wheel_x = 0.0f;
		g_gui_wheel_y = 0.0f;
		g_gui_file_request_kind.clear();
		g_gui_file_request_path.clear();
		g_gui_file_request_name.clear();
		g_gui_file_result_path.clear();
		g_gui_file_result_code = 0;
		g_gui_file_result_pending = false;
		g_gui_clipboard_text.clear();
		g_gui_clipboard_write_text.clear();
		g_gui_clipboard_write_revision = 0;
		g_gui_clipboard_ack_revision = 0;
		g_gui_ime_visible = false;
		g_gui_ime_x = 0.0f;
		g_gui_ime_y = 0.0f;
		g_gui_ime_line_height = 0.0f;
		g_gui_background_rgba.clear();
		g_gui_background_width = 0;
		g_gui_background_height = 0;
		g_gui_background_checked = false;
	}

	casioemu::ModelInfo MakeWebModel(bool real_hardware, bool is_sample_rom, int pd_value, int model_type, bool legacy_ko, bool classwiz_graph) {
	const auto hardware_id = HardwareIdFromCoreType(model_type);
		casioemu::ModelInfo model{};
	model.csr_mask = hardware_id == casioemu::HW_SOLARII ? 0x0 : (hardware_id == casioemu::HW_ES_PLUS ? 0x1 : 0xf);
		model.hardware_id = hardware_id;
		model.real_hardware = real_hardware;
		model.pd_value = static_cast<unsigned char>(pd_value & 0xff);
		model.interface_path = "";
		model.model_name = "CasioEmuCore";
		model.rom_path = kRomPath;
		if (hardware_id == casioemu::HW_FX_5800P) {
			model.flash_path = kFlashPath;
		}
		model.enable_new_screen = false;
		model.is_sample_rom = is_sample_rom;
		model.legacy_ko = legacy_ko;
		model.u16_mode = hardware_id == casioemu::HW_CLASSWIZ || hardware_id == casioemu::HW_CLASSWIZ_II || hardware_id == casioemu::HW_TI;
		model.LARGE_model = hardware_id != casioemu::HW_SOLARII;
		model.ml620_mirroring = hardware_id != casioemu::HW_CLASSWIZ;
		model.ink_color = {0, 0, 0};
		if (!real_hardware) {
			model.extra["limit_spd"] = "1";
		}
		if (classwiz_graph) {
			model.extra["classwiz_graph"] = "1";
		}

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

	bool WriteCoreFile(const char* path, const uint8_t* data, int len) {
		std::filesystem::create_directories(kCoreDir);
		std::ofstream out(path, std::ios::binary);
		if (!out) return false;
		out.write(reinterpret_cast<const char*>(data), len);
		return out.good();
	}

	bool WriteRomFile(const uint8_t* rom, int len) {
		return WriteCoreFile(kRomPath, rom, len);
	}

	bool WriteFlashFile(const uint8_t* flash, int len) {
		return WriteCoreFile(kFlashPath, flash, len);
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

	constexpr uint32_t kFx5800pStateMagic = 0x53503835; // "58PS"
	constexpr uint32_t kFx5800pStateVersion = 1;
	constexpr int kFx5800pPramLen = 0x8000;
	constexpr int kFx5800pFlashLen = 0x80000;
	constexpr int kFx5800pStateHeaderLen = 20;

	void WriteLe32(uint8_t* out, uint32_t value) {
		out[0] = static_cast<uint8_t>(value & 0xff);
		out[1] = static_cast<uint8_t>((value >> 8) & 0xff);
		out[2] = static_cast<uint8_t>((value >> 16) & 0xff);
		out[3] = static_cast<uint8_t>((value >> 24) & 0xff);
	}

	uint32_t ReadLe32(const uint8_t* in) {
		return static_cast<uint32_t>(in[0]) |
			(static_cast<uint32_t>(in[1]) << 8) |
			(static_cast<uint32_t>(in[2]) << 16) |
			(static_cast<uint32_t>(in[3]) << 24);
	}

	IRam* GetRamPeripheral() {
		return g_emulator ? static_cast<IRam*>(g_emulator->chipset.QueryInterface(typeid(IRam).name())) : nullptr;
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
		case casioemu::HW_FX_5800P:
			addr = static_cast<uint32_t>(casioemu::GetRamBaseAddr(casioemu::HW_FX_5800P));
			len = static_cast<int>(casioemu::GetRamSize(casioemu::HW_FX_5800P));
			return true;
		case casioemu::HW_SOLARII:
			addr = 0xE000;
			len = 0x1000;
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

	int Fx5800pRamLen() {
		uint32_t addr = 0;
		int len = 0;
		return UserRamRange(addr, len) ? len : 0;
	}

	int Fx5800pPersistentStateSize() {
		const int ram_len = Fx5800pRamLen();
		if (ram_len <= 0) return 0;
		return kFx5800pStateHeaderLen + ram_len + kFx5800pPramLen + kFx5800pFlashLen;
	}

	int SaveFx5800pPersistentState(uint8_t* out, int max_len) {
		const int ram_len = Fx5800pRamLen();
		const int state_len = Fx5800pPersistentStateSize();
		if (state_len <= 0) return -2;
		if (max_len < state_len) return -state_len;

		IRam* ram = GetRamPeripheral();
		if (!ram || !ram->GetRam() || !ram->GetPRam()) return -3;
		if (static_cast<int>(g_emulator->chipset.flash_data.size()) < kFx5800pFlashLen) return -4;

		WriteLe32(out, kFx5800pStateMagic);
		WriteLe32(out + 4, kFx5800pStateVersion);
		WriteLe32(out + 8, static_cast<uint32_t>(ram_len));
		WriteLe32(out + 12, kFx5800pPramLen);
		WriteLe32(out + 16, kFx5800pFlashLen);

		int pos = kFx5800pStateHeaderLen;
		std::memcpy(out + pos, ram->GetRam(), ram_len);
		pos += ram_len;
		std::memcpy(out + pos, ram->GetPRam(), kFx5800pPramLen);
		pos += kFx5800pPramLen;
		std::memcpy(out + pos, g_emulator->chipset.flash_data.data(), kFx5800pFlashLen);
		return 0;
	}

	int LoadFx5800pPersistentState(const uint8_t* in, int len) {
		if (len < kFx5800pStateHeaderLen) return 2;
		if (ReadLe32(in) != kFx5800pStateMagic) return 3;
		if (ReadLe32(in + 4) != kFx5800pStateVersion) return 4;

		const int ram_len = static_cast<int>(ReadLe32(in + 8));
		const int pram_len = static_cast<int>(ReadLe32(in + 12));
		const int flash_len = static_cast<int>(ReadLe32(in + 16));
		const int expected_ram_len = Fx5800pRamLen();
		if (ram_len != expected_ram_len || pram_len != kFx5800pPramLen || flash_len != kFx5800pFlashLen) return 5;
		if (len < kFx5800pStateHeaderLen + ram_len + pram_len + flash_len) return 6;

		IRam* ram = GetRamPeripheral();
		if (!ram || !ram->GetRam() || !ram->GetPRam()) return 7;
		if (static_cast<int>(g_emulator->chipset.flash_data.size()) < flash_len) return 8;

		int pos = kFx5800pStateHeaderLen;
		std::memcpy(ram->GetRam(), in + pos, ram_len);
		pos += ram_len;
		std::memcpy(ram->GetPRam(), in + pos, pram_len);
		pos += pram_len;
		std::memcpy(g_emulator->chipset.flash_data.data(), in + pos, flash_len);
		return 0;
	}

	void GuiLoadLocale() {
		try {
			chdir("/");
			const std::string locale = g_gui_locale == "zh_CN" ? "zh_CN" : "en_US";
			g_local.ChangeLanguage(locale, false);
			if (g_local.Get("StatusBar.Running") == "StatusBar.Running") {
				throw LocalizationException("StatusBar.Running is still missing after locale load");
			}
		}
		catch (const std::exception& ex) {
			printf("[CasioEmuCore][GUI][Locale] %s\n", ex.what());
			try {
				chdir("/");
				g_local.ChangeLanguage("en_US", false);
			}
			catch (const std::exception& fallback_ex) {
				printf("[CasioEmuCore][GUI][Locale] fallback failed: %s\n", fallback_ex.what());
			}
		}
	}

	void GuiApplyStyle() {
		ThemeManager::Instance().ApplyDefaultTheme();
	}

	void GuiApplyWebStyleOverrides() {
		ImGuiStyle& style = ImGui::GetStyle();
		style.Colors[ImGuiCol_WindowBg].w = std::min(style.Colors[ImGuiCol_WindowBg].w, 0.68f);
		style.Colors[ImGuiCol_ChildBg].w = std::min(style.Colors[ImGuiCol_ChildBg].w, 0.62f);
		style.Colors[ImGuiCol_PopupBg].w = std::min(style.Colors[ImGuiCol_PopupBg].w, 0.96f);
		style.Colors[ImGuiCol_DockingEmptyBg].w = 0.0f;
	}

	const char* GuiGetClipboardText(void*) {
		return g_gui_clipboard_text.c_str();
	}

	void GuiSetClipboardText(void*, const char* text) {
		g_gui_clipboard_text = text ? text : "";
		g_gui_clipboard_write_text = g_gui_clipboard_text;
		++g_gui_clipboard_write_revision;
	}

	void GuiSetImeData(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
		g_gui_ime_visible = data && data->WantVisible;
		if (!data) {
			g_gui_ime_x = 0.0f;
			g_gui_ime_y = 0.0f;
			g_gui_ime_line_height = 0.0f;
			return;
		}
		g_gui_ime_x = data->InputPos.x;
		g_gui_ime_y = data->InputPos.y;
		g_gui_ime_line_height = data->InputLineHeight;
	}

	const ImWchar* GuiCjkRanges() {
		static const ImWchar ranges[] = {
			0x0020, 0x00FF,
			0x2000, 0x206F,
			0x3000, 0x30FF,
			0x31F0, 0x31FF,
			0x4E00, 0x9FAF,
			0xFF00, 0xFFEF,
			0,
		};
		return ranges;
	}

	void GuiLoadFonts(ImGuiIO& io) {
		constexpr float kWebCjkFallbackFontSize = 12.0f;
		ImFontConfig config;
		config.PixelSnapH = true;
		io.Fonts->AddFontDefault(&config);

		constexpr const char* kBundledCjkFont = "/fonts/CasioEmuGuiCJKSubset.otf";
		if (std::filesystem::exists(kBundledCjkFont)) {
			config.MergeMode = true;
			if (io.Fonts->AddFontFromFileTTF(kBundledCjkFont, kWebCjkFallbackFontSize, &config, GuiCjkRanges())) {
				printf("[CasioEmuCore][GUI][Font] Loaded bundled CJK font: %s\n", kBundledCjkFont);
			}
			else {
				printf("[CasioEmuCore][GUI][Font] Failed to load bundled CJK font: %s\n", kBundledCjkFont);
			}
		}
		else {
			printf("[CasioEmuCore][GUI][Font] Bundled CJK font not found: %s\n", kBundledCjkFont);
		}
	}

	bool GuiEnsureImGui() {
		if (!g_emulator || g_gui_width <= 0 || g_gui_height <= 0) return false;
		if (g_gui_imgui_ready) return true;
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.GetClipboardTextFn = GuiGetClipboardText;
		io.SetClipboardTextFn = GuiSetClipboardText;
		io.ClipboardUserData = nullptr;
		io.PlatformSetImeDataFn = GuiSetImeData;
		GuiLoadFonts(io);
		unsigned char* pixels = nullptr;
		int font_width = 0;
		int font_height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(1));
		GuiApplyStyle();
		GuiApplyWebStyleOverrides();
		GuiLoadLocale();
		ThemeManager::Instance().LoadSettings();
		GuiApplyWebStyleOverrides();
		InitWebDebuggerGuiWindows();
		g_gui_imgui_ready = true;
		return true;
	}

	void GuiLoadBackground() {
		constexpr const char* kBackgroundPath = "/persist/background.jpg";
		g_gui_background_checked = true;
		g_gui_background_rgba.clear();
		g_gui_background_width = 0;
		g_gui_background_height = 0;
		if (!std::filesystem::exists(kBackgroundPath)) return;

		SDL_Surface* loaded = IMG_Load(kBackgroundPath);
		if (!loaded) {
			printf("[CasioEmuCore][GUI][Background] Failed to load %s: %s\n", kBackgroundPath, IMG_GetError());
			return;
		}
		SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
		SDL_FreeSurface(loaded);
		if (!rgba) {
			printf("[CasioEmuCore][GUI][Background] Failed to convert %s: %s\n", kBackgroundPath, SDL_GetError());
			return;
		}

		g_gui_background_width = rgba->w;
		g_gui_background_height = rgba->h;
		g_gui_background_rgba.resize(static_cast<size_t>(rgba->w) * static_cast<size_t>(rgba->h) * 4);
		const auto* src = static_cast<const uint8_t*>(rgba->pixels);
		for (int y = 0; y < rgba->h; ++y) {
			std::memcpy(
				g_gui_background_rgba.data() + static_cast<size_t>(y) * rgba->w * 4,
				src + static_cast<size_t>(y) * rgba->pitch,
				static_cast<size_t>(rgba->w) * 4);
		}
		SDL_FreeSurface(rgba);
	}

	void GuiFillDefaultBackground() {
		for (size_t i = 0; i < g_gui_frame_rgba.size(); i += 4) {
			g_gui_frame_rgba[i + 0] = 18;
			g_gui_frame_rgba[i + 1] = 20;
			g_gui_frame_rgba[i + 2] = 24;
			g_gui_frame_rgba[i + 3] = 255;
		}
	}

	void GuiDrawBackground() {
		g_gui_frame_rgba.assign(static_cast<size_t>(g_gui_width) * static_cast<size_t>(g_gui_height) * 4, 0);
		if (g_gui_width <= 0 || g_gui_height <= 0) return;
		if (!g_gui_background_checked || ThemeManager::Instance().IsBgReloadRequested()) {
			GuiLoadBackground();
			ThemeManager::Instance().ClearBgReloadRequest();
		}
		if (g_gui_background_rgba.empty() || g_gui_background_width <= 0 || g_gui_background_height <= 0) {
			GuiFillDefaultBackground();
			return;
		}

		const float window_aspect = static_cast<float>(g_gui_width) / static_cast<float>(g_gui_height);
		const float bg_aspect = static_cast<float>(g_gui_background_width) / static_cast<float>(g_gui_background_height);
		int dst_x = 0;
		int dst_y = 0;
		int dst_w = g_gui_width;
		int dst_h = g_gui_height;
		if (window_aspect > bg_aspect) {
			dst_h = static_cast<int>(static_cast<float>(g_gui_width) / bg_aspect);
			dst_y = (g_gui_height - dst_h) / 2;
		}
		else {
			dst_w = static_cast<int>(static_cast<float>(g_gui_height) * bg_aspect);
			dst_x = (g_gui_width - dst_w) / 2;
		}

		GuiFillDefaultBackground();
		for (int y = 0; y < g_gui_height; ++y) {
			const int rel_y = y - dst_y;
			if (rel_y < 0 || rel_y >= dst_h) continue;
			const int src_y = std::clamp(static_cast<int>((static_cast<int64_t>(rel_y) * g_gui_background_height) / std::max(1, dst_h)), 0, g_gui_background_height - 1);
			for (int x = 0; x < g_gui_width; ++x) {
				const int rel_x = x - dst_x;
				if (rel_x < 0 || rel_x >= dst_w) continue;
				const int src_x = std::clamp(static_cast<int>((static_cast<int64_t>(rel_x) * g_gui_background_width) / std::max(1, dst_w)), 0, g_gui_background_width - 1);
				const auto* src = g_gui_background_rgba.data() + (static_cast<size_t>(src_y) * g_gui_background_width + src_x) * 4;
				auto* dst = g_gui_frame_rgba.data() + (static_cast<size_t>(y) * g_gui_width + x) * 4;
				const int alpha = src[3];
				const int inv = 255 - alpha;
				dst[0] = static_cast<uint8_t>((src[0] * alpha + dst[0] * inv) / 255);
				dst[1] = static_cast<uint8_t>((src[1] * alpha + dst[1] * inv) / 255);
				dst[2] = static_cast<uint8_t>((src[2] * alpha + dst[2] * inv) / 255);
				dst[3] = 255;
			}
		}

		for (size_t i = 0; i < g_gui_frame_rgba.size(); i += 4) {
			g_gui_frame_rgba[i + 0] = static_cast<uint8_t>((g_gui_frame_rgba[i + 0] * 235) / 255);
			g_gui_frame_rgba[i + 1] = static_cast<uint8_t>((g_gui_frame_rgba[i + 1] * 235) / 255);
			g_gui_frame_rgba[i + 2] = static_cast<uint8_t>((g_gui_frame_rgba[i + 2] * 235) / 255);
			g_gui_frame_rgba[i + 3] = 255;
		}
	}

	void GuiShutdown() {
		if (g_gui_imgui_ready) {
			CleanupWebDebuggerGuiWindows();
			ImGui::DestroyContext();
			g_gui_imgui_ready = false;
		}
	}

	void GuiBeginFrame() {
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(g_gui_width), static_cast<float>(g_gui_height));
		io.DeltaTime = 1.0f / 30.0f;
		io.MousePos = ImVec2(g_gui_mouse_x, g_gui_mouse_y);
		for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown) && i < static_cast<int>(g_gui_mouse_down.size()); ++i) {
			io.MouseDown[i] = g_gui_mouse_down[i];
		}
		io.MouseWheelH += g_gui_wheel_x;
		io.MouseWheel += g_gui_wheel_y;
		g_gui_wheel_x = 0.0f;
		g_gui_wheel_y = 0.0f;
		GuiApplyWebStyleOverrides();
		ImGui::NewFrame();
		for (size_t i = 0; i < g_gui_mouse_down.size(); ++i) {
			if (g_gui_mouse_hold_frames[i] > 0) {
				--g_gui_mouse_hold_frames[i];
				if (g_gui_mouse_hold_frames[i] == 0 && g_gui_mouse_release_pending[i]) {
					g_gui_mouse_down[i] = false;
					g_gui_mouse_release_pending[i] = false;
				}
			}
		}
	}

	void GuiClearFrame() {
		GuiDrawBackground();
	}

	void GuiBlendPixel(int x, int y, ImU32 col, const unsigned char* font_pixels, int font_width, int font_height, ImVec2 uv) {
		if (x < 0 || y < 0 || x >= g_gui_width || y >= g_gui_height) return;
		uint8_t sr = col & 0xff;
		uint8_t sg = (col >> 8) & 0xff;
		uint8_t sb = (col >> 16) & 0xff;
		uint8_t sa = (col >> 24) & 0xff;
		if (font_pixels && font_width > 0 && font_height > 0) {
			const int tx = std::clamp(static_cast<int>(uv.x * font_width), 0, font_width - 1);
			const int ty = std::clamp(static_cast<int>(uv.y * font_height), 0, font_height - 1);
			const auto* texel = font_pixels + (static_cast<size_t>(ty) * font_width + tx) * 4;
			sr = static_cast<uint8_t>((static_cast<int>(sr) * texel[0]) / 255);
			sg = static_cast<uint8_t>((static_cast<int>(sg) * texel[1]) / 255);
			sb = static_cast<uint8_t>((static_cast<int>(sb) * texel[2]) / 255);
			sa = static_cast<uint8_t>((static_cast<int>(sa) * texel[3]) / 255);
		}
		if (sa == 0) return;
		auto* dst = g_gui_frame_rgba.data() + (static_cast<size_t>(y) * g_gui_width + x) * 4;
		const int inv = 255 - sa;
		dst[0] = static_cast<uint8_t>((sr * sa + dst[0] * inv) / 255);
		dst[1] = static_cast<uint8_t>((sg * sa + dst[1] * inv) / 255);
		dst[2] = static_cast<uint8_t>((sb * sa + dst[2] * inv) / 255);
		dst[3] = 255;
	}

	float GuiEdge(const ImVec2& a, const ImVec2& b, float x, float y) {
		return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
	}

	void GuiRasterTriangle(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, const ImVec4& clip, const unsigned char* font_pixels, int font_width, int font_height) {
		const float min_xf = std::max(clip.x, std::floor(std::min({v0.pos.x, v1.pos.x, v2.pos.x})));
		const float min_yf = std::max(clip.y, std::floor(std::min({v0.pos.y, v1.pos.y, v2.pos.y})));
		const float max_xf = std::min(clip.z, std::ceil(std::max({v0.pos.x, v1.pos.x, v2.pos.x})));
		const float max_yf = std::min(clip.w, std::ceil(std::max({v0.pos.y, v1.pos.y, v2.pos.y})));
		const int min_x = std::clamp(static_cast<int>(min_xf), 0, g_gui_width);
		const int min_y = std::clamp(static_cast<int>(min_yf), 0, g_gui_height);
		const int max_x = std::clamp(static_cast<int>(max_xf), 0, g_gui_width);
		const int max_y = std::clamp(static_cast<int>(max_yf), 0, g_gui_height);
		const float area = GuiEdge(v0.pos, v1.pos, v2.pos.x, v2.pos.y);
		if (std::abs(area) < 0.0001f) return;
		for (int y = min_y; y < max_y; ++y) {
			for (int x = min_x; x < max_x; ++x) {
				const float px = static_cast<float>(x) + 0.5f;
				const float py = static_cast<float>(y) + 0.5f;
				const float w0 = GuiEdge(v1.pos, v2.pos, px, py) / area;
				const float w1 = GuiEdge(v2.pos, v0.pos, px, py) / area;
				const float w2 = GuiEdge(v0.pos, v1.pos, px, py) / area;
				if (w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f) continue;
				const uint8_t r = static_cast<uint8_t>((v0.col & 0xff) * w0 + (v1.col & 0xff) * w1 + (v2.col & 0xff) * w2);
				const uint8_t g = static_cast<uint8_t>(((v0.col >> 8) & 0xff) * w0 + ((v1.col >> 8) & 0xff) * w1 + ((v2.col >> 8) & 0xff) * w2);
				const uint8_t b = static_cast<uint8_t>(((v0.col >> 16) & 0xff) * w0 + ((v1.col >> 16) & 0xff) * w1 + ((v2.col >> 16) & 0xff) * w2);
				const uint8_t a = static_cast<uint8_t>(((v0.col >> 24) & 0xff) * w0 + ((v1.col >> 24) & 0xff) * w1 + ((v2.col >> 24) & 0xff) * w2);
				const ImVec2 uv(v0.uv.x * w0 + v1.uv.x * w1 + v2.uv.x * w2, v0.uv.y * w0 + v1.uv.y * w1 + v2.uv.y * w2);
				GuiBlendPixel(x, y, IM_COL32(r, g, b, a), font_pixels, font_width, font_height, uv);
			}
		}
	}

	void GuiRenderDrawData(ImDrawData* draw_data) {
		GuiClearFrame();
		if (!draw_data) return;
		unsigned char* font_pixels = nullptr;
		int font_width = 0;
		int font_height = 0;
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
		const ImVec2 clip_off = draw_data->DisplayPos;
		const ImVec2 clip_scale = draw_data->FramebufferScale;
		for (int n = 0; n < draw_data->CmdListsCount; ++n) {
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			const ImDrawVert* vtx = cmd_list->VtxBuffer.Data;
			const ImDrawIdx* idx = cmd_list->IdxBuffer.Data;
			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback) {
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else {
					ImVec4 clip_rect;
					clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
					clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
					clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
					clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;
					for (unsigned int elem = 0; elem + 2 < pcmd->ElemCount; elem += 3) {
						const ImDrawVert& v0 = vtx[idx[elem + 0]];
						const ImDrawVert& v1 = vtx[idx[elem + 1]];
						const ImDrawVert& v2 = vtx[idx[elem + 2]];
						GuiRasterTriangle(v0, v1, v2, clip_rect, font_pixels, font_width, font_height);
					}
				}
				idx += pcmd->ElemCount;
			}
		}
	}

	int GuiFrame() {
		if (!g_emulator) return 1;
		if (!g_gui_attached) return 2;
		if (!GuiEnsureImGui()) return 3;
		++g_gui_frame_counter;
		GuiBeginFrame();
		RenderWebDebuggerGuiWindows();
		ImGui::Render();
		GuiRenderDrawData(ImGui::GetDrawData());
		return 0;
	}
}

const char* WebDebuggerExportDir() {
	return "/tmp/exports";
}

void WebDebuggerQueueDownload(const char* path, const char* name) {
	g_gui_file_request_kind = "download";
	g_gui_file_request_path = path ? path : "";
	g_gui_file_request_name = name ? name : "";
}

	void WebDebuggerQueueOpenFile(const char* target_path, const char* name) {
		g_gui_file_request_kind = "open_file";
		g_gui_file_request_path = target_path ? target_path : "";
		g_gui_file_request_name = name ? name : "";
	}

	void WebDebuggerRequestFsSync() {
		g_gui_file_request_kind = "sync";
		g_gui_file_request_path = "/persist";
		g_gui_file_request_name = "";
	}

	bool WebDebuggerConsumeFileResult(const char* path, int* result) {
		if (!g_gui_file_result_pending) return false;
		if (path && g_gui_file_result_path != path) return false;
		if (result) *result = g_gui_file_result_code;
		g_gui_file_result_path.clear();
		g_gui_file_result_code = 0;
		g_gui_file_result_pending = false;
		return true;
	}

	int InitRealRomCore(const uint8_t* rom, int len, const uint8_t* flash, int flash_len, int pd_value, int model_type, int legacy_ko, int classwiz_graph) {
		if (!rom || len <= 0) return -1;
		const auto hardware_id = HardwareIdFromCoreType(model_type);
		if (hardware_id == casioemu::HW_FX_5800P && (!flash || flash_len <= 0)) return -4;
		try {
			EnsureSdl();
			StopMainLoop();
			GuiResetState();
			g_emulator.reset();
			if (!WriteRomFile(rom, len)) return -2;
			if (flash && flash_len > 0 && !WriteFlashFile(flash, flash_len)) return -4;
			auto model = MakeWebModel(true, false, pd_value, model_type, legacy_ko != 0, classwiz_graph != 0);
			const auto model_dir = CurrentWebModelDir();
			std::filesystem::create_directories(model_dir);
			g_emulator = std::make_unique<casioemu::Emulator>(model, false, true, model_dir);
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

extern "C" {

int casioemu_core_set_model_id(const char* model_id) {
	SetCoreModelId(model_id);
	return 0;
}

int casioemu_core_init_real_rom(const uint8_t* rom, int len, int pd_value, int model_type, int legacy_ko, int classwiz_graph) {
	return InitRealRomCore(rom, len, nullptr, 0, pd_value, model_type, legacy_ko, classwiz_graph);
}

int casioemu_core_init_real_rom_with_flash(const uint8_t* rom, int rom_len, const uint8_t* flash, int flash_len, int pd_value, int model_type, int legacy_ko, int classwiz_graph) {
	return InitRealRomCore(rom, rom_len, flash, flash_len, pd_value, model_type, legacy_ko, classwiz_graph);
}

int casioemu_core_init_sim_rom(const uint8_t* rom, int len, int is_sample_rom, int pd_value, int model_type, int legacy_ko, int classwiz_graph) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		StopMainLoop();
		GuiResetState();
		g_emulator.reset();
		const auto hardware_id = HardwareIdFromCoreType(model_type);
		auto normalized_rom = NormalizeSimulatorRomForWeb(rom, len, hardware_id);
		if (!WriteRomFile(normalized_rom.data(), static_cast<int>(normalized_rom.size()))) return -2;
		auto model = MakeWebModel(false, is_sample_rom != 0, pd_value, model_type, legacy_ko != 0, classwiz_graph != 0);
		const auto model_dir = CurrentWebModelDir();
		std::filesystem::create_directories(model_dir);
		g_emulator = std::make_unique<casioemu::Emulator>(model, false, true, model_dir);
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
	GuiResetState();
	g_emulator.reset();
	m_emu = nullptr;
	me_mmu = nullptr;
	n_ram_buffer = nullptr;
	g_screen_provider = nullptr;
	g_frame_rgba.clear();
	g_source_frame_rgba.clear();
	g_status_alpha.clear();
	g_snapshot_buffer.clear();
	g_qr_active = false;
	g_qr_version = 0;
	g_qr_revision = 0;
	g_qr_data.clear();
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

int casioemu_core_qr_update() {
	if (!g_emulator) {
		g_qr_active = false;
		g_qr_version = 0;
		g_qr_revision = 0;
		g_qr_data.clear();
		return 0;
	}
	g_emulator->qr_code.Poll(*g_emulator);
	const auto state = g_emulator->qr_code.GetState();
	g_qr_active = state.Active;
	g_qr_version = state.Version;
	g_qr_revision = state.Revision;
	g_qr_data = state.Complete ? state.Data : std::string{};
	return state.Complete ? 1 : 0;
}

int casioemu_core_qr_active() {
	return g_qr_active ? 1 : 0;
}

int casioemu_core_qr_version() {
	return g_qr_version;
}

uint32_t casioemu_core_qr_revision() {
	return static_cast<uint32_t>(g_qr_revision);
}

const char* casioemu_core_qr_data() {
	return g_qr_data.c_str();
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
	SyncStatusAlpha();
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

int casioemu_core_persistent_ram_size() {
	if (!g_emulator) return 0;
	if (g_emulator->hardware_id == casioemu::HW_FX_5800P) {
		return Fx5800pPersistentStateSize();
	}
	return casioemu_core_user_ram_size();
}

int casioemu_core_save_persistent_ram(uint8_t* out, int max_len) {
	if (!g_emulator || !out || max_len < 0) return -1;
	if (g_emulator->hardware_id == casioemu::HW_FX_5800P) {
		return SaveFx5800pPersistentState(out, max_len);
	}
	return casioemu_core_save_user_ram(out, max_len);
}

int casioemu_core_load_persistent_ram(const uint8_t* in, int len) {
	if (!g_emulator || !in || len < 0) return 1;
	if (g_emulator->hardware_id == casioemu::HW_FX_5800P) {
		return LoadFx5800pPersistentState(in, len);
	}
	return casioemu_core_load_user_ram(in, len);
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

int casioemu_core_gui_supported() {
	return 1;
}

int casioemu_core_gui_attach(int width, int height) {
	if (!g_emulator) return 1;
	if (width <= 0 || height <= 0) return 2;
	g_gui_width = width;
	g_gui_height = height;
	g_gui_frame_rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
	if (g_gui_imgui_ready) {
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(g_gui_width), static_cast<float>(g_gui_height));
	}
	g_gui_attached = true;
	return 0;
}

void casioemu_core_gui_detach() {
	GuiDetachCanvas();
}

int casioemu_core_gui_is_attached() {
	return g_gui_attached ? 1 : 0;
}

int casioemu_core_gui_frame() {
	try {
		return GuiFrame();
	}
	catch (const std::exception& ex) {
		printf("[CasioEmuCore][GUI][Error] %s\n", ex.what());
		return 4;
	}
	catch (...) {
		printf("[CasioEmuCore][GUI][Error] Unknown GUI render exception\n");
		return 4;
	}
}

uint32_t casioemu_core_gui_frame_ptr() {
	return g_gui_frame_rgba.empty() ? 0 : reinterpret_cast<uint32_t>(g_gui_frame_rgba.data());
}

int casioemu_core_gui_frame_len() {
	return static_cast<int>(g_gui_frame_rgba.size());
}

int casioemu_core_gui_width() {
	return g_gui_width;
}

int casioemu_core_gui_height() {
	return g_gui_height;
}

int casioemu_core_gui_set_locale(const char* locale) {
	if (!locale || !locale[0]) return 1;
	g_gui_locale = std::string(locale) == "zh_CN" ? "zh_CN" : "en_US";
	if (g_gui_imgui_ready) GuiLoadLocale();
	return 0;
}

int casioemu_core_gui_pointer(double x, double y, int button, int pressed) {
	if (!g_gui_attached) return 2;
	g_gui_mouse_x = static_cast<float>(x);
	g_gui_mouse_y = static_cast<float>(y);
	if (button >= 0 && button < static_cast<int>(g_gui_mouse_down.size())) {
		if (pressed) {
			g_gui_mouse_down[button] = true;
			g_gui_mouse_release_pending[button] = false;
			g_gui_mouse_hold_frames[button] = std::max(g_gui_mouse_hold_frames[button], 1);
		}
		else if (g_gui_mouse_hold_frames[button] > 0) {
			g_gui_mouse_release_pending[button] = true;
		}
		else {
			g_gui_mouse_down[button] = false;
			g_gui_mouse_release_pending[button] = false;
		}
	}
	return 0;
}

int casioemu_core_gui_wheel(double x, double y) {
	if (!g_gui_attached) return 2;
	g_gui_wheel_x += static_cast<float>(x);
	g_gui_wheel_y += static_cast<float>(y);
	return 0;
}

int casioemu_core_gui_key(int key, int pressed, int ctrl, int shift, int alt, int super_key) {
	if (!g_gui_imgui_ready) return 2;
	ImGuiIO& io = ImGui::GetIO();
	io.AddKeyEvent(ImGuiMod_Ctrl, ctrl != 0);
	io.AddKeyEvent(ImGuiMod_Shift, shift != 0);
	io.AddKeyEvent(ImGuiMod_Alt, alt != 0);
	io.AddKeyEvent(ImGuiMod_Super, super_key != 0);
	ImGuiKey imgui_key = ImGuiKey_None;
	if (key >= '0' && key <= '9') imgui_key = static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));
	else if (key >= 'a' && key <= 'z') imgui_key = static_cast<ImGuiKey>(ImGuiKey_A + (key - 'a'));
	else if (key >= 'A' && key <= 'Z') imgui_key = static_cast<ImGuiKey>(ImGuiKey_A + (key - 'A'));
	else {
		switch (key) {
		case 8: imgui_key = ImGuiKey_Backspace; break;
		case 9: imgui_key = ImGuiKey_Tab; break;
		case 13: imgui_key = ImGuiKey_Enter; break;
		case 27: imgui_key = ImGuiKey_Escape; break;
		case 32: imgui_key = ImGuiKey_Space; break;
		case 39: imgui_key = ImGuiKey_Apostrophe; break;
		case 44: imgui_key = ImGuiKey_Comma; break;
		case 45: imgui_key = ImGuiKey_Minus; break;
		case 46: imgui_key = ImGuiKey_Period; break;
		case 47: imgui_key = ImGuiKey_Slash; break;
		case 59: imgui_key = ImGuiKey_Semicolon; break;
		case 61: imgui_key = ImGuiKey_Equal; break;
		case 91: imgui_key = ImGuiKey_LeftBracket; break;
		case 92: imgui_key = ImGuiKey_Backslash; break;
		case 93: imgui_key = ImGuiKey_RightBracket; break;
		case 96: imgui_key = ImGuiKey_GraveAccent; break;
		case 127: imgui_key = ImGuiKey_Delete; break;
		case 1073741904: imgui_key = ImGuiKey_LeftArrow; break;
		case 1073741903: imgui_key = ImGuiKey_RightArrow; break;
		case 1073741906: imgui_key = ImGuiKey_UpArrow; break;
		case 1073741905: imgui_key = ImGuiKey_DownArrow; break;
		case 1073741898: imgui_key = ImGuiKey_Home; break;
		case 1073741901: imgui_key = ImGuiKey_End; break;
		case 1073741899: imgui_key = ImGuiKey_PageUp; break;
		case 1073741902: imgui_key = ImGuiKey_PageDown; break;
		default: break;
		}
	}
	if (imgui_key != ImGuiKey_None) io.AddKeyEvent(imgui_key, pressed != 0);
	return 0;
}

int casioemu_core_gui_text(unsigned int codepoint) {
	if (!g_gui_imgui_ready) return 2;
	if (codepoint > 0) ImGui::GetIO().AddInputCharacter(codepoint);
	return 0;
}

int casioemu_core_gui_want_text_input() {
	if (!g_gui_imgui_ready) return 0;
	return ImGui::GetIO().WantTextInput ? 1 : 0;
}

int casioemu_core_gui_ime_visible() {
	if (!g_gui_imgui_ready) return 0;
	return g_gui_ime_visible ? 1 : 0;
}

double casioemu_core_gui_ime_x() {
	return g_gui_ime_x;
}

double casioemu_core_gui_ime_y() {
	return g_gui_ime_y;
}

double casioemu_core_gui_ime_line_height() {
	return g_gui_ime_line_height;
}

int casioemu_core_gui_set_clipboard_text(const char* text) {
	if (!text) return 1;
	g_gui_clipboard_text = text;
	return 0;
}

int casioemu_core_gui_clipboard_write_pending() {
	if (!g_gui_imgui_ready) return 0;
	return g_gui_clipboard_write_revision != g_gui_clipboard_ack_revision ? 1 : 0;
}

const char* casioemu_core_gui_clipboard_write_text() {
	return g_gui_clipboard_write_text.c_str();
}

uint32_t casioemu_core_gui_clipboard_write_revision() {
	return g_gui_clipboard_write_revision;
}

void casioemu_core_gui_clipboard_write_ack(uint32_t revision) {
	if (revision == g_gui_clipboard_write_revision) {
		g_gui_clipboard_ack_revision = revision;
	}
}

int casioemu_core_gui_file_request_pending() {
	return g_gui_file_request_kind.empty() ? 0 : 1;
}

const char* casioemu_core_gui_file_request_kind() {
	return g_gui_file_request_kind.c_str();
}

const char* casioemu_core_gui_file_request_path() {
	return g_gui_file_request_path.c_str();
}

const char* casioemu_core_gui_file_request_name() {
	return g_gui_file_request_name.c_str();
}

void casioemu_core_gui_file_request_ack() {
	g_gui_file_request_kind.clear();
	g_gui_file_request_path.clear();
	g_gui_file_request_name.clear();
}

void casioemu_core_gui_file_request_complete(const char* path, int result) {
	g_gui_file_result_path = path ? path : "";
	g_gui_file_result_code = result;
	g_gui_file_result_pending = true;
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

	if (hw == casioemu::HW_FX_5800P) {
		std::ifstream rom(kRomPath, std::ios::binary);
		std::ifstream flash(kFlashPath, std::ios::binary);
		if (!rom || !flash) return "";

		std::vector<byte> rom_data{std::istreambuf_iterator<char>{rom.rdbuf()}, std::istreambuf_iterator<char>{}};
		std::vector<byte> flash_data{std::istreambuf_iterator<char>{flash.rdbuf()}, std::istreambuf_iterator<char>{}};
		auto ri = rom_info(rom_data, flash_data, false);
		if (!ri.ok) return "";
		snprintf(buffer, sizeof(buffer), "%.8s (%04X)", ri.ver, ri.desired_sum);
		return buffer;
	}

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
	snprintf(buffer, sizeof(buffer), "%.6s %.2s (%04X)", name, ver, sum[1] * 0x100 + sum[0]);
	return buffer;
}

int main() {
	return 0;
}
