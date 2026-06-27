#pragma once
#ifdef CASIOEMU_PLUGIN_API
namespace casioemu {
	struct ModelInfo;
}
#else
#include "ModelInfo.h"
#endif
#include <cstdint>
#include <cstddef>
#include <functional>
#ifndef CASIOEMU_HEADLESS_PLUGIN
#include <imgui.h>
#endif
#include <string>
#include <vector>
#include "DebuggerTypes.h"
#ifdef _NO_FUND_API
#include "Hooks.h"
#include "Ui.hpp"
#endif //  _NO_FUND_API

#ifndef _NO_FUND_API
struct FunctionEventArgs {
	uint32_t pc{};
	uint32_t lr{};
};
struct MemoryEventArgs {
	uint32_t offset{};
	bool handled{};
	uint8_t value{};
};
struct InterruptEventArgs {
	uint8_t index{};
	bool handled{};
};
struct InstructionEventArgs {
	uint32_t pc_before;
	uint32_t pc_after;
	bool should_break{};
};
#endif // ! _NO_FUND_API

class Hooks {
public:
	virtual void SetupOnInstructionHook(std::function<void(InstructionEventArgs&)> handler) = 0;
	virtual void SetupOnCallFunctionHook(std::function<void(const FunctionEventArgs&)> handler) = 0;
	virtual void SetupOnFunctionReturnHook(std::function<void(const FunctionEventArgs&)> handler) = 0;
	virtual void SetupOnMemoryReadHook(std::function<void(MemoryEventArgs&)> handler) = 0;
	virtual void SetupOnMemoryWriteHook(std::function<void(MemoryEventArgs&)> handler) = 0;
	virtual void SetupOnBrkHook(std::function<void(InterruptEventArgs&)> handler) = 0;
	virtual void SetupOnInterruptHook(std::function<void(InterruptEventArgs&)> handler) = 0;
	virtual void SetupOnResetHook(std::function<void()> handler) = 0;
};

class IEmulator {
public:
	virtual float* SolarPanelVoltage() = 0;
	virtual float* BatteryVoltage() = 0;
	virtual casioemu::ModelInfo* ModelDefinition() = 0;
	virtual void RequestScreenshot() = 0;
	virtual bool IsPaused() = 0;
	virtual void Pause() = 0;
	virtual void Resume() = 0;
	virtual unsigned int GetCyclesPerSecond() = 0;
	virtual void SetCyclePerSecond(uint32_t cps) = 0;
	virtual std::string GetModelFilePath(std::string relative_path) = 0;
	virtual void* GetRenderer() = 0;
	virtual void* GetInterfaceTexture() = 0;
	virtual const char* GetRunningModelName() = 0;
	virtual const char* GetRunningRomPath() = 0;
};

class ICPU {
public:
	/// <summary>
	/// Get register's value.
	/// </summary>
	/// <param name="name">Register's name</param>
	/// <returns>Pointer to the register</returns>
	virtual uint16_t* Register(const char* name) = 0;
};

class IMMU {
public:
	virtual uint8_t ReadData(size_t addr) = 0;
	virtual void WriteData(size_t addr, uint8_t dat) = 0;
	virtual uint16_t ReadCode(size_t addr) = 0;
	virtual void WriteCode(size_t addr, uint8_t dat) = 0;
};

class IChipset {
public:
	virtual void RaiseInterrupt(int index) = 0;
	virtual void Tick() = 0;
	enum struct RunStatus {
		Stop,
		Halt,
		Run,
	};
	virtual void SetStatus(RunStatus status) = 0;
	virtual RunStatus GetStatus() = 0;

	// Version 1
	virtual void* GetRom() = 0;
	virtual size_t GetRomSize() = 0;
};

/// <summary>
/// Platform Host automation interface.
/// Obtained via PluginApi::QueryInterface<IPlatformHost>().
/// </summary>
class IPlatformHost {
public:
	virtual const char* GetInternalStoragePath() = 0;
	virtual const char* GetExternalStoragePath() = 0;
	virtual bool ShowFileOpenDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize) = 0;
	virtual void OpenSystemFileDialog(std::function<void(std::string)> callback) = 0;
	virtual void SaveSystemFileDialog(const char* preferred_name, std::function<void(std::string)> callback) = 0;
	virtual void ShowMessageBox(const char* title, const char* message, int type) = 0; // 0=Info, 1=Warn, 2=Error
};

/// <summary>
/// Keyboard automation interface (Version 2).
/// Obtained via PluginApi::QueryInterface<IKeyboard>().
/// </summary>
class IKeyboard {
public:
	/// Press or release the button at the given KI/KO bit position.
	/// ki: KI line index (0-7), ko: KO line index (0-9), pressed: true=press, false=release
	virtual void Key(int ki, int ko, bool pressed) = 0;
	/// Release all non-stuck buttons.
	virtual void ReleaseAll() = 0;
	/// Press/release by raw kiko code byte (same encoding as ModelInfo::buttons[].kiko).
	virtual void PressCode(uint8_t code, bool pressed) = 0;
};

/// Headless access to the same debugger state used by the ImGui windows.
/// Obtained via PluginApi::QueryInterface<IDebugger>().
class IDebugger {
public:
	virtual uint32_t GetProgramCounter() = 0;
	virtual std::vector<DebugRegisterInfo> GetRegisters() = 0;
	virtual bool ReadRegister(const char* name, uint32_t& value, uint32_t& bitWidth) = 0;
	virtual bool WriteRegister(const char* name, uint32_t value) = 0;

	virtual std::vector<uint8_t> ReadMemory(uint32_t address, size_t size) = 0;
	virtual void WriteMemory(uint32_t address, const std::vector<uint8_t>& data) = 0;
	virtual std::vector<uint16_t> ReadCode(uint32_t address, size_t count) = 0;
	virtual void WriteCode(uint32_t address, const std::vector<uint8_t>& data) = 0;
	virtual std::vector<DebugDisassemblyLine> GetDisassembly(uint32_t address, size_t count) = 0;

	virtual void Pause() = 0;
	virtual void Resume() = 0;
	virtual void Reset() = 0;
	virtual bool StepInto() = 0;
	virtual bool StepOver() = 0;
	virtual bool StepOut() = 0;

	virtual std::vector<uint32_t> GetExecutionBreakpoints() = 0;
	virtual bool AddExecutionBreakpoint(uint32_t address) = 0;
	virtual bool RemoveExecutionBreakpoint(uint32_t address) = 0;
	virtual void ClearExecutionBreakpoints() = 0;

	virtual std::vector<DebugMemoryBreakpointInfo> GetMemoryBreakpoints() = 0;
	virtual std::vector<DebugMemoryBreakpointHitInfo> GetMemoryBreakpointHits(uint32_t address, bool write) = 0;
	virtual bool AddMemoryBreakpoint(uint32_t address, bool write, bool breakWhenHit) = 0;
	virtual bool RemoveMemoryBreakpoint(uint32_t address, bool write) = 0;
	virtual void ClearMemoryBreakpoints() = 0;

	virtual std::string GetBacktrace() = 0;
	virtual std::vector<DebugStackFrameInfo> GetStackFrames() = 0;
	virtual std::vector<DebugLabelInfo> GetLabels(const char* query, size_t limit) = 0;
	virtual std::vector<DebugVariableInfo> GetVariables() = 0;

	virtual std::vector<DebugSnapshotInfo> GetSnapshots() = 0;
	virtual uint32_t SaveSnapshot(uint32_t parentId, const char* label) = 0;
	virtual bool LoadSnapshot(uint32_t id, std::string& error) = 0;
	virtual bool DeleteSnapshot(uint32_t id) = 0;
	virtual bool ExportSnapshots(const char* path, uint32_t id, bool subtree, std::string& error) = 0;
	virtual bool ImportSnapshots(const char* path, std::string& error) = 0;

	virtual std::vector<DebugAddressLockInfo> GetAddressLocks() = 0;
	virtual void SetAddressLock(uint32_t address, uint8_t value, bool locked) = 0;
	virtual bool RemoveAddressLock(uint32_t address) = 0;
	virtual void ClearAddressLocks() = 0;

	virtual void StartCallRecording(bool filterCaller, uint32_t caller, bool filterCallee, uint32_t callee) = 0;
	virtual void StopCallRecording() = 0;
	virtual void ClearCallRecording() = 0;
	virtual std::vector<DebugFunctionCallInfo> GetFunctionCalls(uint32_t function) = 0;

	virtual void SetCyclesPerSecond(uint32_t cps) = 0;
	virtual void SetPdValue(uint8_t value) = 0;
	virtual DebugDisplaySettings GetDisplaySettings() = 0;
	virtual void SetDisplaySettings(const DebugDisplaySettings& settings) = 0;
	virtual DebugQrCodeInfo GetQrCode() = 0;
	virtual void RequestScreenshot() = 0;
	virtual void RequestRecording(bool start) = 0;
	virtual bool HotReloadRom(std::string& error) = 0;
};

#if !defined(_NO_FUND_API) && !defined(CASIOEMU_HEADLESS_PLUGIN)
class UIWindow {
public:
	UIWindow(const char* name) : name(name) {}
	const char* name{};
	bool open = true;
	ImVec2 inital_size{800, 800};
	ImGuiWindowFlags flags{};
	virtual void Render() {
		if (!open)
			return;
		ImGui::SetNextWindowSize(inital_size, ImGuiCond_FirstUseEver);
		if (ImGui::Begin(name, &open, flags)) {
			RenderCore();
		}
		ImGui::End();
	}
	virtual void RenderCore() = 0;
	virtual ~UIWindow() {
	}
};
#elif defined(CASIOEMU_HEADLESS_PLUGIN)
class UIWindow;
#endif

class PluginApi {
public:
	/// <summary>
	/// Get plugin api version(currently 1)
	/// </summary>
	/// <returns></returns>
	[[nodiscard]] virtual int GetVersion() = 0;
	/// <summary>
	/// Add a window to debugger window.
	/// </summary>
	/// <param name="wnd">The window, cannot be null.</param>
	virtual void AddWindow(UIWindow* wnd) = 0;
	/// <summary>
	/// When it returns false, a plugin shouldn't be loaded.
	/// </summary>
	/// <param name="name">Name of the plugin</param>
	/// <param name="id">Id of the plugin</param>
	/// <param name="version">Version of the plugin</param>
	/// <returns>Whether the plugin should be loaded</returns>
	[[nodiscard]] virtual bool RegisterPlugin(const char* id, const char* name, int version) = 0;
	/// <summary>
	/// Check if the STL is the same.
	/// </summary>
	virtual void AssertFundamentalSTL(size_t a, size_t b, size_t c, size_t d) = 0;
	[[nodiscard]] virtual void* QueryInterface(const char* name) = 0;
	template <class T>
	[[nodiscard]] T* QueryInterface() {
		return reinterpret_cast<T*>(QueryInterface(typeid(T).name()));
	}
	virtual void* GetImGuiContext() = 0;
	[[nodiscard]] virtual bool RegisterPlugin(const char* id, const char* name, const char* version, const char* author = nullptr, const char* desc = nullptr) = 0;
};

#define PLUGINASSERTSTL(x) x->AssertFundamentalSTL(sizeof(std::string), sizeof(std::vector<int>), sizeof(std::map<int, int>), sizeof(std::mutex))

/// <summary>
/// The plugin DLL's entry point.
/// </summary>
using PluginLoad = void (*)(PluginApi* api);
