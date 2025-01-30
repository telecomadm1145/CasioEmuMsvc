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
	ImGui::TextUnformatted("VarWindow.Variable"_lc);
	ImGui::SameLine(90);
	ImGui::TextUnformatted("VarWindow.ReP"_lc);
	bool is_in_im = (*(base_addr + casioemu::GetModeOffset(m_emu->hardware_id)) & 0xFF) == 0xC4;
	bool s_im = showimg_f ? 1 : (showimg_auto ? is_in_im : 0);
	if (s_im) {
		ImGui::SameLine(320);
		ImGui::TextUnformatted("VarWindow.ImP"_lc);
	}
	for (const auto& v : vars) {
		if (is_in_im && !strcmp(v.Name, "PreAns"))
			continue;
		std::string s;
		ImGui::TextUnformatted(v.Name);
		ImGui::SameLine(90);
		s = cwii::StringizeCwiiNumber(base_addr + v.RealPartOffset);
		ImGui::TextUnformatted(s.c_str());
		if (s_im) {
			ImGui::SameLine(320);
			s = cwii::StringizeCwiiNumber(base_addr + v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id));
			ImGui::TextUnformatted(s.c_str());
		}
		if (showhex) {
			ImGui::TextUnformatted("VarWindow.Hex"_lc);
			ImGui::SameLine(90);
			s = cwii::HexizeString(base_addr + v.RealPartOffset, casioemu::GetVariableSize(m_emu->hardware_id));
			ImGui::TextUnformatted(s.c_str());
			if (s_im) {
				ImGui::SameLine(320);
				s = cwii::HexizeString(base_addr + v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id), casioemu::GetVariableSize(m_emu->hardware_id));
				ImGui::TextUnformatted(s.c_str());
			}
		}
		if (showaddr) {
			ImGui::TextUnformatted("VarWindow.Addr"_lc);
			ImGui::SameLine(90);
			s = to_hex(v.RealPartOffset);
			ImGui::TextUnformatted(s.c_str());
			if (s_im) {
				ImGui::SameLine(320);
				s = to_hex(v.RealPartOffset + casioemu::GetReImOffset(m_emu->hardware_id));
				ImGui::TextUnformatted(s.c_str());
			}
		}
	}
	if (m_emu->hardware_id == casioemu::HW_CLASSWIZ_II) {
		ImGui::TextUnformatted("Theta");
		ImGui::SameLine(90);
		auto a = cwii::StringizeCwiiNumber(n_ram_buffer + 0xBDEC - 0x9000);
		ImGui::TextUnformatted(a.c_str());
		if (showaddr) {
			ImGui::TextUnformatted("VarWindow.Addr"_lc);
			ImGui::SameLine(90);
			ImGui::TextUnformatted("0xBDEC");
		}
	}
	ImGui::Checkbox("VarWindow.ShowAddrOpt"_lc,
		&showaddr);
	ImGui::Checkbox("VarWindow.ShowHexOpt"_lc,
		&showhex);
	ImGui::Checkbox("VarWindow.ShowImPWhenComplex"_lc,
		&showimg_auto);
	ImGui::Checkbox("VarWindow.AlwaysShowImP"_lc,
		&showimg_f);
}
