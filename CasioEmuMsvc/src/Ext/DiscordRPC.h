#pragma once
#include <string>

namespace DiscordRPC {
    void Init();
    void UpdatePresence(const std::string& modelName);
    void Update();
    void Shutdown();
}