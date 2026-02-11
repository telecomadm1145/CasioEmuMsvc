#include "KeyLogWindow.hpp"
#include "Emulator.hpp"
#include "Chipset/Chipset.hpp"
#include "Localization.h"

KeyLogWindow::KeyLogWindow() : UIWindow("KeyLogWindow.Title"_lc) {}

void KeyLogWindow::RenderCore() {
	auto keyLogger = m_emu->chipset.QueryInterface<casioemu::IKeyLogger>();
	if (!keyLogger) {
		ImGui::Text("Key logger interface not found.");
		return;
	}

	if (ImGui::Button("KeyLogWindow.Clear"_lc)) {
		keyLogger->ClearKeyLog();
	}

	ImGui::Separator();

	ImGui::BeginChild("LogRegion", ImVec2(0, 0), true);
	const auto& log = keyLogger->GetKeyLog();
	for (const auto& entry : log) {
		ImGui::Text("%s", entry.key_name.c_str());
	}
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
}
