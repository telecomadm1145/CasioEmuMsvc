#include "CallAnalysis.h"
#include "Chipset/CPU.hpp"
#include "Hooks.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <Localization.h>

struct CallAnalysis : public UIWindow {
	bool is_call_recoding = false;
	bool check_caller = false;
	char caller[260]{};
	uint32_t caller_v{};
	bool check_callee = false;
	char callee[260]{};
	uint32_t callee_v{};
	class FunctionCall {
	public:
		uint32_t pc{};
		uint32_t lr{};
		uint32_t xr0{};
		std::string stack{};
	};
	std::map<uint32_t, std::vector<FunctionCall>> funcs;
	std::vector<FunctionCall> viewing_calls;
	CallAnalysis() : UIWindow("Funcs") {
		SetupHook(on_call_function, [this](casioemu::CPU& sender, const FunctionEventArgs& ea) {
			OnCallFunction(sender, ea.pc, ea.lr);
		});
	}
	inline static std::string lookup_symbol(uint32_t addr) {
		auto iter = std::lower_bound(g_labels.begin(), g_labels.end(), addr,
			[](const Label& label, uint32_t addr) { return label.address < addr; });

		if (iter == g_labels.end() || iter->address > addr) {
			if (iter != g_labels.begin())
				--iter;
			else {
				char buf[20];
				return SDL_ltoa(addr, buf, 16);
			}
		}

		if (addr == iter->address) {
			return iter->name;
		}
		else {
			char buf[20];
			return iter->name + "+" + SDL_ltoa(addr - iter->address, buf, 16);
		}
	}
	void OnCallFunction(casioemu::CPU& sender, uint32_t pc, uint32_t lr) {
		if (is_call_recoding) {
			if (check_caller)
				if (lr != caller_v)
					return;

			if (check_callee)
				if (pc != callee_v)
					return;

			FunctionCall fc{};
			fc.xr0 = (sender.reg_r[3] << 24) | (sender.reg_r[2] << 16) | (sender.reg_r[1] << 8) | (sender.reg_r[0]);
			fc.pc = pc;
			fc.lr = lr;
			fc.stack = sender.GetBacktrace(); // 已经上锁了，草（
			funcs[pc].push_back(fc);
		}
	}
	void RenderCore() override {
		if (is_call_recoding) {
			if (ImGui::Button("CallAnalysis.Stop"_lc)) {
				is_call_recoding = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("CallAnalysis.Clear"_lc)) {
				funcs.clear();
			}
			ImGui::Separator();
			if (ImGui::BeginTable("##records", 2, pretty_table)) {
				ImGui::TableSetupColumn("CallAnalysis.Function"_lc,
					ImGuiTableColumnFlags_WidthStretch, 80);
				ImGui::TableSetupColumn("CallAnalysis.CallCount"_lc,
					ImGuiTableColumnFlags_WidthFixed, 80);
				// ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1);
				ImGui::TableHeadersRow();
				for (auto& func : funcs) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(lookup_symbol(func.first).c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%d", (int)func.second.size());
					ImGui::TableNextColumn();
				}
				ImGui::EndTable();
			}
		}
		else {
			if (!viewing_calls.empty()) {
				if (ImGui::Button("CallAnalysis.Close"_lc)) {
					viewing_calls.clear();
					return;
				}
				if (ImGui::BeginTable("##records", 10, pretty_table)) {
					ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40);
					ImGui::TableSetupColumn("CallAnalysis.Function"_lc,
						ImGuiTableColumnFlags_WidthFixed, 80);
					ImGui::TableSetupColumn("CallAnalysis.Caller"_lc,
						ImGuiTableColumnFlags_WidthFixed, 80);
					ImGui::TableSetupColumn("R0", ImGuiTableColumnFlags_WidthFixed, 20);
					ImGui::TableSetupColumn("R1", ImGuiTableColumnFlags_WidthFixed, 20);
					ImGui::TableSetupColumn("R2", ImGuiTableColumnFlags_WidthFixed, 20);
					ImGui::TableSetupColumn("R3", ImGuiTableColumnFlags_WidthFixed, 20);
					ImGui::TableSetupColumn("ER0", ImGuiTableColumnFlags_WidthFixed, 40);
					ImGui::TableSetupColumn("ER2", ImGuiTableColumnFlags_WidthFixed, 40);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1);
					ImGui::TableHeadersRow();
					int i = 0;
					for (auto& func : viewing_calls) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text("%d", i);
						ImGui::TableNextColumn();
						ImGui::Text("%06x", func.pc);
						ImGui::TableNextColumn();
						ImGui::Text("%06x", func.lr);
						ImGui::TableNextColumn();
						ImGui::Text("%02x", func.xr0 & 0xff);
						ImGui::TableNextColumn();
						ImGui::Text("%02x", (func.xr0 >> 8) & 0xff);
						ImGui::TableNextColumn();
						ImGui::Text("%02x", (func.xr0 >> 16) & 0xff);
						ImGui::TableNextColumn();
						ImGui::Text("%02x", (func.xr0 >> 24) & 0xff);
						ImGui::TableNextColumn();
						ImGui::Text("%04x", func.xr0 & 0xffff);
						ImGui::TableNextColumn();
						ImGui::Text("%04x", (func.xr0 >> 16) & 0xffff);
						ImGui::TableNextColumn();
						ImGui::PushID(i++);
						if (ImGui::Button("CallAnalysis.Stacktrace"_lc)) {
							SDL_ShowSimpleMessageBox(0, "CasioEmuMsvc", func.stack.c_str(), 0);
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
				return;
			}
			if (ImGui::Button("CallAnalysis.StartRec"_lc)) {
				is_call_recoding = true;
				funcs.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button("CallAnalysis.Clear"_lc)) {
				funcs.clear();
			}
			ImGui::Separator();
			ImGui::TextUnformatted("CallAnalysis.Filters"_lc);
			ImGui::Checkbox("CallAnalysis.CalleeFilter"_lc,
				&check_callee);
			ImGui::SameLine();
			if (ImGui::InputText("##callee", callee, 260))
				callee_v = strtol(callee, 0, 16);
			ImGui::Checkbox("CallAnalysis.CallerFilter"_lc,
				&check_caller);
			ImGui::SameLine();
			if (ImGui::InputText("##caller", caller, 260))
				caller_v = strtol(caller, 0, 16);
			ImGui::Separator();
			// ImGui::Text("Shows:");
			// ImGui::Checkbox("R0", &r0);
			// ImGui::SameLine();
			// ImGui::Checkbox("R1", &r1);
			// ImGui::SameLine();
			// ImGui::Checkbox("R2", &r2);
			// ImGui::SameLine();
			// ImGui::Checkbox("R3", &r3);
			// ImGui::Checkbox("ER0", &er0);
			// ImGui::SameLine();
			// ImGui::Checkbox("ER2", &er2);
			// ImGui::Separator();
			if (ImGui::BeginTable("##records", 2, pretty_table)) {
				ImGui::TableSetupColumn("CallAnalysis.Function"_lc, ImGuiTableColumnFlags_WidthStretch, 80);
				ImGui::TableSetupColumn("CallAnalysis.CallCount"_lc, ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableHeadersRow();
				int i = 0;
				for (auto& func : funcs) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					if (ImGui::Button(lookup_symbol(func.first).c_str())) {
						viewing_calls = func.second;
					}
					ImGui::TableNextColumn();
					ImGui::Text("%d", (int)func.second.size());
					ImGui::TableNextColumn();
					ImGui::PushID(i++);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
	}
};

UIWindow* CreateCallAnalysisWindow() {
	return new CallAnalysis();
}
