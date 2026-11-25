#pragma once

#include <stdint.h>

class WebServer;

constexpr unsigned OtaKeyBytes = 32;

void initPanelOta(bool hardwareReady);
void registerPanelOta(WebServer &server, bool (*panelIdle)());
void setPanelOtaKey(const uint8_t *key);
bool otaPending();
bool otaTrialBoot();
void tickOta();
