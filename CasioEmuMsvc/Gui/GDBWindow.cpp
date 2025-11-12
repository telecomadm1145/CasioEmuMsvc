#include "GDBWindow.hpp"
#include "../GDB/GDBServer.hpp"
#include "imgui.h"

static int s_gdb_port_buffer = 1234;

GDBWindow::GDBWindow(GDBServer* gdbServer)
    : UIWindow("GDB Server"), m_gdbServer(gdbServer)
{
    flags = ImGuiWindowFlags_AlwaysAutoResize;
    if (m_gdbServer) {
        s_gdb_port_buffer = m_gdbServer->GetPort();
    }
}

void GDBWindow::RenderCore()
{
    if (!m_gdbServer)
    {
        ImGui::Text("GDB Server not available.");
        return;
    }

    const bool is_running = (m_gdbServer->GetStatus() != "Stopped");

    ImGui::Text("Status:");
    ImGui::SameLine();
    ImGui::TextUnformatted(m_gdbServer->GetStatus().c_str());
    if (is_running) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Port", &s_gdb_port_buffer);
    if (s_gdb_port_buffer < 1024) s_gdb_port_buffer = 1024;
    if (s_gdb_port_buffer > 65535) s_gdb_port_buffer = 65535;
    if (is_running) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("(Stop server to change port)");
    }
    ImGui::Spacing();
    if (!is_running) {
        if (ImGui::Button("Start"))
        {
            m_gdbServer->SetPort(s_gdb_port_buffer);
            m_gdbServer->Start();
        }
    } else {
        if (ImGui::Button("Stop"))
        {
            m_gdbServer->Stop();
        }
    }
}