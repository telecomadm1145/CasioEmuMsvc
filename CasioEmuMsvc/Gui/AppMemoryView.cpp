#include "AppMemoryView.h"
#include "imgui/imgui.h"
#include "hex.hpp"
#include "ui.hpp"

#include "CwiiHelp.h"
#include "../Models.h"

#define SColor \
	i++ % 2 ? ImColor{ 40, 120, 40, 255 } : ImColor { 40, 40, 120, 255 }

AppMemoryView::AppMemoryView() {
	mem_edit.ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
		return me_mmu->ReadData(off + casioemu::GetAppOffset(m_emu->hardware_id));
	};
	mem_edit.WriteFn = [](ImU8* data, size_t off, ImU8 d) {
		return me_mmu->WriteData(off + casioemu::GetAppOffset(m_emu->hardware_id), d);
	};
}
std::array<std::pair<int, std::string>, 16> items = {
	std::make_pair(0, "Reset 68"),
	std::make_pair(0x06, "Matrix"),
	std::make_pair(0x07, "Vector"),
	std::make_pair(0x0D, "Spreadsheet"),
	std::make_pair(0x0E, "Algorithm"),
	std::make_pair(0x4F, "Math Box"),
	std::make_pair(0x88, "Table"),
	std::make_pair(0x89, "Verify"),
	std::make_pair(0xC1, "Calculate"),
	std::make_pair(0xC4, "Complex"),
	std::make_pair(0x02, "Base-N"),
	std::make_pair(0x03, "Statistics"),
	std::make_pair(0x0C, "Distribution"),
	std::make_pair(0x45, "Equation"),
	std::make_pair(0x4A, "Ratio"),
	std::make_pair(0x4B, "Inequality"),
};

int current_item = 0;
std::string FindCurrentMode() {
	int current_mode_id = n_ram_buffer[casioemu::GetModeOffset(m_emu->hardware_id) - casioemu::GetRamBaseAddr(m_emu->hardware_id)] & 0xFF;
	for (const auto& item : items) {
		if (item.first == current_mode_id) {
			return item.second;
		}
	}
	return "Unknown";
}

void ShowCombo() {
	std::vector<const char*> item_names;
	item_names.reserve(items.size());
	for (const auto& item : items) {
		item_names.push_back(item.second.c_str());
	}
	if (ImGui::Combo(
#if LANGUAGE == 2
			"切换 App"
#else
			"Switch mode"
#endif
			,
			&current_item, item_names.data(), item_names.size())) {
		n_ram_buffer[casioemu::GetModeOffset(m_emu->hardware_id) - casioemu::GetRamBaseAddr(m_emu->hardware_id)] = items[current_item].first;
		n_ram_buffer[casioemu::GetModeOffset(m_emu->hardware_id) - casioemu::GetRamBaseAddr(m_emu->hardware_id) + 1] = 0;
		//// 处理选择更改
		// int selected_id = items[current_item].first;
		// const char* selected_name = items[current_item].second.c_str();
		//// 在这里处理选择的项目
		// ImGui::Text("Selected ID: 0x%02X", selected_id);
		// ImGui::Text("Selected Name: %s", selected_name);
	}
}
void AppMemoryView::Draw() {
	ImGui::Begin(
#if LANGUAGE == 2
		"应用内存视图"
#else
		"Application/Mode Memory View"
#endif
	);
	auto s = FindCurrentMode();
	ImGui::Text(s.c_str());
	ImGui::SameLine();
	s = cwii::HexizeString(&n_ram_buffer[casioemu::GetModeOffset(m_emu->hardware_id) - casioemu::GetRamBaseAddr(m_emu->hardware_id)], 2);
	ImGui::Text(s.c_str());
	ShowCombo();
	ImGui::BeginChild("##hexview");
	if (me_mmu != nullptr) {
		mem_edit.DrawContents(0, 3000, casioemu::GetAppOffset(m_emu->hardware_id), spans);
	}
	ImGui::EndChild();
	ImGui::End();
}
