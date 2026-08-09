#include "WatchWindow.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/ePSCpu.h"
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
	auto eps = m_emu->chipset.epscpu;
	if (eps) {
		const auto snapshot = eps->DebugSnapshot();
		const auto& regs = snapshot.registers;
		snprintf(reg_pc, sizeof(reg_pc), "%05x", snapshot.program_counter);
		if (regs[0x01] & 0x80) {
			snprintf(reg_lr, sizeof(reg_lr), "%05x", (uint32_t)((regs[0x02] << 7) | (regs[0x01] & 0x7f)));
		}
		else {
			snprintf(reg_lr, sizeof(reg_lr), "%02x(SFR)", (uint32_t)(regs[0x01] & 0x7f));
		}
		snprintf(reg_ea, sizeof(reg_ea), "%05x", (uint32_t)((regs[0x05] << 7) | (regs[0x04] & 0x7f)));
		snprintf(reg_ex1, sizeof(reg_ex1), "%05x", (uint32_t)((regs[0x12] << 7) | (regs[0x11] & 0x7f)));
		snprintf(reg_ex2, sizeof(reg_ex2), "%05x", (uint32_t)(((regs[0x23] & 0x03) * 0x60) | regs[0x22]));
		snprintf(reg_sp, sizeof(reg_sp), "%04x", snapshot.stack_pointer);
		snprintf(reg_psw, sizeof(reg_psw), "%02x", regs[0x0f]);
		snprintf(reg_dsr, sizeof(reg_dsr), "%02x", regs[0x02]);
	}
	else {
		for (int i = 0; i < 16; i++) {
            snprintf((char*)reg_rx[i], sizeof(reg_rx[i]), "%02x", m_emu->chipset.cpu.reg_r[i] & 0x0ff);
		}
        snprintf(reg_pc, sizeof(reg_pc), "%05x", (uint32_t)(m_emu->chipset.cpu.reg_csr << 16) | m_emu->chipset.cpu.reg_pc);
        snprintf(reg_lr, sizeof(reg_lr), "%05x", (uint32_t)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr);
        snprintf(reg_sp, sizeof(reg_sp), "%04x", m_emu->chipset.cpu.reg_sp | 0);
        snprintf(reg_ea, sizeof(reg_ea), "%04x", m_emu->chipset.cpu.reg_ea | 0);
        snprintf(reg_psw, sizeof(reg_psw), "%02x", m_emu->chipset.cpu.reg_psw | 0);
        snprintf(reg_dsr, sizeof(reg_dsr), "%02x", m_emu->chipset.cpu.reg_dsr | 0);
	}
}

void WatchWindow::ShowRX() {
	char id[10];
	if (m_emu->chipset.epscpu) {
	}
	else {
		ImGui::TextColored(~UIHelpers::kColorSuccess, "RXn: ");
		for (int i = 0; i < 16; i++) {
			ImGui::SameLine();
            snprintf(id, sizeof(id), "##data%d", i);
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
	}
	auto show_sfr = ([&](char* ptr, const char* label, int i, int width = 4) {
		ImGui::TextColored(~UIHelpers::kColorSuccess, "%s", label);
		ImGui::SameLine();
        snprintf(id, sizeof(id), "##sfr%d", i);
		ImGui::SetNextItemWidth(char_width * width + 5);
		ImGui::TextUnformatted(ptr);
	});
	show_sfr(reg_pc, "PC: ", 1, 6);
	ImGui::SameLine();
	if (m_emu->chipset.epscpu) {
		show_sfr(reg_lr, "INDF0: ", 2, 6);
		ImGui::SameLine();
		show_sfr(reg_ea, "INDF1: ", 3, 6);
		ImGui::SameLine();
		show_sfr(reg_ex1, "INDF2: ", 7, 6);
		ImGui::SameLine();
		show_sfr(reg_sp, "STKPTR: ", 4);
		ImGui::SameLine();
		show_sfr(reg_psw, "STATUS: ", 5, 2);
		ImGui::SameLine();
		show_sfr(reg_dsr, "BSR: ", 6, 2);
		show_sfr(reg_ex2, "LCDAR: ", 8, 6);
	}
	else {
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
}
void WatchWindow::ModRX() {
	char id[10];
	if (m_emu->chipset.epscpu) {
		auto edit_sfr = ([&](char* ptr, const char* label, int i, int width) {
			ImGui::TextColored(~UIHelpers::kColorSuccess, "%s", label);
			ImGui::SameLine();
			snprintf(id, sizeof(id), "##epssfr%d", i);
			ImGui::SetNextItemWidth(char_width * width + 5);
			ImGui::InputText(id, ptr, width + 1, ImGuiInputTextFlags_CharsHexadecimal);
		});
		edit_sfr(reg_pc, "PC: ", 1, 6);
		ImGui::SameLine();
		if (m_emu->chipset.epscpu->DebugSnapshot().registers[0x01] & 0x80) {
			edit_sfr(reg_lr, "INDF0: ", 2, 6);
		}
		else {
			ImGui::TextColored(~UIHelpers::kColorSuccess, "INDF0: ");
			ImGui::SameLine();
			ImGui::TextUnformatted(reg_lr);
		}
		ImGui::SameLine();
		edit_sfr(reg_ea, "INDF1: ", 3, 6);
		ImGui::SameLine();
		edit_sfr(reg_ex1, "INDF2: ", 4, 6);
		ImGui::SameLine();
		edit_sfr(reg_sp, "STKPTR: ", 5, 4);
		ImGui::SameLine();
		edit_sfr(reg_psw, "STATUS: ", 6, 2);
		ImGui::SameLine();
		ImGui::TextColored(~UIHelpers::kColorSuccess, "BSR: ");
		ImGui::SameLine();
		ImGui::TextUnformatted(reg_dsr);
		edit_sfr(reg_ex2, "LCDAR: ", 8, 6);
		return;
	}
	ImGui::TextColored(~UIHelpers::kColorSuccess, "RXn: ");
	for (int i = 0; i < 16; i++) {
		ImGui::SameLine();
        snprintf(id, sizeof(id), "##data%d", i);
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
		ImGui::TextColored(~UIHelpers::kColorSuccess, "%s", label);
		ImGui::SameLine();
        snprintf(id, sizeof(id), "##sfr%d", i);
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
	if (auto* eps = m_emu->chipset.epscpu) {
		const auto snapshot = eps->DebugSnapshot();
		const auto set_indirect = [eps](uint32_t value, bool uses_ram, uint8_t fsr_addr, uint8_t bsr_addr) {
			eps->WriteDebugMemory(fsr_addr, static_cast<uint8_t>((uses_ram ? 0x80 : 0) | (value & 0x7f)));
			if (uses_ram)
				eps->WriteDebugMemory(bsr_addr, static_cast<uint8_t>(value >> 7));
		};
		eps->SetPC(static_cast<uint32_t>(strtoul(reg_pc, nullptr, 16)));
		set_indirect(static_cast<uint32_t>(strtoul(reg_lr, nullptr, 16)), (snapshot.registers[0x01] & 0x80) != 0, 0x01, 0x02);
		set_indirect(static_cast<uint32_t>(strtoul(reg_ea, nullptr, 16)), true, 0x04, 0x05);
		set_indirect(static_cast<uint32_t>(strtoul(reg_ex1, nullptr, 16)), true, 0x11, 0x12);
		eps->WriteDebugMemory(0x06, static_cast<uint8_t>(strtoul(reg_sp, nullptr, 16)));
		eps->WriteDebugMemory(0x0f, static_cast<uint8_t>(strtoul(reg_psw, nullptr, 16)));
		const auto lcdar = static_cast<uint32_t>(strtoul(reg_ex2, nullptr, 16));
		eps->WriteDebugMemory(0x22, static_cast<uint8_t>(lcdar % 0x60));
		eps->WriteDebugMemory(0x23, static_cast<uint8_t>((snapshot.registers[0x23] & 0xf0) | ((lcdar / 0x60) & 0x03)));
		return;
	}
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

void WatchWindow::RenderCore() {
	char_width = ImGui::CalcTextSize("F").x;
	casioemu::Chipset& chipset = m_emu->chipset;
	ImGui::BeginChild("##reg_trace", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() *
		(m_emu->chipset.epscpu ? 11.0f : 8.0f)), false, 0);
	auto rm = m_emu->chipset.run_mode;
	using casioemu::Chipset::RM_HALT;
	using casioemu::Chipset::RM_RUN;
	using casioemu::Chipset::RM_STOP;
	ImGui::TextUnformatted(("WatchWindow.CoreStatus"_l + ": " +
							(rm == RM_RUN ? "Run" : (rm == RM_STOP ? "Stop" : (rm == RM_HALT ? "Halt" : "?"))))
			.c_str());
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
	//	m_emu->ModelDefinition.pd_value = pd;
	// }
	PrepareRX();
	if (chipset.epscpu) {
		const auto debug = chipset.epscpu->DebugSnapshot();
		ImGui::Text("Instructions: %llu  Cycles: %llu",
			static_cast<unsigned long long>(debug.instruction_count),
			static_cast<unsigned long long>(debug.cycle_count));
		if (ImGui::Checkbox("Trace", &eps_trace_enabled))
			chipset.epscpu->EnableTrace(eps_trace_enabled);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputInt("Capacity", &eps_trace_capacity, 256, 1024)) {
			eps_trace_capacity = std::clamp(eps_trace_capacity, 0, 65536);
			chipset.epscpu->SetTraceCapacity(static_cast<size_t>(eps_trace_capacity));
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear trace"))
			chipset.epscpu->ClearTrace();
	}
	if (!m_emu->GetPaused()) {
		ShowRX();
		if (ImGui::Button("WatchWindow.Pause"_lc)) {
			if (m_emu->chipset.epscpu)
				m_emu->chipset.epscpu->CancelDebugRun();
			m_emu->SetPaused(1);
		}
	}
	else {
		ModRX();
		UpdateRX();
		if (ImGui::Button("WatchWindow.Continue"_lc)) {
			if (m_emu->chipset.epscpu)
				m_emu->chipset.epscpu->RequestContinue();
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
		if (chipset.epscpu) {
			const auto snapshot = chipset.epscpu->DebugSnapshot();
			for (size_t i = 0; i < snapshot.stack_pointer; i++) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(lookup_symbol(snapshot.stack[i], g_labels).c_str());
				ImGui::TableNextColumn();
				UIHelpers::ClickableAddress(snapshot.stack[i]);
				ImGui::TableNextColumn();
				ImGui::Text("%04zX", i);
				ImGui::TableNextColumn();
				ImGui::Text("%04X", 0);
				ImGui::TableNextColumn();
				ImGui::Text("%04X", 0);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("");
			}
		}
		else {
			auto stack = chipset.cpu.stack.get();
			if (stack->empty()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushStyleColor(ImGuiCol_Text, UIHelpers::kColorMuted);
				ImGui::TextUnformatted("No stack frames. Run or step to update stack trace.");
				ImGui::PopStyleColor();
				for (int col = 1; col < 6; ++col) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted("");
				}
			}
			else {
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
					ImGui::TextUnformatted(lookup_symbol(frame.new_pc, g_labels).c_str());
					ImGui::TableNextColumn();
					UIHelpers::ClickableAddress(frame.new_pc);
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
							ImGui::TextUnformatted(lookup_symbol(frame.lr, g_labels).c_str());
						}
					}
				}
			}
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
	if (chipset.epscpu && ImGui::CollapsingHeader("EPS6800 Trace Buffer")) {
		const auto trace = chipset.epscpu->TraceBuffer();
		ImGui::BeginChild("##eps_trace_buffer", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 10), true);
		if (ImGui::BeginTable("##eps_trace_table", 7, pretty_table)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("#");
			ImGui::TableSetupColumn("Cycle");
			ImGui::TableSetupColumn("PC");
			ImGui::TableSetupColumn("Opcode");
			ImGui::TableSetupColumn("Next PC");
			ImGui::TableSetupColumn("A");
			ImGui::TableSetupColumn("STATUS");
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(trace.size()));
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					const auto& entry = trace[static_cast<size_t>(i)];
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(entry.instruction_count));
					ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(entry.cycle_count));
					ImGui::TableNextColumn(); UIHelpers::ClickableAddress(entry.program_counter, UIHelpers::JumpTarget::Code);
					ImGui::TableNextColumn(); ImGui::Text("%08X", entry.instruction);
					ImGui::TableNextColumn(); UIHelpers::ClickableAddress(entry.next_program_counter, UIHelpers::JumpTarget::Code);
					ImGui::TableNextColumn(); ImGui::Text("%02X", entry.accumulator);
					ImGui::TableNextColumn(); ImGui::Text("%02X", entry.status);
				}
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
	}
	ImGui::BeginChild("##stack_view");
	ImGui::TextUnformatted("WatchWindow.StackMemViewRange"_lc);
	ImGui::SameLine();
	ImGui::SliderInt("##range", &range, 64, 2048);
	uint16_t offset = chipset.epscpu ? 0 : chipset.cpu.reg_sp & 0xffff;
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
