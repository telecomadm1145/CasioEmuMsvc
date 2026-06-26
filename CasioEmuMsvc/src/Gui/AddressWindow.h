#pragma once
#include "Plugin/DebuggerTypes.h"
#include <Ui.hpp>
#include <vector>

UIWindow* CreateAddressWindow();
std::vector<DebugAddressLockInfo> DebugGetAddressLocks();
void DebugSetAddressLock(uint32_t address, uint8_t value, bool locked);
bool DebugRemoveAddressLock(uint32_t address);
void DebugClearAddressLocks();
