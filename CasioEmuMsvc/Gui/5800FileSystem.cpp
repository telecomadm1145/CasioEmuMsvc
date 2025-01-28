#include "BatteryBackedRAM.hpp"
#include "Chipset.hpp"
#include "Ui.hpp"
#include "Localization.h"
class Fx5800FileSystem : public UIWindow {
public:
	Fx5800FileSystem() : UIWindow("Files") {
	}
	std::vector<std::string> pathes;
	struct FileEntry {
	public:
		uint32_t file_ptr;
		uint32_t count;
		std::string name;
	};
	uint32_t curptr = 0;
	std::vector<FileEntry> Dir() {
		std::vector<FileEntry> fe{};
		auto dat = (unsigned char*)ram->GetPRam() - 0x40000;
		auto begin = 0x40060;
		auto end = 0x40128;
		int j = 0;
	redo:
		for (size_t i = begin; i < end; i += 0x14) {
			FileEntry fe2{};
			char name[0xC]{};
			memcpy(name, &dat[i], 0xB);
			fe2.name = name;
			fe2.file_ptr = *(int*)&dat[i + 0xC];
			fe2.count = *(int*)&dat[i + 0x10];
			if (j) {
				i += 0x4;
			}
			if (j < pathes.size()) {
				if (pathes[j] == fe2.name) {
					j++;
					fe.clear();
					begin = fe2.file_ptr;
					end = fe2.file_ptr + 0x14 * fe2.count;
					goto redo;
				}
			}
			fe.push_back(fe2);
		}
		if (j < pathes.size()) {
			return {};
		}
		curptr = begin;
		return fe;
	}
	IRam* ram{};
	// 通过 UIWindow 继承
	void RenderCore() override {
		if (!ram)
			ram = m_emu->chipset.QueryInterface<IRam>();
		std::string fin = "/";
		for (auto& path : pathes) {
			fin += path;
			fin += "/";
		}
		if (fin.size() > 1) {
			fin.resize(fin.size() - 1);
		}
		if (ImGui::Button("5800FS.Back"_lc)) {
			if (pathes.size())
				pathes.resize(pathes.size() - 1);
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(fin.c_str());
		ImGui::Text("%06X", curptr);
		if (ImGui::BeginTable("", 2, pretty_table)) {
			ImGui::TableSetupColumn("5800FS.Name"_lc, ImGuiTableColumnFlags_WidthStretch, 1);
			ImGui::TableSetupColumn("5800FS.Length"_lc, ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableHeadersRow();
			auto d = Dir();
			for (auto& a : d) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (a.file_ptr> 0x40000) {
					ImGui::PushID(a.file_ptr);
					if (ImGui::Selectable(a.name.c_str())) {
						pathes.push_back(a.name);
					}
					ImGui::PopID();
				}
				else {
					if (ImGui::Selectable(a.name.c_str())) {
						char buf[40]{};
						sprintf(buf, "%06X", curptr + a.file_ptr);
						ImGui::SetClipboardText(buf);
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("%06X", curptr+ a.file_ptr);
				}
				ImGui::TableNextColumn();
				ImGui::Text("%d", a.count);
			}
			ImGui::EndTable();
		}
	}
};
UIWindow* CreateFx5800FileSystem() {
	return new Fx5800FileSystem();
}