// #include <X11/Xlib.h>
#include <cstddef>
#include "imgui/imgui.h"
#include "CodeViewer.hpp"
#include "SDL_timer.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <iostream>
#include "ui.hpp"
#include "../Chipset/MMU.hpp"

#include "hex.hpp"
#include "../Peripheral/Screen.hpp"
#include "VariableWindow.h"
#include "KeyLog.h"

char* n_ram_buffer = nullptr;
casioemu::MMU* me_mmu = nullptr;
CodeViewer* code_viewer = nullptr;
static SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
static SDL_Window* window;
Injector* injector;
MemBreakPoint* membp;
WatchWindow* ww;
SDL_Renderer* renderer;
VariableWindow* vw;
KeyLog* kl;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
void gui_loop() {
	if (!m_emu->Running())
		return;

	// cv.LookUp(1, 0x1235);
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	static MemoryEditor mem_edit;
	if (n_ram_buffer != nullptr && me_mmu != nullptr) {
		// std::cout<<"renderhex!";
		/*
				size_t start{};
		size_t length{};
		ImColor color{};
		std::optional<std::string> desc{};
		*/
#define SColor \
	i++ % 2 ? ImColor{ 40, 120, 40, 255 } : ImColor { 40, 40, 120, 255 }
		int i = 0;
		mem_edit.ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
			return me_mmu->ReadData(off + 0x9000);
		};
		mem_edit.WriteFn = [](ImU8* data, size_t off, ImU8 d) {
			return me_mmu->WriteData(off + 0x9000, d);
		};
		MemoryEditor::OptionalMarkedSpans spans{ std::vector<MemoryEditor::MarkedSpan>{
			MemoryEditor::MarkedSpan{ 0x919E, 0x2, SColor, " 输入的字符(小端) " },
			MemoryEditor::MarkedSpan{ 0x91EE, 0x2, SColor, " 输入的字符(小端)(2) " },
			MemoryEditor::MarkedSpan{ 0x91F0, 0x1, SColor, " 光标字符位置备份 " },
			MemoryEditor::MarkedSpan{ 0xF800, 64 * 32, SColor, " 屏幕缓冲区 " },
			MemoryEditor::MarkedSpan{ 0xD66C, 3048, SColor, " 屏幕缓冲区2 " },
			MemoryEditor::MarkedSpan{ 0xE254, 0xF000-0xE254, SColor, " 栈 " },
			MemoryEditor::MarkedSpan{ 0x91A0, 0x1, SColor, " 按键修饰符 " },
			MemoryEditor::MarkedSpan{ 0x91A1, 0x1, SColor, " 模式 " },
			MemoryEditor::MarkedSpan{ 0x91A2, 0x1, SColor, " 子模式 " },
			MemoryEditor::MarkedSpan{ 0x91A3, 0x1, SColor, " 屏幕状态 " },
			MemoryEditor::MarkedSpan{ 0x91A4, 0x1, SColor, " 界面 " },
			MemoryEditor::MarkedSpan{ 0x91AA, 0x1, SColor, " 数字格式设置 " },
			MemoryEditor::MarkedSpan{ 0x91AB, 0x1, SColor, " 数字格式设置2 " },
			MemoryEditor::MarkedSpan{ 0x91AC, 0x1, SColor, " 小数点类型 " },
			MemoryEditor::MarkedSpan{ 0x91AD, 0x1, SColor, " 角度单位设置 " },
			MemoryEditor::MarkedSpan{ 0x91AE, 0x1, SColor, " 数学模式开关 " },
			MemoryEditor::MarkedSpan{ 0x91AF, 0x1, SColor, " 分数类型 " },
			MemoryEditor::MarkedSpan{ 0x91B0, 0x1, SColor, " 复数类型 " },
			MemoryEditor::MarkedSpan{ 0x91B1, 0x1, SColor, " 频数 " },
			MemoryEditor::MarkedSpan{ 0x91B3, 0x1, SColor, " 自动化简 " },
			MemoryEditor::MarkedSpan{ 0x91B4, 0x1, SColor, " 小数输出开关 " },
			MemoryEditor::MarkedSpan{ 0x91B5, 0x1, SColor, " 自动关机时长(1=60min) " },
			MemoryEditor::MarkedSpan{ 0x91B6, 0x1, SColor, " 函数表格类型 " },
			MemoryEditor::MarkedSpan{ 0x91B7, 0x1, SColor, " 工程符号 " },
			MemoryEditor::MarkedSpan{ 0x91B8, 0x1, SColor, " 数字分隔符 " },
			MemoryEditor::MarkedSpan{ 0x91B9, 0x1, SColor, " 字体设置(真) " },
			MemoryEditor::MarkedSpan{ 0x91BA, 0x1, SColor, " 解方程复数根 " },
			MemoryEditor::MarkedSpan{ 0x91BB, 0x1, SColor, " 语言设置 " },
			MemoryEditor::MarkedSpan{ 0x91BC, 0x1, SColor, " 自动计算 " },
			MemoryEditor::MarkedSpan{ 0x91BD, 0x1, SColor, " 数据表格显示 " },
			MemoryEditor::MarkedSpan{ 0x91BE, 0x1, SColor, " 二维码类型(3或11) " },
			MemoryEditor::MarkedSpan{ 0x91BF, 0x1, SColor, " 算法背景 " },
			MemoryEditor::MarkedSpan{ 0x91C0, 0x1, SColor, " 算法单位 " },
			MemoryEditor::MarkedSpan{ 0x91C2, 0x1, SColor, " 光标图形 " },
			MemoryEditor::MarkedSpan{ 0x91C3, 0x1, SColor, " 字体设置3 " },
			MemoryEditor::MarkedSpan{ 0x91C7, 0x1, SColor, " 字体设置2 " },
			MemoryEditor::MarkedSpan{ 0x91D0, 0x1, SColor, " 滚动Y轴 " },
			MemoryEditor::MarkedSpan{ 0x91E0, 0x1, SColor, " 光标临时字符 " },
			MemoryEditor::MarkedSpan{ 0x91E1, 0x1, SColor, " 光标临时字符 " },
			MemoryEditor::MarkedSpan{ 0x91E3, 0x1, SColor, " 对比度 " },
			MemoryEditor::MarkedSpan{ 0x91E5, 0x1, SColor, " 光标字符位置 " },
			MemoryEditor::MarkedSpan{ 0x91E8, 0x1, SColor, " 光标X轴 " },
			MemoryEditor::MarkedSpan{ 0x91E9, 0x1, SColor, " 光标Y轴 " },
			MemoryEditor::MarkedSpan{ 0x91EA, 0x1, SColor, " 线性滚动开始值（数学模式不使用） " },
			MemoryEditor::MarkedSpan{ 0x91F1, 0x1, SColor, " 菜单图标 " },
			MemoryEditor::MarkedSpan{ 0x91F8, 0x1, SColor, " 光标大小 " },
			MemoryEditor::MarkedSpan{ 0x91FD, 0x1, SColor, " 函数界面 " },
			MemoryEditor::MarkedSpan{ 0x91FF, 0x1, SColor, " 变量设置界面(大于1启用) " },
			MemoryEditor::MarkedSpan{ 0x9200, 0x1, SColor, " 变量设置界面(变量编号) " },
			MemoryEditor::MarkedSpan{ 0x9201, 0x1, SColor, " 验算模式 " },
			MemoryEditor::MarkedSpan{ 0x9207, 0x1, SColor, " 阻止进入自检 " },
			MemoryEditor::MarkedSpan{ 0x9206, 0x1, SColor, " 自检正确次数 " },
			MemoryEditor::MarkedSpan{ 0x9205, 0x1, SColor, " 自检次数 " },
			MemoryEditor::MarkedSpan{ 0x924C, 0xE, SColor, "Ans" },
			MemoryEditor::MarkedSpan{ 0x925A, 0xE, SColor, "Ans i" },
			MemoryEditor::MarkedSpan{ 0x9268, 200, SColor, "输入区" },
			MemoryEditor::MarkedSpan{ 0x9268 + 200, 200, SColor, "缓冲区" },
			MemoryEditor::MarkedSpan{ 0x9268 + 400, 400, SColor, "历史记录" },
			MemoryEditor::MarkedSpan{ 0x9268 + 800, 200, SColor, "结果区/分析区" },
			MemoryEditor::MarkedSpan{ 0x9650, 0xC, SColor, "随机数种子" },
			MemoryEditor::MarkedSpan{ 0x965C, 0x2, SColor, "不稳定字节" },
			MemoryEditor::MarkedSpan{ 0x965E, 0xE, SColor, "Ans" },
			MemoryEditor::MarkedSpan{ 0x966C, 0xE, SColor, "A" },
			MemoryEditor::MarkedSpan{ 0x967A, 0xE, SColor, "B" },
			MemoryEditor::MarkedSpan{ 0x9688, 0xE, SColor, "C" },
			MemoryEditor::MarkedSpan{ 0x9696, 0xE, SColor, "D" },
			MemoryEditor::MarkedSpan{ 0x96A4, 0xE, SColor, "E" },
			MemoryEditor::MarkedSpan{ 0x96B2, 0xE, SColor, "F" },
			MemoryEditor::MarkedSpan{ 0x96C0, 0xE, SColor, "x" },
			MemoryEditor::MarkedSpan{ 0x96CE, 0xE, SColor, "y" },
			MemoryEditor::MarkedSpan{ 0x96DC, 0xE, SColor, "z" },
			MemoryEditor::MarkedSpan{ 0x96EA, 0xE, SColor, "PreAns" },
			MemoryEditor::MarkedSpan{ 0x965E + 0xE * 11, 0xE, SColor, "Ans i" },
			MemoryEditor::MarkedSpan{ 0x966C + 0xE * 11, 0xE, SColor, "A i" },
			MemoryEditor::MarkedSpan{ 0x967A + 0xE * 11, 0xE, SColor, "B i" },
			MemoryEditor::MarkedSpan{ 0x9688 + 0xE * 11, 0xE, SColor, "C i" },
			MemoryEditor::MarkedSpan{ 0x9696 + 0xE * 11, 0xE, SColor, "D i" },
			MemoryEditor::MarkedSpan{ 0x96A4 + 0xE * 11, 0xE, SColor, "E i" },
			MemoryEditor::MarkedSpan{ 0x96B2 + 0xE * 11, 0xE, SColor, "F i" },
			MemoryEditor::MarkedSpan{ 0x96C0 + 0xE * 11, 0xE, SColor, "x i" },
			MemoryEditor::MarkedSpan{ 0x96CE + 0xE * 11, 0xE, SColor, "y i" },
			MemoryEditor::MarkedSpan{ 0x96DC + 0xE * 11, 0xE, SColor, "z i" },
			MemoryEditor::MarkedSpan{ 0x96EA + 0xE * 11, 0xE, SColor, "PreAns i" },
			MemoryEditor::MarkedSpan{ 0x97A2, 200, SColor, "f(x)" },
			MemoryEditor::MarkedSpan{ 0x986A, 200, SColor, "g(x)" },
		} };
		mem_edit.DrawWindow("内存编辑器", 0, 0x7000, 0x9000, spans);
	}
	code_viewer->DrawWindow();
	injector->Show();
	membp->Show();
	ww->Show();
	vw->Draw();
	kl->Draw();
	// Rendering
	ImGui::Render();
	SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
	SDL_SetRenderDrawColor(renderer, (Uint8)(clear_color.x * 255), (Uint8)(clear_color.y * 255), (Uint8)(clear_color.z * 255), (Uint8)(clear_color.w * 255));
	SDL_RenderClear(renderer);
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
	SDL_RenderPresent(renderer);
}
int test_gui(bool* guiCreated) {
	// SDL_Delay(1000*5);
	window = SDL_CreateWindow("CasioEmuX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
	if (renderer == nullptr) {
		SDL_Log("Error creating SDL_Renderer!");
		return 0;
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.WantCaptureKeyboard = true;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
	// io.FontGlobalScale = 0.8;
	// io.WantTextInput = true;
	// io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch

	// Setup Platform/Renderer backends
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer2_Init(renderer);
	// bool show_demo_window = true;
	// bool show_another_window = false;
	// char buf[100]{0};
	// Main loop
	// bool done = false;

	*guiCreated = true;
	while (!m_emu)
		;
	code_viewer = new CodeViewer(m_emu->GetModelFilePath("_disas.txt"));
	injector = new Injector();
	membp = new MemBreakPoint();
	ww = new WatchWindow();
	vw = new VariableWindow();
	kl = new KeyLog();
	return 0;
	// ImGui_ImplSDL2_InitForSDLRenderer(renderer);
}

void gui_cleanup() {
	// Cleanup
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
