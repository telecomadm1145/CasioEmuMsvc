#include "QrCodeWindow.h"

#include "Emulator.hpp"
#include "Localization.h"
#include "imgui/imgui.h"
#include <string>
#include <SDL.h>

namespace {
	void RenderPayloadActions(const std::string& data) {
		if (ImGui::Button("QrCode.Copy"_lc)) {
			ImGui::SetClipboardText(data.c_str());
		}
		if (data.starts_with("http://") || data.starts_with("https://")) {
			ImGui::SameLine();
			if (ImGui::Button("QrCode.OpenUrl"_lc)) {
				SDL_OpenURL(data.c_str());
			}
		}
	}
}

QrCodeWindow::QrCodeWindow() : UIWindow("QR Code") {
	inital_size = ImVec2(700, 420);
}

void QrCodeWindow::RenderCore() {
	m_emu->qr_code.Poll(*m_emu);
	const auto qr = m_emu->qr_code.GetState();
	ImGui::Text("QrCode.StatusFmt"_lc, qr.Active ? "QrCode.Active"_lc : "QrCode.Inactive"_lc);
	ImGui::SameLine();
	ImGui::Text("QrCode.CompleteFmt"_lc, qr.Complete ? "QrCode.Yes"_lc : "QrCode.No"_lc);
	ImGui::SameLine();
	ImGui::Text("QrCode.VersionFmt"_lc, qr.Version);
	ImGui::SameLine();
	ImGui::Text("QrCode.LengthFmt"_lc, qr.Data.size());
	ImGui::SameLine();
	ImGui::Text("QrCode.HistoryFmt"_lc, qr.History.size());
	if (qr.Active && qr.RealTotalPages != 0) {
		ImGui::Text("QrCode.RealRomProgressFmt"_lc,
			static_cast<unsigned>(qr.RealCurrentPage),
			static_cast<unsigned>(qr.RealTotalPages),
			qr.RealPageLengths.size(),
			static_cast<unsigned>(qr.RealTotalPages));
	}

	ImGui::Separator();
	ImGui::TextUnformatted("QrCode.Current"_lc);

	if (qr.Active && qr.Complete) {
		RenderPayloadActions(qr.Data);
		ImGui::BeginChild("##qr_current_payload", ImVec2(-1, 90), true);
		ImGui::TextWrapped("%s", qr.Data.c_str());
		ImGui::EndChild();
	}
	else if (qr.Active) {
		ImGui::TextWrapped("%s", "QrCode.IncompleteCapture"_lc);
		if (!qr.RealCurrentPageData.empty()) {
			ImGui::BeginChild("##qr_current_page_payload", ImVec2(-1, 90), true);
			ImGui::TextWrapped("%s", qr.RealCurrentPageData.c_str());
			ImGui::EndChild();
		}
	}
	else {
		ImGui::TextWrapped("%s", "QrCode.NoCurrent"_lc);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("QrCode.History"_lc);
	if (qr.History.empty()) {
		ImGui::TextWrapped("%s", "QrCode.NoHistory"_lc);
		return;
	}

	ImGui::BeginChild("##qr_history", ImVec2(-1, -1), true);
	for (auto it = qr.History.rbegin(); it != qr.History.rend(); ++it) {
		ImGui::PushID(static_cast<int>(it->Id));
		const auto header = localstr(
			"QrCode.HistoryEntryFmt",
			static_cast<unsigned long long>(it->Id),
			it->Version,
			it->Data.size());
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
