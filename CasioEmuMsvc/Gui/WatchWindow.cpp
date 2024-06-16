#include "WatchWindow.hpp"
#include "../Chipset//Chipset.hpp"
#include "../Chipset/CPU.hpp"
#include "Ui.hpp"
#include "CodeViewer.hpp"
#include "imgui/imgui.h"
#include "../Peripheral/BatteryBackedRAM.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdlib.h>

#include "../Config.hpp"
#include "../Models.h"

WatchWindow::WatchWindow() {
	mem_editor.OptShowOptions = false;
}

void WatchWindow::PrepareRX() {
	for (int i = 0; i < 16; i++) {
		sprintf((char*)reg_rx[i], "%02x", m_emu->chipset.cpu.reg_r[i] & 0x0ff);
	}
	sprintf(reg_pc, "%04x", m_emu->chipset.cpu.reg_pc & 0xffff);
	sprintf(reg_lr, "%04x", m_emu->chipset.cpu.reg_lr & 0xffff);
	sprintf(reg_sp, "%04x", m_emu->chipset.cpu.reg_sp & 0xffff);
	sprintf(reg_ea, "%04x", m_emu->chipset.cpu.reg_ea & 0xffff);
	sprintf(reg_psw, "%02x", m_emu->chipset.cpu.reg_psw & 0xffff);
}

void WatchWindow::ShowRX() {
	char id[10];
	ImGui::TextColored(ImVec4(0, 200, 0, 255), "RXn: ");
	for (int i = 0; i < 16; i++) {
		ImGui::SameLine();
		sprintf(id, "##data%d", i);
		ImGui::SetNextItemWidth(char_width * 3);
		ImGui::Text((char*)&reg_rx[i][0]);
	}
	// ERn
	// 不可编辑，必须通过Rn编辑
	ImGui::Text("ERn: ");
	for (int i = 0; i < 16; i += 2) {
		ImGui::SameLine();
		uint16_t val = m_emu->chipset.cpu.reg_r[i + 1]
						   << 8 |
					   m_emu->chipset.cpu.reg_r[i];
		ImGui::Text("%04x ", val);
	}

	auto show_sfr = ([&](char* ptr, const char* label, int i, int width = 4) {
		ImGui::TextColored(ImVec4(0, 200, 0, 255), label);
		ImGui::SameLine();
		sprintf(id, "##sfr%d", i);
		ImGui::SetNextItemWidth(char_width * width + 5);
		ImGui::Text(ptr);
		/* ImGui::InputText(id, (char*)ptr, width + 1
			, ImGuiInputTextFlags_CharsHexadecimal);*/
	});
	show_sfr(reg_pc, "PC: ", 1);
	ImGui::SameLine();
	show_sfr(reg_lr, "LR: ", 2);
	ImGui::SameLine();
	show_sfr(reg_ea, "EA: ", 3);
	ImGui::SameLine();
	show_sfr(reg_sp, "SP: ", 4);
	ImGui::SameLine();
	show_sfr(reg_psw, "PSW: ", 5, 2);
}

void WatchWindow::UpdateRX() {
	for (int i = 0; i < 16; i++) {
		m_emu->chipset.cpu.reg_r[i] = (uint8_t)strtol((char*)reg_rx[i], nullptr, 16);
	}
	m_emu->chipset.cpu.reg_pc = (uint16_t)strtol((char*)reg_pc, nullptr, 16);
	m_emu->chipset.cpu.reg_lr = (uint16_t)strtol((char*)reg_lr, nullptr, 16);
	m_emu->chipset.cpu.reg_ea = (uint16_t)strtol((char*)reg_ea, nullptr, 16);
	m_emu->chipset.cpu.reg_sp = (uint16_t)strtol((char*)reg_sp, nullptr, 16);
	m_emu->chipset.cpu.reg_psw = (uint16_t)strtol((char*)reg_psw, nullptr, 16);
}

void WatchWindow::Show() {
	char_width = ImGui::CalcTextSize("F").x;
	ImGui::Begin(
#if LANGUAGE == 2
		"监视"
#else
		"Watch Window"
#endif
	);
	// ImGui::BeginChild("##stack_trace", ImVec2(0, ImGui::GetWindowHeight() / 4));
	casioemu::Chipset& chipset = m_emu->chipset;
	// std::string s = chipset.cpu.GetBacktrace();
	// ImGui::InputTextMultiline("##as", (char*)s.c_str(), s.size(), ImVec2(ImGui::GetWindowWidth(), 0), ImGuiInputTextFlags_ReadOnly);
	// ImGui::EndChild();
	ImGui::BeginChild("##reg_trace", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 4), false, 0);
	// if (!isbreaked) {
	//	ImGui::TextColored(ImVec4(255, 255, 0, 255), "寄存器请在断点状态下查看");
	//	PrepareRX();
	// }
	// else {
	PrepareRX();
	ShowRX();
	//}


	ImGui::EndChild();
	ImGui::Separator();
	static int range = 64;
	ImGui::BeginChild("##stack_view");
	ImGui::Text(
#if LANGUAGE == 2
		"设置堆栈范围"
#else
		"Set stack range"
#endif
	);
	ImGui::SameLine();
	ImGui::SliderInt("##range", &range, 64, 2048);
	uint16_t offset = chipset.cpu.reg_sp & 0xffff;
	mem_editor.ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
		return me_mmu->ReadData((size_t)data + off);
	};
	mem_editor.WriteFn = [](ImU8* data, size_t off, ImU8 d) {
		return me_mmu->WriteData((size_t)data + off, d);
	};
	auto rng = range;
	if (rng + offset >= casioemu::GetRamBaseAddr(m_emu->hardware_id) + casioemu::GetRamSize(m_emu->hardware_id)) {
		rng = casioemu::GetRamSize(m_emu->hardware_id) - offset + casioemu::GetRamBaseAddr(m_emu->hardware_id);
	}
	mem_editor.DrawContents((void*)offset, rng, offset);
	ImGui::EndChild();
	ImGui::End();
}