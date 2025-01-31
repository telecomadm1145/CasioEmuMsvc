#pragma once
#include "Emulator.hpp"
#include "Ui.hpp"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>

struct InjectorData {
    char addr[10];
    char data[65536];
};

struct InjectionPair {
    std::string address;
    std::string data;
    uint32_t addr_value;
    std::vector<uint8_t> data_bytes;
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
    std::mutex injectionMutex;
    bool needsReload;
    std::atomic<bool> isReloading;
    std::string lastModifiedTime;
    
    // Cache for parsed hex values
    std::unordered_map<std::string, uint32_t> addressCache;
    std::unordered_map<std::string, std::vector<uint8_t>> dataCache;
    
    int current_tab = 0;
    char strbuf[65536];

    void InitCustomInjectionsFile();
    bool ParseCustomInjections(const std::string& content);
    void TrimString(std::string& str);
    bool IsHexString(const std::string& str);
    bool ApplyInjection(const CustomInjection& inj, bool& show_info, std::string& info_msg);
    bool ValidateHexPair(const std::string& hex);
    uint8_t HexToByte(const std::string& hex);
    std::string GetFileModifiedTime(const std::string& filepath);
    void PrecomputeInjectionValues(InjectionPair& pair);
    void AsyncLoadCustomInjections();
    void BackgroundReload();

public:
    Injector();
    ~Injector();
    void RenderCore() override;
    void RenderInjectorTab(InjectorData& inj, int index, bool& show_info, std::string& info_msg);
    void RenderCustomInjectTab(bool& show_info, std::string& info_msg);
};