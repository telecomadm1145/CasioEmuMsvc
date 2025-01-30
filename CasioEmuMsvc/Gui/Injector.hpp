#pragma once
#include "Emulator.hpp"
#include "Ui.hpp"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

struct InjectorData {
    char addr[10];
    char data[65536];
};

struct InjectionPair {
    std::string address;
    std::string data;
};

struct CustomInjection {
    std::string name;
    std::vector<InjectionPair> pairs;
};

class Injector : public UIWindow {
private:
    char data_buf[1024];
    std::vector<InjectorData> injectors;
    std::vector<CustomInjection> customInjections;
    int current_tab = 0;
    char strbuf[65536];
    
    void LoadCustomInjections();
    void InitCustomInjectionsFile();
    bool ParseCustomInjections(const std::string& content);
    void TrimString(std::string& str);
    bool IsHexString(const std::string& str);
    void ApplyInjection(const CustomInjection& inj, bool& show_info, std::string& info_msg);

public:
    Injector() : UIWindow("Rop") {
        injectors.push_back(InjectorData());
        InitCustomInjectionsFile();
        LoadCustomInjections();
    }
    void RenderCore() override;
    void RenderInjectorTab(InjectorData& inj, int index, bool& show_info, std::string& info_msg);
    void RenderCustomInjectTab(bool& show_info, std::string& info_msg);
};