#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DebugRegisterInfo {
	std::string Name;
	uint32_t Value = 0;
	uint32_t BitWidth = 0;
};

struct DebugStackFrameInfo {
	uint32_t ProgramCounter = 0;
	uint32_t LinkRegister = 0;
	uint32_t StackPointer = 0;
	uint16_t Er0 = 0;
	uint16_t Er2 = 0;
	bool LinkRegisterPushed = false;
	bool IsJump = false;
	std::string Symbol;
};

struct DebugLabelInfo {
	uint32_t Address = 0;
	std::string Name;
};

struct DebugVariableInfo {
	std::string Name;
	uint32_t RealAddress = 0;
	uint32_t ImaginaryAddress = 0;
	std::string RealValue;
	std::string ImaginaryValue;
	std::string RealHex;
	std::string ImaginaryHex;
	bool HasImaginaryPart = false;
};

struct DebugMemoryBreakpointInfo {
	uint32_t Address = 0;
	bool Write = false;
	bool BreakWhenHit = false;
	size_t HitCount = 0;
};

struct DebugMemoryBreakpointHitInfo {
	uint32_t ProgramCounter = 0;
	uint32_t LinkRegister = 0;
	std::string Stack;
	std::vector<DebugRegisterInfo> Registers;
};

struct DebugDisassemblyLine {
	uint32_t Address = 0;
	std::string Text;
	bool IsLabel = false;
	int32_t CrossReference = 0;
};

struct DebugSnapshotInfo {
	uint32_t Id = 0;
	uint32_t ParentId = 0;
	std::string Label;
	int64_t Timestamp = 0;
	size_t PreviewSize = 0;
	uint64_t StateSize = 0;
};

struct DebugAddressLockInfo {
	uint32_t Address = 0;
	uint8_t Value = 0;
	bool Locked = false;
};

struct DebugFunctionCallInfo {
	uint32_t Function = 0;
	uint32_t Caller = 0;
	uint32_t Xr0 = 0;
	std::string Stack;
};

struct DebugDisplaySettings {
	int FlashingThreshold = 20;
	float FlashingBrightness = 1.5f;
	int BufferSelect = 0;
	bool FadingEnabled = false;
	float FadingCoefficient = 0.0f;
	bool ResidualEnabled = true;
	float ResidualAlphaScale = 1.0f;
	bool AudioEnabled = false;
};

struct DebugQrCodeHistoryEntry {
	uint64_t Id = 0;
	int Version = 0;
	std::string Data;
};

struct DebugQrCodeInfo {
	bool Active = false;
	bool Complete = false;
	int Version = 0;
	uint64_t Revision = 0;
	std::string Data;
	std::vector<DebugQrCodeHistoryEntry> History;
	uint8_t RealCurrentPage = 0;
	uint8_t RealTotalPages = 0;
	std::string RealCurrentPageData;
	std::vector<size_t> RealPageLengths;
};
