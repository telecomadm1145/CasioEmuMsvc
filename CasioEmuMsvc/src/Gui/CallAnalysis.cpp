#include "CallAnalysis.h"
#include "Chipset/CPU.hpp"
#include "Hooks.h"
#include "Ui.hpp"
#include "imgui/imgui.h"
#include <Localization.h>

struct CallAnalysis : public UIWindow {
	std::mutex data_mutex;
	bool is_call_recoding = false;
	bool check_caller = false;
	std::string message;
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

	void OnCallFunction(casioemu::CPU& sender, uint32_t pc, uint32_t lr) {
		std::lock_guard lock(data_mutex);
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

	void DebugStart(bool filterCaller, uint32_t callerAddress, bool filterCallee, uint32_t calleeAddress) {
		std::lock_guard lock(data_mutex);
		check_caller = filterCaller;
		caller_v = callerAddress;
		check_callee = filterCallee;
		callee_v = calleeAddress;
		funcs.clear();
		is_call_recoding = true;
	}

	void DebugStop() {
		std::lock_guard lock(data_mutex);
		is_call_recoding = false;
	}

	void DebugClear() {
		std::lock_guard lock(data_mutex);
		funcs.clear();
		viewing_calls.clear();
		message.clear();
	}

	std::vector<DebugFunctionCallInfo> DebugList(uint32_t function) {
		std::lock_guard lock(data_mutex);
		std::vector<DebugFunctionCallInfo> result;
		for (const auto& [address, calls] : funcs) {
			if (function && function != address)
				continue;
			for (const auto& call : calls)
				result.push_back({call.pc, call.lr, call.xr0, call.stack});
		}
		return result;
	}

	void RenderCore() override {
		std::lock_guard lock(data_mutex);
		if (message.size()) {
			if (ImGui::Button("CallAnalysis.Close"_lc)) {
				message.clear();
				return;
			}
			ImGui::TextUnformatted(message.c_str());
			return;
		}
		if (is_call_recoding) {
			if (ImGui::Button("CallAnalysis.Stop"_lc)) {
				is_call_recoding = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("CallAnalysis.Clear"_lc)) {
				funcs.clear();
			}
			ImGui::Separator();
			
			if (funcs.empty()) {
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, UIHelpers::kColorMuted);
				ImGui::TextWrapped("Recording... Waiting for function calls to be triggered.");
				ImGui::PopStyleColor();
			}
			else {
				if (ImGui::BeginTable("##records", 2, pretty_table)) {
					ImGui::TableSetupColumn("CallAnalysis.Function"_lc,
						ImGuiTableColumnFlags_WidthStretch, 80);
					ImGui::TableSetupColumn("CallAnalysis.CallCount"_lc,
						ImGuiTableColumnFlags_WidthFixed, 80);
					ImGui::TableHeadersRow();
					for (auto& func : funcs) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(lookup_symbol(func.first, g_labels).c_str());
						ImGui::TableNextColumn();
						ImGui::Text("%d", (int)func.second.size());
					}
					ImGui::EndTable();
				}
			}
		}
		else {
			if (!viewing_calls.empty()) {
				if (ImGui::Button("CallAnalysis.Close"_lc)) {
					viewing_calls.clear();
					return;
				}
				if (ImGui::BeginTable("##records2", 10, pretty_table)) {
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
						UIHelpers::ClickableAddress(func.pc);
						ImGui::TableNextColumn();
						UIHelpers::ClickableAddress(func.lr);
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
							message = func.stack;
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

			if (funcs.empty()) {
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, UIHelpers::kColorMuted);
				ImGui::TextWrapped("Click 'Start Recording' above to begin capturing function calls.");
				ImGui::PopStyleColor();
			}
			else {
				if (ImGui::BeginTable("##records", 2, pretty_table)) {
					ImGui::TableSetupColumn("CallAnalysis.Function"_lc, ImGuiTableColumnFlags_WidthStretch, 80);
					ImGui::TableSetupColumn("CallAnalysis.CallCount"_lc, ImGuiTableColumnFlags_WidthFixed, 80);
					ImGui::TableHeadersRow();
					int i = 0;
					for (auto& func : funcs) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						if (ImGui::Button(lookup_symbol(func.first, g_labels).c_str())) {
							viewing_calls = func.second;
						}
						ImGui::TableNextColumn();
						ImGui::Text("%d", (int)func.second.size());
					}
					ImGui::EndTable();
				}
			}
		}
	}
};

static CallAnalysis* g_callAnalysis = nullptr;

UIWindow* CreateCallAnalysisWindow() {
	g_callAnalysis = new CallAnalysis();
	return g_callAnalysis;
}

void DebugStartCallRecording(bool filterCaller, uint32_t caller, bool filterCallee, uint32_t callee) {
	if (g_callAnalysis)
		g_callAnalysis->DebugStart(filterCaller, caller, filterCallee, callee);
}

void DebugStopCallRecording() {
	if (g_callAnalysis)
		g_callAnalysis->DebugStop();
}

void DebugClearCallRecording() {
	if (g_callAnalysis)
		g_callAnalysis->DebugClear();
}

std::vector<DebugFunctionCallInfo> DebugGetFunctionCalls(uint32_t function) {
	return g_callAnalysis ? g_callAnalysis->DebugList(function) : std::vector<DebugFunctionCallInfo>{};
}
