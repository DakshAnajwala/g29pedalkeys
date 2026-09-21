#include "emitter.h"
#include <stdio.h>
#include <string.h>

static bool IsExtendedKey(UINT vk) {
    switch (vk) {
        case VK_RMENU: case VK_RCONTROL:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_NUMLOCK: case VK_DIVIDE:
            return true;
    }
    return false;
}

void SendInputEmitter::Emit(UINT vk, bool down) {
    if (vk == 0) return;

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = (WORD)scan;
    // Scancode output, not virtual-key: DirectInput-era games read scancodes
    // and ignore VK-only injection.
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (IsExtendedKey(vk)) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (!down) in.ki.dwFlags |= KEYEVENTF_KEYUP;

    // Fall back to virtual-key injection if the layout has no scancode.
    if (scan == 0) {
        in.ki.wVk = (WORD)vk;
        in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    }

    SendInput(1, &in, sizeof(INPUT));
}

void KeyName(UINT vk, char* out, int outSize) {
    if (vk == 0) {
        strncpy(out, "(none)", outSize);
        out[outSize - 1] = 0;
        return;
    }

    UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    LONG lparam = (LONG)(scan << 16);
    if (IsExtendedKey(vk)) lparam |= (1 << 24);

    if (GetKeyNameTextA(lparam, out, outSize) > 0) return;
    sprintf(out, "VK 0x%02X", vk);
}
