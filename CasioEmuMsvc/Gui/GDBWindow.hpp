#pragma once
#include "Gui/Ui.hpp"

class GDBServer;

class GDBWindow : public UIWindow
{
public:
    GDBWindow(GDBServer* gdbServer);
    void RenderCore() override;

private:
    GDBServer* m_gdbServer;
};
