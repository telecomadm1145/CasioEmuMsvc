#pragma once
#include <SDL.h>
#include "imgui/imgui.h"
#include "LabelFile.h"

class GDBServer;
class CodeViewer;
CodeViewer* test_gui(bool* guiCreated,SDL_Window*,SDL_Renderer*, GDBServer* gdbServer);
void gui_cleanup();
void gui_loop();
extern SDL_Window* window;
extern SDL_Renderer* renderer;
extern std::vector<Label> g_labels;

void SetDebugbreak(void);
class UIWindow {
public:
    UIWindow(const char* name) : name(name) {
        inital_size = ImVec2(800, 800);
    }
    const char* name{};
    bool open = true;
    ImVec2 inital_size;
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
    virtual ~UIWindow() {}
};

inline constexpr ImGuiTableFlags pretty_table = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable;