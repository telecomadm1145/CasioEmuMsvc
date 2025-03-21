#include "FileDialog.hpp"
#include <Localization.h>

namespace FileDialog {
    struct FileFilter {
        std::string name;
        std::vector<std::string> extensions;
    };

    std::vector<FileFilter> ParseFilters(const char* filters) {
        std::vector<FileFilter> result;
        std::string input(filters);
        
        size_t pos = 0;
        while (pos < input.length()) {
            size_t nameEnd = input.find('{', pos);
            if (nameEnd == std::string::npos) break;
            
            FileFilter filter;
            filter.name = input.substr(pos, nameEnd - pos);
            
            size_t extStart = nameEnd + 1;
            size_t extEnd = input.find('}', extStart);
            if (extEnd == std::string::npos) break;
            
            std::string extensions = input.substr(extStart, extEnd - extStart);
            size_t extPos = 0;
            while (extPos < extensions.length()) {
                size_t nextPos = extensions.find(',', extPos);
                if (nextPos == std::string::npos) {
                    filter.extensions.push_back(extensions.substr(extPos));
                    break;
                }
                
                filter.extensions.push_back(extensions.substr(extPos, nextPos - extPos));
                extPos = nextPos + 1;
            }
            
            result.push_back(filter);
            pos = extEnd + 1;
            
            // Skip the comma after the filter
            if (pos < input.length() && input[pos] == ',') {
                pos++;
            }
        }
        
        return result;
    }

    bool ShowFileDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize, bool isSaveDialog) {
        static bool doneSelection = false;
        static std::string currentPath = ".";
        static std::vector<FileFilter> currentFilters;
        static int selectedFilterIndex = 0;
        static bool initialized = false;
        if (!initialized) {
            doneSelection = false;
            currentFilters = ParseFilters(filters);
            initialized = true;
        }
        
        bool dialogClosed = false;

        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_Modal)) {

            ImGui::Text("%s: %s", "Files.CurrentPath"_lc, currentPath.c_str());

            if (ImGui::Button("Files.Back"_lc)) {
                std::filesystem::path path(currentPath);
                currentPath = path.parent_path().string();
                if (currentPath.empty()) {
                    currentPath = ".";
                }
            }
            
            ImGui::BeginChild("Files", ImVec2(0, -80), true);
            
            try {
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    const bool isDirectory = entry.is_directory();
                    const std::string filename = entry.path().filename().string();
                    
                    if (isDirectory) {
                        if (ImGui::Selectable((filename + "/").c_str(), false)) {
                            currentPath = entry.path().string();
                        }
                    }
                }
                
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    const bool isDirectory = entry.is_directory();
                    const std::string filename = entry.path().filename().string();
                    
                    if (!isDirectory) {
                        bool showFile = currentFilters.empty();
                        
                        if (!showFile && selectedFilterIndex < currentFilters.size()) {
                            for (const auto& ext : currentFilters[selectedFilterIndex].extensions) {
                                std::string fileExt = entry.path().extension().string();
                                if (ext == ".*" || (ext.length() > 0 && ext[0] == '.' && fileExt == ext)) {
                                    showFile = true;
                                    break;
                                }
                            }
                        }
                        
                        if (showFile) {
                            if (ImGui::Selectable(filename.c_str(), false)) {
                                strncpy(selectedFilePath, (currentPath + "/" + filename).c_str(), pathBufferSize);
                                selectedFilePath[pathBufferSize - 1] = '\0';
                                doneSelection = true;
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", e.what());
            }
            
            ImGui::EndChild();
            
            // Filter selection
            if (!currentFilters.empty()) {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::BeginCombo("##FilterCombo", currentFilters[selectedFilterIndex].name.c_str())) {
                    for (int i = 0; i < currentFilters.size(); i++) {
                        const bool isSelected = (selectedFilterIndex == i);
                        if (ImGui::Selectable(currentFilters[i].name.c_str(), isSelected)) {
                            selectedFilterIndex = i;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::Button(isSaveDialog ? "Files.Save"_lc : "Files.Open"_lc)) {
                if (doneSelection) {
                    ImGui::CloseCurrentPopup();
                    initialized = false;
                    dialogClosed = true;
                }
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Files.Cancel"_lc)) {
                ImGui::CloseCurrentPopup();
                initialized = false;
                dialogClosed = true;
            }
            
            ImGui::EndPopup();
        } else {
            ImGui::OpenPopup(title);
        }
        
        return dialogClosed;
    }

    bool ShowFileOpenDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize) {
        return ShowFileDialog(title, filters, selectedFilePath, pathBufferSize, false);
    }

    bool ShowFileSaveDialog(const char* title, const char* filters, char* selectedFilePath, size_t pathBufferSize) {
        return ShowFileDialog(title, filters, selectedFilePath, pathBufferSize, true);
    }
}