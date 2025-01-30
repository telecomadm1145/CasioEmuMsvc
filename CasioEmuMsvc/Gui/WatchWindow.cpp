#include "WatchWindow.hpp"
#include "Chipset//Chipset.hpp"
#include "Chipset/CPU.hpp"
#include "CodeViewer.hpp"
#include "Config.hpp"
#include "Models.h"
#include "Peripheral/BatteryBackedRAM.hpp"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <stdlib.h>

void WatchWindow::PrepareRX() {
	for (int i = 0; i < 16; i++) {
		sprintf((char*)reg_rx[i], "%02x", m_emu->chipset.cpu.reg_r[i] & 0x0ff);
	}
	sprintf(reg_pc, "%05x", (uint32_t)(m_emu->chipset.cpu.reg_csr << 16) | m_emu->chipset.cpu.reg_pc);
	sprintf(reg_lr, "%05x", (uint32_t)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr);
	sprintf(reg_sp, "%04x", m_emu->chipset.cpu.reg_sp | 0);
	sprintf(reg_ea, "%04x", m_emu->chipset.cpu.reg_ea | 0);
	sprintf(reg_psw, "%02x", m_emu->chipset.cpu.reg_psw | 0);
	sprintf(reg_dsr, "%02x", m_emu->chipset.cpu.reg_dsr | 0);
}

void WatchWindow::ShowRX() {
	char id[10];
	ImGui::TextColored(ImVec4(0, 200, 0, 255), "RXn: ");
	for (int i = 0; i < 16; i++) {
		ImGui::SameLine();
		sprintf(id, "##data%d", i);
		ImGui::SetNextItemWidth(char_width * 3);
		ImGui::TextUnformatted((char*)&reg_rx[i][0]);
	}
	ImGui::TextUnformatted("ERn: ");
	for (int i = 0; i < 16; i += 2) {
		ImGui::SameLine();
		uint16_t val = m_emu->chipset.cpu.reg_r[i + 1]
						   << 8 |
					   m_emu->chipset.cpu.reg_r[i];
		ImGui::Text("%04x ", val);
	}
	auto show_sfr = ([&](char* ptr, const char* label, int i, int width = 4) {
		ImGui::TextColored(ImVec4(0, 200, 0, 255), "%s", label);
		ImGui::SameLine();
		sprintf(id, "##sfr%d", i);
		ImGui::SetNextItemWidth(char_width * width + 5);
		ImGui::TextUnformatted(ptr);
	});
	show_sfr(reg_pc, "PC: ", 1, 6);
	ImGui::SameLine();
	show_sfr(reg_lr, "LR: ", 2, 6);
	ImGui::SameLine();
	show_sfr(reg_ea, "EA: ", 3);
	ImGui::SameLine();
	show_sfr(reg_sp, "SP: ", 4);
	ImGui::SameLine();
	show_sfr(reg_psw, "PSW: ", 5, 2);
	ImGui::SameLine();
	show_sfr(reg_dsr, "DSR: ", 6, 2);
}
void WatchWindow::ModRX() {
	char id[10];
	ImGui::TextColored(ImVec4(0, 200, 0, 255), "RXn: ");
	for (int i = 0; i < 16; i++) {
		ImGui::SameLine();
		sprintf(id, "##data%d", i);
		ImGui::SetNextItemWidth(char_width * 3);
		ImGui::InputText(id, (char*)&reg_rx[i][0], 3, ImGuiInputTextFlags_CharsHexadecimal);
	}
	// ERn
	// 不可编辑，必须通过Rn编辑
	ImGui::TextUnformatted("ERn: ");
	for (int i = 0; i < 16; i += 2) {
		ImGui::SameLine();
		uint16_t val = m_emu->chipset.cpu.reg_r[i + 1]
						   << 8 |
					   m_emu->chipset.cpu.reg_r[i];
		ImGui::Text("%04x ", val);
	}

	auto show_sfr = ([&](char* ptr, const char* label, int i, int width = 4) {
		ImGui::TextColored(ImVec4(0, 200, 0, 255), "%s", label);
		ImGui::SameLine();
		sprintf(id, "##sfr%d", i);
		ImGui::SetNextItemWidth(char_width * width + 2);
		ImGui::InputText(id, (char*)ptr, width + 1, ImGuiInputTextFlags_CharsHexadecimal);
	});
	show_sfr(reg_pc, "PC: ", 1, 6);
	ImGui::SameLine();
	show_sfr(reg_lr, "LR: ", 2, 6);
	ImGui::SameLine();
	show_sfr(reg_ea, "EA: ", 3);
	ImGui::SameLine();
	show_sfr(reg_sp, "SP: ", 4);
	ImGui::SameLine();
	show_sfr(reg_psw, "PSW: ", 5, 2);
	ImGui::SameLine();
	show_sfr(reg_dsr, "DSR: ", 6, 2);
}

void WatchWindow::UpdateRX() {
	for (int i = 0; i < 16; i++) {
		m_emu->chipset.cpu.reg_r[i] = (uint8_t)strtol((char*)reg_rx[i], nullptr, 16);
	}
	auto pc = strtol((char*)reg_pc, nullptr, 16);
	m_emu->chipset.cpu.reg_pc = (uint16_t)pc;
	m_emu->chipset.cpu.reg_csr = pc >> 16;
	 pc = strtol((char*)reg_lr, nullptr, 16);
	m_emu->chipset.cpu.reg_lr = (uint16_t)pc;
	m_emu->chipset.cpu.reg_lcsr = pc >> 16;
	m_emu->chipset.cpu.reg_ea = (uint16_t)strtol((char*)reg_ea, nullptr, 16);
	m_emu->chipset.cpu.reg_sp = (uint16_t)strtol((char*)reg_sp, nullptr, 16);
	m_emu->chipset.cpu.reg_psw = (uint16_t)strtol((char*)reg_psw, nullptr, 16);
}
inline static std::string lookup_symbol(uint32_t addr) {
	auto iter = std::lower_bound(g_labels.begin(), g_labels.end(), addr,
		[](const Label& label, uint32_t addr) { return label.address < addr; });

	if (iter == g_labels.end() || iter->address > addr) {
		if (iter != g_labels.begin())
			--iter;
		else {
			char buf[20];
			return SDL_ltoa(addr, buf, 16);
		}
	}

	if (addr == iter->address) {
		return iter->name;
	}
	else {
		char buf[20];
		return iter->name + "+" + SDL_ltoa(addr - iter->address, buf, 16);
	}
}
void WatchWindow::RenderCore() {
	char_width = ImGui::CalcTextSize("F").x;
	casioemu::Chipset& chipset = m_emu->chipset;
	ImGui::BeginChild("##reg_trace", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() *8), false, 0);
	auto rm = m_emu->chipset.run_mode;
	using casioemu::Chipset::RM_HALT;
	using casioemu::Chipset::RM_RUN;
	using casioemu::Chipset::RM_STOP;
    ImGui::TextUnformatted(("WatchWindow.CoreStatus"_l + ": " + 
        (rm == RM_RUN ? "Run" : (rm == RM_STOP ? "Stop" : (rm == RM_HALT ? "Halt" : "?")))).c_str());
	// ImGui::Text("Psw");
	// for (size_t i = 0; i < 8; i++) {
	//	ImGui::SameLine(i * 25. + 50.);
	//	ImGui::Text("%zu", i);
	// }
	// ImGui::Dummy(ImVec2(0, 0));

	// bool changed = false;
	// for (size_t i = 0; i < 8; i++) {
	//	ImGui::SameLine(i * 25. + 50.);
	//	if (ImGui::Checkbox(("##" + std::to_string(i)).c_str(), &pdx[i])) {
	//		changed = true;
	//	}
	// }

	// if (changed) {
	//	pd = 0;
	//	for (int i = 0; i < 8; i++) {
	//		if (pdx[i]) {
	//			pd |= (1 << i);
	//		}
	//	}
	//	m_emu->modeldef.pd_value = pd;
	// }
	PrepareRX();
	if (!m_emu->GetPaused()) {
		ShowRX();
		if (ImGui::Button("WatchWindow.Pause"_lc)) {
			m_emu->SetPaused(1);
		}
	}
	else {
		ModRX();
		UpdateRX();
		if (ImGui::Button("WatchWindow.Continue"_lc)) {
			m_emu->SetPaused(0);
		}
	}

	ImGui::EndChild();
	ImGui::Separator();
	static int range = 64;
	ImGui::BeginChild("##stack_trace", ImVec2(0, ImGui::GetWindowHeight() / 2));
	if (ImGui::BeginTable("##Stack_trace", 6, pretty_table)) {
		ImGui::TableSetupColumn("WatchWindow.Function"_lc, ImGuiTableColumnFlags_WidthStretch, 1);
		ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("SP", ImGuiTableColumnFlags_WidthFixed, 40);
		ImGui::TableSetupColumn("ER0", ImGuiTableColumnFlags_WidthFixed, 40);
		ImGui::TableSetupColumn("ER2", ImGuiTableColumnFlags_WidthFixed, 40);
		ImGui::TableSetupColumn("LR", ImGuiTableColumnFlags_WidthStretch, 1);
		ImGui::TableHeadersRow();
		auto stack = chipset.cpu.stack.get();
		class reverse_view {
		public:
			reverse_view(decltype(*stack)& vector1) : stk(vector1) {}
			decltype(*stack)& stk;
			auto begin() {
				return stk.rbegin();
			}
			auto end() {
				return stk.rend();
			}
		};

		for (auto& frame : reverse_view{*stack}) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(lookup_symbol(frame.new_pc).c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%06X", frame.new_pc);
			ImGui::TableNextColumn();
			ImGui::Text("%04X", frame.sp);
			ImGui::TableNextColumn();
			ImGui::Text("%04X", frame.er0);
			ImGui::TableNextColumn();
			ImGui::Text("%04X", frame.er2);
			ImGui::TableNextColumn();
			if (frame.lr_pushed) {
				if (frame.lr == 0xffffff) {
					ImGui::TextUnformatted("WatchWindow.LrDestroyed"_lc);
				}
				else {
					ImGui::TextUnformatted(lookup_symbol(frame.lr).c_str());
				}
			}
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
	ImGui::BeginChild("##stack_view");
	ImGui::TextUnformatted("WatchWindow.StackMemViewRange"_lc);
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
	mem_editor.DrawContents((void*)static_cast<size_t>(offset), rng, offset);
	ImGui::EndChild();
}