#pragma once
#include "imgui.h"
#include <string>
#include <vector>
#include <filesystem>

namespace FileDialog {
    bool ShowFileOpenDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize);
    bool ShowFileSaveDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize);
}