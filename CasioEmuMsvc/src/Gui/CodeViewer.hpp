#pragma once
#include "Ui.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
typedef struct {
	uint32_t offset;
	char srcbuf[80];
	bool is_label;
	int xref_operand;
} CodeElem;

enum EmuDebugFlag {
	DEBUG_BREAKPOINT = 1,
	DEBUG_STEP = 2,
	DEBUG_RET_TRACE = 4
};
class CodeViewer : public UIWindow {
private:
	std::map<int, uint8_t> break_points;
	std::vector<CodeElem> codes;
	std::string src_path;
	char adrbuf[9]{0};
	int max_row = 0;
	int max_col = 0;
	int cur_col = 0;
	int first_col = 0;

	bool stepping = false;
	bool tracing = false;
	uint32_t trace_bp = 0;

	bool is_loaded = false;
	bool need_roll = false;
	bool search_activated = false;
	bool help_activated = true;
	bool search_focus = false;
	int hovered_line = 0;
	uint32_t selected_addr = -1;

	// Search related
	char search_buf[256]{0};
	int search_mode = 0; // 0: Hex, 1: Instruction
	int last_found_idx = -1;
	bool search_failed = false;

public:
	uint8_t debug_flags = DEBUG_BREAKPOINT;
	CodeViewer() : UIWindow("Code") {
		PrepareDisasm();
		SetupHooks();
	}
	void SetupHooks();
	void PrepareDisasm();
	bool TryTrigBP(uint8_t seg, uint16_t offset, bool bp_mode = true);
	void ExternalBP();
	CodeElem LookUp(uint32_t offset, int* idx = 0);
	void RenderCore() override;
	void DrawContent();
	void DrawMonitor();
	void JumpTo(uint32_t offset);
	void RequestStep();
	void AddBreakpoint(uint32_t address);
	void RemoveBreakpoint(uint32_t address);
	void Search(bool next);
	void ExportDisassembly();
	size_t GetBreakpointCount() const { return break_points.size(); }
};
