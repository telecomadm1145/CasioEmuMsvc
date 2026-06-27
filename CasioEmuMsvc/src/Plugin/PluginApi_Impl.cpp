#define _NO_FUND_API
#include "FileDialog.hpp"
#include "PluginApi.h"
#include "PluginMan.h"
#include "SysDialog.h"
#include <AddressWindow.h>
#include <CallAnalysis.h>
#include <CPU.hpp>
#include <Chipset.hpp>
#include <CodeViewer.hpp>
#include <CwiiHelp.h>
#include <Emulator.hpp>
#include <ePSCpu.h>
#include <HwController.h>
#include <Keyboard.hpp>
#include <MemBreakPoint.hpp>
#include <MMU.hpp>
#include <Models.h>
#include <SnapshotWindow.h>
#include <Ui.hpp>
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

extern std::vector<UIWindow*> windows;
extern SDL_Window* window;
extern Breakpoints* membp;

class PluginApi_Impl : public PluginApi {

	class IPlatformHost_Impl : public IPlatformHost {
	public:
		const char* GetInternalStoragePath() override {
#ifdef __ANDROID__
			return SDL_AndroidGetInternalStoragePath();
#else
			return ".";
#endif
		}
		const char* GetExternalStoragePath() override {
#ifdef __ANDROID__
			return SDL_AndroidGetExternalStoragePath();
#else
			return ".";
#endif
		}
		bool ShowFileOpenDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize) override {
			return FileDialog::ShowFileOpenDialog(title, filters, selectedFilePath, pathBufferSize);
		}
		void OpenSystemFileDialog(std::function<void(std::string)> callback) override {
			SystemDialogs::OpenFileDialog([callback](std::filesystem::path p) {
				callback(p.string());
			});
		}
		void SaveSystemFileDialog(const char* preferred_name, std::function<void(std::string)> callback) override {
			SystemDialogs::SaveFileDialog(preferred_name, [callback](std::filesystem::path p) {
				callback(p.string());
			});
		}
		void ShowMessageBox(const char* title, const char* message, int type) override {
			Uint32 flags = SDL_MESSAGEBOX_INFORMATION;
			if (type == 1)
				flags = SDL_MESSAGEBOX_WARNING;
			if (type == 2)
				flags = SDL_MESSAGEBOX_ERROR;
			SDL_ShowSimpleMessageBox(flags, title, message, window);
		}
	} platform_impl;

	class IMMU_Impl : public IMMU {
		uint8_t ReadData(size_t addr) override {
			return me_mmu->ReadData(addr);
		}
		void WriteData(size_t addr, uint8_t dat) override {
			me_mmu->WriteData(addr, dat);
		}
		uint16_t ReadCode(size_t addr) override {
			return me_mmu->ReadCode(addr);
		}
		void WriteCode(size_t addr, uint8_t dat) override {
			m_emu->chipset.rom_data[addr] = dat;
		}
	} mmu_impl;
	class ICPU_Impl : public ICPU {
		// 通过 ICPU 继承
		uint16_t* Register(const char* name) override {
			if (!name)
				return nullptr;
			auto it = m_emu->chipset.cpu.register_proxies.find(name);
			if (it == m_emu->chipset.cpu.register_proxies.end() || !it->second)
				return nullptr;
			return &it->second->raw;
		}
	} cpu_impl;
	class IEmulator_Impl : public IEmulator {
		// 通过 IEmulator 继承
		float* SolarPanelVoltage() override {
			return &m_emu->SolarPanelVoltage;
		}
		float* BatteryVoltage() override {
			return &m_emu->BatteryVoltage;
		}
		casioemu::ModelInfo* ModelDefinition() override {
			return &m_emu->ModelDefinition;
		}
		void RequestScreenshot() override {
			m_emu->screenshot_requested.store(true);
		}
		bool IsPaused() override {
			return m_emu->GetPaused();
		}
		void Pause() override {
			m_emu->SetPaused(true);
		}
		void Resume() override {
			m_emu->SetPaused(false);
		}
		unsigned int GetCyclesPerSecond() override {
			return m_emu->cycles.cycles_per_second;
		}
		void SetCyclePerSecond(uint32_t cps) override {
			m_emu->cycles.cycles_per_second = cps;
		}
		void* GetRenderer() override {
			return m_emu->renderer;
		}
		void* GetInterfaceTexture() override {
			return m_emu->interface_texture;
		}
		std::string GetModelFilePath(std::string relative_path) override {
			return m_emu->GetModelFilePath(relative_path);
		}
		const char* GetRunningModelName() override {
			return m_emu->ModelDefinition.model_name.c_str();
		}
		const char* GetRunningRomPath() override {
			return m_emu->ModelDefinition.rom_path.c_str();
		}
	} emu_impl;
	class IChipset_Impl : public IChipset {
		// 通过 IChipset 继承
		void RaiseInterrupt(int index) override {
			m_emu->chipset.RaiseMaskable(index);
		}
		void Tick() override {
			m_emu->chipset.Tick();
		}
		void SetStatus(RunStatus status) override {
			m_emu->chipset.run_mode = (casioemu::Chipset::RunMode)status;
		}
		RunStatus GetStatus() override {
			return RunStatus(m_emu->chipset.run_mode);
		}
		void* GetRom() override {
			return m_emu->chipset.rom_data.data();
		}
		size_t GetRomSize() override {
			return m_emu->chipset.rom_data.size();
		}
	} chipset_impl;
	class IKeyboard_Impl : public IKeyboard {
		// Delegates to the keyboard peripheral's IKeyboardAutomation
		casioemu::IKeyboardAutomation* GetKbd() {
			return (casioemu::IKeyboardAutomation*)m_emu->chipset.QueryInterface(
				typeid(casioemu::IKeyboardAutomation).name());
		}
		void Key(int ki, int ko, bool pressed) override {
			if (auto* kbd = GetKbd())
				kbd->Key(ki, ko, pressed);
		}
		void ReleaseAll() override {
			if (auto* kbd = GetKbd())
				kbd->ReleaseAll();
		}
		void PressCode(uint8_t code, bool pressed) override {
			if (auto* kbd = GetKbd())
				kbd->PressCode(code, pressed);
		}
	} keyboard_impl;
	class IDebugger_Impl : public IDebugger {
		static std::string NormalizeRegisterName(const char* name) {
			std::string normalized = name ? name : "";
			std::transform(normalized.begin(), normalized.end(), normalized.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return normalized;
		}

		static uint32_t CurrentPc() {
			if (m_emu->chipset.epscpu)
				return m_emu->chipset.epscpu->PC() >> 1;
			return (uint32_t)(m_emu->chipset.cpu.reg_csr << 16) | m_emu->chipset.cpu.reg_pc;
		}

	public:
		uint32_t GetProgramCounter() override {
			auto lock = std::lock_guard(m_emu->access_mx);
			return CurrentPc();
		}

		std::vector<DebugRegisterInfo> GetRegisters() override {
			auto lock = std::lock_guard(m_emu->access_mx);
			std::vector<DebugRegisterInfo> result;
			if (auto* eps = m_emu->chipset.epscpu) {
				result = {
					{"pc", eps->PC() >> 1, 20},
					{"lr", (uint32_t)((eps->BSR << 7) | (eps->FSR & 0x7f)), 20},
					{"ea", (uint32_t)((eps->BSR1 << 7) | (eps->FSR1 & 0x7f)), 20},
					{"ex1", (uint32_t)((eps->BSR2 << 7) | (eps->FSR2 & 0x7f)), 20},
					{"lcdar", (uint32_t)(((eps->LCDARH & 0x03) * 0x60) | eps->LCDARL), 16},
					{"sp", (uint32_t)(eps->STKPTR << 1), 16},
					{"psw", eps->STATUS, 8},
					{"dsr", eps->BSR, 8},
				};
				return result;
			}

			result.reserve(m_emu->chipset.cpu.register_proxies.size());
			for (const auto& [name, reg] : m_emu->chipset.cpu.register_proxies) {
				if (!reg)
					continue;
				uint32_t value = reg->raw;
				uint32_t width = static_cast<uint32_t>(reg->type_size * 8);
				if (name == "pc") {
					value = CurrentPc();
					width = 20;
				}
				else if (name == "lr") {
					value = (uint32_t)(m_emu->chipset.cpu.reg_lcsr << 16) | m_emu->chipset.cpu.reg_lr;
					width = 20;
				}
				result.push_back({name, value, width});
			}
			return result;
		}

		bool ReadRegister(const char* name, uint32_t& value, uint32_t& bitWidth) override {
			const auto normalized = NormalizeRegisterName(name);
			for (const auto& reg : GetRegisters()) {
				if (reg.Name == normalized) {
					value = reg.Value;
					bitWidth = reg.BitWidth;
					return true;
				}
			}
			return false;
		}

		bool WriteRegister(const char* name, uint32_t value) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			const auto normalized = NormalizeRegisterName(name);
			if (m_emu->chipset.epscpu)
				return false;
			if (normalized == "pc") {
				m_emu->chipset.cpu.reg_pc = static_cast<uint16_t>(value);
				m_emu->chipset.cpu.reg_csr = static_cast<uint16_t>(value >> 16);
				return true;
			}
			if (normalized == "lr") {
				m_emu->chipset.cpu.reg_lr = static_cast<uint16_t>(value);
				m_emu->chipset.cpu.reg_lcsr = static_cast<uint16_t>(value >> 16);
				return true;
			}
			auto it = m_emu->chipset.cpu.register_proxies.find(normalized);
			if (it == m_emu->chipset.cpu.register_proxies.end() || !it->second)
				return false;
			it->second->raw = static_cast<uint16_t>(value);
			return true;
		}

		std::vector<uint8_t> ReadMemory(uint32_t address, size_t size) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			std::vector<uint8_t> result;
			result.reserve(size);
			for (size_t i = 0; i < size; ++i)
				result.push_back(m_emu->chipset.mmu.ReadData(address + i));
			return result;
		}

		void WriteMemory(uint32_t address, const std::vector<uint8_t>& data) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			for (size_t i = 0; i < data.size(); ++i)
				m_emu->chipset.mmu.WriteData(address + i, data[i]);
		}

		std::vector<uint16_t> ReadCode(uint32_t address, size_t count) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			std::vector<uint16_t> result;
			result.reserve(count);
			for (size_t i = 0; i < count; ++i)
				result.push_back(m_emu->chipset.mmu.ReadCode(address + i * 2));
			return result;
		}

		void WriteCode(uint32_t address, const std::vector<uint8_t>& data) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			const auto romSize = m_emu->chipset.rom_data.size();
			for (size_t i = 0; i < data.size() && address + i < romSize; ++i)
				m_emu->chipset.rom_data[address + i] = data[i];
		}

		std::vector<DebugDisassemblyLine> GetDisassembly(uint32_t address, size_t count) override {
			std::vector<DebugDisassemblyLine> result;
			if (!code_viewer)
				return result;
			for (const auto& line : code_viewer->GetDisassembly(address, count))
				result.push_back({line.offset, line.srcbuf, line.is_label, line.xref_operand});
			return result;
		}

		void Pause() override {
			m_emu->SetPaused(true);
		}
		void Resume() override {
			m_emu->SetPaused(false);
		}
		void Reset() override {
			const bool wasPaused = m_emu->GetPaused();
			m_emu->SetPaused(true);
			auto lock = std::lock_guard(m_emu->access_mx);
			m_emu->chipset.Reset();
			m_emu->SetPaused(wasPaused);
		}
		bool StepInto() override {
			if (!code_viewer || !m_emu->GetPaused())
				return false;
			code_viewer->RequestStep();
			return true;
		}
		bool StepOver() override {
			if (!code_viewer || !m_emu->GetPaused())
				return false;
			code_viewer->RequestTrace();
			return true;
		}
		bool StepOut() override {
			return code_viewer && m_emu->GetPaused() && code_viewer->RequestStepOut();
		}

		std::vector<uint32_t> GetExecutionBreakpoints() override {
			return code_viewer ? code_viewer->GetBreakpoints() : std::vector<uint32_t>{};
		}
		bool AddExecutionBreakpoint(uint32_t address) override {
			if (!code_viewer)
				return false;
			code_viewer->AddBreakpoint(address);
			return true;
		}
		bool RemoveExecutionBreakpoint(uint32_t address) override {
			if (!code_viewer)
				return false;
			code_viewer->RemoveBreakpoint(address);
			return true;
		}
		void ClearExecutionBreakpoints() override {
			if (code_viewer)
				code_viewer->ClearBreakpoints();
		}

		std::vector<DebugMemoryBreakpointInfo> GetMemoryBreakpoints() override {
			std::vector<DebugMemoryBreakpointInfo> result;
			if (!membp)
				return result;
			for (const auto& bp : membp->ExternalListBps())
				result.push_back({bp.addr, bp.enableWrite, bp.breakWhenHit, bp.records.size()});
			return result;
		}
		std::vector<DebugMemoryBreakpointHitInfo> GetMemoryBreakpointHits(uint32_t address, bool write) override {
			std::vector<DebugMemoryBreakpointHitInfo> result;
			if (!membp)
				return result;
			for (const auto& [pc, record] : membp->ExternalListHits(address, write)) {
				DebugMemoryBreakpointHitInfo hit{pc, record.lr, record.stacktrace};
				hit.Registers.reserve(record.registers.size());
				for (const auto& [name, value] : record.registers)
					hit.Registers.push_back({name, value, name.size() >= 2 && name[0] == 'r' ? 8u : 16u});
				result.push_back(std::move(hit));
			}
			return result;
		}
		bool AddMemoryBreakpoint(uint32_t address, bool write, bool breakWhenHit) override {
			if (!membp)
				return false;
			membp->ExternalAddBp(address, write, breakWhenHit);
			return true;
		}
		bool RemoveMemoryBreakpoint(uint32_t address, bool write) override {
			return membp && membp->ExternalRemoveBp(address, write);
		}
		void ClearMemoryBreakpoints() override {
			if (membp)
				membp->ExternalClearBps();
		}

		std::string GetBacktrace() override {
			auto lock = std::lock_guard(m_emu->access_mx);
			return m_emu->chipset.epscpu ? std::string{} : m_emu->chipset.cpu.GetBacktrace();
		}
		std::vector<DebugStackFrameInfo> GetStackFrames() override {
			auto lock = std::lock_guard(m_emu->access_mx);
			std::vector<DebugStackFrameInfo> result;
			if (auto* eps = m_emu->chipset.epscpu) {
				for (size_t i = 0; i < eps->STKPTR; ++i) {
					const uint32_t pc = eps->stack[i] << 1;
					result.push_back({pc, 0, static_cast<uint32_t>(i * 2), 0, 0, false, false, lookup_symbol(pc, g_labels)});
				}
				return result;
			}
			auto stack = m_emu->chipset.cpu.stack.get();
			result.reserve(stack->size());
			for (auto it = stack->rbegin(); it != stack->rend(); ++it) {
				result.push_back({
					it->new_pc,
					it->lr,
					it->sp,
					it->er0,
					it->er2,
					it->lr_pushed,
					it->is_jump,
					lookup_symbol(it->new_pc, g_labels),
				});
			}
			return result;
		}
		std::vector<DebugLabelInfo> GetLabels(const char* query, size_t limit) override {
			std::string needle = query ? query : "";
			std::transform(needle.begin(), needle.end(), needle.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			std::vector<DebugLabelInfo> result;
			for (const auto& label : g_labels) {
				std::string name = label.name;
				std::string lowered = name;
				std::transform(lowered.begin(), lowered.end(), lowered.begin(),
					[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
				if (!needle.empty() && lowered.find(needle) == std::string::npos)
					continue;
				result.push_back({label.address, std::move(name)});
				if (limit && result.size() >= limit)
					break;
			}
			return result;
		}
		std::vector<DebugVariableInfo> GetVariables() override {
			auto lock = std::lock_guard(m_emu->access_mx);
			std::vector<DebugVariableInfo> result;
			if (!n_ram_buffer)
				return result;
			char* base = n_ram_buffer - casioemu::GetRamBaseAddr(m_emu->hardware_id);
			const auto reImOffset = casioemu::GetReImOffset(m_emu->hardware_id);
			const auto variableSize = casioemu::GetVariableSize(m_emu->hardware_id);
			const bool complexMode =
				(*(base + casioemu::GetModeOffset(m_emu->hardware_id)) & 0xff) == 0xc4;
			for (const auto& variable : casioemu::GetVariableOffsets(m_emu->hardware_id)) {
				if (complexMode && !std::strcmp(variable.Name, "PreAns"))
					continue;
				DebugVariableInfo info;
				info.Name = variable.Name;
				info.RealAddress = static_cast<uint32_t>(variable.RealPartOffset);
				info.RealValue = casioemu::BCD2Str(base + variable.RealPartOffset);
				info.RealHex = casioemu::ConvHex(base + variable.RealPartOffset, variableSize);
				info.HasImaginaryPart = complexMode;
				if (complexMode) {
					info.ImaginaryAddress = static_cast<uint32_t>(variable.RealPartOffset + reImOffset);
					info.ImaginaryValue = casioemu::BCD2Str(base + variable.RealPartOffset + reImOffset);
					info.ImaginaryHex = casioemu::ConvHex(base + variable.RealPartOffset + reImOffset, variableSize);
				}
				result.push_back(std::move(info));
			}
			return result;
		}

		std::vector<DebugSnapshotInfo> GetSnapshots() override {
			std::vector<DebugSnapshotInfo> result;
			if (!snapshot_window)
				return result;
			snapshot_window->DebugEnsureLoaded();
			for (const auto& node : snapshot_window->DebugManager().Nodes) {
				result.push_back({
					node.Id,
					node.ParentId,
					node.Label,
					node.Timestamp,
					node.PreviewPng.size(),
					node.UncompressedStateSize,
				});
			}
			return result;
		}

		uint32_t SaveSnapshot(uint32_t parentId, const char* label) override {
			if (!snapshot_window)
				return 0;
			snapshot_window->DebugEnsureLoaded();
			return snapshot_window->DebugManager().SaveSnapshot(
				*m_emu,
				parentId,
				label && *label ? label : "MCP Snapshot",
				false);
		}

		bool LoadSnapshot(uint32_t id, std::string& error) override {
			if (!snapshot_window) {
				error = "Snapshot window is unavailable";
				return false;
			}
			try {
				snapshot_window->DebugEnsureLoaded();
				snapshot_window->DebugManager().LoadSnapshot(*m_emu, id);
				return true;
			}
			catch (const std::exception& exception) {
				error = exception.what();
				return false;
			}
		}

		bool DeleteSnapshot(uint32_t id) override {
			if (!snapshot_window)
				return false;
			snapshot_window->DebugEnsureLoaded();
			const auto before = snapshot_window->DebugManager().Nodes.size();
			snapshot_window->DebugManager().DeleteNode(id);
			return snapshot_window->DebugManager().Nodes.size() != before;
		}

		bool ExportSnapshots(const char* path, uint32_t id, bool subtree, std::string& error) override {
			if (!snapshot_window || !path || !*path) {
				error = "Snapshot window or export path is unavailable";
				return false;
			}
			try {
				snapshot_window->DebugEnsureLoaded();
				if (id == 0)
					snapshot_window->DebugManager().ExportAll(path);
				else if (subtree)
					snapshot_window->DebugManager().ExportSubtree(path, id);
				else
					snapshot_window->DebugManager().ExportNode(path, id);
				return true;
			}
			catch (const std::exception& exception) {
				error = exception.what();
				return false;
			}
		}

		bool ImportSnapshots(const char* path, std::string& error) override {
			if (!snapshot_window || !path || !*path) {
				error = "Snapshot window or import path is unavailable";
				return false;
			}
			try {
				snapshot_window->DebugEnsureLoaded();
				snapshot_window->DebugManager().ImportFromFile(path);
				return true;
			}
			catch (const std::exception& exception) {
				error = exception.what();
				return false;
			}
		}

		std::vector<DebugAddressLockInfo> GetAddressLocks() override {
			return DebugGetAddressLocks();
		}
		void SetAddressLock(uint32_t address, uint8_t value, bool locked) override {
			DebugSetAddressLock(address, value, locked);
		}
		bool RemoveAddressLock(uint32_t address) override {
			return DebugRemoveAddressLock(address);
		}
		void ClearAddressLocks() override {
			DebugClearAddressLocks();
		}

		void StartCallRecording(bool filterCaller, uint32_t caller, bool filterCallee, uint32_t callee) override {
			DebugStartCallRecording(filterCaller, caller, filterCallee, callee);
		}
		void StopCallRecording() override {
			DebugStopCallRecording();
		}
		void ClearCallRecording() override {
			DebugClearCallRecording();
		}
		std::vector<DebugFunctionCallInfo> GetFunctionCalls(uint32_t function) override {
			return DebugGetFunctionCalls(function);
		}

		void SetCyclesPerSecond(uint32_t cps) override {
			auto lock = std::lock_guard(m_emu->access_mx);
			m_emu->cycles.Setup(cps, m_emu->cycles.timer_interval);
		}
		void SetPdValue(uint8_t value) override {
			m_emu->ModelDefinition.pd_value = value;
		}
		DebugDisplaySettings GetDisplaySettings() override {
			return {
				screen_flashing_threshold,
				screen_flashing_brightness_coeff,
				screen_buffer_select,
				enable_screen_fading,
				screen_fading_blending_coefficient,
				screen_residual_enabled,
				screen_residual_alpha_scale,
				audio_enable,
			};
		}
		void SetDisplaySettings(const DebugDisplaySettings& settings) override {
			screen_flashing_threshold = std::clamp(settings.FlashingThreshold, 0, 0x3f);
			screen_flashing_brightness_coeff = std::clamp(settings.FlashingBrightness, 1.0f, 8.0f);
			screen_buffer_select = std::clamp(settings.BufferSelect, 0, 2);
			enable_screen_fading = settings.FadingEnabled;
			screen_fading_blending_coefficient = settings.FadingCoefficient;
			screen_residual_enabled = settings.ResidualEnabled;
			screen_residual_alpha_scale = settings.ResidualAlphaScale;
			audio_enable = settings.AudioEnabled;
		}
		DebugQrCodeInfo GetQrCode() override {
			m_emu->qr_code.Poll(*m_emu);
			const auto state = m_emu->qr_code.GetState();
			DebugQrCodeInfo result{state.Active, state.Complete, state.Version, state.Revision, state.Data};
			result.RealCurrentPage = state.RealCurrentPage;
			result.RealTotalPages = state.RealTotalPages;
			result.RealCurrentPageData = state.RealCurrentPageData;
			result.RealPageLengths = state.RealPageLengths;
			result.History.reserve(state.History.size());
			for (const auto& entry : state.History) {
				result.History.push_back({entry.Id, entry.Version, entry.Data});
			}
			return result;
		}
		void RequestScreenshot() override {
			m_emu->screenshot_requested.store(true);
		}
		void RequestRecording(bool start) override {
			if (start)
				m_emu->recording_requested.store(true);
			else
				m_emu->recording_stop_requested.store(true);
		}
		bool HotReloadRom(std::string& error) override {
			const bool wasPaused = m_emu->GetPaused();
			m_emu->SetPaused(true);
			auto lock = std::lock_guard(m_emu->access_mx);
			std::ifstream stream(
				m_emu->GetModelFilePath(m_emu->ModelDefinition.rom_path),
				std::ifstream::binary);
			if (!stream) {
				error = "Failed to open ROM file";
				m_emu->SetPaused(wasPaused);
				return false;
			}
			std::vector<unsigned char> data{
				std::istreambuf_iterator<char>(stream),
				std::istreambuf_iterator<char>()};
			std::copy_n(
				data.begin(),
				std::min(data.size(), m_emu->chipset.rom_data.size()),
				m_emu->chipset.rom_data.begin());
			m_emu->chipset.Reset();
			m_emu->SetPaused(wasPaused);
			return true;
		}
	} debugger_impl;
	class Hooks_Impl : public Hooks {
		// 通过 Hooks 继承

		// 注册指令执行 hook，传入的 handler 只需要处理 InstructionEventArgs
		void SetupOnInstructionHook(std::function<void(InstructionEventArgs&)> handler) override {
			SetupHook(on_instruction,
				[handler](casioemu::CPU& /*cpu*/, InstructionEventArgs& args) {
					handler(args);
				});
		}

		// 注册函数调用 hook，传入的 handler 只需要处理 FunctionEventArgs
		void SetupOnCallFunctionHook(std::function<void(const FunctionEventArgs&)> handler) override {
			SetupHook(on_call_function,
				[handler](casioemu::CPU& /*cpu*/, const FunctionEventArgs& args) {
					handler(args);
				});
		}

		// 注册函数返回 hook，传入的 handler 只需要处理 FunctionEventArgs
		void SetupOnFunctionReturnHook(std::function<void(const FunctionEventArgs&)> handler) override {
			SetupHook(on_function_return,
				[handler](casioemu::CPU& /*cpu*/, const FunctionEventArgs& args) {
					handler(args);
				});
		}

		// 注册内存读取 hook，传入的 handler 只需要处理 MemoryEventArgs
		void SetupOnMemoryReadHook(std::function<void(MemoryEventArgs&)> handler) override {
			SetupHook(on_memory_read,
				[handler](casioemu::MMU& /*mmu*/, MemoryEventArgs& args) {
					handler(args);
				});
		}

		// 注册内存写入 hook，传入的 handler 只需要处理 MemoryEventArgs
		void SetupOnMemoryWriteHook(std::function<void(MemoryEventArgs&)> handler) override {
			SetupHook(on_memory_write,
				[handler](casioemu::MMU& /*mmu*/, MemoryEventArgs& args) {
					handler(args);
				});
		}

		// 注册中断断点 hook，传入的 handler 只需要处理 InterruptEventArgs
		void SetupOnBrkHook(std::function<void(InterruptEventArgs&)> handler) override {
			SetupHook(on_brk, [handler](casioemu::Chipset& /*chipset*/, InterruptEventArgs& args) {
				handler(args);
			});
		}

		// 注册中断 hook，传入的 handler 只需要处理 InterruptEventArgs
		void SetupOnInterruptHook(std::function<void(InterruptEventArgs&)> handler) override {
			SetupHook(on_interrupt,
				[handler](casioemu::Chipset& /*chipset*/, InterruptEventArgs& args) {
					handler(args);
				});
		}

		// 注册复位 hook，传入的 handler 无参数，但内部 hook 接收 Chipset 引用
		void SetupOnResetHook(std::function<void()> handler) override {
			SetupHook(on_reset,
				[handler](casioemu::Chipset& /*chipset*/) {
					handler();
				});
		}
	} hooks_impl;
	int GetVersion() override {
		return 1;
	}
	void AddWindow(UIWindow* wnd) override {
		windows.push_back(wnd);
	}
	bool RegisterPlugin(const char* id, const char* name, int version) override {
        return RegisterPlugin(id, name, std::to_string(version).c_str(), "", "Legacy Plugin");
    }
	void* QueryInterface(const char* name) override {
		if (strcmp(name, typeid(IPlatformHost).name()) == 0)
			return &platform_impl;
		if (strcmp(name, typeid(IEmulator).name()) == 0)
			return &emu_impl;
		if (strcmp(name, typeid(ICPU).name()) == 0)
			return &cpu_impl;
		if (strcmp(name, typeid(IChipset).name()) == 0)
			return &chipset_impl;
		if (strcmp(name, typeid(IMMU).name()) == 0)
			return &mmu_impl;
		if (strcmp(name, typeid(Hooks).name()) == 0)
			return &hooks_impl;
		if (strcmp(name, typeid(IKeyboard).name()) == 0)
			return &keyboard_impl;
		if (strcmp(name, typeid(IDebugger).name()) == 0)
			return &debugger_impl;
		return m_emu->chipset.QueryInterface(name);
	}
	void* GetImGuiContext() override {
		return ImGui::GetCurrentContext();
	}
	void AssertFundamentalSTL(size_t a, size_t b, size_t c, size_t d) override {
		if (a != sizeof(std::string) || b != sizeof(std::vector<int>) || c != sizeof(std::map<int, int>) || d != sizeof(std::mutex)) {
			PANIC("STL size mismatch.");
		}
	}
	bool RegisterPlugin(const char* id, const char* name, const char* version, const char* author, const char* desc) override {
    std::cout << (name ? name : "Unknown") << " loaded.\n";
    g_loadedPlugins.push_back({
        id ? id : "",
        name ? name : "",
        version ? version : "",
        author ? author : "",
        desc ? desc : ""
    });
    return true;
  }
};
PluginApi* g_pluginapi = new PluginApi_Impl();
