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
#include "../models.h"
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
		int i = 0;
		mem_edit.ReadFn = [](const ImU8* data, size_t off) -> ImU8 {
			return me_mmu->ReadData(off + casioemu::GetRamBaseAddr(m_emu->hardware_id));
		};
		mem_edit.WriteFn = [](ImU8* data, size_t off, ImU8 d) {
			return me_mmu->WriteData(off + casioemu::GetRamBaseAddr(m_emu->hardware_id), d);
		};
		MemoryEditor::OptionalMarkedSpans spans{ GetCommonMemLabels(m_emu->hardware_id) };
		mem_edit.DrawWindow(
#if LANGUAGE == 2
			"内存编辑器"
#else
			"Hex editor"
#endif
			,
			0, 0x10000 - casioemu::GetRamBaseAddr(m_emu->hardware_id), casioemu::GetRamBaseAddr(m_emu->hardware_id), spans);
	}
	if (code_viewer)
		code_viewer->DrawWindow();
	injector->Show();
	membp->Show();
	ww->Show();
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
		std::cout << "please put CWIICN.ttf in current directory to support keylog.\n";
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
