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
#include <regex>
#include <future>
#include <thread>
#include <chrono>

Injector::Injector() : UIWindow("Rop"), needsReload(false), isReloading(false) {
    injectors.push_back(InjectorData());
    InitCustomInjectionsFile();
    AsyncLoadCustomInjections();
}

Injector::~Injector() {
}

std::string Injector::GetFileModifiedTime(const std::string& filepath) {
    try {
        auto ftime = std::filesystem::last_write_time(filepath);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::to_string(std::chrono::system_clock::to_time_t(sctp));
    } catch (...) {
        return "";
    }
}

void Injector::AsyncLoadCustomInjections() {
    std::thread([this]() {
        BackgroundReload();
    }).detach();
}

void Injector::TrimString(std::string& str) {
    str.erase(0, str.find_first_not_of(" \t\n\r"));
    str.erase(str.find_last_not_of(" \t\n\r") + 1);
}

bool Injector::IsHexString(const std::string& str) {
    if (str.empty()) return false;
    std::string processed = str;
    if (processed.substr(0, 2) == "0x" || processed.substr(0, 2) == "0X") {
        processed = processed.substr(2);
    }
    return !processed.empty() && 
           processed.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
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
        file.close();
    } catch (const std::exception& e) {
        // Handle error
    }
}

void Injector::BackgroundReload() {
    if (isReloading.exchange(true)) {
        return; // If already reloading, return
    }

    const std::string filepath = "./hc-inj.txt";
    std::string currentModTime = GetFileModifiedTime(filepath);
    
    if (currentModTime == lastModifiedTime && !needsReload) {
        isReloading.store(false);
        return;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        isReloading.store(false);
        return;
    }
    
    // Read file content efficiently
    std::string content;
    file.seekg(0, std::ios::end);
    content.reserve(file.tellg());
    file.seekg(0, std::ios::beg);
    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    std::lock_guard<std::mutex> lock(injectionMutex);
    if (ParseCustomInjections(content)) {
        lastModifiedTime = currentModTime;
        needsReload = false;
    }
    
    isReloading.store(false);
}

void Injector::PrecomputeInjectionValues(InjectionPair& pair) {
    std::string addr = pair.address;
    if (addr.substr(0, 2) == "0x") {
        addr = addr.substr(2);
    }
    pair.addr_value = std::stoul(addr, nullptr, 16);
    
    const std::string& data = pair.data;
    pair.data_bytes.reserve(data.length() / 2);
    
    for (size_t i = 0; i < data.length(); i += 2) {
        std::string byte_str = data.substr(i, 2);
        pair.data_bytes.push_back(HexToByte(byte_str));
    }
}

bool Injector::ParseCustomInjections(const std::string& content) {
    std::vector<CustomInjection> newInjections;
    std::istringstream stream(content);
    std::string line;
    CustomInjection currentInj;
    bool inInjection = false;
    
    static const std::regex name_pattern(R"((\w+)\s*=\s*\{)", std::regex::optimize);
    static const std::regex pair_pattern(R"(^\s*(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s*=\s*\"([0-9a-fA-F]+)\")", std::regex::optimize);
    
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::smatch matches;
        if (std::regex_search(line, matches, name_pattern)) {
            if (inInjection && !currentInj.pairs.empty()) {
                newInjections.push_back(std::move(currentInj));
            }
            currentInj = CustomInjection();
            currentInj.name = matches[1].str();
            inInjection = true;
            continue;
        }
        
        if (line == "}") {
            if (inInjection && !currentInj.pairs.empty()) {
                newInjections.push_back(std::move(currentInj));
            }
            inInjection = false;
            continue;
        }
        
        if (inInjection && std::regex_search(line, matches, pair_pattern)) {
            InjectionPair pair;
            pair.address = matches[1].str();
            pair.data = matches[2].str();
            
            if (IsHexString(pair.address) && IsHexString(pair.data)) {
                PrecomputeInjectionValues(pair);
                currentInj.pairs.push_back(std::move(pair));
            }
        }
    }
    
    if (inInjection && !currentInj.pairs.empty()) {
        newInjections.push_back(std::move(currentInj));
    }
    
    customInjections = std::move(newInjections);
    return true;
}

bool Injector::ValidateHexPair(const std::string& hex) {
    if (hex.length() % 2 != 0) return false;
    return IsHexString(hex);
}

uint8_t Injector::HexToByte(const std::string& hex) {
    return static_cast<uint8_t>(std::stoul(hex, nullptr, 16));
}

bool Injector::ApplyInjection(const CustomInjection& inj, bool& show_info, std::string& info_msg) {
    try {
        for (const auto& pair : inj.pairs) {
            for (size_t i = 0; i < pair.data_bytes.size(); ++i) {
                me_mmu->WriteData(pair.addr_value + i, pair.data_bytes[i]);
            }
        }
        
        char buf[256];
        snprintf(buf, sizeof(buf), "Rop.CustomInjectApplied"_lc, inj.name.c_str());
        info_msg = buf;
        show_info = true;
        return true;
    } catch (const std::exception& e) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Rop.CustomInjectError"_lc, inj.name.c_str());
        info_msg = buf;
        show_info = true;
        return false;
    }
}

void Injector::RenderCustomInjectTab(bool& show_info, std::string& info_msg) {
    if (ImGui::Button("Rop.ReloadCustomInjects"_lc)) {
        if (!isReloading.load()) {
            needsReload = true;
            std::thread([this]() {
                BackgroundReload();
            }).detach();
            info_msg = "Rop.CustomInjectReloading"_l;
            show_info = true;
        }
    }
    
    if (isReloading.load()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Rop.Loading"_lc);
    }
    
    ImGui::Separator();
    
    for (const auto& inj : customInjections) {
        if (ImGui::CollapsingHeader(inj.name.c_str())) {
            ImGui::Text("%s:", "Rop.Address"_lc);
            for (const auto& pair : inj.pairs) {
                std::string displayAddr = pair.address;
                if (displayAddr.substr(0, 2) == "0x" || displayAddr.substr(0, 2) == "0X") {
                    displayAddr = "0x" + displayAddr.substr(2);
                } else {
                    displayAddr = "0x" + displayAddr;
                }
                ImGui::BulletText("%s", displayAddr.c_str());
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
        return (c >= '0' && c <= '9') || 
               (c >= 'a' && c <= 'f') || 
               (c >= 'A' && c <= 'F');
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