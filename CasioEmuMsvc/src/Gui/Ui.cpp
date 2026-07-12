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
#include "RendererBackend.h"
#include "Theme.h"
#include "VariableWindow.h"
#include "WatchWindow.hpp"
#ifndef CASIOEMU_CORE_WEB
#include "QrCodeWindow.h"
#endif
#ifndef TEST_BUILD
#include "Rop/RopCompilerUI.h"
#include "PluginLogWindow.hpp"
#include "SnapshotWindow.h"
#endif
#include "imgui/imgui.h"
#ifdef CASIOEMU_CORE_WEB
#include "WebDebuggerGui.h"
#else
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#endif
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
SnapshotWindow* snapshot_window = 0;

std::vector<UIWindow*> windows{};

#ifdef CASIOEMU_CORE_WEB
SDL_Surface* background = nullptr;
SDL_Texture* bg_txt = nullptr;
#endif

static float GetStatusBarHeight() {
	return ImGui::GetFrameHeight() + 4.0f;
}

void RenderStatusBar() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float barHeight = GetStatusBarHeight();
	
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - barHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 2.0f));
#ifdef CASIOEMU_CORE_WEB
	ImVec4 statusBg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
	statusBg.w = std::max(statusBg.w, 0.82f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, statusBg);
#else
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 1.0f));
#endif
	
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

static ImGuiID RenderDockSpace(float reservedBottom) {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float dockHeight = viewport->Size.y - reservedBottom;
	if (dockHeight < 1.0f) dockHeight = 1.0f;

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, dockHeight));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::Begin("##DebuggerDockSpaceHost", nullptr, flags);
	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
	ImGui::PopStyleVar(3);
	return dockspace_id;
}

static void RenderDebuggerGuiWindows() {
#ifndef __ANDROID__
	ImGuiID dockspace_id = RenderDockSpace(GetStatusBarHeight());
#endif
	for (auto win : windows) {
#ifndef __ANDROID__
		if (dockspace_id != 0) {
			ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
		}
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
}

void gui_loop() {
	if (!m_emu->Running())
		return;

	ImGuiIO& io = ImGui::GetIO();

#ifdef __ANDROID__
	ThemeManager::Instance().UpdateUIScale();
#endif

#ifndef CASIOEMU_CORE_WEB
	ImGui_ImplSDLRenderer2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
#endif
	ImGui::NewFrame();
	RenderDebuggerGuiWindows();
	ImGui::Render();
#ifndef CASIOEMU_CORE_WEB
#ifdef SINGLE_WINDOW
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
#else
	ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
	SDL_RenderPresent(renderer);
#endif
#endif
}

static CodeViewer* CreateDebuggerGuiWindows() {
	while (!me_mmu)
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	std::filesystem::path label_file = m_emu->GetModelFilePath("labels.txt");
	if (!label_file.empty() && std::filesystem::exists(label_file))
		g_labels = parseFile(label_file.string());
	else if (!m_emu->IsMemoryModel())
		std::cout << "[Warning] " << label_file.string() << " doesn't exist. You can consider create one for better debugging experiences. Format: address(0x1234),func name(can be quoted)\n";

	if (m_emu->hardware_id == casioemu::HW_FX_5800P) {
		windows.push_back(CreateFx5800FileSystem());
	}

	if (m_emu->hardware_id != casioemu::HW_SOLARII) {
		windows.push_back(new VariableWindow());
	}

	for (auto item : std::initializer_list<UIWindow*>{
			 new HwController(),
			 new LabelViewer(),
			 new WatchWindow(),
			 CreateCallAnalysisWindow(),
			 code_viewer = new CodeViewer(),
			 injector = new Injector(),
			 membp = new Breakpoints(),
			 CreateAddressWindow(),
			 // MakeAssemblerUI(),
#if !defined(TEST_BUILD)
			 CreateRopCompilerWindow(),
#endif
#if !defined(TEST_BUILD) && !defined(CASIOEMU_CORE_WEB)
			 new PluginLogWindow(),
#endif
#if !defined(TEST_BUILD)
			 snapshot_window = static_cast<SnapshotWindow*>(CreateSnapshotWindow()),
#endif
#ifndef CASIOEMU_CORE_WEB
			 new QrCodeWindow(),
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

#ifndef CASIOEMU_CORE_WEB
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
	int winX = ThemeManager::Instance().Settings().windowX;
	int winY = ThemeManager::Instance().Settings().windowY;
	int winW = ThemeManager::Instance().Settings().windowW;
	int winH = ThemeManager::Instance().Settings().windowH;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(0, &bounds) == 0) {
		if (winW > bounds.w) winW = bounds.w;
		if (winH > bounds.h) winH = bounds.h;

		if (winX != SDL_WINDOWPOS_CENTERED) {
			if (winX < bounds.x) winX = bounds.x;
			if (winX + winW > bounds.x + bounds.w) winX = bounds.x + bounds.w - winW;
		}
		if (winY != SDL_WINDOWPOS_CENTERED) {
			if (winY < bounds.y) winY = bounds.y;
			if (winY + winH > bounds.y + bounds.h) winY = bounds.y + bounds.h - winH;
		}
	}

	window = SDL_CreateWindow("CasioEmuMsvc Debugger",
		winX,
		winY,
		winW, winH,
		SDL_WINDOW_RESIZABLE);
#endif
#ifdef _WIN32
	EnableDarkTitleBar(GetSDLWindowHandle(window));
#endif
	casioemu::SetPreferredRendererDriverHint();
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

	// RebuildFont();
	// SetupDefaultTheme();

	io.WantCaptureKeyboard = true;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer2_Init(renderer);

	ThemeManager::Instance().RequestFontRebuild();
	ThemeManager::Instance().ProcessFontRebuild();

	if (guiCreated)
		*guiCreated = true;
	return CreateDebuggerGuiWindows();
}
#endif

#ifdef CASIOEMU_CORE_WEB
void InitWebDebuggerGuiWindows() {
	if (windows.empty()) {
		CreateDebuggerGuiWindows();
	}
}

void RenderWebDebuggerGuiWindows() {
	RenderDebuggerGuiWindows();
}

void CleanupWebDebuggerGuiWindows() {
	for (auto* win : windows) {
		delete win;
	}
	windows.clear();
	code_viewer = nullptr;
	injector = nullptr;
	membp = nullptr;
	g_labels.clear();
}
#endif

namespace UIHelpers {

	void JumpToMemory(uint32_t addr) {
		// Prefer the "Ram" window; fall back to any window that overrides GotoMemoryAddress.
		UIWindow* fallback = nullptr;
		for (auto* win : windows) {
			const char* n = win->name;
			if (n && strcmp(n, "Ram") == 0) {
				win->GotoMemoryAddress(addr);
				win->BringToFront();
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
			fallback->BringToFront();
		}
	}

	void ClickableAddress(uint32_t addr, JumpTarget defaultTarget) {
		char addrLabel[16];
		snprintf(addrLabel, sizeof(addrLabel), "%05X", addr);
		ImGui::PushID(addrLabel);
		const ImVec2 textSize = ImGui::CalcTextSize(addrLabel);
		const ImVec2 textPos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##clickable_address", textSize);
		const bool hovered = ImGui::IsItemHovered();
		const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 textColor = ImGui::GetColorU32(hovered ? ImVec4(0.55f, 0.72f, 1.0f, 1.0f) : kColorInfo);
		drawList->AddText(textPos, textColor, addrLabel);
		if (hovered) {
			const float underlineY = textPos.y + textSize.y;
			drawList->AddLine(ImVec2(textPos.x, underlineY), ImVec2(textPos.x + textSize.x, underlineY), textColor);
		}

		if (hovered) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			ImGui::BeginTooltip();
			if (defaultTarget == JumpTarget::Code) {
				ImGui::Text("ClickableAddress.CodeJumpTooltip"_lc, addr);
				ImGui::TextDisabled("%s", "ClickableAddress.RightClickHint"_lc);
			} else if (defaultTarget == JumpTarget::Memory) {
				ImGui::Text("ClickableAddress.MemJumpTooltip"_lc, addr);
				ImGui::TextDisabled("%s", "ClickableAddress.RightClickHint"_lc);
			} else {
				ImGui::Text("ClickableAddress.BothTooltip"_lc, addr);
			}
			ImGui::EndTooltip();
		}

		// Left-click: default action
		if (leftClicked) {
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
		if (rightClicked) {
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
		ImGui::PopID();
	}
}


void gui_cleanup() {
#ifndef CASIOEMU_CORE_WEB
#ifndef __ANDROID__
#ifndef SINGLE_WINDOW
	if (window) {
		int x, y, w, h;
		SDL_GetWindowPosition(window, &x, &y);
		SDL_GetWindowSize(window, &w, &h);

		ThemeManager::Instance().Settings().windowX = x;
		ThemeManager::Instance().Settings().windowY = y;
		ThemeManager::Instance().Settings().windowW = w;
		ThemeManager::Instance().Settings().windowH = h;
		ThemeManager::Instance().SaveSettings();
	}
#endif
#endif
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
#else
	CleanupWebDebuggerGuiWindows();
#endif
}
