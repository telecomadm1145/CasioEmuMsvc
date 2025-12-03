#include "Ui.hpp"
#include "CodeViewer.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <vector>

SDL_Window* window = 0;
SDL_Renderer* renderer = 0;
std::vector<Label> g_labels;
std::vector<UIWindow*> windows{};

// Izanagi doesn't use the emulator core, but Ui.hpp (from CasioEmuMsvc/Gui/Ui.hpp probably) might declare gui_loop as taking void.
// However, Izanagi/Ui.hpp declares void gui_loop();

void gui_loop() {
    // ImGuiIO& io = ImGui::GetIO(); // Not strictly needed here unless we use io

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    for (auto win : windows) {
        win->Render();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer);
}

CodeViewer* test_gui(bool* guiCreated, SDL_Window* wnd, SDL_Renderer* rnd, GDBServer* gdbServer) {
    window = wnd;
    renderer = rnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    io.WantCaptureKeyboard = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    if (guiCreated)
        *guiCreated = true;

    // TODO: Load labels if needed, or pass file to CodeViewer
    // g_labels = parseFile("labels.txt");

    auto cv = new CodeViewer();
    windows.push_back(cv);

    return cv;
}

void gui_cleanup() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
