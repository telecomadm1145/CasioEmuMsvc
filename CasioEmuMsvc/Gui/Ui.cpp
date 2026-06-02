#include "Ui.hpp"
#include "hex.hpp"
#include "5800FileSystem.h"
#include "AddressWindow.h"
#include "BitmapViewer.h"
#include "CallAnalysis.h"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "CodeViewer.hpp"
#include "Editors.h"
#include "HwController.h"
#include "Injector.hpp"
#include "LabelFile.h"
#include "LabelViewer.h"
#include "MemBreakPoint.hpp"
#include "Random.hpp"
#include "Theme.h"
#include "VariableWindow.h"
#include "WatchWindow.hpp"
#ifndef TEST_BUILD
#include "Rop/RopCompilerUI.h"
#include "PluginLogWindow.hpp"
#include "SnapshotWindow.h"
#endif
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <Gui.h>
#include <SDL.h>
#include <filesystem>
#ifdef ENABLE_SENTRY
#include <sentry.h>
#endif
#include <sdl_win32_extra.h>
bool show_sentry_feedback = false;
char sentry_user_comments[1024] = "";
char sentry_user_email[128] = "";
char sentry_user_name[128] = "";

char* n_ram_buffer = 0;
casioemu::MMU* me_mmu = 0;
SDL_Window* window = 0;
SDL_Renderer* renderer = 0;

std::vector<Label> g_labels;

CodeViewer* code_viewer = 0;
Injector* injector = 0;
int top_bar_size = 0;
Breakpoints* membp = 0;

std::vector<UIWindow*> windows{};

void RenderStatusBar() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float barHeight = ImGui::GetFrameHeight() + 4.0f;
	
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - barHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
	
	if (ImGui::Begin("##StatusBar", nullptr, 
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoDocking)) {
		
		// Run/Pause state status indicator
		if (m_emu->GetPaused()) {
			ImGui::TextColored(UIHelpers::kColorWarning, "\xe2\x8f\xb8 %s", "StatusBar.Paused"_lc);  // ⏸
		} else {
			ImGui::TextColored(UIHelpers::kColorSuccess, "\xe2\x96\xb6 %s", "StatusBar.Running"_lc); // ▶
		}
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		// Current PC
		ImGui::Text("PC: %05X", pc_cache);
		
		ImGui::SameLine(0.0f, 20.0f);
		ImGui::TextDisabled("|");
		ImGui::SameLine(0.0f, 20.0f);
		
		// Breakpoints count
		int bpCount = code_viewer ? (int)code_viewer->GetBreakpointCount() : 0;
		ImGui::Text("BP: %d", bpCount);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void gui_loop() {
	if (!m_emu->Running())
		return;

	ImGuiIO& io = ImGui::GetIO();

#ifdef __ANDROID__
	ThemeManager::Instance().UpdateUIScale();
#endif

	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
#ifndef __ANDROID__
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
#endif
	for (auto win : windows) {
#ifndef __ANDROID__
		ImGui::SetNextWindowDockID(ImGui::GetCurrentContext()->DockContext.Nodes.Data[0].key, ImGuiCond_FirstUseEver);
#endif
		win->Render();
	}

#ifndef __ANDROID__
	RenderStatusBar();
#endif

	//	ImGui::Begin("Testing");
	//	if (ImGui::Button("Crash"_lc)) {
	//		throw 0;
	//	}
	//	// --- 新增：手动反馈选项 ---
	// #ifdef ENABLE_SENTRY
	//	ImGui::SameLine(); // 放在 Crash 按钮旁边
	//	if (ImGui::Button("Send Feedback"_lc)) {
	//		// 重置之前的输入内容
	//		memset(sentry_user_comments, 0, sizeof(sentry_user_comments));
	//		show_sentry_feedback = true;
	//	}
	// #endif
	//	ImGui::End();
	//	// --- Sentry 反馈对话框逻辑 ---
	// #ifdef ENABLE_SENTRY
	//	if (show_sentry_feedback) {
	//		// 确保每一帧都调用 OpenPopup，直到它真正打开
	//		ImGui::OpenPopup("User Feedback");
	//	}
	//
	//	// 使用 Modal 窗口确保反馈过程不被打断
	//	if (ImGui::BeginPopupModal("User Feedback", &show_sentry_feedback, ImGuiWindowFlags_AlwaysAutoResize)) {
	//		ImGui::Text("Help us improve CasioEmuMsvc!");
	//		ImGui::Separator();
	//
	//		ImGui::Text("Email (Optional):");
	//		ImGui::InputText("##email", sentry_user_email, IM_ARRAYSIZE(sentry_user_email));
	//
	//		ImGui::Text("What happened?");
	//		ImGui::InputTextMultiline("##comments", sentry_user_comments, IM_ARRAYSIZE(sentry_user_comments),
	//			ImVec2(350, 120), ImGuiInputTextFlags_AllowTabInput);
	//
	//		if (ImGui::Button("Submit", ImVec2(120, 0))) {
	//			auto uuid = Binary::LoadOrInit("uuid.bin", util::Random::getRandomObject<sentry_uuid_t>());
	//			char buf[37]{};
	//			sentry_uuid_as_string(&uuid, buf);
	//			sentry_value_t feedback = sentry_value_new_feedback(sentry_user_comments, sentry_user_email, buf, 0);
	//			sentry_capture_feedback(feedback);
	//
	//			show_sentry_feedback = false;
	//			ImGui::CloseCurrentPopup();
	//		}
	//
	//		ImGui::SameLine();
	//		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
	//			show_sentry_feedback = false;
	//			ImGui::CloseCurrentPopup();
	//		}
	//		ImGui::EndPopup();
	//	}
	// #endif

#ifdef SINGLE_WINDOW
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::Begin("Overlay", nullptr,
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove);

	auto& tm = ThemeManager::Instance();
	float safeAreaPadding = tm.padding * 1.5f;
	ImGui::SetWindowPos(ImVec2(safeAreaPadding, safeAreaPadding));

	float displayWidth = ImGui::GetIO().DisplaySize.x;
	float totalWidth = displayWidth - (safeAreaPadding * 2);
	float spacingBetweenElements = tm.padding * 1.2f;
	float buttonWidth = (totalWidth - spacingBetweenElements * 2) * 0.25f;
	float comboWidth = totalWidth - (buttonWidth * 2) - (spacingBetweenElements * 2);

	static UIWindow* current_filter = 0;
	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("##cb", current_filter ? current_filter->name : 0)) {
		for (int n = 0; n < windows.size(); n++) {
			bool is_selected = (current_filter == windows[n]);
			if (ImGui::Selectable(windows[n]->name, is_selected))
				current_filter = windows[n];
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine(0, spacingBetweenElements);
	ImVec2 buttonSize(buttonWidth, tm.buttonHeight * 1.2f);
	if (ImGui::Button("Open", buttonSize)) {
		if (current_filter != 0)
			current_filter->open = true;
	}

	ImGui::SameLine(0, spacingBetweenElements);
	if (ImGui::Button("Close all", buttonSize)) {
		for (auto& win : windows) {
			win->open = false;
		}
	}
	// Let's record where we are.
	top_bar_size = ImGui::GetCursorPosY();
	ImGui::End();
#endif
	ImGui::Render();
#ifdef SINGLE_WINDOW
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
#else
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
	SDL_RenderPresent(renderer);
#endif
}

CodeViewer* test_gui(bool* guiCreated, SDL_Window* wnd, SDL_Renderer* rnd) {
	SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

#ifdef SINGLE_WINDOW
	window = wnd;
	renderer = rnd;
#else
#ifdef __ANDROID__
	window = SDL_CreateWindow("CasioEmuMsvc Debugger",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		(int)ThemeManager::Instance().windowWidth,
		(int)ThemeManager::Instance().windowHeight,
		SDL_WINDOW_RESIZABLE);
#else
	window = SDL_CreateWindow("CasioEmuMsvc Debugger",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		1600, 1080,
		SDL_WINDOW_RESIZABLE);
#endif
#ifdef _WIN32
	EnableDarkTitleBar(GetSDLWindowHandle(window));
#endif
	renderer = SDL_CreateRenderer(window, -1,
		SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
#endif

	if (renderer == nullptr) {
		SDL_Log("Error creating SDL_Renderer!");
		return 0;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

#ifdef __ANDROID__
	ThemeManager::Instance().UpdateUIScale();
#endif

	RebuildFont();
	// SetupDefaultTheme();

	io.WantCaptureKeyboard = true;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer2_Init(renderer);
	if (guiCreated)
		*guiCreated = true;
	while (!me_mmu)
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	auto label_file = m_emu->GetModelFilePath("labels.txt");
	if (std::filesystem::exists(label_file))
		g_labels = parseFile(label_file);
	else
		std::cout << "[Warning] labels.txt doesn't exist. You can consider create one for better debugging experiences. Format: address(0x1234),func name(can be quoted)\n";

	if (m_emu->hardware_id == casioemu::HW_FX_5800P) {
		windows.push_back(CreateFx5800FileSystem());
	}

	for (auto item : std::initializer_list<UIWindow*>{
			 new VariableWindow(),
			 new HwController(),
			 new LabelViewer(),
			 new WatchWindow(),
			 CreateCallAnalysisWindow(),
			 code_viewer = new CodeViewer(),
			 injector = new Injector(),
			 membp = new Breakpoints(),
			 CreateAddressWindow(),
			 // MakeAssemblerUI(),
#ifndef TEST_BUILD
			 CreateRopCompilerWindow(),
			 new PluginLogWindow(),
			 CreateSnapshotWindow(),
#endif
			 MakeThemeWindow(),
			 CreateBitmapViewer(), })
		windows.push_back(item);
	for (auto item : GetEditors())
		windows.push_back(item);

#ifdef __ANDROID__
	for (auto item : windows) {
		item->open = false;
	}
#endif

	return 0;
}

namespace UIHelpers {

	void JumpToMemory(uint32_t addr) {
		// Prefer the "Ram" window; fall back to any window that overrides GotoMemoryAddress.
		UIWindow* fallback = nullptr;
		for (auto* win : windows) {
			const char* n = win->name;
			if (n && strcmp(n, "Ram") == 0) {
				win->GotoMemoryAddress(addr);
				return;
			}
			// Track first editor-like window as fallback
			if (!fallback && n && (strcmp(n, "Rom") == 0 || strcmp(n, "All") == 0
				|| strcmp(n, "PRam") == 0 || strcmp(n, "Flash") == 0)) {
				fallback = win;
			}
		}
		if (fallback) {
			fallback->GotoMemoryAddress(addr);
		}
	}

	void ClickableAddress(uint32_t addr, JumpTarget defaultTarget) {
		// Render the colored address text
		ImGui::PushStyleColor(ImGuiCol_Text, kColorInfo);
		char addrLabel[16];
		snprintf(addrLabel, sizeof(addrLabel), "%05X", addr);
		ImGui::TextUnformatted(addrLabel);
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			ImGui::BeginTooltip();
			if (defaultTarget == JumpTarget::Code) {
				ImGui::Text("ClickableAddress.CodeJumpTooltip"_lc, addr);
				ImGui::TextDisabled("ClickableAddress.RightClickHint"_lc);
			} else if (defaultTarget == JumpTarget::Memory) {
				ImGui::Text("ClickableAddress.MemJumpTooltip"_lc, addr);
				ImGui::TextDisabled("ClickableAddress.RightClickHint"_lc);
			} else {
				ImGui::Text("ClickableAddress.BothTooltip"_lc, addr);
			}
			ImGui::EndTooltip();
		}

		// Left-click: default action
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
			if (defaultTarget == JumpTarget::Code || defaultTarget == JumpTarget::Both) {
				if (code_viewer) {
					code_viewer->JumpTo(addr);
					code_viewer->BringToFront();
				}
			} else {
				JumpToMemory(addr);
			}
		}

		// Right-click: context menu with both options
		char popupId[32];
		snprintf(popupId, sizeof(popupId), "##ca_popup_%05X", addr);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup(popupId);
		}
		if (ImGui::BeginPopup(popupId)) {
			ImGui::TextDisabled("0x%05X", addr);
			ImGui::Separator();
			if (ImGui::MenuItem("ClickableAddress.CodeJump"_lc)) {
				if (code_viewer) {
					code_viewer->JumpTo(addr);
					code_viewer->BringToFront();
				}
			}
			if (ImGui::MenuItem("ClickableAddress.MemJump"_lc)) {
				JumpToMemory(addr);
			}
			ImGui::EndPopup();
		}
	}
}


void gui_cleanup() {
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}