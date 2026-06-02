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
	UIHelpers::SectionHeader("Display");
	
	if (ImGui::Button("ScreenshotBtn"_lc)) {
		m_emu->screenshot_requested.store(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("PopUpScreenBtn"_lc)) {
		m_emu->mirroring_requested.store(true);
	}
	
	ImGui::SliderInt("HwController.Value1"_lc, &screen_flashing_threshold, 0, 0x3F);
	ImGui::SliderFloat("HwController.Value2"_lc, &screen_flashing_brightness_coeff, 1.0f, 8.0f);
	ImGui::SliderInt("HwController.ScreenBufferSelect"_lc, &screen_buffer_select, 0, 2);

	UIHelpers::SectionHeader("CPU & Performance");
	
	static int cps = log(m_emu->cycles.cycles_per_second) / log(2);
	if (ImGui::SliderInt("HwController.CPS"_lc, &cps, 1, 28, "2^%d CPS")) {
		m_emu->cycles.Setup((Uint64)1 << cps, m_emu->cycles.timer_interval);
	}
	ImGui::Text("%.6f MHz", (double)m_emu->cycles.cycles_per_second / 1024 / 1024);
	
	ImGui::Spacing();
	
	static int pd = m_emu->ModelDefinition.pd_value;
	static bool pdx[8];

	// Initialize pdx array based on the initial value of pd
	for (int i = 0; i < 8; i++) {
		pdx[i] = (pd & (1 << i)) != 0;
	}

	bool changed = false;
	ImGui::Text("PD Register:");
	if (ImGui::BeginTable("##pd_table", 9, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_BordersOuter)) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled(" Bit");
		for (int i = 0; i < 8; ++i) {
			ImGui::TableNextColumn();
			ImGui::Text("  %d", i);
		}
		
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextDisabled(" Val");
		for (int i = 0; i < 8; ++i) {
			ImGui::TableNextColumn();
			ImGui::PushID(i);
			if (ImGui::Checkbox("##pd_bit", &pdx[i])) {
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
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

	ImGui::Spacing();
	
	static int irq = 5;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
	ImGui::InputInt("##irqid", &irq);
	ImGui::SameLine();
	if (ImGui::Button("HwController.Interrupt"_lc)) {
		if (irq >= 5 && irq < 64) {
			m_emu->chipset.RaiseMaskable(irq);
		}
	}

	UIHelpers::SectionHeader("Advanced");
	
	if (ImGui::Button("HwController.HotReload"_lc)) {
		m_emu->SetPaused(true);
		auto lg = std::lock_guard(m_emu->access_mx);
		std::ifstream rom_handle(m_emu->GetModelFilePath(m_emu->ModelDefinition.rom_path), std::ifstream::binary);
		if (rom_handle.fail())
			PANIC("std::ifstream failed: %s\n", std::strerror(errno));
		auto dat = std::vector<unsigned char>((std::istreambuf_iterator<char>(rom_handle)), std::istreambuf_iterator<char>());
		for (size_t i = 0; i < std::min(dat.size(), m_emu->chipset.rom_data.size()); i++) {
			m_emu->chipset.rom_data[i] = dat[i];
		}
		m_emu->chipset.Reset();
		m_emu->SetPaused(false);
	}
}
