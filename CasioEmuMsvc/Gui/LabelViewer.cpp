#include <algorithm>
#include "LabelViewer.h"
#include "Models.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include "stringhelper.h"

void LabelViewer::RenderCore() {
	ImGui::Text("%s", "Label.GeneralHeader"_lc);
	ImGui::Separator();
	auto labels = casioemu::GetCommonMemLabels(m_emu->hardware_id);
	std::sort(labels.begin(), labels.end());
	char buf[32];
	int i = 40;
	for (const auto& lb : labels) {
		ImGui::PushID(i++);
		if (ImGui::Button("Label.Copy"_lc)) {
			sprintf(buf, "%X", static_cast<unsigned int>(lb.start));
			ImGui::SetClipboardText(buf);
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("%X", (unsigned int)lb.start);
		ImGui::SameLine();
		std::string desc = lb.desc;
		ltrim(desc);
		ImGui::TextUnformatted(desc.c_str());
		ImGui::Separator();
	}
	ImGui::Text("%s", "Label.SfrsHeader"_lc);
	ImGui::Separator();
	auto regs = me_mmu->GetRegions();
	std::sort(regs.begin(), regs.end(), [](casioemu::MMURegion* a, casioemu::MMURegion* b) { return a->base < b->base; });
	for (auto lb : regs) {
		ImGui::PushID(i++);
		if (ImGui::Button("Label.Copy"_lc)) {
			sprintf(buf, "%X", static_cast<unsigned int>(lb->base));
			ImGui::SetClipboardText(buf);
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text("%X", (unsigned int)lb->base);
		ImGui::SameLine();
		ImGui::TextUnformatted(lb->description.c_str());
		ImGui::Separator();
	}
}
