#include "VariableWindow.h"
#include "CwiiHelp.h"
#include "imgui/imgui.h"
#include <string>
#include "ui.hpp"
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
void VariableWindow::Draw()
{
	ImGui::Begin("变量窗口");
	std::string vars[] = { "Ans","A","B","C","D","E","F","x","y","z","PreAns" };
	char* base_addr = n_ram_buffer - 0x9000 + 0x965E;
	int ptr = 0x965E;
	static bool showaddr = false;
	static bool showhex = false;
	static bool showimg_auto = true;
	static bool showimg_f = false;
	ImGui::Text("变量");
	ImGui::SameLine(90);
	ImGui::Text("实部");
	bool is_in_im = (*(n_ram_buffer - 0x9000 + 0x91A1) & 0xFF) == 0xC4;
	bool s_im = showimg_f ? 1 : (showimg_auto ? is_in_im : 0);
	if (s_im) {
		ImGui::SameLine(320);
		ImGui::Text("虚部");
	}
	for (const auto& v : vars) {
		if (is_in_im && v == "PreAns")
			continue;
		std::string s;
		ImGui::Text(v.c_str());
		ImGui::SameLine(90);
		s = cwii::StringizeCwiiNumber(base_addr);
		ImGui::Text(s.c_str());
		if (s_im) {
			ImGui::SameLine(320);
			s = cwii::StringizeCwiiNumber(base_addr + 0xE * 11);
			ImGui::Text(s.c_str());
		}
		if (showhex) {
			ImGui::Text("十六进制");
			ImGui::SameLine(90);
			s = cwii::HexizeString(base_addr,0xE);
			ImGui::Text(s.c_str());
			if (s_im) {
				ImGui::SameLine(320);
				s = cwii::HexizeString(base_addr + 0xE * 11, 0xE);
				ImGui::Text(s.c_str());
			}
		}
		if (showaddr)
		{
			ImGui::Text("地址");
			ImGui::SameLine(90);
			s = "0x" + to_hex(ptr);
			ImGui::Text(s.c_str());
			if (s_im) {
				ImGui::SameLine(320);
				s = "0x" + to_hex(ptr + 0xE * 11);
				ImGui::Text(s.c_str());
			}
		}
		base_addr += 0xE;
		ptr += 0xE;
	}
	ImGui::Checkbox("显示地址", &showaddr);
	ImGui::Checkbox("显示十六进制", &showhex);
	ImGui::Checkbox("进入复数模式时显示虚部", &showimg_auto);
	ImGui::Checkbox("总是显示虚部", &showimg_f);
	ImGui::End();
}
