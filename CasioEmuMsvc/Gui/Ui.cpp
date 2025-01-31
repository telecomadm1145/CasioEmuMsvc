#include "Ui.hpp"
#include "5800FileSystem.h"
#include "AddressWindow.h"
#include "CallAnalysis.h"
#include "CasioData.h"
#include "Chipset/Chipset.hpp"
#include "Chipset/MMU.hpp"
#include "CodeViewer.hpp"
#include "Editors.h"
#include "HwController.h"
#include "Injector.hpp"
#include "LabelFile.h"
#include "LabelViewer.h"
#include "MemBreakPoint.hpp"
#include "Theme.h"
#include "VariableWindow.h"
#include "WatchWindow.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <Assemblier.h>
#include <Gui.h>
#include <SDL.h>
#include <filesystem>

char* n_ram_buffer = 0;
casioemu::MMU* me_mmu = 0;
SDL_Window* window = 0;
SDL_Renderer* renderer = 0;

std::vector<Label> g_labels;

CodeViewer* code_viewer = 0;
Injector* injector = 0;
MemBreakPoint* membp = 0;

std::vector<UIWindow*> windows{};

static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

void gui_loop() {
    if (!m_emu->Running())
        return;

    ImGuiIO& io = ImGui::GetIO();
    
    #ifdef __ANDROID__
    UI::Scaling::UpdateUIScale();
    #endif

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    
    for (auto win : windows) {
        win->Render();
    }

    #ifndef __ANDROID__
    ImGui::Begin("Debug");
    if (ImGui::Button("Crash")) {
        throw 0;
    }
    ImGui::End();
    #endif

    #ifdef __ANDROID__
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("Overlay", nullptr, 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_NoDocking | 
        ImGuiWindowFlags_AlwaysAutoResize | 
        ImGuiWindowFlags_NoBackground | 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoMove);

    float safeAreaPadding = UI::Scaling::padding * 1.5f;
    ImGui::SetWindowPos(ImVec2(safeAreaPadding, safeAreaPadding));

    float displayWidth = ImGui::GetIO().DisplaySize.x;
    float totalWidth = displayWidth - (safeAreaPadding * 2);
    float spacingBetweenElements = UI::Scaling::padding * 1.2f;
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
    ImVec2 buttonSize(buttonWidth, UI::Scaling::buttonHeight * 1.2f);
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

int test_gui(bool* guiCreated, SDL_Window* wnd, SDL_Renderer* rnd) {
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    
    #ifdef SINGLE_WINDOW
    window = wnd;
    renderer = rnd;
    #else
    #ifdef __ANDROID__
    window = SDL_CreateWindow("CasioEmuMsvc Debugger", 
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED, 
        (int)UI::Scaling::windowWidth, 
        (int)UI::Scaling::windowHeight, 
        SDL_WINDOW_RESIZABLE);
    #else
    window = SDL_CreateWindow("CasioEmuMsvc Debugger", 
        SDL_WINDOWPOS_CENTERED, 
        SDL_WINDOWPOS_CENTERED, 
        1280, 720, 
        SDL_WINDOW_RESIZABLE);
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
    UI::Scaling::UpdateUIScale();
    #endif
    
    RebuildFont();
    SetupDefaultTheme();
    
    io.WantCaptureKeyboard = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    if (guiCreated)
        *guiCreated = true;
    while (!me_mmu)
        std::this_thread::sleep_for(std::chrono::microseconds(1));

    g_labels = parseFile(m_emu->GetModelFilePath("labels"));

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
             membp = new MemBreakPoint(),
             CreateAddressWindow(),
             MakeAssemblerUI(),
             MakeThemeWindow()})
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

void gui_cleanup() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}