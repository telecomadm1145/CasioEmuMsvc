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
#include "AppMemoryView.h"
#include "KeyLog.h"
#include <filesystem>

char* n_ram_buffer = nullptr;
casioemu::MMU* me_mmu = nullptr;
CodeViewer* code_viewer = nullptr;
static SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
static SDL_Window* window;
Injector* injector;
MemBreakPoint* membp;
AppMemoryView* amv;
WatchWindow* ww;
SDL_Renderer* renderer;
VariableWindow* vw;
KeyLog* kl;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
const ImWchar* GetPua() {
	static const ImWchar ranges[] = {
		0xE000,
		0xE900, // PUA
		0,
	};
	return &ranges[0];
}
const ImWchar* GetKanji() {
	static const ImWchar ranges[] = {
		0x2000,
		0x206F, // General Punctuation
		0x3000,
		0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
		0x31F0,
		0x31FF, // Katakana Phonetic Extensions
		0xFF00,
		0xFFEF, // Half-width characters
		0xFFFD,
		0xFFFD, // Invalid
		0x4e00,
		0x9FAF, // CJK Ideograms
		0,
	};
	return &ranges[0];
}
void gui_loop() {
	if (!m_emu->Running())
		return;

	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	static MemoryEditor mem_edit;
	if (n_ram_buffer != nullptr && me_mmu != nullptr) {
#define SColor \
	i++ % 2 ? ImColor{ 40, 120, 40, 255 } : ImColor { 40, 40, 120, 255 }
		int i = 0;
		mem_edit.ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
			return me_mmu->ReadData(off + 0x9000);
		};
		mem_edit.WriteFn = [](ImU8* data, size_t off, ImU8 d) {
			return me_mmu->WriteData(off + 0x9000, d);
		};

		unsigned long long v = 0x9188;
#if LANGUAGE == 2
		MemoryEditor::OptionalMarkedSpans spans{ std::vector<MemoryEditor::MarkedSpan>{
			MemoryEditor::MarkedSpan{ v, 0x2, SColor, "VctA(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "VctB(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "VctC(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "VctD(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "MatAns(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "MatAns虚数部分/FB1A(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1B(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1C(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1D(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1E(a*b)" },
			MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1F(a*b)" },
			MemoryEditor::MarkedSpan{ 0x919E, 0x2, SColor, " 输入的字符(小端) " },
			MemoryEditor::MarkedSpan{ 0x91EE, 0x2, SColor, " 输入的字符(小端)(2) " },
			MemoryEditor::MarkedSpan{ 0x91F0, 0x1, SColor, " 光标字符位置备份 " },
			MemoryEditor::MarkedSpan{ 0xF800, 0x800, SColor, " 屏幕缓冲区 " },
			MemoryEditor::MarkedSpan{ 0xCA54, 0x1800, SColor, " 屏幕缓冲区2 " },
			MemoryEditor::MarkedSpan{ 0xE254, 0xF000 - 0xE254, SColor, " 栈 " },
			MemoryEditor::MarkedSpan{ 0x91A0, 0x1, SColor, " 按键修饰符 " },
			MemoryEditor::MarkedSpan{ 0x91A1, 0x1, SColor, " 模式 " },
			MemoryEditor::MarkedSpan{ 0x91A2, 0x1, SColor, " 子模式 " },
			MemoryEditor::MarkedSpan{ 0x91A3, 0x1, SColor, " 屏幕状态(0:输入公式,1:主屏幕,3:其他菜单) " },
			MemoryEditor::MarkedSpan{ 0x91A4, 0x1, SColor, " 界面?反正和屏幕有关就对了 " },
			MemoryEditor::MarkedSpan{ 0x91A6, 0x1, SColor, " 是否闪烁光标 " },
			MemoryEditor::MarkedSpan{ 0x91AA, 0x1, SColor, " 数字格式设置 " },
			MemoryEditor::MarkedSpan{ 0x91AB, 0x1, SColor, " 数字格式设置2 " },
			MemoryEditor::MarkedSpan{ 0x91AC, 0x1, SColor, " 小数点类型 " },
			MemoryEditor::MarkedSpan{ 0x91AD, 0x1, SColor, " 角度单位设置 " },
			MemoryEditor::MarkedSpan{ 0x91AE, 0x1, SColor, " 数学模式开关 " },
			MemoryEditor::MarkedSpan{ 0x91AF, 0x1, SColor, " 分数类型 " },
			MemoryEditor::MarkedSpan{ 0x91B0, 0x1, SColor, " 复数类型 " },
			MemoryEditor::MarkedSpan{ 0x91B1, 0x1, SColor, " 频数 " },
			MemoryEditor::MarkedSpan{ 0x91B2, 0x1, SColor, " 10的幂的类型 " },
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
			MemoryEditor::MarkedSpan{ 0x91C3, 0x1, SColor, " 光标大小 " },
			MemoryEditor::MarkedSpan{ 0x91C4, 0x1, SColor, " 表格滚动开始 " },
			MemoryEditor::MarkedSpan{ 0x91C5, 0x1, SColor, " 表格光标行位置 " },
			MemoryEditor::MarkedSpan{ 0x91C6, 0x1, SColor, " 表格光标列位置" },
			MemoryEditor::MarkedSpan{ 0x91C7, 0x1, SColor, " 光标大小(?) " },
			MemoryEditor::MarkedSpan{ 0x91D0, 0x1, SColor, " 滚动Y轴 " },

			MemoryEditor::MarkedSpan{ 0x91E0, 0x2, SColor, " 上一次的KI和KO " },
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
			MemoryEditor::MarkedSpan{ 0x9932, 0xE * 80 * 2, SColor, "统计数据" },
			MemoryEditor::MarkedSpan{ 0xA1F2, 0x1, SColor, "统计数据模式(非0就是双数据统计)" },
			MemoryEditor::MarkedSpan{ 0xA1F4, 0x1, SColor, "统计数据条数" },
			MemoryEditor::MarkedSpan{ 0xA1F6, 0xE * 16, SColor, "MatA(数据)" },
			MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 1, 0xE * 16, SColor, "MatB(数据)" },
			MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 2, 0xE * 16, SColor, "MatC(数据)" },
			MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 3, 0xE * 16, SColor, "MatD(数据)" },
			MemoryEditor::MarkedSpan{ 0xA576, 0x2, SColor, "MatA(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA576 + 2, 0x2, SColor, "MatB(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA576 + 2 * 2, 0x2, SColor, "MatC(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA576 + 2 * 3, 0x2, SColor, "MatD(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA57E, 0xE * 3, SColor, "VctA(数据)" },
			MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 1, 0xE * 3, SColor, "VctB(数据)" },
			MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 2, 0xE * 3, SColor, "VctC(数据)" },
			MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 3, 0xE * 3, SColor, "VctD(数据)" },
			MemoryEditor::MarkedSpan{ 0xA626, 0x2, SColor, "VctA(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA626 + 2, 0x2, SColor, "VctB(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA626 + 2 * 2, 0x2, SColor, "VctC(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA626 + 2 * 3, 0x2, SColor, "VctD(a*b)" },
			MemoryEditor::MarkedSpan{ 0xA62E, 0xE * 9, SColor, "分布模式变量" },
			MemoryEditor::MarkedSpan{ 0xB1CE, 900, SColor, "算法模式程序" },
			MemoryEditor::MarkedSpan{ 0xB552, 0xE * 7 + 2 * 6, SColor, "绘图模式变量" },
			MemoryEditor::MarkedSpan{ 0xA6AC, 2380, SColor, "数据表格数据" },
			MemoryEditor::MarkedSpan{ 0xA6AC + 2380, 2 * 45 * 5, SColor, "数据表格数据(标记，第一个字节是尾随字符串长度，第二个字节:D0==公式 80==数值 00==不存在)" },
			MemoryEditor::MarkedSpan{ 0xB1BA, 2, SColor, "数据表格数值大小" },
			MemoryEditor::MarkedSpan{ 0xB1BA + 2, 2, SColor, "数据表格公式大小" },
			MemoryEditor::MarkedSpan{ 0xB1BA + 4, 2, SColor, "数据表格总大小" },
			MemoryEditor::MarkedSpan{ 0xB1C2, 1, SColor, "数据表格存在" },
			MemoryEditor::MarkedSpan{ 0xB1C3, 1, SColor, "数据表格类型(1==纯数值 2==有公式)" },
			MemoryEditor::MarkedSpan{ 0xBDEC, 0xE, SColor, "Theta变量" },
			MemoryEditor::MarkedSpan{ 0xBA68, 3000, SColor, "激活的应用数据" },
			MemoryEditor::MarkedSpan{ 0x9186, 0x1, SColor, "激活的应用条目数量(统计/函数表格)" },
			MemoryEditor::MarkedSpan{ 0xC620, 0x10, SColor, "内存完整性检查(魔法字符串/Magic string)" },
			MemoryEditor::MarkedSpan{ 0xC860, 0x1F4, SColor, "MathIo 垃圾" },
		} };
#else
		MemoryEditor::OptionalMarkedSpans spans{
			std::vector<MemoryEditor::MarkedSpan>{
				MemoryEditor::MarkedSpan{ v, 0x2, SColor, "Vct/MatA(ab)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "Vct/MatB(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "Vct/MatC(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "Vct/MatD(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "MatAns(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "MatAns Imaginary Part/FB1A(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1B(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1C(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1D(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1E(a*b)" },
				MemoryEditor::MarkedSpan{ v += 2, 0x2, SColor, "FB1F(a*b)" },
				MemoryEditor::MarkedSpan{ 0x919E, 0x2, SColor, " Input characters (little-endian) " },
				MemoryEditor::MarkedSpan{ 0x91EE, 0x2, SColor, " Input characters (little-endian)(2) " },
				MemoryEditor::MarkedSpan{ 0x91F0, 0x1, SColor, " Cursor character position backup " },
				MemoryEditor::MarkedSpan{ 0xF800, 0x800, SColor, " Screen buffer " },
				MemoryEditor::MarkedSpan{ 0xCA54, 0x1800, SColor, " Screen buffer2 " },
				MemoryEditor::MarkedSpan{ 0xE254, 0xF000 - 0xE254, SColor, " Stack " },
				MemoryEditor::MarkedSpan{ 0x91A0, 0x1, SColor, " Key modifiers " },
				MemoryEditor::MarkedSpan{ 0x91A1, 0x1, SColor, " Mode " },
				MemoryEditor::MarkedSpan{ 0x91A2, 0x1, SColor, " Submode " },
				MemoryEditor::MarkedSpan{ 0x91A3, 0x1, SColor, " Screen state (0:input formula, 1:main screen, 3:other menus) " },
				MemoryEditor::MarkedSpan{ 0x91A4, 0x1, SColor, " Interface? Anyway it's related to screen" },
				MemoryEditor::MarkedSpan{ 0x91A6, 0x1, SColor, " Whether to blink the cursor " },
				MemoryEditor::MarkedSpan{ 0x91AA, 0x1, SColor, " Numeric format settings " },
				MemoryEditor::MarkedSpan{ 0x91AB, 0x1, SColor, " Numeric format settings 2 " },
				MemoryEditor::MarkedSpan{ 0x91AC, 0x1, SColor, " Decimal type " },
				MemoryEditor::MarkedSpan{ 0x91AD, 0x1, SColor, " Angle unit setting " },
				MemoryEditor::MarkedSpan{ 0x91AE, 0x1, SColor, " Math mode switch " },
				MemoryEditor::MarkedSpan{ 0x91AF, 0x1, SColor, " Fraction type " },
				MemoryEditor::MarkedSpan{ 0x91B0, 0x1, SColor, " Complex number type " },
				MemoryEditor::MarkedSpan{ 0x91B1, 0x1, SColor, " Frequency " },
				MemoryEditor::MarkedSpan{ 0x91B2, 0x1, SColor, " Type of 10's exponent " },
				MemoryEditor::MarkedSpan{ 0x91B3, 0x1, SColor, " Automatic simplification " },
				MemoryEditor::MarkedSpan{ 0x91B4, 0x1, SColor, " Decimal output switch " },
				MemoryEditor::MarkedSpan{ 0x91B5, 0x1, SColor, " Automatic power-off duration (1=60min) " },
				MemoryEditor::MarkedSpan{ 0x91B6, 0x1, SColor, " Function table type " },
				MemoryEditor::MarkedSpan{ 0x91B7, 0x1, SColor, " Engineering symbols " },
				MemoryEditor::MarkedSpan{ 0x91B8, 0x1, SColor, " Numeric separator " },
				MemoryEditor::MarkedSpan{ 0x91B9, 0x1, SColor, " Font setting (true) " },
				MemoryEditor::MarkedSpan{ 0x91BA, 0x1, SColor, " Solve equation complex roots " },
				MemoryEditor::MarkedSpan{ 0x91BB, 0x1, SColor, " Language setting" },
				MemoryEditor::MarkedSpan{ 0x91BC, 0x1, SColor, " Automatic calculation " },
				MemoryEditor::MarkedSpan{ 0x91BD, 0x1, SColor, " Data table display " },
				MemoryEditor::MarkedSpan{ 0x91BE, 0x1, SColor, " Two-dimensional code type (3 or 11) " },
				MemoryEditor::MarkedSpan{ 0x91BF, 0x1, SColor, " Algorithm background " },
				MemoryEditor::MarkedSpan{ 0x91C0, 0x1, SColor, " Algorithm unit" },
				MemoryEditor::MarkedSpan{ 0x91C2, 0x1, SColor, " Cursor shape " },
				MemoryEditor::MarkedSpan{ 0x91C3, 0x1, SColor, " Cursor size " },
				MemoryEditor::MarkedSpan{ 0x91C4, 0x1, SColor, " Table scrolling start " },
				MemoryEditor::MarkedSpan{ 0x91C5, 0x1, SColor, " Table cursor row position " },
				MemoryEditor::MarkedSpan{ 0x91C6, 0x1, SColor, " Table cursor column position" },
				MemoryEditor::MarkedSpan{ 0x91C7, 0x1, SColor, " Cursor size(?) " },
				MemoryEditor::MarkedSpan{ 0x91D0, 0x1, SColor, " Scroll Y-axis " },
				MemoryEditor::MarkedSpan{ 0x91E0, 0x2, SColor, " Previous KI and KO " },
				MemoryEditor::MarkedSpan{ 0x91E3, 0x1, SColor, " Contrast " },
				MemoryEditor::MarkedSpan{ 0x91E5, 0x1, SColor, " Cursor character position " },
				MemoryEditor::MarkedSpan{ 0x91E8, 0x1, SColor, " Cursor X-axis " },
				MemoryEditor::MarkedSpan{ 0x91E9, 0x1, SColor, " Cursor Y-axis " },
				MemoryEditor::MarkedSpan{ 0x91EA, 0x1, SColor, " Linear scroll start value (not used in math mode) " },
				MemoryEditor::MarkedSpan{ 0x91F1, 0x1, SColor, " Menu icon " },
				MemoryEditor::MarkedSpan{ 0x91F8, 0x1, SColor, " Cursor size " },
				MemoryEditor::MarkedSpan{ 0x91FD, 0x1, SColor, " Function interface " },
				MemoryEditor::MarkedSpan{ 0x91FF, 0x1, SColor, " Variable settings interface (enable if > 1) " },
				MemoryEditor::MarkedSpan{ 0x9200, 0x1, SColor, " Variable settings interface (variable number) " },
				MemoryEditor::MarkedSpan{ 0x9201, 0x1, SColor, " Verification mode " },
				MemoryEditor::MarkedSpan{ 0x9207, 0x1, SColor, " Prevent entering self-test " },
				MemoryEditor::MarkedSpan{ 0x9206, 0x1, SColor, " Self-test correct count " },
				MemoryEditor::MarkedSpan{ 0x9205, 0x1, SColor, " Self-test count " },
				MemoryEditor::MarkedSpan{ 0x924C, 0xE, SColor, "Ans" },
				MemoryEditor::MarkedSpan{ 0x925A, 0xE, SColor, "Ans Imaginary Part/2nd Result" },
				MemoryEditor::MarkedSpan{ 0x9268, 200, SColor, "Input area" },
				MemoryEditor::MarkedSpan{ 0x9268 + 200, 200, SColor, "Buffer area" },
				MemoryEditor::MarkedSpan{ 0x9268 + 400, 400, SColor, "History area" },
				MemoryEditor::MarkedSpan{ 0x9268 + 800, 200, SColor, "Result area/Analysis area" },
				MemoryEditor::MarkedSpan{ 0x9650, 0xC, SColor, "Random number seed" },
				MemoryEditor::MarkedSpan{ 0x965C, 0x2, SColor, "Unstable bytes" },
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
				MemoryEditor::MarkedSpan{ 0x965E + 0xE * 11, 0xE, SColor, "Ans Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x966C + 0xE * 11, 0xE, SColor, "A Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x967A + 0xE * 11, 0xE, SColor, "B Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x9688 + 0xE * 11, 0xE, SColor, "C Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x9696 + 0xE * 11, 0xE, SColor, "D Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96A4 + 0xE * 11, 0xE, SColor, "E Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96B2 + 0xE * 11, 0xE, SColor, "F Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96C0 + 0xE * 11, 0xE, SColor, "x Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96CE + 0xE * 11, 0xE, SColor, "y Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96DC + 0xE * 11, 0xE, SColor, "z Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x96EA + 0xE * 11, 0xE, SColor, "PreAns Imaginary Part" },
				MemoryEditor::MarkedSpan{ 0x97A2, 200, SColor, "f(x)" },
				MemoryEditor::MarkedSpan{ 0x986A, 200, SColor, "g(x)" },
				MemoryEditor::MarkedSpan{ 0x9932, 0xE * 80 * 2, SColor, "Statistics mode data" },
				MemoryEditor::MarkedSpan{ 0xA1F2, 0x1, SColor, "Statistics mode data mode (non-0 is 2-var stat)" },
				MemoryEditor::MarkedSpan{ 0xA1F4, 0x1, SColor, "Statistics mode data count" },
				MemoryEditor::MarkedSpan{ 0xA1F6, 0xE * 16, SColor, "MatA(data)" },
				MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 1, 0xE * 16, SColor, "MatB(data)" },
				MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 2, 0xE * 16, SColor, "MatC(data)" },
				MemoryEditor::MarkedSpan{ 0xA1F6 + 0xE * 16 * 3, 0xE * 16, SColor, "MatD(data)" },
				MemoryEditor::MarkedSpan{ 0xA576, 0x2, SColor, "MatA(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA576 + 2, 0x2, SColor, "MatB(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA576 + 2 * 2, 0x2, SColor, "MatC(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA576 + 2 * 3, 0x2, SColor, "MatD(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA57E, 0xE * 3, SColor, "VctA(data)" },
				MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 1, 0xE * 3, SColor, "VctB(data)" },
				MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 2, 0xE * 3, SColor, "VctC(data)" },
				MemoryEditor::MarkedSpan{ 0xA57E + 0xE * 3 * 3, 0xE * 3, SColor, "VctD(data)" },
				MemoryEditor::MarkedSpan{ 0xA626, 0x2, SColor, "VctA(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA626 + 2, 0x2, SColor, "VctB(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA626 + 2 * 2, 0x2, SColor, "VctC(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA626 + 2 * 3, 0x2, SColor, "VctD(a*b)" },
				MemoryEditor::MarkedSpan{ 0xA62E, 0xE * 9, SColor, "Distribution mode variable" },
				MemoryEditor::MarkedSpan{ 0xB1CE, 900, SColor, "Algorithm mode program" },
				MemoryEditor::MarkedSpan{ 0xB552, 0xE * 7 + 2 * 6, SColor, "Plotting mode variable" },
				MemoryEditor::MarkedSpan{ 0xA6AC, 2380, SColor, "Data table data" },
				MemoryEditor::MarkedSpan{ 0xA6AC + 2380, 2 * 45 * 5, SColor, "Data table data (markers, first byte is tail string length, second byte: D0=formula 80=value 00=nonexistent)" },
				MemoryEditor::MarkedSpan{ 0xB1BA, 2, SColor, "Data table value size" },
				MemoryEditor::MarkedSpan{ 0xB1BA + 2, 2, SColor, "Data table formula size" },
				MemoryEditor::MarkedSpan{ 0xB1BA + 4, 2, SColor, "Data table total size" },
				MemoryEditor::MarkedSpan{ 0xB1C2, 1, SColor, "Data table existence" },
				MemoryEditor::MarkedSpan{ 0xB1C3, 1, SColor, "Data table type (1==value only 2==with formula)" },
				MemoryEditor::MarkedSpan{ 0xBDEC, 0xE, SColor, "Theta variable" },
				MemoryEditor::MarkedSpan{ 0xBA68, 3000, SColor, "Activated application data" },
				MemoryEditor::MarkedSpan{ 0x9186, 0x1, SColor, "Activated application entry count (statistics/function table)" },
				MemoryEditor::MarkedSpan{ 0xC620, 0x10, SColor, "Memory integrity check (magic string)" },
				MemoryEditor::MarkedSpan{ 0xC860, 0x1F4, SColor, "MathIo garbage" },
			}
		};
#endif
		mem_edit.DrawWindow(
#if LANGUAGE == 2
			"内存编辑器"
#else
			"Hex editor"
#endif
			,
			0, 0x7000, 0x9000, spans);
	}
	if (code_viewer)
		code_viewer->DrawWindow();
	injector->Show();
	membp->Show();
	// ww->Show();
	vw->Draw();
	if (kl)
		kl->Draw();
	amv->Draw();
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
	io.Fonts->AddFontDefault();
	ImFontConfig config;
	config.MergeMode = true;
#if LANGUAGE == 2
	io.Fonts->AddFontFromFileTTF("NotoSansSC-Medium.otf", 18, &config, GetKanji());
#else
#endif
	// config.GlyphOffset = ImVec2(0,1.5);
	if (std::filesystem::exists("CWIICN.ttf")) {
		io.Fonts->AddFontFromFileTTF("CWIICN.ttf", 12, &config, GetPua());
		kl = new KeyLog();
	}
	else {
		std::cout << "please put CWIICN.ttf in current directory to support keylog.";
	}
	io.Fonts->Build();
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
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	auto disas_path = m_emu->GetModelFilePath("_disas.txt");
	if (std::filesystem::exists(disas_path))
		code_viewer = new CodeViewer(disas_path);
	injector = new Injector();
	membp = new MemBreakPoint();
	ww = new WatchWindow();
	vw = new VariableWindow();
	amv = new AppMemoryView();
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
