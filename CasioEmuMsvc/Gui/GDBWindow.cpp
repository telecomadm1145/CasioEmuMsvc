#include "GDBWindow.hpp"
#include "../GDB/GDBServer.hpp"
#include "imgui.h"

GDBWindow::GDBWindow(GDBServer* gdbServer)
    : UIWindow("GDB Server"), m_gdbServer(gdbServer)
{
    flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void GDBWindow::RenderCore()
{
    if (m_gdbServer)
    {
        ImGui::Text("GDB Server Status:");
        ImGui::Text(m_gdbServer->GetStatus().c_str());
        ImGui::Text("Listening on port %d", m_gdbServer->GetPort());

        if (ImGui::Button("Start"))
        {
            m_gdbServer->Start();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            m_gdbServer->Stop();
        }
    }
    else
    {
        ImGui::Text("GDB Server not available.");
    }
}
