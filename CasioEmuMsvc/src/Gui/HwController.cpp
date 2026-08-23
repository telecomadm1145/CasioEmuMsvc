#include "HwController.h"
#include "../Config.hpp"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include "Chipset.hpp"
#include "CodeViewer.hpp"
#include "Localization.h"
#include "ModelInfo.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
int screen_flashing_threshold = 20;
float screen_fading_blending_coefficient = 0;
bool enable_screen_fading = false;
float screen_flashing_brightness_coeff = 1.5f;
bool screen_residual_enabled = true;
float screen_residual_alpha_scale = 1.0f;
int screen_buffer_select = 0;
bool audio_enable = false;
void HwController::RenderCore() {
	UIHelpers::SectionHeader("Display");

#ifndef CASIOEMU_CORE_WEB
	if (ImGui::Button("ScreenshotBtn"_lc)) {
		m_emu->screenshot_requested.store(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("PopUpScreenBtn"_lc)) {
		m_emu->mirroring_requested.store(true);
	}
	ImGui::SameLine();
	const bool recording = m_emu->recording_active.load();
	if (UIHelpers::ButtonWithShortcut(recording ? "RecordStopBtn"_lc : "RecordStartBtn"_lc, "Ctrl+F12")) {
		if (recording) {
			m_emu->recording_stop_requested.store(true);
		}
		else {
			m_emu->recording_requested.store(true);
		}
	}
	if (recording) {
		ImGui::SameLine();
		ImGui::Text("RecordStatus"_lc, m_emu->recording_frame_count.load());
	}
	int capture_scale = std::max(1, m_emu->capture_scale.load());
	bool capture_scale_changed = false;
	ImGui::PushID("HwController.CaptureScale");
	if (ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
		capture_scale--;
		capture_scale_changed = true;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
	if (ImGui::InputInt("##Value", &capture_scale, 0, 0)) {
		capture_scale_changed = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
		capture_scale++;
		capture_scale_changed = true;
	}
	ImGui::PopID();
	ImGui::SameLine();
	ImGui::TextUnformatted("HwController.CaptureScale"_lc);
	if (capture_scale_changed) {
		m_emu->capture_scale.store(std::max(1, capture_scale));
	}
	else if (capture_scale != m_emu->capture_scale.load()) {
		m_emu->capture_scale.store(capture_scale);
	}
	uint32_t capture_background = m_emu->capture_background_rgb.load();
	float capture_background_color[3] = {
		static_cast<float>((capture_background >> 16) & 0xff) / 255.0f,
		static_cast<float>((capture_background >> 8) & 0xff) / 255.0f,
		static_cast<float>(capture_background & 0xff) / 255.0f};
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
	if (ImGui::ColorEdit3("HwController.CaptureBackground"_lc, capture_background_color)) {
		const auto channel = [](float value) {
			return static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255));
		};
		m_emu->capture_background_rgb.store((channel(capture_background_color[0]) << 16) |
			(channel(capture_background_color[1]) << 8) |
			channel(capture_background_color[2]));
	}
#endif

	if (!casioemu::IsEpsFamily(m_emu->hardware_id)) {
		ImGui::SliderInt("HwController.Value1"_lc, &screen_flashing_threshold, 0, 0x3F);
		ImGui::SliderFloat("HwController.Value2"_lc, &screen_flashing_brightness_coeff, 1.0f, 8.0f);
		ImGui::SliderInt("HwController.ScreenBufferSelect"_lc, &screen_buffer_select, 0, 2);
	}

	UIHelpers::SectionHeader("CPU & Performance");
	
	static int cps = log(m_emu->cycles.cycles_per_second) / log(2);
	if (ImGui::SliderInt("HwController.CPS"_lc, &cps, 1, 28, "2^%d CPS")) {
		m_emu->cycles.Setup((Uint64)1 << cps, m_emu->cycles.timer_interval);
	}
	ImGui::Text("%.6f MHz", (double)m_emu->cycles.cycles_per_second / 1024 / 1024);
	
	ImGui::Spacing();
	
	if (!casioemu::IsEpsFamily(m_emu->hardware_id)) {
		static bool pdx[8];
		int pd = m_emu->ModelDefinition.pd_value;

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
	}

	UIHelpers::SectionHeader("Advanced");
	
	if (ImGui::Button("HwController.HotReload"_lc)) {
		const bool was_paused = m_emu->GetPaused();
		m_emu->SetPaused(true);
		auto lg = std::lock_guard(m_emu->access_mx);
		std::string error;
		if (m_emu->chipset.ReloadRom(error)) {
			if (m_emu->chipset.epscpu && code_viewer)
				code_viewer->PrepareDisasm();
		}
		else {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
				"HwController.HotReload"_lc, error.c_str(), window);
		}
		m_emu->SetPaused(was_paused);
	}
}
