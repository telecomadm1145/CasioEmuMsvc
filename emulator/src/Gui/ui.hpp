#pragma once
#include "../Emulator.hpp"
#include "CodeViewer.hpp"
int test_gui();

extern char *n_ram_buffer;
extern casioemu::Emulator *m_emu;
extern CodeViewer *code_viewer;