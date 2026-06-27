#include "QrCodeWindow.h"

#include "Emulator.hpp"
#include "imgui/imgui.h"
#include <string>
#ifndef CASIOEMU_CORE_WEB
#include <SDL.h>
#endif

namespace {
	void RenderPayloadActions(const std::string& data) {
		if (ImGui::Button("Copy")) {
			ImGui::SetClipboardText(data.c_str());
		}
#ifndef CASIOEMU_CORE_WEB
		if (data.starts_with("http://") || data.starts_with("https://")) {
			ImGui::SameLine();
			if (ImGui::Button("Open URL")) {
				SDL_OpenURL(data.c_str());
			}
		}
#endif
	}
}

QrCodeWindow::QrCodeWindow() : UIWindow("QR Code") {
	inital_size = ImVec2(700, 420);
}

void QrCodeWindow::RenderCore() {
	m_emu->qr_code.Poll(*m_emu);
	const auto qr = m_emu->qr_code.GetState();
	ImGui::Text("Status: %s", qr.Active ? "Active" : "Inactive");
	ImGui::SameLine();
	ImGui::Text("Complete: %s", qr.Complete ? "Yes" : "No");
	ImGui::SameLine();
	ImGui::Text("Version: %d", qr.Version);
	ImGui::SameLine();
	ImGui::Text("Length: %zu", qr.Data.size());
	ImGui::SameLine();
	ImGui::Text("History: %zu", qr.History.size());
	if (qr.Active && qr.RealTotalPages != 0) {
		ImGui::Text("Real ROM capture progress: page %u / %u, captured %zu / %u",
			qr.RealCurrentPage,
			qr.RealTotalPages,
			qr.RealPageLengths.size(),
			qr.RealTotalPages);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Current");

	if (qr.Active && qr.Complete) {
		RenderPayloadActions(qr.Data);
		ImGui::BeginChild("##qr_current_payload", ImVec2(-1, 90), true);
		ImGui::TextWrapped("%s", qr.Data.c_str());
		ImGui::EndChild();
	}
	else if (qr.Active) {
		ImGui::TextWrapped("QR code is displayed, but full multi-page payload has not been captured yet.");
		if (!qr.RealCurrentPageData.empty()) {
			ImGui::BeginChild("##qr_current_page_payload", ImVec2(-1, 90), true);
			ImGui::TextWrapped("%s", qr.RealCurrentPageData.c_str());
			ImGui::EndChild();
		}
	}
	else {
		ImGui::TextWrapped("No QR code is currently displayed by the calculator.");
	}

	ImGui::Separator();
	ImGui::TextUnformatted("History");
	if (qr.History.empty()) {
		ImGui::TextWrapped("No QR code has been captured in this session.");
		return;
	}

	ImGui::BeginChild("##qr_history", ImVec2(-1, -1), true);
	for (auto it = qr.History.rbegin(); it != qr.History.rend(); ++it) {
		ImGui::PushID(static_cast<int>(it->Id));
		const std::string header =
			"#" + std::to_string(it->Id) +
			"  Version " + std::to_string(it->Version) +
			"  Length " + std::to_string(it->Data.size());
		if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			RenderPayloadActions(it->Data);
			ImGui::BeginChild("payload", ImVec2(-1, 80), true);
			ImGui::TextWrapped("%s", it->Data.c_str());
			ImGui::EndChild();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}
