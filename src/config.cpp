#include "config.h"
#include <stdio.h>
#include <string>

static std::string IniPath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path(buf);
    size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos) path.resize(slash + 1);
    return path + "g29pedalkeys.ini";
}

const char* PedalName(int index) {
    switch (index) {
        case PEDAL_THROTTLE: return "Throttle";
        case PEDAL_BRAKE:    return "Brake";
        case PEDAL_CLUTCH:   return "Clutch";
    }
    return "?";
}

static int ReadInt(const char* section, const char* key, int fallback, const std::string& ini) {
    return (int)GetPrivateProfileIntA(section, key, fallback, ini.c_str());
}

static void WriteInt(const char* section, const char* key, int value, const std::string& ini) {
    char buf[32];
    sprintf(buf, "%d", value);
    WritePrivateProfileStringA(section, key, buf, ini.c_str());
}

void LoadConfig(AppCfg& cfg) {
    std::string ini = IniPath();
    for (int i = 0; i < PEDAL_COUNT; ++i) {
        const char* s = PedalName(i);
        PedalCfg& p = cfg.pedals[i];
        p.usagePage    = (USHORT)ReadInt(s, "UsagePage", 0, ini);
        p.usage        = (USHORT)ReadInt(s, "Usage", 0, ini);
        p.vendorId     = (USHORT)ReadInt(s, "VendorId", 0, ini);
        p.productId    = (USHORT)ReadInt(s, "ProductId", 0, ini);
        p.rest         = (LONG)ReadInt(s, "Rest", 0, ini);
        p.full         = (LONG)ReadInt(s, "Full", 0, ini);
        p.thresholdPct = ReadInt(s, "Threshold", 40, ini);
        p.vk           = (UINT)ReadInt(s, "Key", 0, ini);
        p.enabled      = ReadInt(s, "Enabled", 0, ini) != 0;
    }
}

void SaveConfig(const AppCfg& cfg) {
    std::string ini = IniPath();
    for (int i = 0; i < PEDAL_COUNT; ++i) {
        const char* s = PedalName(i);
        const PedalCfg& p = cfg.pedals[i];
        WriteInt(s, "UsagePage", p.usagePage, ini);
        WriteInt(s, "Usage", p.usage, ini);
        WriteInt(s, "VendorId", p.vendorId, ini);
        WriteInt(s, "ProductId", p.productId, ini);
        WriteInt(s, "Rest", p.rest, ini);
        WriteInt(s, "Full", p.full, ini);
        WriteInt(s, "Threshold", p.thresholdPct, ini);
        WriteInt(s, "Key", (int)p.vk, ini);
        WriteInt(s, "Enabled", p.enabled ? 1 : 0, ini);
    }
}

double Normalize(const PedalCfg& p, LONG raw) {
    if (p.full == p.rest) return 0.0;
    double n = (double)(raw - p.rest) / (double)(p.full - p.rest);
    if (n < 0.0) n = 0.0;
    if (n > 1.0) n = 1.0;
    return n;
}
