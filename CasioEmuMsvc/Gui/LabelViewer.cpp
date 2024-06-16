#include "LabelViewer.h"
#include "imgui/imgui.h"
#include "../Models.h"
#include "Ui.hpp"
void LabelViewer::Draw() {
	ImGui::Begin(
#if LANGUAGE == 2
		"标签查看器"
#else
		"Label viewer"
#endif
	);
	ImGui::Text("Avaliable in this model:");
	ImGui::Separator();
	auto labels = casioemu::GetCommonMemLabels(m_emu->hardware_id);
	std::sort(labels.begin(), labels.end());
	char buf[32];
	for (auto lb : labels) {
		sprintf(buf, "Copy %X", (int)lb.start);
		if (ImGui::Button(buf))
			ImGui::SetClipboardText(buf + 5);
		ImGui::SameLine(120, 0);
		ImGui::Text(lb.desc);
		ImGui::Separator();
	}
	ImGui::Text("SFRs in this model:");
	ImGui::Separator();
	auto regs = me_mmu->GetRegions();
	std::sort(regs.begin(), regs.end(), [](casioemu::MMURegion* a, casioemu::MMURegion* b) { return a->base  < b->base; });
	for (auto lb : regs) {
		sprintf(buf, "Copy %X", (int)lb->base);
		if (ImGui::Button(buf))
			ImGui::SetClipboardText(buf + 5);
		ImGui::SameLine(120, 0);
		ImGui::Text(lb->description.c_str());
		ImGui::Separator();
	}
	ImGui::End();
}
