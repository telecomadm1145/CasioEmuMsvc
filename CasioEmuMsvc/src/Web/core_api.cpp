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
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

#ifdef CASIOEMU_CORE_WEB_GUI
#include "Gui/Localization.h"
#include "Gui/hex.hpp"
#include "Gui/imgui/imgui.h"
#include "Gui/imgui/imgui_internal.h"
#endif

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

#ifdef CASIOEMU_CORE_WEB_GUI
ImVec4 ThemeManager::Harmonize(const ImVec4& source, float) const {
	return source;
}
#endif

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
	bool g_gui_attached = false;
	int g_gui_width = 0;
	int g_gui_height = 0;
	uint32_t g_gui_frame_counter = 0;
	std::vector<uint8_t> g_gui_frame_rgba;
#ifdef CASIOEMU_CORE_WEB_GUI
	bool g_gui_imgui_ready = false;
	bool g_gui_show_demo = false;
	float g_gui_mouse_x = -FLT_MAX;
	float g_gui_mouse_y = -FLT_MAX;
	std::array<bool, 5> g_gui_mouse_down{};
	float g_gui_wheel_x = 0.0f;
	float g_gui_wheel_y = 0.0f;
	std::string g_gui_locale = "en_US";
	MemoryEditor g_gui_rom_editor;
	MemoryEditor g_gui_ram_editor;
	MemoryEditor g_gui_all_editor;
	std::vector<MemoryEditor::MarkedSpan> g_gui_ram_spans;
#endif

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

#ifdef CASIOEMU_CORE_WEB_GUI
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
		g_gui_wheel_x = 0.0f;
		g_gui_wheel_y = 0.0f;
		g_gui_ram_spans.clear();
	}
#else
	void GuiResetState() {
		g_gui_attached = false;
		g_gui_width = 0;
		g_gui_height = 0;
		g_gui_frame_counter = 0;
		g_gui_frame_rgba.clear();
	}
#endif

	casioemu::ModelInfo MakeWebModel(bool real_hardware, bool is_sample_rom, int pd_value, int model_type, bool legacy_ko, bool classwiz_graph) {
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
		model.legacy_ko = legacy_ko;
		model.u16_mode = hardware_id != casioemu::HW_ES_PLUS;
		model.LARGE_model = true;
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

#ifdef CASIOEMU_CORE_WEB_GUI
	ImU8 GuiReadMmu(const ImU8* data, size_t off) {
		return me_mmu ? me_mmu->ReadData(static_cast<uint32_t>(reinterpret_cast<size_t>(data) + off), 0) : 0;
	}

	void GuiWriteMmu(ImU8* data, size_t off, ImU8 value) {
		if (me_mmu) me_mmu->WriteData(static_cast<uint32_t>(reinterpret_cast<size_t>(data) + off), value, 0);
	}

	void GuiSetupEditor(MemoryEditor& editor, bool read_only, bool mmu) {
		editor.ReadOnly = read_only;
		editor.OptShowDataPreview = true;
		editor.OptGreyOutZeroes = true;
		editor.ReadFn = mmu ? GuiReadMmu : nullptr;
		editor.WriteFn = mmu && !read_only ? GuiWriteMmu : nullptr;
	}

	void GuiLoadLocale() {
		try {
			chdir("/");
			const std::string locale = g_gui_locale == "zh_CN" ? "zh_CN" : "en_US";
			g_local.ChangeLanguage(locale, false);
			if (g_local.Get("CoreGui.Title") == "CoreGui.Title") {
				throw LocalizationException("CoreGui.Title is still missing after locale load");
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
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.ScrollbarRounding = 3.0f;
		style.WindowBorderSize = 1.0f;
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.15f, 0.18f, 1.0f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.22f, 0.30f, 1.0f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.32f, 0.48f, 1.0f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.36f, 0.56f, 1.0f);
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
		ImFontConfig config;
		config.PixelSnapH = true;
		io.Fonts->AddFontDefault(&config);

		constexpr const char* kBundledCjkFont = "/fonts/CasioEmuGuiCJKSubset.otf";
		if (std::filesystem::exists(kBundledCjkFont)) {
			config.MergeMode = true;
			if (io.Fonts->AddFontFromFileTTF(kBundledCjkFont, 16.0f, &config, GuiCjkRanges())) {
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
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		GuiLoadFonts(io);
		unsigned char* pixels = nullptr;
		int font_width = 0;
		int font_height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(1));
		GuiApplyStyle();
		GuiSetupEditor(g_gui_rom_editor, true, false);
		GuiSetupEditor(g_gui_ram_editor, false, true);
		GuiSetupEditor(g_gui_all_editor, false, true);
		g_gui_ram_spans = casioemu::GetCommonMemLabels(g_emulator->hardware_id);
		GuiLoadLocale();
		g_gui_imgui_ready = true;
		return true;
	}

	void GuiShutdown() {
		if (g_gui_imgui_ready) {
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
		ImGui::NewFrame();
	}

	void GuiRenderMainWindow() {
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		std::string title = std::string("CoreGui.Title"_lc) + "###CasioEmuMsvcTools";
		ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
		ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (ImGui::Begin(title.c_str(), nullptr, flags)) {
			ImGui::Text("CoreGui.ModelFmt"_lc, g_emulator->ModelDefinition.model_name.c_str());
			ImGui::SameLine();
			ImGui::Text("CoreGui.PCFmt"_lc, static_cast<unsigned>((g_emulator->chipset.cpu.reg_csr << 16) | g_emulator->chipset.cpu.reg_pc));
			ImGui::SameLine();
			ImGui::Text("%s", g_emulator->GetPaused() ? "StatusBar.Paused"_lc : "StatusBar.Running"_lc);
			ImGui::Separator();
			if (ImGui::BeginTabBar("##core_tools_tabs")) {
				if (ImGui::BeginTabItem("CoreGui.TabRAM"_lc)) {
					const auto base = casioemu::GetRamBaseAddr(g_emulator->hardware_id);
					const auto size = casioemu::GetRamSize(g_emulator->hardware_id);
					g_gui_ram_editor.DrawContents(reinterpret_cast<void*>(static_cast<size_t>(base)), size, base, g_gui_ram_spans);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("CoreGui.TabROM"_lc)) {
					g_gui_rom_editor.DrawContents(g_emulator->chipset.rom_data.data(), g_emulator->chipset.rom_data.size(), 0);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("CoreGui.TabAll"_lc)) {
					g_gui_all_editor.DrawContents(nullptr, 0xfffff, 0, g_gui_ram_spans);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("CoreGui.TabState"_lc)) {
					ImGui::Text("CoreGui.CpuTimeFmt"_lc, g_cpu_time);
					ImGui::Text("CoreGui.RamBaseFmt"_lc, casioemu::GetRamBaseAddr(g_emulator->hardware_id));
					ImGui::Text("CoreGui.RamSizeFmt"_lc, casioemu::GetRamSize(g_emulator->hardware_id));
					ImGui::Text("CoreGui.RomSizeFmt"_lc, g_emulator->chipset.rom_data.size());
					ImGui::Checkbox("CoreGui.ShowDemo"_lc, &g_gui_show_demo);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
		if (g_gui_show_demo) ImGui::ShowDemoWindow(&g_gui_show_demo);
	}

	void GuiClearFrame() {
		g_gui_frame_rgba.assign(static_cast<size_t>(g_gui_width) * static_cast<size_t>(g_gui_height) * 4, 0);
		for (size_t i = 0; i < g_gui_frame_rgba.size(); i += 4) {
			g_gui_frame_rgba[i + 0] = 18;
			g_gui_frame_rgba[i + 1] = 20;
			g_gui_frame_rgba[i + 2] = 24;
			g_gui_frame_rgba[i + 3] = 255;
		}
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
		GuiRenderMainWindow();
		ImGui::Render();
		GuiRenderDrawData(ImGui::GetDrawData());
		return 0;
	}
#endif
}

extern "C" {

int casioemu_core_init_real_rom(const uint8_t* rom, int len, int pd_value, int model_type, int legacy_ko, int classwiz_graph) {
	if (!rom || len <= 0) return -1;
	try {
		EnsureSdl();
		StopMainLoop();
		GuiResetState();
		g_emulator.reset();
		if (!WriteRomFile(rom, len)) return -2;
		auto model = MakeWebModel(true, false, pd_value, model_type, legacy_ko != 0, classwiz_graph != 0);
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

#ifdef CASIOEMU_CORE_WEB_GUI
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
	GuiResetState();
}

int casioemu_core_gui_is_attached() {
	return g_gui_attached ? 1 : 0;
}

int casioemu_core_gui_frame() {
	return GuiFrame();
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
		g_gui_mouse_down[button] = pressed != 0;
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
#else
int casioemu_core_gui_supported() {
	return 0;
}

int casioemu_core_gui_attach(int, int) {
	return 99;
}

void casioemu_core_gui_detach() {}

int casioemu_core_gui_is_attached() {
	return 0;
}

int casioemu_core_gui_frame() {
	return 99;
}

uint32_t casioemu_core_gui_frame_ptr() {
	return 0;
}

int casioemu_core_gui_frame_len() {
	return 0;
}

int casioemu_core_gui_width() {
	return 0;
}

int casioemu_core_gui_height() {
	return 0;
}

int casioemu_core_gui_set_locale(const char*) {
	return 99;
}

int casioemu_core_gui_pointer(double, double, int, int) {
	return 99;
}

int casioemu_core_gui_wheel(double, double) {
	return 99;
}

int casioemu_core_gui_key(int, int, int, int, int, int) {
	return 99;
}

int casioemu_core_gui_text(unsigned int) {
	return 99;
}
#endif

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
