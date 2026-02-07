#include "SnapshotWindow.hpp"
#include "imgui/imgui.h"
#include "Gui.h"
#include "../Emulator.hpp"
#include "FileDialog.hpp"
#include <cstring>

extern SDL_Renderer* renderer;
extern casioemu::Emulator* m_emu;

SnapshotWindow::SnapshotWindow(casioemu::SnapshotManager& mgr) : UIWindow("Snapshots"), manager(mgr) {}

SnapshotWindow::~SnapshotWindow() {
    for(auto& kv : thumbnails) {
        SDL_DestroyTexture(kv.second);
    }
}

SDL_Texture* SnapshotWindow::GetTexture(casioemu::SnapshotNode* node) {
    if(!node || node->thumbnail.size() < sizeof(int) * 2) return nullptr;
    if(thumbnails.count(node)) return thumbnails[node];

    int* hdr = (int*)node->thumbnail.data();
    int w = hdr[0];
    int h = hdr[1];
    void* pixels = node->thumbnail.data() + sizeof(int)*2;

    // Note: this surface references the data in node->thumbnail.
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, w*4, SDL_PIXELFORMAT_RGBA32);
    if(!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);

    if(tex) thumbnails[node] = tex;
    return tex;
}

void SnapshotWindow::DrawTree(casioemu::SnapshotNode* node) {
    if(!node) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if(node == manager.current) flags |= ImGuiTreeNodeFlags_Selected;
    if(node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
    else flags |= ImGuiTreeNodeFlags_DefaultOpen;

    bool open = ImGui::TreeNodeEx((void*)node, flags, "%s", node->description.empty() ? "Snapshot" : node->description.c_str());

    if(ImGui::IsItemClicked()) {
        manager.current = node;
        strncpy(descriptionBuf, node->description.c_str(), sizeof(descriptionBuf)-1);
    }

    if(open) {
        for(auto child : node->children) {
            DrawTree(child);
        }
        ImGui::TreePop();
    }
}

void SnapshotWindow::RenderCore() {
    ImGui::Columns(2);

    // Left: Tree
    ImGui::BeginChild("Tree");
    if(manager.root) {
        DrawTree(manager.root);
    } else {
        ImGui::Text("No snapshots.");
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // Right: Details
    if(manager.current) {
        auto node = manager.current;
        ImGui::Text("Timestamp: %s", std::asctime(std::localtime(&node->timestamp)));

        ImGui::InputText("Description", descriptionBuf, sizeof(descriptionBuf));
        if(ImGui::IsItemDeactivatedAfterEdit()) {
            node->description = descriptionBuf;
        }

        SDL_Texture* tex = GetTexture(node);
        if(tex) {
            int w, h;
            SDL_QueryTexture(tex, NULL, NULL, &w, &h);
            // Keep aspect ratio, max width 200
            float scale = 200.0f / w;
            ImGui::Image((ImTextureID)tex, ImVec2(w*scale, h*scale));
        }

        if(ImGui::Button("Load State")) {
            manager.LoadSnapshot(m_emu->chipset, node);
        }
        ImGui::SameLine();
        if(ImGui::Button("Delete")) {
            if(thumbnails.count(node)) {
                SDL_DestroyTexture(thumbnails[node]);
                thumbnails.erase(node);
            }
            manager.DeleteSnapshot(node);
        }
    }

    ImGui::Separator();
    if(ImGui::Button("Take Snapshot")) {
        void* pixels;
        int pitch;
        if(SDL_LockTexture(m_emu->interface_texture, NULL, &pixels, &pitch) == 0) {
            int w, h;
            SDL_QueryTexture(m_emu->interface_texture, NULL, NULL, &w, &h);

            std::vector<uint8_t> packed(w*h*4);
            uint8_t* src = (uint8_t*)pixels;
            uint8_t* dst = packed.data();
            for(int y=0; y<h; ++y) {
                memcpy(dst + y*w*4, src + y*pitch, w*4);
            }

            SDL_UnlockTexture(m_emu->interface_texture);

            manager.TakeSnapshot(m_emu->chipset, "Snapshot", w, h, packed.data());

            // Auto select
            strncpy(descriptionBuf, "Snapshot", sizeof(descriptionBuf)-1);
        }
    }

    ImGui::SameLine();
    if(ImGui::Button("Save Tree")) {
        char pathBuf[1024] = "";
        if(FileDialog::ShowFileSaveDialog("Save Snapshots", ".snapshot", pathBuf, sizeof(pathBuf))) {
            manager.SaveToFile(pathBuf);
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("Load Tree")) {
        char pathBuf[1024] = "";
        if(FileDialog::ShowFileOpenDialog("Load Snapshots", ".snapshot", pathBuf, sizeof(pathBuf))) {
            manager.LoadFromFile(pathBuf);
            // Clear textures
            for(auto& kv : thumbnails) SDL_DestroyTexture(kv.second);
            thumbnails.clear();
        }
    }

    ImGui::Columns(1);
}
