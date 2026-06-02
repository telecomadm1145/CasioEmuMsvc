#include "VariableWindow.h"
#include "CwiiHelp.h"
#include "Models.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <string>
std::string to_hex(unsigned int num) {
	std::string hex_digits = "0123456789ABCDEF";
	std::string result;

	while (num > 0) {
		result += hex_digits[num % 16];
		num /= 16;
	}

	// Reverse the result to get the correct order
	std::reverse(result.begin(), result.end());

	// If the result is empty, return "0"
	if (result.empty()) {
		result = "0";
	}

	return result;
}
void VariableWindow::RenderCore() {
	char* base_addr = n_ram_buffer - casioemu::GetRamBaseAddr(m_emu->hardware_id);
	auto vars = casioemu::GetVariableOffsets(m_emu->hardware_id);
	static bool showaddr = false;
	static bool showhex = false;
	static bool showimg_auto = true;
	static bool showimg_f = false;

	bool is_in_im = (*(base_addr + casioemu::GetModeOffset(m_emu->hardware_id)) & 0xFF) == 0xC4;
	bool s_im = showimg_f ? 1 : (showimg_auto ? is_in_im : 0);

	int cols = 2;
	if (s_im) cols++;
	if (showhex) cols += (s_im ? 2 : 1);
	if (showaddr) cols += (s_im ? 2 : 1);

	if (ImGui::BeginTable("##vars_table", cols, pretty_table)) {
		ImGui::TableSetupColumn("VarWindow.Variable"_lc, ImGuiTableColumnFlags_WidthStretch, 1.0f);
		ImGui::TableSetupColumn("VarWindow.ReP"_lc, ImGuiTableColumnFlags_WidthStretch, 2.0f);
		if (s_im) {
			ImGui::TableSetupColumn("VarWindow.ImP"_lc, ImGuiTableColumnFlags_WidthStretch, 2.0f);
		}
		if (showhex) {
			ImGui::TableSetupColumn("Real Hex", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			if (s_im) {
				ImGui::TableSetupColumn("Imag Hex", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			}
		}
		if (showaddr) {
			ImGui::TableSetupColumn("Real Addr", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			if (s_im) {
				ImGui::TableSetupColumn("Imag Addr", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			}
		}
		ImGui::TableHeadersRow();

		for (const auto& v : vars) {
			if (is_in_im && !strcmp(v.Name, "PreAns"))
				continue;
			ImGui::TableNextRow();
			
			// Column 0: Variable Name
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(v.Name);

			// Column 1: Real BCD
			ImGui::TableNextColumn();
			std::string s = casioemu::BCD2Str(base_addr + v.RealPartOffset);
			ImGui::TextUnformatted(s.c_str());

			// Column 2: Imag BCD (if s_im)
			if (s_im) {
				ImGui::TableNextColumn();
				s = casioemu::BCD2Str(base_addr + v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id));
				ImGui::TextUnformatted(s.c_str());
			}

			// Hex
			if (showhex) {
				ImGui::TableNextColumn();
				s = casioemu::ConvHex(base_addr + v.RealPartOffset, casioemu::GetVariableSize(m_emu->hardware_id));
				ImGui::TextUnformatted(s.c_str());
				if (s_im) {
					ImGui::TableNextColumn();
					s = casioemu::ConvHex(base_addr + v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id), casioemu::GetVariableSize(m_emu->hardware_id));
					ImGui::TextUnformatted(s.c_str());
				}
			}

			// Addr
			if (showaddr) {
				ImGui::TableNextColumn();
				UIHelpers::ClickableAddress(v.RealPartOffset);
				if (s_im) {
					ImGui::TableNextColumn();
					UIHelpers::ClickableAddress(v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id));
				}
			}
		}

		if (m_emu->hardware_id == casioemu::HW_CLASSWIZ_II) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Theta");

			ImGui::TableNextColumn();
			auto a = casioemu::BCD2Str(n_ram_buffer + 0xBDEC - 0x9000);
			ImGui::TextUnformatted(a.c_str());

			if (s_im) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("");
			}

			if (showhex) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("");
				if (s_im) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("");
				}
			}

			if (showaddr) {
				ImGui::TableNextColumn();
				UIHelpers::ClickableAddress(0xBDEC);
				if (s_im) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("");
				}
			}
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Checkbox("VarWindow.ShowAddrOpt"_lc, &showaddr);
	ImGui::SameLine();
	ImGui::Checkbox("VarWindow.ShowHexOpt"_lc, &showhex);
	ImGui::SameLine();
	ImGui::Checkbox("VarWindow.ShowImPWhenComplex"_lc, &showimg_auto);
	ImGui::SameLine();
	ImGui::Checkbox("VarWindow.AlwaysShowImP"_lc, &showimg_f);
}
