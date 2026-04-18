#define _NO_FUND_API
#include "FileDialog.hpp"
#include "PluginApi.h"
#include "PluginMan.h"
#include "SysDialog.h"
#include <CPU.hpp>
#include <Chipset.hpp>
#include <Keyboard.hpp>
#include <MMU.hpp>
#include <SDL.h>

extern std::vector<UIWindow*> windows;
extern SDL_Window* window;

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
			return &m_emu->chipset.cpu.register_proxies[name]->raw;
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
	bool RegisterPlugin(const char* id, const char* name, const char* version, const char* author, const char* desc) override {
		std::cout << (name ? name : "Unknown") << " loaded.\n";
		g_loadedPlugins.push_back({id ? id : "",
			name ? name : "",
			/*version ? version :*/ "",
			/*author ? author :*/ "",
			/*desc ? desc :*/ ""}); // Hack: 兼容性问题
		return true;
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
};
PluginApi* g_pluginapi = new PluginApi_Impl();