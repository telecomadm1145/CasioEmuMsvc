#include "Injector.hpp"
#include "Chipset/Chipset.hpp"
#include "Peripheral/BatteryBackedRAM.hpp"
#include "hex.hpp"
#include "imgui/imgui.h"
#include "Models.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include "Config.hpp"
#include "Ui.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <Localization.h>
#include <Gui.h>
#include <algorithm>
#include <sstream>

void Injector::TrimString(std::string& str) {
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    if (!str.empty()) {
        str.erase(str.find_last_not_of(" \t\n\r") + 1);
    }
}

bool Injector::IsHexString(const std::string& str) {
    return str.find_first_not_of("0123456789abcdefABCDEFxX") == std::string::npos;
}

void Injector::InitCustomInjectionsFile() {
    const std::filesystem::path filepath = "./hc-inj.txt";
    
    if (std::filesystem::exists(filepath)) {
        return;
    }

    const std::string template_content = R"(# Custom Injection Template
# Format: name = {
#     address = "hex_data",
#     address = "hex_data"
# }
)";

    try {
        std::ofstream file(filepath);
        if (!file) {
            throw std::runtime_error("Failed to create injection template file");
        }
        file << template_content;
        if (!file) {
            throw std::runtime_error("Failed to write injection template content");
        }
        file.close();
    } catch (const std::exception& e) {
        // TODO: Add proper logging
    }
}

bool Injector::ParseCustomInjections(const std::string& content) {
    customInjections.clear();

    if (content.empty()) {
        return true;
    }
    
    std::istringstream stream(content);
    std::string line;
    CustomInjection currentInj;
    bool inInjection = false;
    
    while (std::getline(stream, line)) {
        TrimString(line);
        
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        if (line.find('=') != std::string::npos && line.find('{') != std::string::npos) {
            if (inInjection) continue;
            
            inInjection = true;
            currentInj = CustomInjection();
            currentInj.name = line.substr(0, line.find('='));
            TrimString(currentInj.name);
            continue;
        }
        
        if (line.find('}') != std::string::npos) {
            if (inInjection) {
                inInjection = false;
                if (!currentInj.pairs.empty()) {
                    customInjections.push_back(currentInj);
                }
            }
            continue;
        }
        
        if (inInjection && line.find('=') != std::string::npos) {
            size_t equalPos = line.find('=');
            std::string addr = line.substr(0, equalPos);
            std::string data = line.substr(equalPos + 1);
            
            TrimString(addr);
            TrimString(data);
            
            // Remove quotes and comma
            data.erase(std::remove(data.begin(), data.end(), '"'), data.end());
            data.erase(std::remove(data.begin(), data.end(), ','), data.end());
            TrimString(data);
            
            if (IsHexString(addr) && !data.empty()) {
                InjectionPair pair;
                pair.address = addr;
                pair.data = data;
                currentInj.pairs.push_back(pair);
            }
        }
    }    
    return true;
}

void Injector::LoadCustomInjections() {
    std::ifstream file("./hc-inj.txt");
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
        file.close();
        ParseCustomInjections(content);
    }
}

void Injector::ApplyInjection(const CustomInjection& inj, bool& show_info, std::string& info_msg) {
    bool success = true;
    
    for (const auto& pair : inj.pairs) {
        try {
            auto plc = strtol(pair.address.c_str(), nullptr, 16);
            const auto& data = pair.data;
            
            for (size_t i = 0; i < data.length(); i += 2) {
                if (i + 1 >= data.length()) break;
                
                char hex[3] = {data[i], data[i + 1], '\0'};
                uint8_t byte = strtoul(hex, nullptr, 16);
                me_mmu->WriteData(plc + (i/2), byte);
            }
        } catch (...) {
            success = false;
            break;
        }
    }
    
    char buf[256];
    if (success) {
        snprintf(buf, sizeof(buf), "Rop.CustomInjectApplied"_lc, inj.name.c_str());
    } else {
        snprintf(buf, sizeof(buf), "Rop.CustomInjectError"_lc, inj.name.c_str());
    }
    info_msg = buf;
    show_info = true;
}

void Injector::RenderCustomInjectTab(bool& show_info, std::string& info_msg) {
    if (ImGui::Button("Rop.ReloadCustomInjects"_lc)) {
        LoadCustomInjections();
        info_msg = "Rop.CustomInjectReloaded"_l;
        show_info = true;
    }
    
    ImGui::Separator();
    
    for (const auto& inj : customInjections) {
        if (ImGui::CollapsingHeader(inj.name.c_str())) {
            for (const auto& pair : inj.pairs) {
                ImGui::Text("%s: %s", "Rop.Address"_lc, pair.address.c_str());
                ImGui::SameLine();
                ImGui::Text("%s: %s", "Rop.Data"_lc, pair.data.c_str());
            }
            
            if (ImGui::Button((inj.name + "##inject").c_str())) {
                ApplyInjection(inj, show_info, info_msg);
            }
            
            ImGui::Separator();
        }
    }
}

void Injector::RenderInjectorTab(InjectorData& inj, int index, bool& show_info, std::string& info_msg) {
    auto valid_hex = [](char c) {
        if (c >= '0' && c <= '9') return true;
        if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) return true;
        return false;
    };

    ImGui::TextUnformatted("Rop.InjectAddr"_lc);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText(("##addr" + std::to_string(index)).c_str(), inj.addr, 10);

    if (ImGui::Button(("Rop.Paste"_l + "##" + std::to_string(index)).c_str())) {
        if (ImGui::GetClipboardText() != nullptr) {
            strncpy(inj.data, ImGui::GetClipboardText(), sizeof(inj.data) - 1);
            inj.data[sizeof(inj.data) - 1] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(("Rop.Clear"_l + "##" + std::to_string(index)).c_str())) {
        memset(inj.data, 0, sizeof(inj.data));
    }

    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 1.4f);
    ImGui::InputTextMultiline(
        ("##hex" + std::to_string(index)).c_str(),
        inj.data,
        IM_ARRAYSIZE(inj.data) - 1,
        ImVec2(-1, ImGui::GetTextLineHeight() * 8)
    );

    if (ImGui::Button(("Rop.Inject"_l + "##" + std::to_string(index)).c_str())) {
        auto plc = strtol(inj.addr, 0, 16);
        size_t i = 0, j = 0;
        char hex_buf[3];
        
        while (inj.data[i] != '\0' && inj.data[i + 1] != '\0') {
            if (inj.data[i] == ';' || inj.data[i + 1] == ';') {
                for (;; ++i) {
                    if (inj.data[i] == '\0')
                        goto exit;
                    if (inj.data[i] == '\n') {
                        ++i;
                        break;
                    }
                }
            }
            else {
                if (!(valid_hex(inj.data[i]) && valid_hex(inj.data[i + 1]))) {
                    ++i;
                    continue;
                }
                hex_buf[0] = inj.data[i];
                hex_buf[1] = inj.data[i + 1];
                hex_buf[2] = '\0';
                uint8_t byte = strtoul(hex_buf, nullptr, 16);
                me_mmu->WriteData(plc + j, byte);
                i += 2;
                ++j;
            }
        }
    exit:
        info_msg = "Rop.Injected"_l;
        show_info = true;
    }
}

void Injector::RenderCore() {
    static int range = 64;
    static char buf[10] = {0};
    static MemoryEditor editor;
    static bool show_info = false;
    static std::string info_msg;
    auto inputbase = m_emu->hardware_id == casioemu::HardwareId::HW_CLASSWIZ_II ? 0x9268 : 0xD180;
    char* base_addr = n_ram_buffer - casioemu::GetRamBaseAddr(m_emu->hardware_id);

    if (ImGui::BeginTabBar("Rop.TabBar"_lc)) {
        if (ImGui::BeginTabItem("Rop.XAnMode"_lc)) {
            ImGui::TextUnformatted("Rop.InputSize"_lc);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputText("##off", buf, 9);
            ImGui::SameLine();
            
            if (ImGui::Button("Rop.InputAn"_lc)) {
                int off = atoi(buf);
                if (off > 100) {
                    memset(base_addr + inputbase, 0x31, 100);
                    memset(base_addr + inputbase + 100, 0xa6, 1);
                    memset(base_addr + inputbase + 101, 0x31, off - 100);
                }
                else {
                    memset(base_addr + inputbase, 0x31, off);
                }
                *(base_addr + inputbase + off) = 0xfd;
                *(base_addr + inputbase + off + 1) = 0x20;
                info_msg = "Rop.AnInputed"_l;
                show_info = true;
            }
            
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rop.InjectHex"_lc)) {
            if (ImGui::Button("Rop.AddInjector"_lc)) {
                injectors.push_back(InjectorData());
            }

            ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 1.4f);
            for (size_t i = 0; i < injectors.size(); i++) {
                ImGui::PushID(static_cast<int>(i));
                
                std::string header = "Rop.InjectorNum"_l + " " + std::to_string(i + 1);
                
                if (injectors.size() > 1) {
                    if (ImGui::Button(("Rop.RemoveInjector"_l + "##" + std::to_string(i)).c_str())) {
                        injectors.erase(injectors.begin() + i);
                        ImGui::PopID();
                        break;
                    }
                }

                if (ImGui::CollapsingHeader(header.c_str())) {
                    RenderInjectorTab(injectors[i], i, show_info, info_msg);
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rop.Input"_lc)) {
            ImGui::BeginChild("RopInput");
            editor.DrawContents(data_buf, range);
            ImGui::EndChild();
            
            ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 1.4f);
            ImGui::SliderInt("Rop.InputSize"_lc, &range, 64, 1024);
            
            if (ImGui::Button("Rop.LoadToInputArea"_lc)) {
                memcpy(base_addr + inputbase, data_buf, range);
                info_msg = "Rop.LoadedTip"_l;
                show_info = true;
            }
            
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rop.CustomInject"_lc)) {
            RenderCustomInjectTab(show_info, info_msg);
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }

    if (show_info) {
        ImGui::OpenPopup("Rop.InfoPopup"_lc);
    }

    if (ImGui::BeginPopupModal("Rop.InfoPopup"_lc, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(info_msg.c_str());
        if (ImGui::Button("Button.Positive"_lc)) {
            show_info = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}