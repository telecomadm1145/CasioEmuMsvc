#include "CasioData.h"
#include "Models.h"
#include "CwiiHelp.h"
#include "Ui.hpp"
#include "imgui/imgui.h"

// std::array<std::pair<int, std::string>, 16> items = {
//	std::make_pair(0, "Reset 68"),
//	std::make_pair(0x06, "Matrix"),
//	std::make_pair(0x07, "Vector"),
//	std::make_pair(0x0D, "Spreadsheet"),
//	std::make_pair(0x0E, "Algorithm"),
//	std::make_pair(0x4F, "Math Box"),
//	std::make_pair(0x88, "Table"),
//	std::make_pair(0x89, "Verify"),
//	std::make_pair(0xC1, "Calculate"),
//	std::make_pair(0xC4, "Complex"),
//	std::make_pair(0x02, "Base-N"),
//	std::make_pair(0x03, "Statistics"),
//	std::make_pair(0x0C, "Distribution"),
//	std::make_pair(0x45, "Equation"),
//	std::make_pair(0x4A, "Ratio"),
//	std::make_pair(0x4B, "Inequality"),
// };

void CasioData::RenderCore() {
	auto hwid = m_emu->hardware_id;
	if (hwid != casioemu::HardwareId::HW_CLASSWIZ && hwid != casioemu::HardwareId::HW_CLASSWIZ_II) {
		return;
	}
	auto ram = casioemu::GetRamBaseAddr(hwid);
	auto mode = me_mmu->ReadData(casioemu::GetModeOffset(hwid));
	if (mode == 0xd) {
		static char formula[200]{}; // TODO: instance data
		static int row{}, col{};
		ImGui::InputText("##input", formula, 200);
		ImGui::Text("%c%d", "ABCDE"[col], row);
		std::string ss_data[45][5]{};
		auto sptr = n_ram_buffer - ram + casioemu::GetAppOffset(hwid); // BA68
		auto ptr = sptr;
		auto sz = (size_t)0;
		auto iptr = ptr; // C3FC
		if (hwid == casioemu::HW_CLASSWIZ) {
			iptr = n_ram_buffer - ram + 0xDBE8;
		}
		else {
			iptr = n_ram_buffer - ram + 0xC3FC;
		}
		for (size_t i = 0; i < 45 * 5 * 2; i += 2) {
			// 第一个字节: 公式长度
			// 第二个字节: ?
			//auto fmla = iptr[i];
			auto type = iptr[i + 1];
			if ((type & 0x80) == 0x80) {
				sz += casioemu::GetVariableSize(hwid);
				if ((type & 0x40) != 0x40) {
					auto a = i >> 1;
					auto r = a % 45;
					auto c = a / 45;
					ss_data[r][c] = cwii::StringizeCwiiNumber(ptr);
				}
				ptr = sptr + sz;
				if (sz > 2452)
					break;
			}
		}
		for (size_t i = 0; i < 45 * 5 * 2; i += 2) {
			// 第一个字节: 公式长度
			// 第二个字节: ?
			auto fmla = iptr[i];
			auto type = iptr[i + 1];
			if (fmla) {
				sz += fmla;
				if ((type & 0x40) == 0x40) {
					auto a = i >> 1;
					auto r = a % 45;
					auto c = a / 45;
					std::string s{ptr, (size_t)fmla};
					ss_data[r][c] = s;
				}
				ptr = sptr + sz;
				if (sz > 2452)
					break;
			}
		}
		if (ImGui::BeginTable("Spreadsheet", 5)) {
			ImGui::TableSetupColumn("A");
			ImGui::TableSetupColumn("B");
			ImGui::TableSetupColumn("C");
			ImGui::TableSetupColumn("D");
			ImGui::TableSetupColumn("E");
			ImGui::TableHeadersRow();
			for (size_t i = 0; i < 45; i++) {
				ImGui::TableNextRow();
				for (size_t j = 0; j < 5; j++) {
					ImGui::TableNextColumn();
					auto& s = ss_data[i][j];
					ImGui::PushID(i * 5 + j + 20);
					if (ImGui::Selectable(s.c_str())) {
						row = i;
						col = j;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
	}
}