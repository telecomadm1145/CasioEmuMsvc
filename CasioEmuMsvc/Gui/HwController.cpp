#include "HwController.h"
#include "../Config.hpp"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include "Chipset.hpp"
#include "Localization.h"
int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0;
bool enable_screen_fading = false;
float screen_flashing_brightness_coeff = 1.5f;
int screen_buffer_select = 0;
bool audio_enable = false;
void HwController::RenderCore() {

	if (ImGui::Button("ScreenshotBtn"_lc)) {
		m_emu->screenshot_requested.store(true);
	}
	if (ImGui::Button("PopUpScreenBtn"_lc)) {
		m_emu->mirroring_requested.store(true);
	}
	ImGui::SliderInt("HwController.Value1"_lc,&screen_flashing_threshold, 0, 0x3F);
	ImGui::SliderFloat("HwController.Value2"_lc,
		&screen_flashing_brightness_coeff, 1, 8);
	//	ImGui::Checkbox(
	// #if LANGUAGE == 2
	//		"启用屏幕渐变"
	// #else
	//		"enable_screen_fading"
	// #endif
	//		,
	//		&enable_screen_fading);
	//	if (enable_screen_fading)
	//		ImGui::SliderFloat(
	// #if LANGUAGE == 2
	//			"屏幕渐变混合因子"
	// #else
	//			"Blending rate"
	// #endif
	//			,
	//			&screen_fading_blending_coefficient, 0, 1);
	ImGui::Separator();
	ImGui::SliderInt("HwController.ScreenBufferSelect"_lc,
		&screen_buffer_select, 0, 2);
	ImGui::Separator();
	static int cps = log(m_emu->cycles.cycles_per_second) / log(2);
	if (ImGui::SliderInt("HwController.CPS"_lc,
			&cps, 1, 28, "2^%d CPS")) {
		m_emu->cycles.Setup((Uint64)1 << cps, m_emu->cycles.timer_interval);
	}
	ImGui::Text("%.6f MHz", (double)m_emu->cycles.cycles_per_second / 1024 / 1024);
	static int pd = m_emu->ModelDefinition.pd_value;
	static bool pdx[8];

	// 初始化 pdx 数组，基于 pd 的初始值
	for (int i = 0; i < 8; i++) {
		pdx[i] = (pd & (1 << i)) != 0;
	}

	ImGui::Text("Pd");
	for (size_t i = 0; i < 8; i++) {
		ImGui::SameLine(i * 25. + 50.);
		ImGui::Text("%zu", i);
	}
	ImGui::Dummy(ImVec2(0, 0));

	bool changed = false;
	for (size_t i = 0; i < 8; i++) {
		ImGui::SameLine(i * 25. + 50.);
		if (ImGui::Checkbox(("##" + std::to_string(i)).c_str(), &pdx[i])) {
			changed = true;
		}
	}

	if (changed) {
		pd = 0;
		for (int i = 0; i < 8; i++) {
			if (pdx[i]) {
				pd |= (1 << i);
			}
		}
		m_emu->ModelDefinition.pd_value = pd;
	}

	static int irq = 0;
	ImGui::InputInt("##0d000721",&irq);
	if (ImGui::Button("HwController.Interrupt"_lc)) {
		m_emu->chipset.RaiseMaskable(irq);
	}
	if (ImGui::Button("HwController.HotReload"_lc)) {
		m_emu->SetPaused(true);
		auto lg = std::lock_guard(m_emu->access_mx);
		std::ifstream rom_handle(m_emu->GetModelFilePath(m_emu->ModelDefinition.rom_path), std::ifstream::binary);
		if (rom_handle.fail())
			PANIC("std::ifstream failed: %s\n", std::strerror(errno));
		auto dat = std::vector<unsigned char>((std::istreambuf_iterator<char>(rom_handle)), std::istreambuf_iterator<char>());
		for (size_t i = 0; i <std::min(dat.size(),m_emu->chipset.rom_data.size()); i++) {
			m_emu->chipset.rom_data[i] = dat[i];
		}
		m_emu->chipset.Reset();
		m_emu->SetPaused(false);
	}
	//	static char buf4[40];
	//	ImGui::InputText("##cps_in", buf4, 40);
	//	ImGui::SameLine();
	//	if (ImGui::Button(
	// #if LANGUAGE == 2
	//			"设置"
	// #else
	//			"Set"
	// #endif
	//			)) {
	//		m_emu->cycles.Setup((std::stoi(buf4)), m_emu->cycles.timer_interval);
	//	}

	//	ImGui::Checkbox(
	// #if LANGUAGE == 2
	//		"音响，启动！"
	// #else
	//		"Enable Audio Controller"
	// #endif
	//		, &audio_enable);
}
