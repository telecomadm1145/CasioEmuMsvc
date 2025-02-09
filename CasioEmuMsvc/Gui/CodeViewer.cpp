#include "CodeViewer.hpp"
#include "Chipset/CPU.hpp"
#include "Chipset/Chipset.hpp"
#include "Config.hpp"
#include "Emulator.hpp"
#include "Hooks.h"
#include "Logger.hpp"
#include "U8Disas.h"
#include "imgui/imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <iostream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <Localization.h>
casioemu::Emulator* m_emu = nullptr;

uint32_t pc_cache = 0;

CodeElem CodeViewer::LookUp(uint32_t offset, int* idx) {
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

inline static std::string lookup_symbol(uint32_t addr) {
	auto iter = std::lower_bound(g_labels.begin(), g_labels.end(), addr,
		[](const Label& label, uint32_t addr) { return label.address < addr; });

	if (iter == g_labels.end() || iter->address > addr) {
		if (iter != g_labels.begin())
			--iter;
		else {
			char buf[20] = "f_";
			SDL_ltoa(addr, buf + 2, 16);
			return buf;
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

void CodeViewer::PrepareDisasm() {
	std::thread t1([this]() {
#ifndef _DEBUG
		printf("[UI][Info] Start to disasm ...\n");
		auto dat = std::unique_ptr<uint8_t>(new uint8_t[0x80100]);
		std::memset(dat.get(), 0xff, 0x80100);
		std::memcpy(dat.get(), m_emu->chipset.rom_data.data(), std::min((size_t)0x5e000, m_emu->chipset.rom_data.size()));
		if (m_emu->chipset.rom_data.size() >= 0x60000) // TODO: fix this hack!!!
			std::memcpy(dat.get() + 0x70000, m_emu->chipset.rom_data.data() + 0x5e000, 0x2000);
		uint8_t* beg = dat.get();
		auto rom = beg;
		auto end = rom + 0x80000;
		printf("[UI][Info] Pass1: decoding opcodes...\n");
		std::stringstream ss{};
		while (rom < end) {
			auto pc = rom - beg;
			auto before = rom;
			decode(ss, rom, rom - beg);
			auto size = rom - before;
			CodeElem ce{};
			if (size == 2) {
				sprintf(ce.srcbuf, "%04X         ", (*(uint16_t*)before));
			}
			else if (size == 4) {
				sprintf(ce.srcbuf, "%04X %04X    ", (*(uint16_t*)before), ((uint16_t*)before)[1]);
			}
			else {
				strcpy(ce.srcbuf, "             ");
			}
			ce.offset = pc;
			auto s = ss.str();
			if (s[8] == '$') {
				ce.xref_operand = SDL_strtol(&s[9], 0, 16);
			}
			strcpy(ce.srcbuf + 9 + 4, s.c_str());
			codes.push_back(ce);
			ss.str("");
		}
		printf("[UI][Info] Pass2: handling xrefs...\n");
		std::optional<int> last_label{};
		std::unordered_set<int> quick_find{};
		for (auto& ce : codes) {
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
				auto symb = lookup_symbol(lb.first);
				strcpy(ce.srcbuf, symb.c_str());
				ce.offset = 0;
				labels[lb.first] = std::move(symb);
				last_label = lb.first;
			}
			else {
				if (last_label.has_value()) {
					char buf[20]{};
					auto symb = std::string(".l_") + SDL_itoa(lb.first - *last_label, buf, 16);
					strcpy(ce.srcbuf, symb.c_str());
					ce.offset = 0;
					labels[lb.first] = std::move(symb);
				}
			}
		}
		printf("[UI][Info] Pass3: applying xrefs...\n");
		std::vector<CodeElem> finals;
		finals.reserve(codes.size() + labels.size());
		for (auto& ce : codes) {
			auto iter = labels.find(ce.offset);
			if (iter != labels.end()) {
				CodeElem ce2{};
				ce2.is_label = true;
				strcpy(ce2.srcbuf, (iter->second + ":").c_str());
				ce2.offset = 0;
				finals.push_back(ce2);
			}
			if (ce.xref_operand) {
				strcpy(&ce.srcbuf[8 + 13], labels[ce.xref_operand].c_str());
			}
			finals.push_back(ce);
		}
		codes = std::move(finals);
		printf("[UI][Info] Finished!\n");
		max_row = codes.size();
#endif
		is_loaded = true;
	});
	t1.detach();
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
	JumpTo(pc_cache);
	return;
}

void CodeViewer::DrawContent() {
	ImGuiListClipper c;
	c.Begin(max_row, ImGui::GetTextLineHeight());
	while (c.Step()) {
		first_col = c.DisplayStart;
		for (int line_i = c.DisplayStart; line_i < c.DisplayEnd; line_i++) {
			CodeElem e = codes[line_i];
			auto it = break_points.find(line_i);
			auto bb = it == break_points.end();
			if (!e.is_label) {
				if (e.offset == pc_cache) {
					ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), " > ");
				}
				else {
					if (bb) {
						ImGui::Text("   ");
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
							break_points[line_i] = 1;
						}
					}
					else {
						if (it->second == 1) {
							ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), " x ");
							if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
								break_points.erase(line_i);
							}
						}
						else {
							break_points.erase(line_i);
							ImGui::Text("   ");
						}
					}
				}
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0, 1.0, 0.0, 1.0), "%05x", e.offset);
				ImGui::SameLine();
			}
			ImGui::PushID(line_i);
			if (ImGui::Selectable(e.srcbuf)) {
				if (e.xref_operand)
					JumpTo(e.xref_operand);
			}
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
	int idx = 0;
	// printf("jumpto:seg%d\n",seg);
	LookUp(offset, &idx);
	cur_col = idx;
	need_roll = true;
}

void CodeViewer::RenderCore() {

	int h = ImGui::GetTextLineHeight() + 4;
	int w = ImGui::CalcTextSize("F").x;
	if (!is_loaded) {
		ImGui::SetCursorPos(ImVec2(w * 2, h * 5));
		ImGui::TextUnformatted("CodeViewer.Loading"_lc);
		return;
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
	ImGui::BeginChild("##scrolling", ImVec2(0, -25));
	DrawContent();
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::Separator();
	ImGui::TextUnformatted("CodeViewer.Goto"_lc);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("000000").x);
	ImGui::InputText("##input", adrbuf, 8);
	if (adrbuf[0] != '\0' && ImGui::IsItemFocused()) {
		uint32_t addr = std::stoi(adrbuf, 0, 16);
		JumpTo(addr);
	}
	ImGui::SameLine();
	if (m_emu->GetPaused()) {
		if (ImGui::Button("CodeViewer.Step"_lc)) {
			stepping = true;
			m_emu->SetPaused(false);
		}
		ImGui::SameLine();
		if (ImGui::Button("CodeViewer.Trace"_lc)) {
			tracing = true;
			m_emu->SetPaused(false);
		}
		ImGui::SameLine();
		if (ImGui::Button("CodeViewer.JumpOut"_lc)) {
			auto stk = m_emu->chipset.cpu.stack.get();
			if (!stk->empty()) {
				if (!stk->back().is_jump) {
					if (stk->back().lr_pushed) {
						trace_bp = stk->back().lr;
					}
					else {
						trace_bp = m_emu->chipset.cpu.reg_lcsr << 16 | m_emu->chipset.cpu.reg_lr;
					}
					m_emu->SetPaused(false);
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("CodeViewer.Continue"_lc)) {
			m_emu->SetPaused(false);
		}
		ImGui::SameLine();
	}
	else {
		if (ImGui::Button("CodeViewer.Pause"_lc)) {
			trace_bp = false;
			stepping = false;
			m_emu->SetPaused(true);
			JumpTo(pc_cache);
		}
		ImGui::SameLine();
	}
	if (ImGui::Button("CodeViewer.GotoPC"_lc)) {
		JumpTo(pc_cache);
	}
}