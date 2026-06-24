#include "MemBreakPoint.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Emulator.hpp"
#include "Gui/Hooks.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <Localization.h>
#include <cstdint>
#include <cstdlib>
#include <stdlib.h>

Breakpoints* membp_cv = 0;

void Breakpoints::DrawContent() {
	if (break_point_hash.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, UIHelpers::kColorMuted);
		ImGui::TextWrapped("No memory breakpoints set. Enter an address in hex and click 'Add' below to monitor memory access.");
		ImGui::PopStyleColor();
		return;
	}
	ImGuiListClipper c;
	static int selected = -1;
	c.Begin(break_point_hash.size());
	char buf[5] = {0};
	while (c.Step()) {

		for (int i = c.DisplayStart; i < c.DisplayEnd; i++) {
			MemBPData_t& data = break_point_hash[i];
			snprintf(buf, sizeof(buf), "%lx", (unsigned long)data.addr);
			ImGui::PushID(i);
			if (ImGui::Selectable(buf, selected == i)) {
				selected = i;
			}
			ImGui::PopID();
			if (ImGui::BeginPopupContextItem()) {
				selected = i;

				ImGui::TextUnformatted("MemBP.BPType"_lc);
				if (ImGui::Button("HexEditors.ContextMenu.MonitorRead"_lc)) {
					target_addr = i;
					data.enableWrite = 0;
					data.records.clear();
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::Button("HexEditors.ContextMenu.MonitorWrite"_lc)) {
					data.enableWrite = true;
					target_addr = i;
					data.records.clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::Separator();
				if (ImGui::Button("MemBP.Delete"_lc)) {
					data.records.clear();
					if (target_addr == i) {
						target_addr = -1;
					}
					break_point_hash.erase(break_point_hash.begin() + i);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
	}
}

void Breakpoints::DrawFindContent() {
	if (target_addr == -1) {
		ImGui::TextColored(~UIHelpers::kColorWarning, "%s", "MemBP.NoBPHint"_lc);
		return;
	}
	int write = break_point_hash[target_addr].enableWrite;
	static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
	ImGui::Text("MemBP.MonitoringHint"_lc,
		break_point_hash[target_addr].addr);
	ImGui::SameLine();
	static const char* fx = "";
	if (ImGui::Button("MemBP.ClearRec"_lc)) {
		break_point_hash[target_addr].records.clear();
	}
	if (ImGui::BeginTable("##outputtable", 2, flags)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("MemBP.ColPC"_lc);
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		int i = 0;
		for (auto kv : break_point_hash[target_addr].records) {
			uint32_t pc_addr = kv.first;
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			// Clickable address → left-click navigates CodeViewer; right-click offers memory jump
			UIHelpers::ClickableAddress(pc_addr, UIHelpers::JumpTarget::Code);
			ImGui::TableSetColumnIndex(1);
			ImGui::PushID(i++);
			if (ImGui::Button("MemBP.ViewCallstack"_lc)) {
				fx = kv.second.stacktrace.c_str();
				SDL_ShowSimpleMessageBox(0, "", fx, 0);
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
}


void Breakpoints::SetupHooks() {
	SetupHook(on_memory_read, [&](casioemu::MMU& sender, MemoryEventArgs& mea) {
		TryTrigBp(mea.offset, false);
	});
	SetupHook(on_memory_write, [&](casioemu::MMU& sender, MemoryEventArgs& mea) {
		TryTrigBp(mea.offset, true);
	});
	SetupHook(on_instruction, [&](casioemu::CPU& sender, InstructionEventArgs& iea) {
		if (break_on_sp) {
			bool trig = false;
			switch (reg_compare_mode) {
			case 1:
				trig = sender.reg_sp == target_sp;
				break;
			case 2:
				trig = sender.reg_sp != target_sp;
				break;
			case 3:
				trig = sender.reg_sp > target_sp;
				break;
			case 4:
				trig = sender.reg_sp < target_sp;
				break;
			case 5:
				trig = sender.reg_sp >= target_sp;
				break;
			case 6:
				trig = sender.reg_sp <= target_sp;
				break;
			}
			if (trig) {
				SetDebugbreak();
			}
		}
	});
	membp_cv = this;
}

void Breakpoints::TryTrigBp(uint32_t addr, bool write) {
	std::lock_guard lock(breakpoints_mutex);
	for (auto& bp : break_point_hash) {
		if (bp.addr != addr || bp.enableWrite != write)
			continue;
		if (bp.breakWhenHit) {
			SetDebugbreak();
		}
		else {
			bp.records[(m_emu->chipset.cpu.reg_csr << 16) | m_emu->chipset.cpu.reg_pc] =
				Record{m_emu->chipset.cpu.GetBacktrace(),
					(unsigned int)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr};
		}
	}
}

void Breakpoints::RenderCore() {
	std::lock_guard lock(breakpoints_mutex);
	if (ImGui::BeginTabBar("Breakpoints")) {
		if (ImGui::BeginTabItem("Memory")) {
			static char buf[10] = {0};
			ImGui::BeginChild("##srcollingmbp", ImVec2(0, break_on_cv ? ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() * 6 : ImGui::GetWindowHeight() / 3));
			DrawContent();
			ImGui::EndChild();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("F").x * 8);
			ImGui::InputText(
				"##addressin",
				buf, 10, ImGuiInputTextFlags_CharsHexadecimal);
			ImGui::SameLine();
			if (ImGui::Button("MemBP.AddAddr"_lc)) {
				break_point_hash.push_back({.addr = (uint32_t)strtol(buf, nullptr, 16)});
			}
			ImGui::Checkbox("MemBP.BreakWhenHit"_lc,
				&break_on_cv);
			if (target_addr >= 0 && static_cast<size_t>(target_addr) < break_point_hash.size())
				break_point_hash[target_addr].breakWhenHit = break_on_cv;
			if (!break_on_cv) {
				ImGui::BeginChild("##findoutput");
				DrawFindContent();
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Register")) {
			static char buf[10] = {0};
			ImGui::Combo("BP.RegCmpMode"_lc, &reg_compare_mode, "Disabled\0Equal\0Not Equal\0Greater\0Less\0Greater or Equal\0Less or Equal\0");
			ImGui::Separator();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("F").x * 8);
			if (ImGui::InputText(
					"##addressin2",
					buf, 10, ImGuiInputTextFlags_CharsHexadecimal)) {
				target_sp = (uint16_t)strtol(buf, nullptr, 16);
			}
			ImGui::SameLine();
			ImGui::Checkbox("BP.SPHint"_lc, &break_on_sp);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void Breakpoints::ExternalAddBp(uint32_t addr, bool write) {
	ExternalAddBp(addr, write, false);
}

void Breakpoints::ExternalAddBp(uint32_t addr, bool write, bool breakWhenHit) {
	std::lock_guard lock(breakpoints_mutex);
	break_point_hash.push_back({.enableWrite = write, .breakWhenHit = breakWhenHit, .addr = addr});
	target_addr = break_point_hash.size() - 1;
	break_on_cv = breakWhenHit;
}

bool Breakpoints::ExternalRemoveBp(uint32_t addr, bool write) {
	std::lock_guard lock(breakpoints_mutex);
	auto it = std::find_if(break_point_hash.begin(), break_point_hash.end(), [&](const MemBPData_t& bp) {
		return bp.addr == addr && bp.enableWrite == write;
	});
	if (it == break_point_hash.end())
		return false;
	const auto removed = static_cast<int>(std::distance(break_point_hash.begin(), it));
	break_point_hash.erase(it);
	if (target_addr == removed)
		target_addr = -1;
	else if (target_addr > removed)
		--target_addr;
	return true;
}

void Breakpoints::ExternalClearBps() {
	std::lock_guard lock(breakpoints_mutex);
	break_point_hash.clear();
	target_addr = -1;
	break_on_cv = false;
}

std::vector<MemBPData_t> Breakpoints::ExternalListBps() const {
	std::lock_guard lock(breakpoints_mutex);
	return break_point_hash;
}

std::vector<std::pair<uint32_t, Record>> Breakpoints::ExternalListHits(uint32_t addr, bool write) const {
	std::lock_guard lock(breakpoints_mutex);
	std::vector<std::pair<uint32_t, Record>> result;
	auto it = std::find_if(break_point_hash.begin(), break_point_hash.end(), [&](const MemBPData_t& bp) {
		return bp.addr == addr && bp.enableWrite == write;
	});
	if (it == break_point_hash.end())
		return result;
	result.reserve(it->records.size());
	for (const auto& record : it->records)
		result.push_back(record);
	return result;
}

void SetMemBp(uint32_t addr, bool write) {
	if (membp_cv) {
		membp_cv->ExternalAddBp(addr, write);
	}
}
