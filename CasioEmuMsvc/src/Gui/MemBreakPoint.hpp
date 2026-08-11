#pragma once
#include "Ui.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Record {
	std::string stacktrace;
	uint32_t lr;
	std::map<std::string, uint32_t> registers;
};

struct MemBPData_t {
	bool enableWrite = false;
	bool breakWhenHit = false;
	bool enabled = true;
	bool compareData = false;
	uint8_t data = 0;
	uint8_t mask = 0xff;
	uint64_t skipCount = 0;
	uint32_t addr;
	std::unordered_map<uint32_t, Record> records;
};

void SetMemBp(uint32_t addr, bool write);

class Breakpoints : public UIWindow {

private:
	mutable std::recursive_mutex breakpoints_mutex;
	std::vector<MemBPData_t> break_point_hash;

	int target_addr = -1;

	bool break_on_cv = false;

	bool break_on_sp = false;

	int reg_compare_mode = 0;
	
	int target_sp = 0;
	std::atomic<uint64_t> register_breakpoint_config{0};
	uint64_t last_eps_breakpoint_version{~0ull};

	void DrawFindContent();

	void DrawContent();
	bool RegisterBreakpointTriggered(uint32_t value) const;
	void UpdateRegisterBreakpointConfig();
	void RefreshEpsBreakpoints();
	void SyncEpsBreakpoints();

public:
	Breakpoints() : UIWindow("Breakpoints") {
		SetupHooks();
	}

	void SetupHooks();

	void TryTrigBp(uint32_t addr_edit, bool write);

	void RenderCore() override;

	void ExternalAddBp(uint32_t addr, bool write);
	void ExternalAddBp(uint32_t addr, bool write, bool breakWhenHit,
		bool enabled = true, bool compareData = false, uint8_t data = 0,
		uint8_t mask = 0xff, uint64_t skipCount = 0);
	bool ExternalRemoveBp(uint32_t addr, bool write);
	void ExternalClearBps();
	std::vector<MemBPData_t> ExternalListBps() const;
	std::vector<std::pair<uint32_t, Record>> ExternalListHits(uint32_t addr, bool write) const;
};
