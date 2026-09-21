#pragma once
#include <windows.h>
#include <vector>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

// One axis value pulled out of a HID input report.
struct AxisSample {
    USHORT usagePage;
    USHORT usage;
    LONG value;
};

// Identifies the physical device a report came from, so a saved axis binding
// survives across sessions (raw input HANDLEs do not).
struct DeviceId {
    USHORT vendorId;
    USHORT productId;
};

class HidReader {
public:
    // Subscribes hwnd to joystick/gamepad reports, including while another
    // window is foreground (RIDEV_INPUTSINK) -- required because the game has
    // focus while we read the pedals.
    bool Register(HWND hwnd);

    // Decodes a WM_INPUT message. Returns false for anything that is not a
    // HID report we can parse.
    bool Parse(LPARAM lParam, std::vector<AxisSample>& out, DeviceId& id);

    void Forget(HANDLE device);

private:
    struct DeviceCache {
        HANDLE handle;
        std::vector<BYTE> preparsed;
        std::vector<HIDP_VALUE_CAPS> valueCaps;
        DeviceId id;
    };

    DeviceCache* Lookup(HANDLE device);

    std::vector<DeviceCache> cache_;
    std::vector<BYTE> buffer_;
};
