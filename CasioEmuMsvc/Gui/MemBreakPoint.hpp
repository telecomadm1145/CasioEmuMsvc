#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

struct MemRecord_t {
    bool Write;
    uint16_t pc;
};

struct MemBPData_t {
    bool enableWrite = false;
    uint16_t addr;
    std::unordered_map<uint32_t, int> records;
};

class MemBreakPoint
{

private:
    std::vector<MemBPData_t> break_point_hash;

    int target_addr = -1;

    void DrawFindContent();

    void DrawContent();
public:

    MemBreakPoint();

    void TryTrigBp(uint16_t addr_edit,bool write);

    void Show();
};