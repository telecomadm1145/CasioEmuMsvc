#include "RopTool.h"
#include "Chipset/Chipset.hpp"
#include "CodeViewer.hpp" // for code_viewer->JumpTo
#include "Emulator.hpp"
#include "imgui/imgui.h"
#include <Localization.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>

extern CodeViewer* code_viewer;
// =========================================================
// GadgetInfo helpers
// =========================================================
std::string GadgetInfo::GetDisasmString() const {
	std::string r;
	for (auto& i : insns) {
		if (!r.empty())
			r += "; ";
		r += i.disasm;
	}
	return r;
}

std::string GadgetInfo::GetHexString() const {
	std::string r;
	char buf[8];
	for (auto& i : insns) {
		for (int b = 0; b < i.byte_size / 2; b++) {
			snprintf(buf, sizeof(buf), "%04X ", i.raw_bytes[b]);
			r += buf;
		}
	}
	if (!r.empty() && r.back() == ' ')
		r.pop_back();
	return r;
}

static const char* s_type_names[] = {
	"POP+RT", "POP PC", "MOV+RT", "LOAD+RT", "STORE+RT",
	"SP+RT", "ARITH+RT", "LEA+RT", "OTHER+RT", "SWI"};

std::string GadgetInfo::GetTypeString() const {
	if ((int)type < 10)
		return s_type_names[(int)type];
	return "?";
}

int GadgetInfo::ClobberCount() const {
	int c = 0;
	for (int i = 0; i < 16; i++)
		if (regs_written & (1 << i))
			c++;
	return c;
}

std::string GadgetInfo::GetClobberString() const {
	std::string r;
	for (int i = 0; i < 16; i++) {
		if (regs_written & (1 << i)) {
			if (!r.empty())
				r += ",";
			r += "R" + std::to_string(i);
		}
	}
	if (modifies_sp) {
		if (!r.empty())
			r += ",";
		r += "SP";
	}
	if (modifies_lr) {
		if (!r.empty())
			r += ",";
		r += "LR";
	}
	if (modifies_ea) {
		if (!r.empty())
			r += ",";
		r += "EA";
	}
	return r;
}

// =========================================================
// nU8 Instruction Decoder (for effect analysis)
// =========================================================
static InsnEffect DecodeInsnEffect(const uint8_t* rom, uint32_t pc, int rom_size) {
	InsnEffect e{};
	e.address = pc;
	e.byte_size = 2;
	if (pc + 1 >= (uint32_t)rom_size)
		return e;

	uint8_t b0 = rom[pc], b1 = rom[pc + 1];
	e.raw_bytes[0] = b0 | (b1 << 8);

	auto set_disasm = [&](const char* fmt, ...) {
		va_list args;
		va_start(args, fmt);
		vsnprintf(e.disasm, sizeof(e.disasm), fmt, args);
		va_end(args);
	};

	// RT
	if (b0 == 0x1F && b1 == 0xFE) {
		set_disasm("RT");
		e.is_ret = true;
		e.reads_lr = true;
		return e;
	}
	// RTI
	if (b0 == 0x0F && b1 == 0xFE) {
		set_disasm("RTI");
		e.is_ret = true;
		e.reads_sp = true;
		e.writes_sp = true;
		e.writes_psw = true;
		return e;
	}
	// POP Rn
	if (b0 == 0x0E && (b1 & 0xF0) == 0xF0) {
		int n = b1 & 0xF;
		set_disasm("POP R%d", n);
		e.regs_written = 1 << n;
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = 2;
		return e;
	}
	// POP ERn
	if (b0 == 0x1E && (b1 & 0xF1) == 0xF0) {
		int n = (b1 >> 1) & 7;
		set_disasm("POP ER%d", n * 2);
		e.regs_written = (3 << (n * 2));
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = 2;
		return e;
	}
	// POP XRn
	if (b0 == 0x2E && (b1 & 0xF3) == 0xF0) {
		int n = (b1 >> 2) & 3;
		set_disasm("POP XR%d", n * 4);
		e.regs_written = (0xF << (n * 4));
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = 4;
		return e;
	}
	// POP QRn
	if (b0 == 0x3E && (b1 & 0xF7) == 0xF0) {
		int n = (b1 >> 3) & 1;
		set_disasm("POP QR%d", n * 8);
		e.regs_written = (0xFF << (n * 8));
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = 8;
		return e;
	}
	// POP special (LR, EA, PSW, PC)
	if (b0 == 0x8E && (b1 & 0xF0) == 0xF0) {
		int a = b1 & 1, l = (b1 >> 3) & 1, es = (b1 >> 2) & 1, p = (b1 >> 1) & 1;
		std::string s = "POP ";
		int delta = 0;
		if (a) {
			s += "EA ";
			e.writes_ea = true;
			delta += 2;
		}
		if (l) {
			s += "LR ";
			e.writes_lr = true;
			delta += 2;
		}
		if (es) {
			s += "PSW ";
			e.writes_psw = true;
			delta += 2;
		}
		if (p) {
			s += "PC ";
			e.is_ret = true;
			delta += 2;
		}
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = delta;
		strncpy(e.disasm, s.c_str(), sizeof(e.disasm) - 1);
		return e;
	}
	// PUSH Rn
	if (b0 == 0x4E && (b1 & 0xF0) == 0xF0) {
		int n = b1 & 0xF;
		set_disasm("PUSH R%d", n);
		e.regs_read = 1 << n;
		e.reads_sp = true;
		e.writes_sp = true;
		e.writes_mem = true;
		e.sp_delta = -2;
		return e;
	}
	// PUSH ERn
	if (b0 == 0x5E && (b1 & 0xF1) == 0xF0) {
		int n = (b1 >> 1) & 7;
		set_disasm("PUSH ER%d", n * 2);
		e.regs_read = (3 << (n * 2));
		e.reads_sp = true;
		e.writes_sp = true;
		e.writes_mem = true;
		e.sp_delta = -2;
		return e;
	}
	// PUSH special (LR, EA, EPSW, ELR)
	if (b0 == 0xCE && (b1 & 0xF0) == 0xF0) {
		set_disasm("PUSH special");
		e.reads_sp = true;
		e.writes_sp = true;
		e.writes_mem = true;
		int cnt = 0;
		for (int i = 0; i < 4; i++)
			if (b1 & (1 << i))
				cnt++;
		e.sp_delta = -(int8_t)(cnt * 2);
		return e;
	}
	// ADD SP, #imm
	if ((b1 & 0xFF) == 0xE1) {
		int8_t imm = (int8_t)b0;
		set_disasm("ADD SP, %d", (int)imm);
		e.reads_sp = true;
		e.writes_sp = true;
		e.sp_delta = imm;
		return e;
	}
	// MOV Rn, Rm
	if ((b0 & 0x0F) == 0x00 && (b1 & 0xF0) == 0x80) {
		int m = (b0 >> 4) & 0xF, n = b1 & 0xF;
		set_disasm("MOV R%d, R%d", n, m);
		e.regs_read = 1 << m;
		e.regs_written = 1 << n;
		return e;
	}
	// MOV Rn, #imm
	if ((b1 & 0xF0) == 0x00) {
		int n = b1 & 0xF;
		set_disasm("MOV R%d, #%02X", n, b0);
		e.regs_written = 1 << n;
		return e;
	}
	// MOV ERn, ERm
	if ((b0 & 0x1F) == 0x05 && (b1 & 0xF1) == 0xF0) {
		int m = (b0 >> 5) & 7, n = (b1 >> 1) & 7;
		set_disasm("MOV ER%d, ER%d", n * 2, m * 2);
		e.regs_read = 3 << (m * 2);
		e.regs_written = 3 << (n * 2);
		return e;
	}
	// MOV ER0, SP (for pivot)
	if (b0 == 0x1A && (b1 & 0xF1) == 0xA0) {
		int n = (b1 >> 1) & 7;
		set_disasm("MOV ER%d, SP", n * 2);
		e.reads_sp = true;
		e.regs_written = 3 << (n * 2);
		return e;
	}
	// MOV SP, ERn (stack pivot)
	if ((b0 & 0x1F) == 0x0A && b1 == 0xA1) {
		int m = (b0 >> 5) & 7;
		set_disasm("MOV SP, ER%d", m * 2);
		e.regs_read = 3 << (m * 2);
		e.writes_sp = true;
		return e;
	}
	// ADD Rn, Rm
	if ((b0 & 0x0F) == 0x01 && (b1 & 0xF0) == 0x80) {
		int m = (b0 >> 4) & 0xF, n = b1 & 0xF;
		set_disasm("ADD R%d, R%d", n, m);
		e.regs_read = (1 << m) | (1 << n);
		e.regs_written = 1 << n;
		e.writes_psw = true;
		return e;
	}
	// ADD Rn, #imm
	if ((b1 & 0xF0) == 0x10) {
		int n = b1 & 0xF;
		set_disasm("ADD R%d, #%02X", n, b0);
		e.regs_read = 1 << n;
		e.regs_written = 1 << n;
		e.writes_psw = true;
		return e;
	}
	// SUB Rn, Rm
	if ((b0 & 0x0F) == 0x08 && (b1 & 0xF0) == 0x80) {
		int m = (b0 >> 4) & 0xF, n = b1 & 0xF;
		set_disasm("SUB R%d, R%d", n, m);
		e.regs_read = (1 << m) | (1 << n);
		e.regs_written = 1 << n;
		e.writes_psw = true;
		return e;
	}
	// L Rn, [ERm]
	if ((b0 & 0x1F) == 0x00 && (b1 & 0xF0) == 0x90) {
		int m = (b0 >> 5) & 7, n = b1 & 0xF;
		set_disasm("L R%d, [ER%d]", n, m * 2);
		e.regs_read = 3 << (m * 2);
		e.regs_written = 1 << n;
		e.reads_mem = true;
		return e;
	}
	// ST Rn, [ERm]
	if ((b0 & 0x1F) == 0x01 && (b1 & 0xF0) == 0x90) {
		int m = (b0 >> 5) & 7, n = b1 & 0xF;
		set_disasm("ST R%d, [ER%d]", n, m * 2);
		e.regs_read = (1 << n) | (3 << (m * 2));
		e.writes_mem = true;
		return e;
	}
	// LEA [ERn]
	if ((b0 & 0x1F) == 0x0A && b1 == 0xF0) {
		int m = (b0 >> 5) & 7;
		set_disasm("LEA [ER%d]", m * 2);
		e.regs_read = 3 << (m * 2);
		e.writes_ea = true;
		return e;
	}
	// L/ST R/ER via [EA]
	if (b0 == 0x30 && (b1 & 0xF0) == 0x90) {
		int n = b1 & 0xF;
		set_disasm("L R%d, [EA]", n);
		e.reads_ea = true;
		e.reads_mem = true;
		e.regs_written = 1 << n;
		return e;
	}
	if (b0 == 0x31 && (b1 & 0xF0) == 0x90) {
		int n = b1 & 0xF;
		set_disasm("ST R%d, [EA]", n);
		e.reads_ea = true;
		e.writes_mem = true;
		e.regs_read = 1 << n;
		return e;
	}
	// Conditional branch Bcc
	if ((b1 & 0xF0) == 0xC0) {
		int c = b1 & 0xF;
		int8_t rel = (int8_t)b0;
		uint32_t target = (pc & 0x0F0000) | (uint16_t)(pc + 2 + (rel << 1));
		if (c == 15) {
			set_disasm("NOP");
		}
		else if (c == 14) {
			set_disasm("B 0x%05X", target);
			e.is_branch = true;
			e.branch_target = target;
		}
		else {
			static const char* conds[] = {
				"GE", "LT", "GT", "LE", "GES", "LTS", "GTS", "LES",
				"NE", "EQ", "NV", "OV", "PS", "NS", "", ""};
			set_disasm("B%s 0x%05X", conds[c], target);
			e.is_branch = true;
			e.branch_target = target;
			e.reads_psw = true;
		}
		return e;
	}
	// SWI
	if ((b0 & 0xC0) == 0x00 && b1 == 0xE5) {
		int imm = b0 & 0x3F;
		set_disasm("SWI %d", imm);
		e.reads_sp = true;
		e.writes_sp = true;
		return e;
	}
	// DI
	if (b0 == 0xF7 && b1 == 0xEB) {
		set_disasm("DI");
		e.writes_psw = true;
		return e;
	}
	// EI
	if (b0 == 0x08 && b1 == 0xED) {
		set_disasm("EI");
		e.writes_psw = true;
		return e;
	}
	// BL (4-byte)
	if (b0 == 0x01 && (b1 & 0xF0) == 0xF0 && pc + 3 < (uint32_t)rom_size) {
		uint8_t b2 = rom[pc + 2], b3 = rom[pc + 3];
		int g = b1 & 0xF;
		uint32_t addr = (g << 16) | (b3 << 8) | b2;
		set_disasm("BL 0x%05X", addr);
		e.byte_size = 4;
		e.raw_bytes[1] = b2 | (b3 << 8);
		e.writes_lr = true;
		return e;
	}
	// B (4-byte)
	if (b0 == 0x00 && (b1 & 0xF0) == 0xF0 && pc + 3 < (uint32_t)rom_size) {
		uint8_t b2 = rom[pc + 2], b3 = rom[pc + 3];
		int g = b1 & 0xF;
		uint32_t addr = (g << 16) | (b3 << 8) | b2;
		set_disasm("B 0x%05X", addr);
		e.byte_size = 4;
		e.raw_bytes[1] = b2 | (b3 << 8);
		e.is_branch = true;
		e.branch_target = addr;
		return e;
	}
	// NOP
	if (b0 == 0x8F && b1 == 0xFE) {
		set_disasm("NOP");
		return e;
	}
	// Default: unknown
	set_disasm("dw %04X", b0 | (b1 << 8));
	return e;
}

// =========================================================
// Gadget Scoring
// =========================================================
static int ScoreGadget(const GadgetInfo& g, const std::set<uint8_t>& bad_bytes,
	uint16_t desired_writes, const uint8_t* rom) {
	int sc = (int)g.insns.size() * 10;
	// Count clobbered regs beyond desired
	int clobber = 0;
	for (int i = 0; i < 16; i++) {
		if ((g.regs_written & (1 << i)) && !(desired_writes & (1 << i)))
			clobber++;
	}
	sc += clobber * 15;
	sc += std::abs((int)g.sp_delta) * 5;
	if (g.modifies_psw)
		sc += 3;
	if (g.modifies_ea)
		sc += 2;
	// Check bad bytes in address
	if (!bad_bytes.empty()) {
		uint8_t ab[4] = {
			(uint8_t)(g.address), (uint8_t)(g.address >> 8),
			(uint8_t)(g.address >> 16), 0};
		for (int i = 0; i < 3; i++) {
			if (bad_bytes.count(ab[i])) {
				sc += 100;
				break;
			}
		}
	}
	// Unintended memory writes
	for (auto& ins : g.insns) {
		if (ins.writes_mem && !ins.is_ret)
			sc += 20;
	}
	return sc;
}

// =========================================================
// Gadget Finder
// =========================================================
static void FindGadgets(const uint8_t* rom, int rom_size, int max_depth,
	std::vector<GadgetInfo>& out) {
	out.clear();
	std::set<uint32_t> seen;

	for (uint32_t pc = 0; pc + 1 < (uint32_t)rom_size; pc += 2) {
		uint8_t b0 = rom[pc], b1 = rom[pc + 1];
		bool is_rt = (b0 == 0x1F && b1 == 0xFE);
		bool is_pop_pc = (b0 == 0x8E && (b1 & 0xF0) == 0xF0 && (b1 & 0x02));
		if (!is_rt && !is_pop_pc)
			continue;

		// Try gadgets of increasing depth ending at this RT/POP PC
		for (int depth = 1; depth <= max_depth; depth++) {
			uint32_t start = pc - (depth - 1) * 2;
			if (start > pc)
				break; // underflow
			if (seen.count(start))
				continue;

			// Decode instruction sequence
			std::vector<InsnEffect> insns;
			uint32_t cur = start;
			bool valid = true;
			while (cur <= pc && valid) {
				auto eff = DecodeInsnEffect(rom, cur, rom_size);
				insns.push_back(eff);
				cur += eff.byte_size;
				// If we hit a branch/ret before the end, this sequence is invalid
				if ((eff.is_ret || eff.is_branch) && cur <= pc) {
					valid = false;
				}
			}
			if (!valid || insns.empty())
				continue;
			if (!insns.back().is_ret)
				continue;

			GadgetInfo gi{};
			gi.address = start;
			gi.insns = std::move(insns);

			// Aggregate effects
			for (auto& ins : gi.insns) {
				gi.regs_read |= ins.regs_read;
				gi.regs_written |= ins.regs_written;
				if (ins.writes_sp)
					gi.modifies_sp = true;
				if (ins.writes_lr)
					gi.modifies_lr = true;
				if (ins.writes_ea)
					gi.modifies_ea = true;
				if (ins.writes_psw)
					gi.modifies_psw = true;
				gi.sp_delta += ins.sp_delta;
			}

			// Count pops
			for (auto& ins : gi.insns) {
				if (ins.sp_delta > 0 && ins.reads_sp)
					gi.pop_count++;
			}
			gi.controllable_pc = is_pop_pc;

			// Classify type
			if (is_pop_pc)
				gi.type = GadgetInfo::GT_POP_PC;
			else if (gi.insns.size() >= 2) {
				auto& first = gi.insns[0];
				if (first.sp_delta > 0 && first.reads_sp)
					gi.type = GadgetInfo::GT_POP_RT;
				else if (first.reads_mem)
					gi.type = GadgetInfo::GT_LOAD_RT;
				else if (first.writes_mem)
					gi.type = GadgetInfo::GT_STORE_RT;
				else if (first.writes_sp && !first.reads_sp)
					gi.type = GadgetInfo::GT_ADD_SP_RT;
				else if (first.writes_ea)
					gi.type = GadgetInfo::GT_LEA_RT;
				else
					gi.type = GadgetInfo::GT_CUSTOM_RT;
			}
			else {
				gi.type = GadgetInfo::GT_CUSTOM_RT;
			}

			gi.score = ScoreGadget(gi, {}, 0, rom);
			seen.insert(start);
			out.push_back(std::move(gi));
		}
	}
	std::sort(out.begin(), out.end(),
		[](const GadgetInfo& a, const GadgetInfo& b) { return a.score < b.score; });
}

// =========================================================
// DAG Analysis
// =========================================================
static DagAnalysis AnalyzeChain(const std::vector<const GadgetInfo*>& chain) {
	DagAnalysis dag;
	if (chain.empty())
		return dag;

	// Build nodes
	for (int i = 0; i < (int)chain.size(); i++) {
		DagNode node;
		node.gadget_idx = i;
		node.gadget = chain[i];
		// Default successor
		if (i + 1 < (int)chain.size()) {
			node.false_successor = i + 1;
		}
		// Check for branch inside gadget
		for (auto& ins : chain[i]->insns) {
			if (ins.is_branch && !ins.is_ret) {
				node.is_branch_point = true;
				// Branch target: try to find matching gadget
				for (int j = 0; j < (int)chain.size(); j++) {
					if (chain[j]->address == ins.branch_target) {
						node.true_successor = j;
						break;
					}
				}
			}
		}
		dag.nodes.push_back(node);
	}

	// Build edges
	for (int i = 0; i < (int)dag.nodes.size(); i++) {
		if (dag.nodes[i].false_successor >= 0)
			dag.edges.push_back({i, dag.nodes[i].false_successor});
		if (dag.nodes[i].true_successor >= 0)
			dag.edges.push_back({i, dag.nodes[i].true_successor});
	}

	// Forward propagation
	auto propagate = [&](DagNode& node, const DagNode::RegState prev_regs[16], int prev_sp) {
		std::copy(prev_regs, prev_regs + 16, node.reg_state_in);
		std::copy(prev_regs, prev_regs + 16, node.reg_state_out);
		node.sp_offset = prev_sp;

		for (auto& ins : node.gadget->insns) {
			for (int r = 0; r < 16; r++) {
				if (ins.regs_written & (1 << r)) {
					// POP = controlled from stack
					if (ins.sp_delta > 0 && ins.reads_sp)
						node.reg_state_out[r] = DagNode::CONTROLLED;
					else
						node.reg_state_out[r] = DagNode::CLOBBERED;
				}
			}
			node.sp_offset += ins.sp_delta;
		}

		// Mark skip/clobber for non-terminator instructions
		if (node.gadget->insns.size() > 1) {
			std::string clobbers;
			bool any = false;
			for (size_t ii = 0; ii + 1 < node.gadget->insns.size(); ii++) {
				auto& ins = node.gadget->insns[ii];
				if (ins.regs_written && !ins.is_ret) {
					for (int r = 0; r < 16; r++) {
						if (ins.regs_written & (1 << r)) {
							if (any)
								clobbers += ", ";
							clobbers += "R" + std::to_string(r);
							any = true;
						}
					}
				}
			}
			if (any) {
				node.is_skip = true;
				node.skip_reason = "clobbers " + clobbers;
			}
		}
	};

	// BFS propagation
	std::vector<bool> visited(dag.nodes.size(), false);
	std::vector<int> queue = {0};
	DagNode::RegState init[16];
	for (int i = 0; i < 16; i++)
		init[i] = DagNode::UNKNOWN;

	while (!queue.empty()) {
		int idx = queue.front();
		queue.erase(queue.begin());
		if (visited[idx])
			continue;
		visited[idx] = true;
		auto& node = dag.nodes[idx];

		DagNode::RegState* prev = (idx == 0) ? init : dag.nodes[idx - 1].reg_state_out;
		int prev_sp = (idx == 0) ? 0 : dag.nodes[idx - 1].sp_offset;
		propagate(node, prev, prev_sp);

		if (node.false_successor >= 0)
			queue.push_back(node.false_successor);
		if (node.true_successor >= 0)
			queue.push_back(node.true_successor);
	}

	// Enumerate paths (DFS, limit to prevent explosion)
	struct PathState {
		int node;
		std::vector<int> path;
		int sp;
		DagNode::RegState regs[16];
	};
	std::vector<PathState> stack;
	PathState start_state;
	start_state.node = 0;
	start_state.sp = 0;
	for (int i = 0; i < 16; i++)
		start_state.regs[i] = DagNode::UNKNOWN;
	stack.push_back(start_state);

	int max_paths = 32;
	while (!stack.empty() && (int)dag.paths.size() < max_paths) {
		auto cur = stack.back();
		stack.pop_back();
		if (cur.node < 0 || cur.node >= (int)dag.nodes.size()) {
			DagAnalysis::PathResult pr;
			pr.node_indices = cur.path;
			pr.final_sp_offset = cur.sp;
			std::copy(cur.regs, cur.regs + 16, pr.final_regs);
			dag.paths.push_back(pr);
			continue;
		}
		auto& node = dag.nodes[cur.node];
		cur.path.push_back(cur.node);

		// Apply gadget effects
		for (auto& ins : node.gadget->insns) {
			for (int r = 0; r < 16; r++) {
				if (ins.regs_written & (1 << r)) {
					cur.regs[r] = (ins.sp_delta > 0 && ins.reads_sp)
									  ? DagNode::CONTROLLED
									  : DagNode::CLOBBERED;
				}
			}
			cur.sp += ins.sp_delta;
		}

		bool has_successor = false;
		if (node.false_successor >= 0) {
			auto next = cur;
			next.node = node.false_successor;
			stack.push_back(next);
			has_successor = true;
		}
		if (node.true_successor >= 0) {
			auto next = cur;
			next.node = node.true_successor;
			stack.push_back(next);
			has_successor = true;
		}
		if (!has_successor) {
			DagAnalysis::PathResult pr;
			pr.node_indices = cur.path;
			pr.final_sp_offset = cur.sp;
			std::copy(cur.regs, cur.regs + 16, pr.final_regs);
			dag.paths.push_back(pr);
		}
	}
	return dag;
}

// =========================================================
// ROP Compiler
// =========================================================
static CompileResult CompileRopSource(const std::string& source,
	const std::vector<GadgetInfo>& gadgets,
	const std::vector<Label>& labels,
	const std::map<std::string, uint32_t>& user_defs,
	const std::set<uint8_t>& bad_bytes) {

	CompileResult result;
	result.success = true;

	auto resolve_label = [&](const std::string& name) -> std::pair<bool, uint32_t> {
		auto it = user_defs.find(name);
		if (it != user_defs.end())
			return {true, it->second};
		for (auto& lb : labels) {
			if (lb.name == name)
				return {true, lb.address};
		}
		return {false, 0};
	};

	auto parse_value = [&](const std::string& s) -> std::pair<bool, uint32_t> {
		if (s.empty())
			return {false, 0};
		// Check for arithmetic: name+offset
		auto plus = s.find('+');
		if (plus != std::string::npos) {
			auto base = s.substr(0, plus);
			auto off = s.substr(plus + 1);
			auto [ok1, v1] = resolve_label(base);
			if (!ok1) {
				v1 = (uint32_t)strtoul(base.c_str(), nullptr, 16);
			}
			uint32_t v2 = (uint32_t)strtoul(off.c_str(), nullptr, 16);
			return {true, v1 + v2};
		}
		if (s[0] == '0' && s.size() > 1 && (s[1] == 'x' || s[1] == 'X'))
			return {true, (uint32_t)strtoul(s.c_str(), nullptr, 16)};
		if (isdigit(s[0]))
			return {true, (uint32_t)strtoul(s.c_str(), nullptr, 0)};
		return resolve_label(s);
	};

	// Find best gadget for an operation
	auto find_best_gadget = [&](uint16_t need_write, bool need_pop,
								CompileResult::GadgetChoice& choice) -> const GadgetInfo* {
		std::vector<std::pair<const GadgetInfo*, int>> candidates;
		for (auto& g : gadgets) {
			if ((g.regs_written & need_write) != need_write)
				continue;
			if (need_pop) {
				bool has_pop = false;
				for (auto& ins : g.insns) {
					if (ins.sp_delta > 0 && ins.reads_sp &&
						(ins.regs_written & need_write))
						has_pop = true;
				}
				if (!has_pop)
					continue;
			}
			if (g.type == GadgetInfo::GT_POP_RT)
				continue;
			int sc = ScoreGadget(g, bad_bytes, need_write, nullptr);
			candidates.push_back({&g, sc});
		}
		if (candidates.empty())
			return nullptr;
		std::sort(candidates.begin(), candidates.end(),
			[](auto& a, auto& b) { return a.second < b.second; });
		choice.chosen = candidates[0].first;
		choice.chosen_score = candidates[0].second;
		choice.alternatives_count = (int)candidates.size() - 1;
		choice.all_candidates = candidates;
		return candidates[0].first;
	};

	std::map<std::string, uint32_t> defs = user_defs;
	std::istringstream ss(source);
	std::string line;
	int lineno = 0;

	while (std::getline(ss, line)) {
		lineno++;
		// Trim
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
			line.pop_back();
		while (!line.empty() && line[0] == ' ')
			line = line.substr(1);
		if (line.empty() || line[0] == ';')
			continue;

		// Parse command
		std::istringstream ls(line);
		std::string cmd;
		ls >> cmd;
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

		auto emit16 = [&](uint32_t v, const std::string& comment) {
			ChainEntry ce;
			ce.type = ChainEntry::CE_DATA;
			ce.value = v & 0xFFFF;
			ce.byte_count = 2;
			ce.source_line = lineno;
			ce.comment = comment;
			result.entries.push_back(ce);
		};
		auto emit32 = [&](uint32_t v, const std::string& comment) {
			ChainEntry ce;
			ce.type = ChainEntry::CE_DATA;
			ce.value = v;
			ce.byte_count = 4;
			ce.source_line = lineno;
			ce.comment = comment;
			result.entries.push_back(ce);
		};

		if (cmd == "DEF") {
			std::string name, val;
			ls >> name >> val;
			auto [ok, v] = parse_value(val);
			if (ok)
				defs[name] = v;
			else
				result.errors.push_back("Line " + std::to_string(lineno) + ": Cannot resolve " + val);
		}
		else if (cmd == "ADDR") {
			std::string arg;
			ls >> arg;
			auto [ok, v] = parse_value(arg);
			if (ok)
				emit16(v, "ADDR " + arg);
			else {
				result.errors.push_back("Line " + std::to_string(lineno) + ": Cannot resolve " + arg);
				result.success = false;
			}
		}
		else if (cmd == "DATA8") {
			std::string arg;
			ls >> arg;
			auto [ok, v] = parse_value(arg);
			ChainEntry ce;
			ce.type = ChainEntry::CE_DATA;
			ce.value = v & 0xFF;
			ce.byte_count = 1;
			ce.source_line = lineno;
			ce.comment = line;
			result.entries.push_back(ce);
		}
		else if (cmd == "DATA16") {
			std::string arg;
			ls >> arg;
			auto [ok, v] = parse_value(arg);
			emit16(v, line);
		}
		else if (cmd == "DATA32") {
			std::string arg;
			ls >> arg;
			auto [ok, v] = parse_value(arg);
			emit32(v, line);
		}
		else if (cmd == "PAD") {
			std::string arg;
			ls >> arg;
			int n = std::atoi(arg.c_str());
			for (int i = 0; i < n; i++) {
				ChainEntry ce;
				ce.type = ChainEntry::CE_PADDING;
				ce.value = 0;
				ce.byte_count = 1;
				ce.source_line = lineno;
				ce.comment = (i == 0 ? line : "");
				result.entries.push_back(ce);
			}
		}
		else if (cmd == "STR") {
			auto q1 = line.find('"'), q2 = line.rfind('"');
			if (q1 != std::string::npos && q2 > q1) {
				auto s = line.substr(q1 + 1, q2 - q1 - 1);
				for (char c : s) {
					ChainEntry ce;
					ce.type = ChainEntry::CE_DATA;
					ce.value = (uint8_t)c;
					ce.byte_count = 1;
					ce.source_line = lineno;
					result.entries.push_back(ce);
				}
				ChainEntry ce;
				ce.type = ChainEntry::CE_DATA;
				ce.value = 0;
				ce.byte_count = 1;
				ce.source_line = lineno;
				result.entries.push_back(ce);
			}
		}
		else if (cmd == "SET") {
			// SET R0, 0x42 or SET ER0, 0x1234
			std::string reg_s, val_s;
			ls >> reg_s;
			if (reg_s.back() == ',')
				reg_s.pop_back();
			ls >> val_s;
			std::transform(reg_s.begin(), reg_s.end(), reg_s.begin(), ::toupper);

			auto [vok, val] = parse_value(val_s);
			if (!vok) {
				result.errors.push_back("Line " + std::to_string(lineno) + ": Cannot resolve " + val_s);
				result.success = false;
				continue;
			}

			uint16_t need_write = 0;
			bool is_er = reg_s.substr(0, 2) == "ER";
			if (is_er) {
				int n = std::atoi(reg_s.c_str() + 2);
				need_write = 3 << n;
			}
			else {
				int n = std::atoi(reg_s.c_str() + 1);
				need_write = 1 << n;
			}

			CompileResult::GadgetChoice choice;
			choice.source_line = lineno;
			choice.operation = line;
			auto* g = find_best_gadget(need_write, true, choice);
			if (!g) {
				result.errors.push_back("Line " + std::to_string(lineno) + ": No gadget found for " + line);
				result.success = false;
				continue;
			}
			result.choices.push_back(choice);

			// Emit gadget address
			ChainEntry ga;
			ga.type = ChainEntry::CE_GADGET_ADDR;
			ga.value = g->address;
			ga.byte_count = 4;
			ga.source_line = lineno;
			ga.chosen_gadget = g;
			char cbuf[128];
			snprintf(cbuf, sizeof(cbuf), "%s -> %s @ %05X (score:%d)",
				line.c_str(), g->GetDisasmString().c_str(), g->address, choice.chosen_score);
			ga.comment = cbuf;
			result.entries.push_back(ga);

			// Emit value
			if (is_er) {
				emit16(val, reg_s + " = " + val_s);
			}
			else {
				emit16(val & 0xFF, reg_s + " = " + val_s);
			}
		}
		else if (cmd == "CALL") {
			std::string arg;
			ls >> arg;
			auto [ok, v] = parse_value(arg);
			if (ok)
				emit32(v, "CALL " + arg);
			else {
				result.errors.push_back("Line " + std::to_string(lineno) + ": Cannot resolve " + arg);
				result.success = false;
			}
		}
		else if (cmd == "PIVOT") {
			std::string arg;
			ls >> arg;
			auto [ok, val] = parse_value(arg);
			if (!ok) {
				result.errors.push_back("Line " + std::to_string(lineno) + ": Cannot resolve " + arg);
				result.success = false;
				continue;
			}
			// Find MOV SP, ERn gadget
			const GadgetInfo* best = nullptr;
			int best_sc = INT_MAX;
			for (auto& g : gadgets) {
				bool has_sp_write = false;
				for (auto& ins : g.insns)
					if (ins.writes_sp && !ins.reads_sp)
						has_sp_write = true;
				if (!has_sp_write)
					continue;
				int sc = ScoreGadget(g, bad_bytes, 0, nullptr);
				if (sc < best_sc) {
					best_sc = sc;
					best = &g;
				}
			}
			if (!best) {
				result.errors.push_back("Line " + std::to_string(lineno) + ": No stack pivot gadget found");
				result.success = false;
				continue;
			}
			ChainEntry ga;
			ga.type = ChainEntry::CE_GADGET_ADDR;
			ga.value = best->address;
			ga.byte_count = 2;
			ga.source_line = lineno;
			ga.chosen_gadget = best;
			ga.comment = "PIVOT -> " + best->GetDisasmString();
			result.entries.push_back(ga);
			emit16(val, "SP = " + arg);
		}
		else if (cmd == "GADGET") {
			std::string name;
			ls >> name;
			const GadgetInfo* found = nullptr;
			for (auto& g : gadgets) {
				if (g.user_name == name) {
					found = &g;
					break;
				}
			}
			if (found) {
				ChainEntry ga;
				ga.type = ChainEntry::CE_GADGET_ADDR;
				ga.value = found->address;
				ga.byte_count = 4;
				ga.source_line = lineno;
				ga.chosen_gadget = found;
				ga.comment = "GADGET " + name;
				result.entries.push_back(ga);
			}
			else {
				result.errors.push_back("Line " + std::to_string(lineno) + ": Unknown gadget " + name);
				result.success = false;
			}
		}
		else {
			result.warnings.push_back("Line " + std::to_string(lineno) + ": Unknown directive " + cmd);
		}
	}

	// Build raw bytes
	for (auto& ce : result.entries) {
		if (ce.byte_count == 1)
			result.raw_bytes.push_back(ce.value & 0xFF);
		else if (ce.byte_count == 2) {
			result.raw_bytes.push_back(ce.value & 0xFF);
			result.raw_bytes.push_back((ce.value >> 8) & 0xFF);
		}
		else if (ce.byte_count == 4) {
			for (int i = 0; i < 4; i++)
				result.raw_bytes.push_back((ce.value >> (i * 8)) & 0xFF);
		}
	}
	return result;
}

// =========================================================
// ROP Tool UI Window
// =========================================================
class RopToolWindow : public UIWindow {
	std::vector<GadgetInfo> gadgets_;
	bool scanned_ = false;
	int max_depth_ = 5;
	char search_hex_[128] = {};
	int filter_type_ = -1; // -1 = all
	char compiler_src_[4096] = {};
	CompileResult compile_result_;
	bool compiled_ = false;
	DagAnalysis dag_;
	bool dag_valid_ = false;
	int selected_path_ = 0;
	char bad_bytes_str_[64] = "00";
	char gadget_name_buf_[64] = {};
	int naming_gadget_idx_ = -1;

	std::set<uint8_t> ParseBadBytes() {
		std::set<uint8_t> r;
		std::string s = bad_bytes_str_;
		for (size_t i = 0; i + 1 < s.size(); i += 2) {
			while (i < s.size() && (s[i] == ' ' || s[i] == ','))
				i++;
			if (i + 1 < s.size()) {
				char buf[3] = {s[i], s[i + 1], 0};
				r.insert((uint8_t)strtoul(buf, nullptr, 16));
			}
		}
		return r;
	}

	void DrawGadgetFinder() {
		ImGui::SliderInt("Max Depth", &max_depth_, 1, 8);
		ImGui::SameLine();
		if (ImGui::Button("Scan ROM")) {
			auto* rom = m_emu->chipset.rom_data.data();
			int sz = (int)m_emu->chipset.rom_data.size();
			FindGadgets(rom, sz, max_depth_, gadgets_);
			scanned_ = true;
		}
		if (!scanned_) {
			ImGui::Text("Click Scan ROM to find gadgets.");
			return;
		}

		ImGui::Text("Found %d gadgets", (int)gadgets_.size());
		ImGui::Separator();

		ImGui::InputText("Hex Filter", search_hex_, sizeof(search_hex_));
		ImGui::SameLine();
		ImGui::Combo("Type", &filter_type_,
			"All\0POP+RT\0POP PC\0MOV+RT\0LOAD+RT\0STORE+RT\0SP+RT\0ARITH+RT\0LEA+RT\0OTHER+RT\0SWI\0");

		if (ImGui::BeginTable("Gadgets", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
					ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
				ImVec2(0, ImGui::GetContentRegionAvail().y - 30))) {
			ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 120);
			ImGui::TableSetupColumn("Disasm", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
			ImGui::TableSetupColumn("Clobbers", ImGuiTableColumnFlags_WidthFixed, 100);
			ImGui::TableSetupColumn("SPΔ", ImGuiTableColumnFlags_WidthFixed, 40);
			ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 50);
			ImGui::TableHeadersRow();

			std::string hex_filter = search_hex_;
			for (int i = 0; i < (int)gadgets_.size(); i++) {
				auto& g = gadgets_[i];
				if (filter_type_ > 0 && (int)g.type != filter_type_ - 1)
					continue;
				if (!hex_filter.empty()) {
					auto hex = g.GetHexString();
					if (hex.find(hex_filter) == std::string::npos)
						continue;
				}

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				char addr_buf[8];
				snprintf(addr_buf, sizeof(addr_buf), "%05X", g.address);
				if (ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_SpanAllColumns)) {
					// Copy address to clipboard
					char clip[16];
					snprintf(clip, sizeof(clip), "0x%05X", g.address);
					ImGui::SetClipboardText(clip);
				}
				if (ImGui::BeginPopupContextItem()) {
					if (ImGui::MenuItem("Copy Address")) {
						char clip[16];
						snprintf(clip, sizeof(clip), "0x%05X", g.address);
						ImGui::SetClipboardText(clip);
					}
					if (ImGui::MenuItem("Jump to CodeViewer")) {
						if (code_viewer)
							code_viewer->JumpTo(g.address);
					}
					if (ImGui::MenuItem("Name Gadget")) {
						naming_gadget_idx_ = i;
						memset(gadget_name_buf_, 0, sizeof(gadget_name_buf_));
						if (!g.user_name.empty())
							strncpy(gadget_name_buf_, g.user_name.c_str(), sizeof(gadget_name_buf_) - 1);
						ImGui::OpenPopup("NameGadget");
					}
					ImGui::EndPopup();
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(g.GetHexString().c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(g.GetDisasmString().c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(g.GetTypeString().c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(g.GetClobberString().c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%+d", (int)g.sp_delta);
				ImGui::TableNextColumn();
				ImGui::Text("%d", g.score);
			}
			ImGui::EndTable();
		}

		// Name gadget popup
		if (ImGui::BeginPopup("NameGadget")) {
			ImGui::Text("Name this gadget:");
			ImGui::InputText("##name", gadget_name_buf_, sizeof(gadget_name_buf_));
			if (ImGui::Button("OK") && naming_gadget_idx_ >= 0) {
				gadgets_[naming_gadget_idx_].user_name = gadget_name_buf_;
				naming_gadget_idx_ = -1;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	void DrawCompiler() {
		ImGui::Text("ROP Source:");
		ImGui::InputTextMultiline("##src", compiler_src_, sizeof(compiler_src_),
			ImVec2(-1, ImGui::GetContentRegionAvail().y * 0.4f));

		if (ImGui::Button("Compile")) {
			auto bad = ParseBadBytes();
			compile_result_ = CompileRopSource(compiler_src_, gadgets_, g_labels, {}, bad);
			compiled_ = true;
		}
		ImGui::SameLine();
		ImGui::InputText("Bad Bytes", bad_bytes_str_, sizeof(bad_bytes_str_));
		ImGui::SameLine();
		if (compiled_) {
			ImGui::Text("| %d bytes", (int)compile_result_.raw_bytes.size());
		}

		if (!compiled_)
			return;

		// Errors
		for (auto& e : compile_result_.errors)
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", e.c_str());
		for (auto& w : compile_result_.warnings)
			ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "%s", w.c_str());

		ImGui::Separator();
		ImGui::Text("Compiled Output:");

		if (ImGui::BeginTable("Chain", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
				ImVec2(0, ImGui::GetContentRegionAvail().y * 0.4f))) {
			ImGui::TableSetupColumn("Off", ImGuiTableColumnFlags_WidthFixed, 40);
			ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 80);
			ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 50);
			ImGui::TableHeadersRow();

			int offset = 0;
			for (auto& ce : compile_result_.entries) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%04X", offset);
				ImGui::TableNextColumn();
				if (ce.byte_count == 1)
					ImGui::Text("%02X", ce.value & 0xFF);
				else if (ce.byte_count == 2)
					ImGui::Text("%02X %02X", ce.value & 0xFF, (ce.value >> 8) & 0xFF);
				else
					ImGui::Text("%08X", ce.value);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ce.comment.c_str());
				ImGui::TableNextColumn();
				if (ce.chosen_gadget)
					ImGui::Text("%d", ce.chosen_gadget->score);
				offset += ce.byte_count;
			}
			ImGui::EndTable();
		}

		// Auto-selection choices
		if (!compile_result_.choices.empty()) {
			ImGui::Separator();
			ImGui::Text("Gadget Selection:");
			for (auto& ch : compile_result_.choices) {
				ImGui::BulletText("L%d: %s -> score %d (%d alternatives)",
					ch.source_line, ch.operation.c_str(), ch.chosen_score, ch.alternatives_count);
				if (ImGui::IsItemHovered() && ch.chosen) {
					ImGui::BeginTooltip();
					ImGui::Text("Chosen: %s @ %05X", ch.chosen->GetDisasmString().c_str(), ch.chosen->address);
					int shown = 0;
					for (auto& [g, sc] : ch.all_candidates) {
						if (g == ch.chosen)
							continue;
						ImGui::Text("  Alt: %s @ %05X (score:%d)", g->GetDisasmString().c_str(), g->address, sc);
						if (++shown >= 5) {
							ImGui::Text("  ... and %d more", ch.alternatives_count - shown);
							break;
						}
					}
					ImGui::EndTooltip();
				}
			}
		}

		if (ImGui::Button("Copy Hex")) {
			std::string hex;
			char buf[4];
			for (auto b : compile_result_.raw_bytes) {
				snprintf(buf, sizeof(buf), "%02X", b);
				hex += buf;
			}
			ImGui::SetClipboardText(hex.c_str());
		}
	}

	void DrawStaticChecker() {
		if (!compiled_ || compile_result_.entries.empty()) {
			ImGui::Text("Compile a chain first in the Compiler tab.");
			return;
		}

		if (ImGui::Button("Run DAG Analysis")) {
			std::vector<const GadgetInfo*> chain;
			for (auto& ce : compile_result_.entries) {
				if (ce.chosen_gadget)
					chain.push_back(ce.chosen_gadget);
			}
			dag_ = AnalyzeChain(chain);
			dag_valid_ = true;
		}
		if (!dag_valid_)
			return;

		ImGui::Text("DAG: %d nodes, %d edges, %d paths",
			(int)dag_.nodes.size(), (int)dag_.edges.size(), (int)dag_.paths.size());
		ImGui::Separator();

		// Node table
		ImGui::Text("Nodes:");
		if (ImGui::BeginTable("DAGNodes", 6,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
				ImVec2(0, 200))) {
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30);
			ImGui::TableSetupColumn("Gadget", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("SP", ImGuiTableColumnFlags_WidthFixed, 40);
			ImGui::TableSetupColumn("Branch", ImGuiTableColumnFlags_WidthFixed, 50);
			ImGui::TableSetupColumn("Skip", ImGuiTableColumnFlags_WidthFixed, 120);
			ImGui::TableSetupColumn("Regs Out", ImGuiTableColumnFlags_WidthFixed, 150);
			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)dag_.nodes.size(); i++) {
				auto& n = dag_.nodes[i];
				ImGui::TableNextRow();
				if (n.is_skip)
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(80, 80, 40, 128));

				ImGui::TableNextColumn();
				ImGui::Text("%d", i);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(n.gadget->GetDisasmString().c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%+d", n.sp_offset);
				ImGui::TableNextColumn();
				if (n.is_branch_point)
					ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Y");
				else
					ImGui::Text("-");
				ImGui::TableNextColumn();
				if (n.is_skip)
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.3f, 1), "%s", n.skip_reason.c_str());
				else
					ImGui::Text("-");
				ImGui::TableNextColumn();
				std::string regs;
				for (int r = 0; r < 16; r++) {
					if (n.reg_state_out[r] != DagNode::UNKNOWN) {
						if (!regs.empty())
							regs += " ";
						regs += "R" + std::to_string(r) + "=" + DagNode::RegStateStr(n.reg_state_out[r]);
					}
				}
				ImGui::TextUnformatted(regs.c_str());
			}
			ImGui::EndTable();
		}

		// Path comparison
		if (!dag_.paths.empty()) {
			ImGui::Separator();
			ImGui::Text("Path Comparison:");
			if (ImGui::BeginTable("Paths", 4,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
					ImVec2(0, 0))) {
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthFixed, 50);
				ImGui::TableSetupColumn("SP Final", ImGuiTableColumnFlags_WidthFixed, 60);
				ImGui::TableSetupColumn("Controlled Regs", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Nodes", ImGuiTableColumnFlags_WidthFixed, 120);
				ImGui::TableHeadersRow();

				for (int p = 0; p < (int)dag_.paths.size(); p++) {
					auto& path = dag_.paths[p];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					bool sel = (selected_path_ == p);
					char lbl[16];
					snprintf(lbl, sizeof(lbl), "%c", 'A' + p);
					if (ImGui::Selectable(lbl, sel))
						selected_path_ = p;
					ImGui::TableNextColumn();
					ImGui::Text("%+d", path.final_sp_offset);
					ImGui::TableNextColumn();
					std::string ctrl;
					for (int r = 0; r < 16; r++) {
						if (path.final_regs[r] == DagNode::CONTROLLED) {
							if (!ctrl.empty())
								ctrl += ", ";
							ctrl += "R" + std::to_string(r);
						}
					}
					ImGui::TextUnformatted(ctrl.c_str());
					ImGui::TableNextColumn();
					std::string nodes_str;
					for (int ni : path.node_indices) {
						if (!nodes_str.empty())
							nodes_str += "→";
						nodes_str += std::to_string(ni);
					}
					ImGui::TextUnformatted(nodes_str.c_str());
				}
				ImGui::EndTable();
			}
		}

		// Bad byte check
		ImGui::Separator();
		auto bad = ParseBadBytes();
		if (!bad.empty()) {
			bool found_bad = false;
			for (size_t i = 0; i < compile_result_.raw_bytes.size(); i++) {
				if (bad.count(compile_result_.raw_bytes[i])) {
					ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "Bad byte 0x%02X at offset 0x%04X",
						compile_result_.raw_bytes[i], (int)i);
					found_bad = true;
				}
			}
			if (!found_bad)
				ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "No bad bytes found.");
		}
	}

public:
	RopToolWindow()
		: UIWindow("ROP Tools") {
		memset(compiler_src_, 0, sizeof(compiler_src_));
	}

	void RenderCore() override {
		if (!m_emu) {
			ImGui::Text("No emulator loaded.");
			return;
		}

		if (ImGui::BeginTabBar("RopTabs")) {
			if (ImGui::BeginTabItem("Gadget Finder")) {
				DrawGadgetFinder();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("ROP Compiler")) {
				DrawCompiler();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Static Checker")) {
				DrawStaticChecker();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
};

UIWindow* CreateRopToolWindow() {
	return new RopToolWindow();
}
