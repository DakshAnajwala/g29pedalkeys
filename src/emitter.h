#pragma once
#include <windows.h>

// Key output backend. SendInput is the only implementation today; a driver
// backend (Interception) can be dropped in behind this interface without the
// UI or pedal code changing.
class KeyEmitter {
public:
    virtual ~KeyEmitter() {}
    virtual void Emit(UINT vk, bool down) = 0;
};

class SendInputEmitter : public KeyEmitter {
public:
    void Emit(UINT vk, bool down);
};

// Human-readable name for a virtual-key code, for the UI.
void KeyName(UINT vk, char* out, int outSize);
