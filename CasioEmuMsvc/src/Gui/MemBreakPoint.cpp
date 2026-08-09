#include "MemBreakPoint.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Chipset/ePSCpu.h"
#include "Emulator.hpp"
#include "Gui/Hooks.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <Localization.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdlib.h>

namespace {
	std::map<std::string, uint32_t> CaptureRegisters(const casioemu::CPU& cpu) {
		std::map<std::string, uint32_t> registers;
		registers["pc"] = (static_cast<uint32_t>(cpu.reg_csr.raw) << 16) | cpu.reg_pc.raw;
		registers["lr"] = (static_cast<uint32_t>(cpu.reg_lcsr.raw) << 16) | cpu.reg_lr.raw;
		registers["sp"] = cpu.reg_sp.raw;
		registers["dsr"] = cpu.reg_dsr.raw;
		registers["psw"] = cpu.reg_psw.raw;
		for (int i = 0; i < 16; ++i) {
			registers["r" + std::to_string(i)] = cpu.reg_r[i].raw;
		}
		for (int i = 0; i < 16; i += 2) {
			registers["er" + std::to_string(i)] =
				static_cast<uint16_t>(cpu.reg_r[i].raw | (cpu.reg_r[i + 1].raw << 8));
		}
		for (int i = 0; i < 16; i += 4) {
			registers["xr" + std::to_string(i)] =
				static_cast<uint32_t>(cpu.reg_r[i].raw)
				| (static_cast<uint32_t>(cpu.reg_r[i + 1].raw) << 8)
				| (static_cast<uint32_t>(cpu.reg_r[i + 2].raw) << 16)
				| (static_cast<uint32_t>(cpu.reg_r[i + 3].raw) << 24);
		}
		return registers;
	}
}

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
					SyncEpsBreakpoints();
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::Button("HexEditors.ContextMenu.MonitorWrite"_lc)) {
					data.enableWrite = true;
					target_addr = i;
					data.records.clear();
					SyncEpsBreakpoints();
					ImGui::CloseCurrentPopup();
				}
				if (m_emu && m_emu->chipset.epscpu) {
					bool configuration_changed = false;
					configuration_changed |= ImGui::Checkbox("Enabled", &data.enabled);
					configuration_changed |= ImGui::Checkbox("Compare data", &data.compareData);
					if (data.compareData) {
						int compare_data = data.data;
						int compare_mask = data.mask;
						int skip_count = static_cast<int>(std::min<uint64_t>(data.skipCount, 0x7fffffffu));
						if (ImGui::InputInt("Data", &compare_data, 1, 16, ImGuiInputTextFlags_CharsHexadecimal)) {
							data.data = static_cast<uint8_t>(compare_data);
							configuration_changed = true;
						}
						if (ImGui::InputInt("Mask", &compare_mask, 1, 16, ImGuiInputTextFlags_CharsHexadecimal)) {
							data.mask = static_cast<uint8_t>(compare_mask);
							configuration_changed = true;
						}
						if (ImGui::InputInt("Skip count", &skip_count, 1, 10)) {
							data.skipCount = static_cast<uint64_t>(std::max(skip_count, 0));
							configuration_changed = true;
						}
					}
					if (configuration_changed)
						SyncEpsBreakpoints();
				}
				ImGui::Separator();
				if (ImGui::Button("MemBP.Delete"_lc)) {
					data.records.clear();
					if (target_addr == i) {
						target_addr = -1;
					}
					break_point_hash.erase(break_point_hash.begin() + i);
					SyncEpsBreakpoints();
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
		if (m_emu && m_emu->chipset.epscpu) {
			for (const auto& hit : m_emu->chipset.epscpu->MemoryBreakpointHits(
				break_point_hash[target_addr].addr, write != 0)) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				UIHelpers::ClickableAddress(hit.program_counter, UIHelpers::JumpTarget::Code);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s %02X", hit.write ? "W" : "R", hit.value);
			}
		}
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
		if (RegisterBreakpointTriggered(sender.reg_sp.raw))
			SetDebugbreak();
	});
	SetupHook(on_eps_instruction, [&](InstructionEventArgs& iea) {
		if (RegisterBreakpointTriggered(iea.stack_pointer))
			iea.should_break = true;
	});
	membp_cv = this;
}

bool Breakpoints::RegisterBreakpointTriggered(uint32_t value) const {
	const uint64_t config = register_breakpoint_config.load(std::memory_order_acquire);
	if ((config & (uint64_t{1} << 63)) == 0)
		return false;
	const auto target = static_cast<uint32_t>(config);
	switch (static_cast<uint8_t>(config >> 32)) {
	case 1:
		return value == target;
	case 2:
		return value != target;
	case 3:
		return value > target;
	case 4:
		return value < target;
	case 5:
		return value >= target;
	case 6:
		return value <= target;
	default:
		return false;
	}
}

void Breakpoints::UpdateRegisterBreakpointConfig() {
	const uint64_t config = (break_on_sp ? (uint64_t{1} << 63) : 0) |
		(static_cast<uint64_t>(static_cast<uint8_t>(reg_compare_mode)) << 32) |
		static_cast<uint32_t>(target_sp);
	register_breakpoint_config.store(config, std::memory_order_release);
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
					(unsigned int)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr,
					CaptureRegisters(m_emu->chipset.cpu)};
		}
	}
}

void Breakpoints::RenderCore() {
	std::lock_guard lock(breakpoints_mutex);
	RefreshEpsBreakpoints();
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
				SyncEpsBreakpoints();
			}
			ImGui::Checkbox("MemBP.BreakWhenHit"_lc,
				&break_on_cv);
			if (target_addr >= 0 && static_cast<size_t>(target_addr) < break_point_hash.size() &&
				break_point_hash[target_addr].breakWhenHit != break_on_cv) {
				break_point_hash[target_addr].breakWhenHit = break_on_cv;
				SyncEpsBreakpoints();
			}
			if (!break_on_cv) {
				ImGui::BeginChild("##findoutput");
				DrawFindContent();
				ImGui::EndChild();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Register")) {
			const bool eps6800 = m_emu && m_emu->chipset.epscpu;
			static char buf[10] = {0};
			ImGui::Combo("BP.RegCmpMode"_lc, &reg_compare_mode, "Disabled\0Equal\0Not Equal\0Greater\0Less\0Greater or Equal\0Less or Equal\0");
			if (eps6800)
				ImGui::TextDisabled("EPS6800 STKPTR (00-1F)");
			ImGui::Separator();
			ImGui::SetNextItemWidth(ImGui::CalcTextSize("F").x * 8);
			if (ImGui::InputText(
					"##addressin2",
					buf, 10, ImGuiInputTextFlags_CharsHexadecimal)) {
				target_sp = (uint16_t)strtol(buf, nullptr, 16);
				if (eps6800)
					target_sp &= 0x1f;
			}
			ImGui::SameLine();
			ImGui::Checkbox(eps6800 ? "STKPTR" : "BP.SPHint"_lc, &break_on_sp);
			UpdateRegisterBreakpointConfig();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void Breakpoints::RefreshEpsBreakpoints() {
	if (!m_emu || !m_emu->chipset.epscpu)
		return;
	const auto core_breakpoints = m_emu->chipset.epscpu->MemoryBreakpoints();
	std::vector<MemBPData_t> refreshed;
	refreshed.reserve(core_breakpoints.size());
	for (const auto& item : core_breakpoints) {
		refreshed.push_back({
			.enableWrite = item.write,
			.breakWhenHit = item.break_when_hit,
			.enabled = item.enabled,
			.compareData = item.compare_data,
			.data = item.data,
			.mask = item.mask,
			.skipCount = item.skip_count,
			.addr = item.address});
	}
	break_point_hash = std::move(refreshed);
	if (target_addr < 0 || static_cast<size_t>(target_addr) >= break_point_hash.size()) {
		target_addr = -1;
		break_on_cv = false;
	}
	else {
		break_on_cv = break_point_hash[target_addr].breakWhenHit;
	}
}

void Breakpoints::ExternalAddBp(uint32_t addr, bool write) {
	ExternalAddBp(addr, write, false);
}

void Breakpoints::ExternalAddBp(uint32_t addr, bool write, bool breakWhenHit,
	bool enabled, bool compareData, uint8_t data, uint8_t mask, uint64_t skipCount) {
	std::lock_guard lock(breakpoints_mutex);
	break_point_hash.push_back({.enableWrite = write, .breakWhenHit = breakWhenHit,
		.enabled = enabled, .compareData = compareData, .data = data, .mask = mask,
		.skipCount = skipCount, .addr = addr});
	target_addr = break_point_hash.size() - 1;
	break_on_cv = breakWhenHit;
	SyncEpsBreakpoints();
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
	SyncEpsBreakpoints();
	return true;
}

void Breakpoints::ExternalClearBps() {
	std::lock_guard lock(breakpoints_mutex);
	break_point_hash.clear();
	target_addr = -1;
	break_on_cv = false;
	SyncEpsBreakpoints();
}

void Breakpoints::SyncEpsBreakpoints() {
	if (!m_emu || !m_emu->chipset.epscpu)
		return;
	auto* eps = m_emu->chipset.epscpu;
	eps->ClearMemoryBreakpoints();
	for (const auto& item : break_point_hash) {
		casioemu::Eps6800MemoryBreakpoint breakpoint{};
		breakpoint.address = item.addr;
		breakpoint.write = item.enableWrite;
		breakpoint.enabled = item.enabled;
		breakpoint.break_when_hit = item.breakWhenHit;
		breakpoint.compare_data = item.compareData;
		breakpoint.data = item.data;
		breakpoint.mask = item.mask;
		breakpoint.skip_count = item.skipCount;
		eps->AddMemoryBreakpoint(breakpoint);
	}
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
