#pragma once
#include "../Emulator.hpp"
#include "CodeViewer.hpp"
int test_gui();
void gui_cleanup();
void gui_loop();
extern char *n_ram_buffer;
extern casioemu::Emulator *m_emu;
extern CodeViewer *code_viewer;