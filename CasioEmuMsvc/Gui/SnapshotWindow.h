#pragma once
#include "Ui.hpp"
#include "Snapshot.h"
#include <SDL.h>
#include <string>

// Preview texture wrapper
struct SnapshotPreview {
    SDL_Texture* Tex  = nullptr;
    int Width  = 0;
    int Height = 0;
    uint32_t NodeId = 0;
    void Free() { if (Tex) { SDL_DestroyTexture(Tex); Tex = nullptr; } }
};

class SnapshotWindow : public UIWindow {
public:
    SnapshotWindow();
    ~SnapshotWindow();
    void RenderCore() override;

private:
    SnapshotManager m_Manager;
    uint32_t        m_SelectedId = 0;     // currently selected node
    char            m_LabelBuf[256] = {}; // label input buffer
    SnapshotPreview m_Preview;
    bool            m_ShowError = false;
    std::string     m_ErrorMsg;
    uint32_t        m_NodeToDelete = 0;
    double          m_LoadSuccessTime = 0.0;

    // Helpers
    void RenderToolbar();
    void RenderTree();
    void RenderTreeNode(uint32_t parentId, int depth);
    void RenderDetails();
    void LoadPreview(const SnapshotNode& node);
    void TryLoadPreview(uint32_t id);

    void ShowError(const std::string& msg);
};

UIWindow* CreateSnapshotWindow();
