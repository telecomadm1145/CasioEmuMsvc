#pragma once
#include "Ui.hpp"
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

	void DrawFindContent();

	void DrawContent();

public:
	Breakpoints() : UIWindow("Breakpoints") {
		SetupHooks();
	}

	void SetupHooks();

	void TryTrigBp(uint32_t addr_edit, bool write);

	void RenderCore() override;

	void ExternalAddBp(uint32_t addr, bool write);
	void ExternalAddBp(uint32_t addr, bool write, bool breakWhenHit);
	bool ExternalRemoveBp(uint32_t addr, bool write);
	void ExternalClearBps();
	std::vector<MemBPData_t> ExternalListBps() const;
	std::vector<std::pair<uint32_t, Record>> ExternalListHits(uint32_t addr, bool write) const;
};
