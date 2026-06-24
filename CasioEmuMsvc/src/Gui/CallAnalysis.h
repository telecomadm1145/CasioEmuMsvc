#pragma once
#include "Plugin/DebuggerTypes.h"
#include "Ui.hpp"
#include <vector>

UIWindow* CreateCallAnalysisWindow();
void DebugStartCallRecording(bool filterCaller, uint32_t caller, bool filterCallee, uint32_t callee);
void DebugStopCallRecording();
void DebugClearCallRecording();
std::vector<DebugFunctionCallInfo> DebugGetFunctionCalls(uint32_t function);
