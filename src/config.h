#pragma once
#include <windows.h>

// Pedal slots. Index order is fixed and used by the INI section names.
enum PedalIndex { PEDAL_THROTTLE = 0, PEDAL_BRAKE = 1, PEDAL_CLUTCH = 2, PEDAL_COUNT = 3 };

struct PedalCfg {
    // HID axis identity, learned by the Detect button.
    USHORT usagePage = 0;
    USHORT usage = 0;
    USHORT vendorId = 0;
    USHORT productId = 0;

    // Raw axis values at rest and at full travel. full may be below rest on an
    // inverted axis; every comparison goes through Normalize so direction is
    // handled in one place.
    LONG rest = 0;
    LONG full = 0;

    int thresholdPct = 40;   // press depth at which the key goes down
    UINT vk = 0;             // virtual-key code to emit
    bool enabled = false;
};

struct AppCfg {
    PedalCfg pedals[PEDAL_COUNT];
};

void LoadConfig(AppCfg& cfg);
void SaveConfig(const AppCfg& cfg);
const char* PedalName(int index);

// 0.0 at rest, 1.0 at full travel, clamped.
double Normalize(const PedalCfg& p, LONG raw);
