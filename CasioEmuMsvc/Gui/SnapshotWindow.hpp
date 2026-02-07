#pragma once
#include "Ui.hpp"
#include "../Snapshot.hpp"
#include <SDL.h>
#include <map>

class SnapshotWindow : public UIWindow {
    casioemu::SnapshotManager& manager;
    std::map<casioemu::SnapshotNode*, SDL_Texture*> thumbnails;
    char descriptionBuf[256] = "";

    // Helper to get or create texture
    SDL_Texture* GetTexture(casioemu::SnapshotNode* node);
    void DrawTree(casioemu::SnapshotNode* node);

public:
    SnapshotWindow(casioemu::SnapshotManager& mgr);
    ~SnapshotWindow();
    void RenderCore() override;
};
