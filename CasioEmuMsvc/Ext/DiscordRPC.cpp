#include "DiscordRPC.h"
#include "Config.hpp"
#include "Localization.h"
#include <chrono>
#include <cstring>

#ifndef __ANDROID__
#include <discord_rpc.h>
#endif

namespace DiscordRPC {
    static int64_t StartTime = 0;

    void Init() {
#ifndef __ANDROID__
        DiscordEventHandlers handlers;
        std::memset(&handlers, 0, sizeof(handlers));
        Discord_Initialize(DISCORD_APP_ID, &handlers, 1, nullptr);
        StartTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
#endif
    }

    void UpdatePresence(const std::string& modelName) {
#ifndef __ANDROID__
        DiscordRichPresence discordPresence;
        std::memset(&discordPresence, 0, sizeof(discordPresence));
        static std::string details_str;
        if (modelName.empty()) {
            details_str = "DiscordRPC.ChoosingModel"_l;
        } else {
            details_str = "Model: " + modelName;
        }
        discordPresence.details = details_str.c_str();
        discordPresence.startTimestamp = StartTime;
        discordPresence.button1Label = "View Repository";
        discordPresence.button1Url = "https://github.com/telecomadm1145/CasioEmuMsvc";
        Discord_UpdatePresence(&discordPresence);
#endif
    }

    void Update() {
#ifndef __ANDROID__
        Discord_RunCallbacks();
#endif
    }

    void Shutdown() {
#ifndef __ANDROID__
        Discord_ClearPresence();
        Discord_Shutdown();
#endif
    }
}