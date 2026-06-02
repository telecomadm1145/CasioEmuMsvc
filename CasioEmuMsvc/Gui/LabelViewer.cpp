#include <algorithm>
#include "LabelViewer.h"
#include "Models.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include "stringhelper.h"

void LabelViewer::RenderCore() {
	ImGui::TextUnformatted("Search:");
	ImGui::SameLine();
	ImGui::InputText("##labelsearch", m_SearchBuf, sizeof(m_SearchBuf));

	std::string filter(m_SearchBuf);
	std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

	UIHelpers::SectionHeader("Label.GeneralHeader"_lc);

	auto labels = casioemu::GetCommonMemLabels(m_emu->hardware_id);
	std::sort(labels.begin(), labels.end());

	if (ImGui::BeginTable("##labels_table", 3, pretty_table)) {
		ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthFixed, 45.0f);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		int i = 40;
		for (const auto& lb : labels) {
			std::string desc_lower = lb.desc;
			std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(), ::tolower);
			char addr_str[16];
			sprintf(addr_str, "%X", (unsigned int)lb.start);
			std::string addr_lower(addr_str);
			std::transform(addr_lower.begin(), addr_lower.end(), addr_lower.begin(), ::tolower);

			if (!filter.empty() && 
				desc_lower.find(filter) == std::string::npos && 
				addr_lower.find(filter) == std::string::npos) {
				continue;
			}

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::PushID(i++);
			if (ImGui::SmallButton("Copy")) {
				ImGui::SetClipboardText(addr_str);
			}
			ImGui::PopID();

			ImGui::TableNextColumn();
			UIHelpers::ClickableAddress(lb.start);

			ImGui::TableNextColumn();
			std::string desc = lb.desc;
			ltrim(desc);
			ImGui::TextUnformatted(desc.c_str());
		}
		ImGui::EndTable();
	}

	UIHelpers::SectionHeader("Label.SfrsHeader"_lc);

	auto regs = me_mmu->GetRegions();
	std::sort(regs.begin(), regs.end(), [](casioemu::MMURegion* a, casioemu::MMURegion* b) { return a->base < b->base; });

	if (ImGui::BeginTable("##regs_table", 3, pretty_table)) {
		ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthFixed, 45.0f);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		int i = 1000;
		for (auto lb : regs) {
			std::string desc_lower = lb->description;
			std::transform(desc_lower.begin(), desc_lower.end(), desc_lower.begin(), ::tolower);
			char addr_str[16];
			sprintf(addr_str, "%X", (unsigned int)lb->base);
			std::string addr_lower(addr_str);
			std::transform(addr_lower.begin(), addr_lower.end(), addr_lower.begin(), ::tolower);

			if (!filter.empty() && 
				desc_lower.find(filter) == std::string::npos && 
				addr_lower.find(filter) == std::string::npos) {
				continue;
			}

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::PushID(i++);
			if (ImGui::SmallButton("Copy")) {
				ImGui::SetClipboardText(addr_str);
			}
			ImGui::PopID();

			ImGui::TableNextColumn();
			UIHelpers::ClickableAddress(lb->base);

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(lb->description.c_str());
		}
		ImGui::EndTable();
	}
}
