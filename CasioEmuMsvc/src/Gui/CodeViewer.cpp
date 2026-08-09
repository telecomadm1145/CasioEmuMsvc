#include "CodeViewer.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Config.hpp"
#include "Emulator.hpp"
#include "FileDialog.hpp"
#include "Hooks.h"
#include "Logger.hpp"
#include "SysDialog.h"
#include "U8Disas.h"
#include "ePSCpu.h"
#ifdef CASIOEMU_CORE_WEB
#include "WebDebuggerGui.h"
#endif
#include "imgui/imgui.h"
#include <Localization.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ePSDisas.h>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
casioemu::Emulator* m_emu = nullptr;

uint32_t pc_cache = 0;

static bool IsRegister(std::string_view s) {
	// Casio fx-9860G (nX-U8/100) 的常见寄存器列表
	static const char* const regs[] = {
		"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
		"ER0", "ER2", "ER4", "ER6", "ER8", "ER10", "ER12", "ER14",
		"XR0", "XR4", "XR8", "XR12",
		"QR0", "QR8",
		"CR0", "CR1", "CR2", "CR3", "CR4", "CR5", "CR6", "CR7", "CR8", "CR9", "CR10", "CR11", "CR12", "CR13", "CR14", "CR15",
		"CER0", "CER2", "CER4", "CER6", "CER8", "CER10", "CER12", "CER14",
		"CXR0", "CXR4", "CXR8", "CXR12",
		"CQR0", "CQR8",
		"SP", "EA", "PC", "PSW", "EPSW", "ECSR", "ELR", "LR"};
	for (const char* r : regs) {
		if (s == r)
			return true;
	}
	return false;
}

static bool IsImmediate(std::string_view word) {
	if (word.empty())
		return false;
	// 如果是以数字、$开头，或者是形如 -5 的负数，则认定为立即数或地址
	if (std::isdigit(word[0]) || word[0] == '$')
		return true;
	if (word.length() > 1 && word[0] == '-' && (std::isdigit(word[1]) || (word[1] >= 'A' && word[1] <= 'F')))
		return true;

	// 如果全部为 16 进制字符 (U8Disas 格式化出来的通常是大写 A-F)
	for (char c : word) {
		if (!std::isxdigit(c))
			return false;
	}
	return true;
}

static void RenderSyntaxHighlight(const char* text, bool is_label, bool eps6800) {
	if (is_label) {
		// 标号整体着色为高亮黄
		ImGui::TextColored(~ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "%s", text);
		return;
	}

	std::string_view sv(text);
	if (sv.empty()) {
		ImGui::TextUnformatted("");
		return;
	}

	// 定义语法高亮的主题颜色
	ImVec4 col_hex = ~ImVec4(0.5f, 0.5f, 0.5f, 1.0f);	// 机器码：灰色
	ImVec4 col_mnem = ~ImVec4(0.3f, 0.8f, 1.0f, 1.0f);	// 助记符：亮蓝
	ImVec4 col_reg = ~ImVec4(1.0f, 0.6f, 0.6f, 1.0f);	// 寄存器：粉红
	ImVec4 col_imm = ~ImVec4(0.6f, 1.0f, 0.6f, 1.0f);	// 立即数：亮绿
	ImVec4 col_punct = ~ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // 标点符：浅灰
	ImVec4 col_def = ~ImVec4(0.9f, 0.9f, 0.9f, 1.0f);	// 默认符：白色

	struct Token {
		ImVec4 color;
		std::string_view text;
	};
	std::vector<Token> tokens;

	// 前13个字符在你的 U8Disas 生成规则里，必然是机器码的 Hex 区域（固定填充空格）
	size_t hex_len = eps6800 ? 0 : std::min<size_t>(13, sv.length());
	if (hex_len > 0) {
		tokens.push_back({col_hex, sv.substr(0, hex_len)});
		sv.remove_prefix(hex_len);
	}

	bool first_word = true;
	size_t i = 0;
	while (i < sv.length()) {
		if (std::isspace(sv[i])) {
			size_t start = i;
			while (i < sv.length() && std::isspace(sv[i]))
				i++;
			tokens.push_back({col_def, sv.substr(start, i - start)});
		}
		else if (std::isalpha(sv[i]) || sv[i] == '$' || sv[i] == '-' || std::isdigit(sv[i]) || sv[i] == '_' || sv[i] == '.') {
			// 提取词语
			size_t start = i;
			while (i < sv.length() && (std::isalnum(sv[i]) || sv[i] == '$' || sv[i] == '_' || sv[i] == '-' || sv[i] == '.'))
				i++;
			std::string_view word = sv.substr(start, i - start);

			ImVec4 color = col_def;
			if (first_word && std::isalpha(word[0])) { // 第一段非空字母视作指令助记符
				color = col_mnem;
				first_word = false;
			}
			else if (IsRegister(word)) {
				color = col_reg;
			}
			else if (IsImmediate(word)) {
				color = col_imm;
			}
			tokens.push_back({color, word});
		}
		else {
			// 提取标点符号 (比如 , [ ] + :)
			size_t start = i;
			while (i < sv.length() && !std::isalnum(sv[i]) && !std::isspace(sv[i]) && sv[i] != '$' && sv[i] != '-' && sv[i] != '_' && sv[i] != '.')
				i++;
			tokens.push_back({col_punct, sv.substr(start, i - start)});
		}
	}

	// 一次性紧凑渲染所有 Token
	for (size_t t = 0; t < tokens.size(); t++) {
		ImGui::TextColored(tokens[t].color, "%.*s", (int)tokens[t].text.length(), tokens[t].text.data());
		if (t + 1 < tokens.size()) {
			ImGui::SameLine(0, 0); // 并行绘制，消除元素间隙
		}
	}
}

CodeElem CodeViewer::LookUp(uint32_t offset, int* idx) {
	if (codes.empty()) {
		if (idx)
			*idx = 0;
		return {};
	}
	auto it = std::find_if(
		codes.begin(), codes.end(), [&](const CodeElem& a) {
			return a.offset == offset && !a.is_label;
		});
	if (it == codes.end()) {
		it = codes.begin();
	}
	if (idx)
		*idx = it - codes.begin();
	return {.offset = it->offset};
}
CodeViewer* cv_a;

CodeViewer::~CodeViewer() {
	if (disasm_thread.joinable()) {
		disasm_thread.join();
	}
}

static bool Eps6800BranchTarget(casioemu::ePSCPU& cpu, uint32_t pc, uint16_t opcode, uint32_t& target) {
	const uint16_t group = opcode & 0xff00u;
	if ((opcode & 0xfff0u) == 0x0020u || (opcode & 0xfff0u) == 0x0030u) {
		target = (static_cast<uint32_t>(opcode & 0x000fu) << 16) | cpu.ReadCodeWord(pc + 1);
		return true;
	}
	if ((opcode & 0xf000u) == 0x3000u) {
		target = opcode & 0x0fffu;
		return true;
	}
	if ((opcode & 0xe000u) == 0xc000u) {
		target = ((pc + 1) & ~0x1fffu) | (opcode & 0x1fffu);
		return true;
	}
	switch (group) {
	case 0x4700u: case 0x4800u: case 0x4900u:
	case 0x5000u: case 0x5100u:
	case 0x5500u: case 0x5600u: case 0x5700u:
	case 0x5800u: case 0x6000u:
		target = ((pc + 1) & ~0xffffu) | cpu.ReadCodeWord(pc + 1);
		return true;
	default:
		return false;
	}
}

void CodeViewer::SetupHooks() {
	SetupHook(on_instruction,
		[&](casioemu::CPU& cup, InstructionEventArgs& iea) {
			pc_cache = iea.pc_after;
			if (stepping) {
				iea.should_break = true;
				JumpTo(pc_cache);
				stepping = false;
			}
			else if (trace_bp && trace_bp == pc_cache) {
				iea.should_break = true;
				JumpTo(pc_cache);
				trace_bp = 0;
			}
			else if (tracing) { // detect only 01 fx pattern
				auto ins = m_emu->chipset.mmu.ReadCode(iea.pc_before);
				if ((ins & 0xf0ff) == 0xf001) // this is a BL command, we do not consider ill format like dsr:BL xxx
				{
					trace_bp = (iea.pc_before + 4);
				}
				else {
					iea.should_break = true;
					JumpTo(pc_cache);
				}
				tracing = false;
			}
			else if (TryTrigBP(cup.reg_csr, cup.reg_pc)) {
				iea.should_break = true;
			}
			else if (TryTrigBP(cup.reg_csr, cup.reg_pc, false)) {
				iea.should_break = true;
			}
		});
	cv_a = this;
}
void SetDebugbreak(void) {
	if (cv_a) {
		cv_a->ExternalBP();
		m_emu->SetPaused(true);
	}
}



void CodeViewer::PrepareDisasm() {
	disasm_requested = true;
	is_loaded.store(false, std::memory_order_release);
		auto build_disasm = [this]() {
		std::vector<CodeElem> new_codes;
		if (m_emu->chipset.epscpu) {
			std::map<uint32_t, std::string> labels = {
				{0x0000u, "reset"}, {0x0002u, "paint"}, {0x0004u, "reserved_04"},
				{0x0006u, "reserved_06"}, {0x0008u, "tmrxi"}, {0x000au, "reserved_0a"},
				{0x0010u, "test"}};
			for (const auto& label : g_labels) {
				if (label.address < 0x10000u)
					labels[label.address] = label.name;
			}
			std::set<uint32_t> branch_targets;
			new_codes.reserve(0x10000);
			auto ptr = m_emu->chipset.rom_data.data();
			for (size_t i = 0; i < 0x10000; i++) {
				if (i >= 0xc && i <= 0xf) {
					new_codes.push_back(CodeElem{(uint32_t)(i), "<Code Option>", 0, 0});
					continue;
				}
				CodeElem ce{};
				ce.offset = i;
				bool l = false;
				auto str = decodeeps((char*)ptr, i, l);
				strncpy(ce.srcbuf, str, sizeof(ce.srcbuf) - 1);
				ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
				free(str);
				uint32_t target = 0;
				if (Eps6800BranchTarget(*m_emu->chipset.epscpu, static_cast<uint32_t>(i),
						m_emu->chipset.epscpu->ReadCodeWord(static_cast<uint32_t>(i)), target) && target < 0x10000u) {
					ce.xref_operand = static_cast<int>(target);
					branch_targets.insert(target);
				}
				new_codes.push_back(ce);
				if (l)
					i++;
			}
			for (uint32_t target : branch_targets) {
				if (!labels.contains(target)) {
					char name[24]{};
					snprintf(name, sizeof(name), "loc_%04X", target);
					labels.emplace(target, name);
				}
			}
			std::vector<CodeElem> finals;
			finals.reserve(new_codes.size() + labels.size());
			for (auto ce : new_codes) {
				if (auto it = labels.find(ce.offset); it != labels.end()) {
					CodeElem label{};
					label.offset = ce.offset;
					label.is_label = true;
					snprintf(label.srcbuf, sizeof(label.srcbuf), "%s:", it->second.c_str());
					finals.push_back(label);
				}
				if (ce.xref_operand) {
					if (auto it = labels.find(static_cast<uint32_t>(ce.xref_operand)); it != labels.end()) {
						const size_t used = strnlen(ce.srcbuf, sizeof(ce.srcbuf));
						if (used < sizeof(ce.srcbuf) - 4)
							snprintf(ce.srcbuf + used, sizeof(ce.srcbuf) - used, " ; %s", it->second.c_str());
					}
				}
				finals.push_back(ce);
			}
			new_codes = std::move(finals);
			printf("[UI][Info] Finished!\n");
			max_row = static_cast<int>(new_codes.size());
			codes = std::move(new_codes);
			is_loaded.store(true, std::memory_order_release);
		}
		else {
#ifndef _DEBUG
			printf("[UI][Info] Start to disasm ...\n");
			auto dat = std::unique_ptr<uint8_t[]>(new uint8_t[0x80100]);
			std::memset(dat.get(), 0xff, 0x80100);
			std::memcpy(dat.get(), m_emu->chipset.rom_data.data(), std::min((size_t)0x5e000, m_emu->chipset.rom_data.size()));
			if (m_emu->chipset.rom_data.size() >= 0x60000) // TODO: fix this hack!!!
				std::memcpy(dat.get() + 0x70000, m_emu->chipset.rom_data.data() + 0x5e000, 0x2000);
			uint8_t* beg = dat.get();
			auto rom = beg;
			auto end = rom + 0x80000;
			printf("[UI][Info] Pass1: decoding opcodes...\n");
			std::stringstream ss{};
			p_labels.clear();
			new_codes.reserve((end - rom) / 2);
			while (rom < end) {
				auto pc = rom - beg;
				auto before = rom;
				decode(ss, rom, rom - beg);
				auto size = rom - before;
				CodeElem ce{};
				if (size == 2) {
					snprintf(ce.srcbuf, sizeof(ce.srcbuf), "%04X         ", (*(uint16_t*)before));
				}
				else if (size == 4) {
					snprintf(ce.srcbuf, sizeof(ce.srcbuf), "%04X %04X    ", (*(uint16_t*)before), ((uint16_t*)before)[1]);
				}
				else {
					strncpy(ce.srcbuf, "             ", sizeof(ce.srcbuf) - 1);
					ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
				}
				ce.offset = pc;
				auto s = ss.str();
				if (s.size() > 9 && s[8] == '$') {
					ce.xref_operand = SDL_strtol(&s[9], 0, 16);
				}
				{
					size_t start = 9 + 4;
					if (start < sizeof(ce.srcbuf)) {
						strncpy(ce.srcbuf + start, s.c_str(), sizeof(ce.srcbuf) - start - 1);
						ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
					}
				}
				new_codes.push_back(ce);
				ss.str("");
			}
			printf("[UI][Info] Pass2: handling xrefs...\n");
			std::optional<int> last_label{};
			std::unordered_set<int> quick_find{};
			for (auto& ce : new_codes) {
				quick_find.emplace(ce.offset);
			}
			std::map<int, std::string> labels;
			for (auto& lb : p_labels) {
				CodeElem ce{};
				auto iter = quick_find.find(lb.first);
				if (iter == quick_find.end()) // 如果找不到指令那就是错误解码数据了
					continue;
				ce.is_label = true;
				if (lb.second) {
					auto symb = lookup_symbol(lb.first, g_labels);
					strncpy(ce.srcbuf, symb.c_str(), sizeof(ce.srcbuf) - 1);
					ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
					ce.offset = 0;
					labels[lb.first] = std::move(symb);
					last_label = lb.first;
				}
				else {
					if (last_label.has_value()) {
						char buf[20]{};
						auto symb = std::string(".l_") + SDL_itoa(lb.first - *last_label, buf, 16);
						strncpy(ce.srcbuf, symb.c_str(), sizeof(ce.srcbuf) - 1);
						ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
						ce.offset = 0;
						labels[lb.first] = std::move(symb);
					}
				}
			}
			printf("[UI][Info] Pass3: applying xrefs...\n");
			std::vector<CodeElem> finals;
			finals.reserve(new_codes.size() + labels.size());
			for (auto& ce : new_codes) {
				auto iter = labels.find(ce.offset);
				if (iter != labels.end()) {
					CodeElem ce2{};
					ce2.is_label = true;
					{
						auto tmp = iter->second + ":";
						strncpy(ce2.srcbuf, tmp.c_str(), sizeof(ce2.srcbuf) - 1);
						ce2.srcbuf[sizeof(ce2.srcbuf) - 1] = '\0';
					}
					ce2.offset = 0;
					finals.push_back(ce2);
				}
				if (ce.xref_operand) {
					size_t start = 8 + 13;
					if (start < sizeof(ce.srcbuf)) {
						auto& lab = labels[ce.xref_operand];
						strncpy(&ce.srcbuf[start], lab.c_str(), sizeof(ce.srcbuf) - start - 1);
						ce.srcbuf[sizeof(ce.srcbuf) - 1] = '\0';
					}
				}
				finals.push_back(ce);
			}
			new_codes = std::move(finals);
			printf("[UI][Info] Finished!\n");
			max_row = static_cast<int>(new_codes.size());
			codes = std::move(new_codes);
#endif
			is_loaded.store(true, std::memory_order_release);
		}
	};
	if (disasm_thread.joinable()) {
		disasm_thread.join();
	}
	disasm_thread = std::thread(build_disasm);
}

bool CodeViewer::TryTrigBP(uint8_t seg, uint16_t offset, bool bp_mode) {
	for (auto it = break_points.begin(); it != break_points.end(); it++) {
		if (it->second == 1) {
			// TODO: We ignore a second trigger
			CodeElem e = codes[it->first];
			if (e.offset == pc_cache) {
				break_points[it->first] = 2;
				cur_col = it->first;
				need_roll = true;
				return true;
			}
		}
	}
	if (!bp_mode && (debug_flags & DEBUG_STEP || debug_flags & DEBUG_RET_TRACE)) {
		int idx = 0;
		LookUp(pc_cache, &idx);
		break_points[idx] = 2;
		cur_col = idx;
		need_roll = true;
		return true;
	}
	return false;
}

void CodeViewer::ExternalBP() {
	BringToFront();
	JumpTo(pc_cache);
	return;
}

void CodeViewer::DrawContent() {
	ImGuiListClipper c;
	c.Begin(max_row, ImGui::GetTextLineHeight());
	hovered_line = -1;
	while (c.Step()) {
		first_col = c.DisplayStart;
		for (int line_i = c.DisplayStart; line_i < c.DisplayEnd; line_i++) {
			CodeElem e = codes[line_i];
			auto it = break_points.find(line_i);
			auto bb = it == break_points.end();
			if (!e.is_label) {
				if (e.offset == pc_cache) {
					ImGui::TextColored(~ImVec4(0.0, 1.0, 0.0, 1.0), " > ");
				}
				else {
					if (bb) {
						ImGui::Text("   ");
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
							AddBreakpoint(e.offset);
						}
					}
					else {
						if (it->second == 1) {
							ImGui::TextColored(~ImVec4(1.0, 0.0, 0.0, 1.0), " x ");
							if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
								RemoveBreakpoint(e.offset);
							}
							if (auto* eps = m_emu->chipset.epscpu) {
								const std::string popup_id = "##eps_exec_bp_" + std::to_string(line_i);
								if (ImGui::BeginPopupContextItem(popup_id.c_str())) {
									auto details = eps->ExecutionBreakpointDetails();
									auto detail = std::find_if(details.begin(), details.end(), [&](const auto& item) {
										return item.address == e.offset;
									});
									if (detail != details.end()) {
										bool changed = ImGui::Checkbox("Enabled", &detail->enabled);
										int skip_count = static_cast<int>(std::min<uint64_t>(detail->skip_count, 0x7fffffffu));
										if (ImGui::InputInt("Skip count", &skip_count, 1, 10)) {
											detail->skip_count = static_cast<uint64_t>(std::max(skip_count, 0));
											changed = true;
										}
										ImGui::Text("Hit count: %llu", static_cast<unsigned long long>(detail->hit_count));
										if (changed)
											eps->ConfigureExecutionBreakpoint(*detail);
									}
									ImGui::EndPopup();
								}
							}
						}
						else {
							break_points.erase(line_i);
							ImGui::Text("   ");
						}
					}
				}
				ImGui::SameLine();
				ImGui::TextColored(~ImVec4(1.0, 1.0, 0.0, 1.0), "%05x", e.offset);
				ImGui::SameLine();
			}
			ImGui::PushID(line_i);
			bool selected = (last_found_idx == line_i);

			// Draw row background highlights (PC and searched line)
			ImVec2 min_pos = ImGui::GetCursorScreenPos();
			ImVec2 max_pos = ImVec2(min_pos.x + ImGui::GetContentRegionAvail().x, min_pos.y + ImGui::GetTextLineHeight() + 2.0f);
			if (!e.is_label && e.offset == pc_cache) {
				ImGui::GetWindowDrawList()->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(ImVec4(0.0f, 0.8f, 0.0f, 0.12f)));
			}
			else if (selected) {
				ImGui::GetWindowDrawList()->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 0.9f, 0.15f)));
			}

			// 记录当前光标位置，实现控件叠加绘制
			ImVec2 pos = ImGui::GetCursorPos();

			// 绘制占位背景，响应用户点击逻辑（但不使用默认文本显示）
			if (ImGui::Selectable("##sel", selected, ImGuiSelectableFlags_AllowItemOverlap)) {
				if (!e.is_label)
					selected_addr = e.offset;
				if (e.xref_operand)
					JumpTo(e.xref_operand);
			}
			if (!e.is_label && ImGui::IsItemHovered()) {
				hovered_line = line_i;
			}

			// 光标回到本行的原点位置，准备绘制分色的文本内容
			ImGui::SetCursorPos(pos);
			RenderSyntaxHighlight(e.srcbuf, e.is_label, m_emu->chipset.epscpu != nullptr);

			ImGui::PopID();
		}
	}
	if (need_roll) {
		float v = (float)cur_col / max_row * ImGui::GetScrollMaxY(); // 谁写的j7代码啊，跳着都吐了
		auto origv = ImGui::GetScrollY();
		if (v < origv || (v - origv > (ImGui::GetWindowHeight() - 200))) {
			ImGui::SetScrollY(v);
		}
		need_roll = false;
		selected_addr = codes[cur_col].offset;
	}
}

void CodeViewer::DrawMonitor() {
	if (m_emu != nullptr) {
		casioemu::Chipset& chipset = m_emu->chipset;
		std::string s = chipset.cpu.GetBacktrace();
		ImGui::InputTextMultiline("##as", (char*)s.c_str(), s.size(), ImVec2(ImGui::GetWindowWidth(), -1), ImGuiInputTextFlags_ReadOnly);
	}
}

void CodeViewer::JumpTo(uint32_t offset) {
#ifdef CASIOEMU_CORE_WEB
	if (!disasm_requested) {
		PrepareDisasm();
	}
#endif
	int idx = 0;
	// printf("jumpto:seg%d\n",seg);
	LookUp(offset, &idx);
	cur_col = idx;
	need_roll = true;
}

void CodeViewer::Search(bool next) {
	search_failed = false;
	if (codes.empty())
		return;
	std::string needle = search_buf;
	if (needle.empty())
		return;

	// Normalize needle for Hex search
	if (search_mode == 0) { // Hex Pattern
		std::string hex_needle = "";
		for (char c : needle) {
			if (c != ' ')
				hex_needle += toupper(c);
		}
		needle = hex_needle;
	}
	else { // Instruction / Opcode
		   // Just make uppercase for case-insensitive search if desired, or keep as is.
		   // Assuming case-insensitive search for instruction mnemonics.
		std::transform(needle.begin(), needle.end(), needle.begin(), ::toupper);
	}

	int start_idx = next ? (last_found_idx + 1) : 0;
	if (start_idx >= codes.size())
		start_idx = 0;

	// If wrapping around or starting fresh, we loop through all codes starting from start_idx
	for (size_t i = 0; i < codes.size(); ++i) {
		int idx = (start_idx + i) % codes.size();
		const auto& ce = codes[idx];
		if (ce.is_label)
			continue;

		std::string haystack = ce.srcbuf;

		// Split haystack into Hex part and Instruction part
		// Format from PrepareDisasm: "%04X         " or "%04X %04X    " followed by instruction
		// Hex part is roughly first 13 characters.

		std::string hex_part;
		std::string instr_part;
		if (m_emu->chipset.epscpu) {
			char word[5]{};
			std::snprintf(word, sizeof(word), "%04X", m_emu->chipset.epscpu->ReadCodeWord(ce.offset));
			hex_part = word;
			instr_part = haystack;
		}
		else {
			hex_part = haystack.substr(0, 13);
			instr_part = (haystack.length() > 13) ? haystack.substr(13) : "";
		}

		// Clean hex part
		std::string hex_clean = "";
		for (char c : hex_part) {
			if (isalnum(c))
				hex_clean += c;
		}

		bool found = false;
		if (search_mode == 0) { // Hex Pattern
			if (hex_clean.find(needle) != std::string::npos) {
				found = true;
			}
		}
		else { // Instruction
			std::transform(instr_part.begin(), instr_part.end(), instr_part.begin(), ::toupper);
			if (instr_part.find(needle) != std::string::npos) {
				found = true;
			}
		}

		if (found) {
			last_found_idx = idx;
			cur_col = idx;
			need_roll = true;
			return;
		}
	}
	search_failed = true;
}

void CodeViewer::ExportDisassembly() {
#ifdef CASIOEMU_CORE_WEB
	std::filesystem::create_directories(WebDebuggerExportDir());
	const auto path = std::filesystem::path(WebDebuggerExportDir()) / "asm.txt";
	std::ofstream out(path);
	if (out.is_open()) {
		for (const auto& ce : codes) {
			if (ce.is_label) {
				out << ce.srcbuf << "\n";
			}
			else {
				out << "  " << ce.srcbuf << "\n";
			}
		}
		out.close();
		WebDebuggerQueueDownload(path.string().c_str(), "asm.txt");
	}
#else
	SystemDialogs::SaveFileDialog("asm.txt", [&](std::filesystem::path pth) {
		std::ofstream out(pth);
		if (out.is_open()) {
			for (const auto& ce : codes) {
				if (ce.is_label) {
					out << ce.srcbuf << "\n";
				}
				else {
					out << "  " << ce.srcbuf << "\n";
				}
			}
			out.close();
		}
	});
#endif
}
static void ExtractMnemAndOps(const char* src, std::string& mnem, std::string& ops, bool eps6800) {
	std::string_view sv(src);
	mnem.clear();
	ops.clear();

	if (!eps6800) {
		if (sv.length() <= 13)
			return; // 跳过前面固定长度的 Hex 区域
		sv.remove_prefix(13);
	}
	while (!sv.empty() && std::isspace(sv.front()))
		sv.remove_prefix(1);

	// 提取指令名（遇到空格停止匹配指令名）
	size_t end = 0;
	while (end < sv.length() && std::isalpha(sv[end]))
		end++;
	mnem = std::string(sv.substr(0, end));
	for (char& c : mnem)
		c = std::toupper(c);

	// 提取剩下的部分作为操作数
	sv.remove_prefix(end);
	while (!sv.empty() && std::isspace(sv.front()))
		sv.remove_prefix(1);
	ops = std::string(sv);
}
static std::string GetInstructionHelp(const std::string& mnem, const std::string& ops) {
	if (mnem.empty())
		return "CodeViewer.Help.Unknown"_lc;

	std::string help;

	// 1. 基础指令解析
	if (mnem == "MOV")
		help = "CodeViewer.Help.MOV"_lc;
	else if (mnem == "L")
		help = "CodeViewer.Help.L"_lc;
	else if (mnem == "ST")
		help = "CodeViewer.Help.ST"_lc;
	else if (mnem == "ADD")
		help = "CodeViewer.Help.ADD"_lc;
	else if (mnem == "ADDC")
		help = "CodeViewer.Help.ADDC"_lc;
	else if (mnem == "SUB")
		help = "CodeViewer.Help.SUB"_lc;
	else if (mnem == "SUBC")
		help = "CodeViewer.Help.SUBC"_lc;
	else if (mnem == "CMP")
		help = "CodeViewer.Help.CMP"_lc;
	else if (mnem == "CMPC")
		help = "CodeViewer.Help.CMPC"_lc;
	else if (mnem == "AND")
		help = "CodeViewer.Help.AND"_lc;
	else if (mnem == "OR")
		help = "CodeViewer.Help.OR"_lc;
	else if (mnem == "XOR")
		help = "CodeViewer.Help.XOR"_lc;
	else if (mnem == "SLL")
		help = "CodeViewer.Help.SLL"_lc;
	else if (mnem == "SLLC")
		help = "CodeViewer.Help.SLLC"_lc;
	else if (mnem == "SRL")
		help = "CodeViewer.Help.SRL"_lc;
	else if (mnem == "SRLC")
		help = "CodeViewer.Help.SRLC"_lc;
	else if (mnem == "SRA")
		help = "CodeViewer.Help.SRA"_lc;
	else if (mnem == "PUSH")
		help = "CodeViewer.Help.PUSH"_lc;
	else if (mnem == "POP")
		help = "CodeViewer.Help.POP"_lc;
	else if (mnem == "MUL")
		help = "CodeViewer.Help.MUL"_lc;
	else if (mnem == "DIV")
		help = "CodeViewer.Help.DIV"_lc;
	else if (mnem == "LEA")
		help = "CodeViewer.Help.LEA"_lc;
	else if (mnem == "DAA")
		help = "CodeViewer.Help.DAA"_lc;
	else if (mnem == "DAS")
		help = "CodeViewer.Help.DAS"_lc;
	else if (mnem == "NEG")
		help = "CodeViewer.Help.NEG"_lc;
	else if (mnem == "SB")
		help = "CodeViewer.Help.SB"_lc;
	else if (mnem == "RB")
		help = "CodeViewer.Help.RB"_lc;
	else if (mnem == "TB")
		help = "CodeViewer.Help.TB"_lc;
	else if (mnem == "EI")
		help = "CodeViewer.Help.EI"_lc;
	else if (mnem == "DI")
		help = "CodeViewer.Help.DI"_lc;
	else if (mnem == "SC")
		help = "CodeViewer.Help.SC"_lc;
	else if (mnem == "RC")
		help = "CodeViewer.Help.RC"_lc;
	else if (mnem == "CPLC")
		help = "CodeViewer.Help.CPLC"_lc;
	else if (mnem == "SWI")
		help = "CodeViewer.Help.SWI"_lc;
	else if (mnem == "BRK")
		help = "CodeViewer.Help.BRK"_lc;
	else if (mnem == "RT")
		help = "CodeViewer.Help.RT"_lc;
	else if (mnem == "RTI")
		help = "CodeViewer.Help.RTI"_lc;
	else if (mnem == "INC")
		help = "CodeViewer.Help.INC"_lc;
	else if (mnem == "DEC")
		help = "CodeViewer.Help.DEC"_lc;
	else if (mnem == "NOP")
		help = "CodeViewer.Help.NOP"_lc;
	else if (mnem == "EXTBW")
		help = "CodeViewer.Help.EXTBW"_lc;
	else if (mnem == "BL")
		help = "CodeViewer.Help.BL"_lc;
	else if (mnem.front() == 'B') {
		if (mnem == "B") {
			help = "CodeViewer.Help.B"_lc;
		}
		else {
			help = "CodeViewer.Help.BCond"_lc;
			std::string cond = mnem.substr(1);
			help += " (";
			if (cond == "GE")
				help += "CodeViewer.Help.CondGE"_lc;
			else if (cond == "LT")
				help += "CodeViewer.Help.CondLT"_lc;
			else if (cond == "GT")
				help += "CodeViewer.Help.CondGT"_lc;
			else if (cond == "LE")
				help += "CodeViewer.Help.CondLE"_lc;
			else if (cond == "GES")
				help += "CodeViewer.Help.CondGES"_lc;
			else if (cond == "LTS")
				help += "CodeViewer.Help.CondLTS"_lc;
			else if (cond == "GTS")
				help += "CodeViewer.Help.CondGTS"_lc;
			else if (cond == "LES")
				help += "CodeViewer.Help.CondLES"_lc;
			else if (cond == "NE")
				help += "CodeViewer.Help.CondNE"_lc;
			else if (cond == "EQ")
				help += "CodeViewer.Help.CondEQ"_lc;
			else if (cond == "NV")
				help += "CodeViewer.Help.CondNV"_lc;
			else if (cond == "OV")
				help += "CodeViewer.Help.CondOV"_lc;
			else if (cond == "PS")
				help += "CodeViewer.Help.CondPS"_lc;
			else if (cond == "NS")
				help += "CodeViewer.Help.CondNS"_lc;
			else
				help += cond;
			help += ")";
		}
	}
	else {
		help = "CodeViewer.Help.NoHelp"_lc;
	}

	// 2. 寻址模式解析（针对 L, ST, INC, DEC, LEA 等可能包含内存操作的指令）
	if (ops.find("[") != std::string::npos) {
		help += "\n\n";
		help += "CodeViewer.Help.AddrTitle"_lc;
		help += " ";
		if (ops.find("[EA+]") != std::string::npos) {
			help += "CodeViewer.Help.AddrEAPlus"_lc;
		}
		else if (ops.find("[EA]") != std::string::npos) {
			help += "CodeViewer.Help.AddrEA"_lc;
		}
		else {
			size_t brk_start = ops.find("[");
			size_t reg_start = ops.find("ER");
			if (reg_start != std::string::npos && reg_start < brk_start) {
				// 例如：ER12[disp], ER14[disp] -> 寄存器相对寻址
				help += "CodeViewer.Help.AddrRegDisp"_lc;
			}
			else if (reg_start != std::string::npos && reg_start > brk_start) {
				// 例如：[ER0] -> 寄存器间接寻址
				help += "CodeViewer.Help.AddrRegInd"_lc;
			}
			else {
				// 例如：[disp] -> 绝对地址寻址
				help += "CodeViewer.Help.AddrAbs"_lc;
			}
		}
	}
	else if ((mnem == "L" || mnem == "ST") && !ops.empty() && ops.find(",") != std::string::npos) {
		// 如果 L 或 ST 后面没有括号，但是有操作数，可能是 DSR: 或者 直接绝对地址 / 立即数加载
		size_t comma = ops.find(",");
		std::string src_op = ops.substr(comma + 1);
		if (src_op.find("R") == std::string::npos) { // 没有使用寄存器作为源，通常是立即数或地址
			help += "\n\n";
			help += "CodeViewer.Help.AddrTitle"_lc;
			help += " ";
			help += "CodeViewer.Help.AddrImmOrAbs"_lc;
		}
	}

	return help;
}
static int s(bool x) { return x ? 1 : 0; }
void CodeViewer::RenderCore() {
#ifdef CASIOEMU_CORE_WEB
	if (!disasm_requested) {
		PrepareDisasm();
	}
#endif
	int h = ImGui::GetTextLineHeight() + 4;
	int w = ImGui::CalcTextSize("F").x;
	if (!is_loaded.load(std::memory_order_acquire)) {
		ImGui::SetCursorPos(ImVec2(w * 2, h * 5));
		const char* spinner = "|/-\\";
		int idx = (int)(ImGui::GetTime() / 0.15f) % 4;
		ImGui::Text("%c %s", spinner[idx], "CodeViewer.Loading"_lc);
		return;
	}
	if (m_emu->chipset.epscpu) {
		pc_cache = m_emu->chipset.epscpu->ProgramCounter();
	}
	ImVec2 sz;
	h *= 10;
	w *= max_col;
	sz.x = w;
	sz.y = h;
	std::string header = "";
	for (size_t i = first_col; i; i--) {
		if (codes[i].is_label) {
			if (codes[i].srcbuf[0] == '.') {
				if (header.empty()) {
					header = codes[i].srcbuf;
					header.resize(header.size() - 1);
				}
				continue;
			}
			std::string nh = codes[i].srcbuf;
			nh.resize(nh.size() - 1);
			if (header.empty())
				header = ".";
			header = nh + " > " + header;
			break;
		}
	}
	ImGui::TextUnformatted(header.c_str());
	ImGui::Separator();
	ImGui::BeginChild("##scrolling", ImVec2(0, -30 * (1 + s(search_activated) + s(help_activated) * 1.6))); // Adjusted to make space for bottom controls
	DrawContent();
	ImGui::EndChild();
	// ImGui::SameLine(); // ？？？
	ImGui::Separator();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			search_activated = true;
			search_focus = true;
		}
		if (search_activated && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			search_activated = false;
		}
		// F5: Continue / Pause
		if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
			if (m_emu->GetPaused()) {
				RequestContinue();
			}
			else {
				trace_bp = 0;
				stepping = false;
				if (m_emu->chipset.epscpu)
					m_emu->chipset.epscpu->CancelDebugRun();
				m_emu->SetPaused(true);
				JumpTo(pc_cache);
			}
		}
		// F10: Trace (Step Over)
		if (m_emu->GetPaused() && ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
			RequestTrace();
		}
		// F11: Step (Step Into)
		if (m_emu->GetPaused() && ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
			RequestStep();
		}
		// Ctrl+G: Go to PC
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
			JumpTo(pc_cache);
		}
		// F3: Find next
		if (search_activated && ImGui::IsKeyPressed(ImGuiKey_F3, false)) {
			Search(true);
		}
	}
	// Bottom controls
	// First line: Search
	if (search_activated) {
		ImGui::TextUnformatted("CodeViewer.Search"_lc);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		if (search_focus) {
			ImGui::SetKeyboardFocusHere();
			search_focus = false;
		}
		if (ImGui::InputText("##search", search_buf, sizeof(search_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			Search(false);
		}
		if (ImGui::IsItemActive()) {
			search_failed = false;
		}
		ImGui::SameLine();
		const char* items[] = {"CodeViewer.Hex"_lc, "CodeViewer.Inst"_lc};
		ImGui::SetNextItemWidth(100);
		ImGui::Combo("##search_mode", &search_mode, items, IM_ARRAYSIZE(items));
		ImGui::SameLine();
		if (ImGui::Button("CodeViewer.Find"_lc)) {
			Search(false);
		}
		ImGui::SameLine();
		if (UIHelpers::ButtonWithShortcut("CodeViewer.Next"_lc, "F3")) {
			Search(true);
		}
		if (search_failed) {
			ImGui::SameLine();
			ImGui::TextColored(UIHelpers::kColorError, "Not found");
		}
	}
	if (help_activated) {
		ImGui::BeginChild("##help_panel", ImVec2(0, 30*1.5)); // 高度稍微增加以适应多行说明

		std::string help_text = "CodeViewer.Help.DefaultPrompt"_lc;
		if (hovered_line >= 0 && hovered_line < codes.size() && !codes[hovered_line].is_label) {
			std::string mnem, ops;
			ExtractMnemAndOps(codes[hovered_line].srcbuf, mnem, ops, m_emu->chipset.epscpu != nullptr);
			help_text = GetInstructionHelp(mnem, ops);
		}

		// 自动换行显示帮助内容
		ImGui::TextWrapped("%s", help_text.c_str());

		// 靠右放置一个关闭按钮
		ImGui::SameLine(ImGui::GetWindowWidth() - 30);
		if (ImGui::Button("X")) {
			help_activated = false;
		}

		ImGui::EndChild();
	}
	// Second line: Goto and Control
	ImGui::TextUnformatted("CodeViewer.Goto"_lc);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x);
	ImGui::InputText("##input", adrbuf, 8);
	if (adrbuf[0] != '\0' && ImGui::IsItemFocused()) {
		uint32_t addr = strtol(adrbuf, 0, 16);
		JumpTo(addr);
	}
	ImGui::SameLine();
	if (m_emu->GetPaused()) {
		if (UIHelpers::ButtonWithShortcut("CodeViewer.Step"_lc, "F11")) {
			RequestStep();
		}
		ImGui::SameLine();
		if (UIHelpers::ButtonWithShortcut("CodeViewer.Trace"_lc, "F10")) {
			RequestTrace();
		}
		ImGui::SameLine();
		if (UIHelpers::ButtonWithShortcut("CodeViewer.JumpOut"_lc, "Shift+F11")) {
			RequestStepOut();
		}
		ImGui::SameLine();
		if (UIHelpers::ButtonWithShortcut("CodeViewer.Continue"_lc, "F5")) {
			RequestContinue();
		}
		ImGui::SameLine();
		if (m_emu->chipset.epscpu) {
			if (ImGui::Button("Free Run"))
				RequestContinue(false);
			ImGui::SameLine();
			ImGui::BeginDisabled(selected_addr == static_cast<uint32_t>(-1));
			if (ImGui::Button("Run to cursor"))
				RequestRunTo(selected_addr);
			ImGui::EndDisabled();
			ImGui::SameLine();
		}
	}
	else {
		if (UIHelpers::ButtonWithShortcut("CodeViewer.Pause"_lc, "F5")) {
			trace_bp = false;
			stepping = false;
			if (m_emu->chipset.epscpu)
				m_emu->chipset.epscpu->CancelDebugRun();
			m_emu->SetPaused(true);
			JumpTo(pc_cache);
		}
		ImGui::SameLine();
	}
	if (UIHelpers::ButtonWithShortcut("CodeViewer.GotoPC"_lc, "Ctrl+G")) {
		JumpTo(pc_cache);
	}
	ImGui::SameLine();
	if (ImGui::Button("CodeViewer.Export"_lc)) {
		ExportDisassembly();
	}
	ImGui::SameLine();
	ImGui::Checkbox("CodeViewer.ShowHelp"_lc, &help_activated);
}

void CodeViewer::RequestStep() {
	if (m_emu->chipset.epscpu) {
		m_emu->chipset.epscpu->RequestStepInto();
		m_emu->SetPaused(false);
		return;
	}
	stepping = true;
	m_emu->SetPaused(false);
}

void CodeViewer::RequestTrace() {
	if (m_emu->chipset.epscpu) {
		m_emu->chipset.epscpu->RequestStepOver();
		m_emu->SetPaused(false);
		return;
	}
	tracing = true;
	m_emu->SetPaused(false);
}

bool CodeViewer::RequestStepOut() {
	if (m_emu->chipset.epscpu) {
		if (!m_emu->chipset.epscpu->RequestStepOut())
			return false;
		m_emu->SetPaused(false);
		return true;
	}
	auto stk = m_emu->chipset.cpu.stack.get();
	if (stk->empty() || stk->back().is_jump)
		return false;
	trace_bp = stk->back().lr_pushed
		? stk->back().lr
		: (uint32_t)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr;
	m_emu->SetPaused(false);
	return true;
}

void CodeViewer::RequestContinue(bool honor_breakpoints) {
	if (m_emu->chipset.epscpu)
		m_emu->chipset.epscpu->RequestContinue(honor_breakpoints);
	m_emu->SetPaused(false);
}

void CodeViewer::RequestRunTo(uint32_t word_address) {
	if (m_emu->chipset.epscpu) {
		m_emu->chipset.epscpu->RequestRunToAddress(word_address);
		m_emu->SetPaused(false);
	}
}

void CodeViewer::AddBreakpoint(uint32_t address) {
	if (!is_loaded.load(std::memory_order_acquire))
		return;
	auto it = std::find_if(codes.begin(), codes.end(), [address](const CodeElem& line) {
		return !line.is_label && line.offset == address;
	});
	if (it == codes.end())
		return;
	const int idx = static_cast<int>(it - codes.begin());
	break_points[idx] = 1;
	if (m_emu->chipset.epscpu)
		m_emu->chipset.epscpu->AddExecutionBreakpoint(address);
}

void CodeViewer::RemoveBreakpoint(uint32_t address) {
	if (!is_loaded.load(std::memory_order_acquire))
		return;
	auto it = std::find_if(codes.begin(), codes.end(), [address](const CodeElem& line) {
		return !line.is_label && line.offset == address;
	});
	if (it != codes.end())
		break_points.erase(static_cast<int>(it - codes.begin()));
	if (m_emu->chipset.epscpu)
		m_emu->chipset.epscpu->RemoveExecutionBreakpoint(address);
}

void CodeViewer::ClearBreakpoints() {
	break_points.clear();
	if (m_emu->chipset.epscpu)
		m_emu->chipset.epscpu->ClearExecutionBreakpoints();
}

std::vector<uint32_t> CodeViewer::GetBreakpoints() const {
	std::vector<uint32_t> result;
	for (const auto& [index, state] : break_points) {
		if (state == 1 && index >= 0 && static_cast<size_t>(index) < codes.size())
			result.push_back(codes[index].offset);
	}
	return result;
}

std::vector<CodeElem> CodeViewer::GetDisassembly(uint32_t address, size_t count) const {
	std::vector<CodeElem> result;
	if (!is_loaded.load(std::memory_order_acquire) || count == 0)
		return result;
	result.reserve(count);
	for (const auto& line : codes) {
		if (!line.is_label && line.offset < address)
			continue;
		if (line.is_label && result.empty())
			continue;
		result.push_back(line);
		if (!line.is_label && --count == 0)
			break;
	}
	return result;
}
