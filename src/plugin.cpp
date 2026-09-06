#include "logger.h"
#include "UI.h"
#include "PickpocketEventHandler.h"
#include "PickpocketHooks.h"
#include "Config.h"

namespace {

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
            PickpocketConfig::LoadSettings();
            PickpocketConfig::EnsureSettingsFile();
            UI::Register();
            PickpocketEvents::Register();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();

    SKSE::Init(skse);
    SKSE::AllocTrampoline(64);
    if (!PickpocketHooks::Install()) {
        logger::critical("Pickpocket Minigame could not install its required chance hook");
        return false;
    }

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("Failed to get messaging interface!");
        return false;
    }

    messaging->RegisterListener(MessageHandler);

    logger::info("Pickpocket Minigame loaded");
    return true;
}
