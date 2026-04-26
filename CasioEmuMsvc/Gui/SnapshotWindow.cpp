#include "SnapshotWindow.h"
#include "Snapshot.h"
#include "Ui.hpp"
#include "Ext/SysDialog.h"
#include "imgui/imgui.h"
#include <SDL.h>
#include <SDL_image.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include "Localization.h"

// ============================================================
// Helpers
// ============================================================

static std::string FormatTimestamp(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm tm_info;
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    return buf;
}

// ============================================================
// SnapshotWindow
// ============================================================

SnapshotWindow::SnapshotWindow()
    : UIWindow("Snapshot"/*"SnapshotWindow.Title"_lc*/) { // Reminder here: don't localize ImGui window titles as they are linked to imgui presistent storage key
    inital_size = ImVec2(880, 560);
    memset(m_LabelBuf, 0, sizeof(m_LabelBuf));
}

SnapshotWindow::~SnapshotWindow() {
    m_Preview.Free();
}

void SnapshotWindow::ShowError(const std::string& msg) {
    m_ErrorMsg  = msg;
    m_ShowError = true;
}

void SnapshotWindow::LoadPreview(const SnapshotNode& node) {
    if (m_Preview.NodeId == node.Id && m_Preview.Tex) return;
    m_Preview.Free();
    m_Preview.NodeId = node.Id;
    if (node.PreviewPng.empty() || !::renderer) return;

    // Load BMP from memory using SDL_RWops
    SDL_RWops* rw = SDL_RWFromConstMem(node.PreviewPng.data(),
                                       static_cast<int>(node.PreviewPng.size()));
    if (!rw) return;

    SDL_Surface* surface = SDL_LoadBMP_RW(rw, 1); // closes rw
    if (!surface) return;

    m_Preview.Tex    = SDL_CreateTextureFromSurface(::renderer, surface);
    m_Preview.Width  = surface->w;
    m_Preview.Height = surface->h;
    SDL_FreeSurface(surface);
}

void SnapshotWindow::TryLoadPreview(uint32_t id) {
    for (const auto& n : m_Manager.Nodes)
        if (n.Id == id) { LoadPreview(n); return; }
    m_Preview.Free();
}

// ============================================================
// Toolbar
// ============================================================

void SnapshotWindow::RenderToolbar() {
    bool hasEmu = (::m_emu != nullptr);

    // --- Save button ---
    if (!hasEmu) ImGui::BeginDisabled();
    if (ImGui::Button("SnapshotWindow.Save"_lc)) {
        try {
            std::string lbl(m_LabelBuf);
            if (lbl.empty()) lbl = std::string("SnapshotWindow.SnapshotPrefix"_lc) + std::to_string(m_Manager.Nodes.size() + 1);
            uint32_t newId = m_Manager.SaveSnapshot(*::m_emu, m_SelectedId, lbl);
            m_SelectedId = newId;
            TryLoadPreview(newId);
            memset(m_LabelBuf, 0, sizeof(m_LabelBuf));
        } catch (const std::exception& e) {
            ShowError(e.what());
        }
    }
    if (!hasEmu) ImGui::EndDisabled();
    ImGui::SameLine();

    // --- Load button ---
    bool hasSelected = (m_SelectedId != 0 && hasEmu);
    if (!hasSelected) ImGui::BeginDisabled();
    if (ImGui::Button("SnapshotWindow.Load"_lc)) {
        try {
            m_Manager.LoadSnapshot(*::m_emu, m_SelectedId);
        } catch (const std::exception& e) {
            ShowError(e.what());
        }
    }
    if (!hasSelected) ImGui::EndDisabled();
    ImGui::SameLine();

    // --- Delete button ---
    if (!hasSelected) ImGui::BeginDisabled();
    if (ImGui::Button("SnapshotWindow.Delete"_lc)) {
        if (m_SelectedId != 0) {
            m_Manager.DeleteNode(m_SelectedId);
            m_SelectedId = 0;
            m_Preview.Free();
        }
    }
    if (!hasSelected) ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // --- Export selected ---
    if (!hasSelected) ImGui::BeginDisabled();
    if (ImGui::Button("SnapshotWindow.ExportNode"_lc)) {
        if (m_SelectedId != 0) {
            uint32_t exportId = m_SelectedId;
            SystemDialogs::SaveFileDialog("snapshot.snapshot", [this, exportId](std::filesystem::path path) {
                try { m_Manager.ExportNode(path, exportId); }
                catch (const std::exception& e) { ShowError(e.what()); }
            });
        }
    }
    if (!hasSelected) ImGui::EndDisabled();
    ImGui::SameLine();

    // --- Export subtree ---
    if (!hasSelected) ImGui::BeginDisabled();
    if (ImGui::Button("SnapshotWindow.ExportSubtree"_lc)) {
        if (m_SelectedId != 0) {
            uint32_t exportId = m_SelectedId;
            SystemDialogs::SaveFileDialog("snapshot.snapshot", [this, exportId](std::filesystem::path path) {
                try { m_Manager.ExportSubtree(path, exportId); }
                catch (const std::exception& e) { ShowError(e.what()); }
            });
        }
    }
    if (!hasSelected) ImGui::EndDisabled();
    ImGui::SameLine();

    // --- Export all ---
    if (ImGui::Button("SnapshotWindow.ExportAll"_lc)) {
        SystemDialogs::SaveFileDialog("snapshots.snapshot", [this](std::filesystem::path path) {
            try { m_Manager.ExportAll(path); }
            catch (const std::exception& e) { ShowError(e.what()); }
        });
    }
    ImGui::SameLine();

    // --- Import ---
    if (ImGui::Button("SnapshotWindow.Import"_lc)) {
        SystemDialogs::OpenFileDialog([this](std::filesystem::path path) {
            try { m_Manager.ImportFromFile(path); }
            catch (const std::exception& e) { ShowError(e.what()); }
        });
    }

    ImGui::Separator();

    // --- Label input ---
    ImGui::SetNextItemWidth(320);
    ImGui::InputText("SnapshotWindow.Label"_lc, m_LabelBuf, sizeof(m_LabelBuf));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "SnapshotWindow.LabelHint"_lc);
    ImGui::Separator();
}

// ============================================================
// Tree
// ============================================================

void SnapshotWindow::RenderTreeNode(uint32_t parentId, int depth) {
    for (const auto& node : m_Manager.Nodes) {
        if (node.ParentId != parentId) continue;

        bool isRoot = (node.ParentId == 0);
        bool hasChildren = false;
        for (const auto& c : m_Manager.Nodes)
            if (c.ParentId == node.Id && c.Id != node.Id) { hasChildren = true; break; }

        std::string displayLabel = node.Label.empty()
            ? (std::string("SnapshotWindow.NodePrefix"_lc) + std::to_string(node.Id))
            : node.Label;
        displayLabel += "  [" + FormatTimestamp(node.Timestamp) + "]";

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
        if (node.Id == m_SelectedId) flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<intptr_t>(node.Id)),
            flags, "%s", displayLabel.c_str());

        if (ImGui::IsItemClicked()) {
            m_SelectedId = node.Id;
            TryLoadPreview(node.Id);
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("SnapshotWindow.SaveChild"_lc)) {
                if (::m_emu) {
                    try {
                        std::string lbl(m_LabelBuf);
                        if (lbl.empty()) lbl = std::string("SnapshotWindow.ChildOf"_lc) + std::to_string(node.Id);
                        uint32_t newId = m_Manager.SaveSnapshot(*::m_emu, node.Id, lbl);
                        m_SelectedId = newId;
                        TryLoadPreview(newId);
                        memset(m_LabelBuf, 0, sizeof(m_LabelBuf));
                    } catch (const std::exception& e) { ShowError(e.what()); }
                }
            }
            if (ImGui::MenuItem("SnapshotWindow.LoadThis"_lc)) {
                if (::m_emu) {
                    try { m_Manager.LoadSnapshot(*::m_emu, node.Id); }
                    catch (const std::exception& e) { ShowError(e.what()); }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("SnapshotWindow.DeleteWithChildren"_lc)) {
                m_Manager.DeleteNode(node.Id);
                if (m_SelectedId == node.Id) { m_SelectedId = 0; m_Preview.Free(); }
                ImGui::EndPopup();
                if (open) ImGui::TreePop();
                return; // node is gone, stop iterating siblings here
            }
            ImGui::EndPopup();
        }

        if (open) {
            RenderTreeNode(node.Id, depth + 1);
            ImGui::TreePop();
        }
    }
}

void SnapshotWindow::RenderTree() {
    ImGui::BeginChild("##snap_tree", ImVec2(380, 0), true);
    if (m_Manager.Nodes.empty()) {
        ImGui::TextDisabled("%s", "SnapshotWindow.NoSnapshots"_lc);
    } else {
        RenderTreeNode(0, 0);
    }
    ImGui::EndChild();
}

// ============================================================
// Details panel (preview + info)
// ============================================================

void SnapshotWindow::RenderDetails() {
    ImGui::BeginChild("##snap_detail", ImVec2(0, 0), false);

    // Find selected node
    const SnapshotNode* sel = nullptr;
    for (const auto& n : m_Manager.Nodes)
        if (n.Id == m_SelectedId) { sel = &n; break; }

    if (!sel) {
        ImGui::TextDisabled("%s", "SnapshotWindow.SelectToSeeDetails"_lc);
        ImGui::EndChild();
        return;
    }

    // Preview image
    if (m_Preview.Tex && m_Preview.NodeId == m_SelectedId) {
        float avail = ImGui::GetContentRegionAvail().x;
        float imgW  = static_cast<float>(m_Preview.Width);
        float imgH  = static_cast<float>(m_Preview.Height);
        float scale = avail / imgW;
        if (scale > 1.0f) scale = 1.0f;
        ImGui::Image(reinterpret_cast<ImTextureID>(m_Preview.Tex),
                     ImVec2(imgW * scale, imgH * scale));
    } else {
        ImGui::TextDisabled("%s", "SnapshotWindow.NoPreview"_lc);
    }

    ImGui::Separator();

    ImGui::Text("SnapshotWindow.ID"_lc, sel->Id);
    ImGui::Text("SnapshotWindow.ParentID"_lc, sel->ParentId);
    ImGui::Text("SnapshotWindow.LabelFmt"_lc, sel->Label.c_str());
    ImGui::Text("SnapshotWindow.SavedFmt"_lc, FormatTimestamp(sel->Timestamp).c_str());
    ImGui::Text("SnapshotWindow.StateFmt"_lc,
                static_cast<size_t>(sel->UncompressedStateSize),
                sel->CompressedState.size());
    ImGui::Text("SnapshotWindow.PreviewFmt"_lc, sel->PreviewPng.size());

    ImGui::EndChild();
}

// ============================================================
// Main render
// ============================================================

void SnapshotWindow::RenderCore() {
    RenderToolbar();

    // Error popup
    if (m_ShowError) {
        ImGui::OpenPopup("SnapshotWindow.ErrorTitle"_lc);
        m_ShowError = false;
    }
    if (ImGui::BeginPopupModal("SnapshotWindow.ErrorTitle"_lc, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_ErrorMsg.c_str());
        if (ImGui::Button("Button.Positive"_lc, ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Two-panel layout: tree | details
    RenderTree();
    ImGui::SameLine();
    RenderDetails();
}

UIWindow* CreateSnapshotWindow() { return new SnapshotWindow(); }
