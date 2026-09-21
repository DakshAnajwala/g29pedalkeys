// Console diagnostic for the G29 pedal remapper.
//
// Answers, in order: does Windows see the wheel at all, what usage page and
// usage does it claim, does it deliver raw input reports to a background
// window, and which HID axes move when a pedal is pressed.
//
// Everything printed here also goes to g29diag.log next to the exe.

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

static FILE* g_log = NULL;

static void Say(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fputs(buf, stdout);
    fflush(stdout);
    if (g_log) {
        fputs(buf, g_log);
        fflush(g_log);
    }
}

// ------------------------------------------------------------- device survey

static const char* TypeName(DWORD type) {
    switch (type) {
        case RIM_TYPEMOUSE:    return "mouse";
        case RIM_TYPEKEYBOARD: return "keyboard";
        case RIM_TYPEHID:      return "hid";
    }
    return "?";
}

static void ListDevices() {
    UINT count = 0;
    if (GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) != 0) {
        Say("GetRawInputDeviceList failed, error %lu\n", GetLastError());
        return;
    }
    std::vector<RAWINPUTDEVICELIST> list(count);
    count = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    if (count == (UINT)-1) {
        Say("GetRawInputDeviceList failed, error %lu\n", GetLastError());
        return;
    }

    Say("== Raw input devices (%u) ==\n", count);
    int hidCount = 0;
    for (UINT i = 0; i < count; ++i) {
        RID_DEVICE_INFO info;
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1)
            continue;
        if (info.dwType != RIM_TYPEHID) continue;
        ++hidCount;

        char name[512] = {0};
        UINT nameSize = sizeof(name);
        GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, name, &nameSize);

        Say("  [%s] VID %04X PID %04X  usagePage %04X usage %04X\n",
            TypeName(info.dwType), info.hid.dwVendorId, info.hid.dwProductId,
            info.hid.usUsagePage, info.hid.usUsage);
        Say("        %s\n", name);

        // Report the axis list up front, so a device that has no value caps
        // is obvious before any pedal is pressed.
        UINT ppSize = 0;
        if (GetRawInputDeviceInfoA(list[i].hDevice, RIDI_PREPARSEDDATA, NULL, &ppSize) != 0 ||
            ppSize == 0) {
            Say("        no preparsed data\n");
            continue;
        }
        std::vector<BYTE> pp(ppSize);
        if (GetRawInputDeviceInfoA(list[i].hDevice, RIDI_PREPARSEDDATA, pp.data(), &ppSize) == (UINT)-1) {
            Say("        preparsed data read failed, error %lu\n", GetLastError());
            continue;
        }
        HIDP_CAPS caps;
        if (HidP_GetCaps((PHIDP_PREPARSED_DATA)pp.data(), &caps) != HIDP_STATUS_SUCCESS) {
            Say("        HidP_GetCaps failed\n");
            continue;
        }
        Say("        input report %u bytes, %u value caps, %u button caps\n",
            caps.InputReportByteLength, caps.NumberInputValueCaps, caps.NumberInputButtonCaps);

        if (caps.NumberInputValueCaps == 0) continue;
        std::vector<HIDP_VALUE_CAPS> vcaps(caps.NumberInputValueCaps);
        USHORT n = caps.NumberInputValueCaps;
        if (HidP_GetValueCaps(HidP_Input, vcaps.data(), &n, (PHIDP_PREPARSED_DATA)pp.data())
            != HIDP_STATUS_SUCCESS)
            continue;
        for (USHORT v = 0; v < n; ++v) {
            const HIDP_VALUE_CAPS& c = vcaps[v];
            USHORT lo = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
            USHORT hi = c.IsRange ? c.Range.UsageMax : c.NotRange.Usage;
            Say("        axis page %04X usage %04X..%04X  bits %u  logical %ld..%ld\n",
                c.UsagePage, lo, hi, c.BitSize, (long)c.LogicalMin, (long)c.LogicalMax);
        }
    }
    if (hidCount == 0)
        Say("  no HID devices at all -- Windows does not see the wheel\n");
    Say("\n");
}

// ---------------------------------------------------------------- live watch

struct KnownAxis {
    USHORT vid, pid, page, usage;
    LONG last;
};

static std::vector<KnownAxis> g_axes;
static std::vector<BYTE> g_buffer;
static int g_reports = 0;

static void OnInput(LPARAM lParam) {
    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0)
        return;
    if (g_buffer.size() < size) g_buffer.resize(size);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, g_buffer.data(), &size,
                        sizeof(RAWINPUTHEADER)) != size)
        return;

    RAWINPUT* raw = (RAWINPUT*)g_buffer.data();
    if (raw->header.dwType != RIM_TYPEHID) return;
    if (raw->data.hid.dwCount == 0) return;
    ++g_reports;

    RID_DEVICE_INFO info;
    info.cbSize = sizeof(info);
    UINT infoSize = sizeof(info);
    USHORT vid = 0, pid = 0;
    if (GetRawInputDeviceInfoA(raw->header.hDevice, RIDI_DEVICEINFO, &info, &infoSize) != (UINT)-1) {
        vid = (USHORT)info.hid.dwVendorId;
        pid = (USHORT)info.hid.dwProductId;
    }

    UINT ppSize = 0;
    if (GetRawInputDeviceInfoA(raw->header.hDevice, RIDI_PREPARSEDDATA, NULL, &ppSize) != 0 ||
        ppSize == 0)
        return;
    std::vector<BYTE> pp(ppSize);
    if (GetRawInputDeviceInfoA(raw->header.hDevice, RIDI_PREPARSEDDATA, pp.data(), &ppSize) == (UINT)-1)
        return;
    PHIDP_PREPARSED_DATA parsed = (PHIDP_PREPARSED_DATA)pp.data();

    HIDP_CAPS caps;
    if (HidP_GetCaps(parsed, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0)
        return;
    std::vector<HIDP_VALUE_CAPS> vcaps(caps.NumberInputValueCaps);
    USHORT n = caps.NumberInputValueCaps;
    if (HidP_GetValueCaps(HidP_Input, vcaps.data(), &n, parsed) != HIDP_STATUS_SUCCESS)
        return;

    PCHAR report = (PCHAR)(raw->data.hid.bRawData +
                           (raw->data.hid.dwCount - 1) * raw->data.hid.dwSizeHid);
    const ULONG reportLen = raw->data.hid.dwSizeHid;

    for (USHORT i = 0; i < n; ++i) {
        const HIDP_VALUE_CAPS& c = vcaps[i];
        USHORT lo = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
        USHORT hi = c.IsRange ? c.Range.UsageMax : c.NotRange.Usage;
        for (USHORT usage = lo; usage <= hi; ++usage) {
            ULONG value = 0;
            if (HidP_GetUsageValue(HidP_Input, c.UsagePage, 0, usage, &value, parsed,
                                   report, reportLen) != HIDP_STATUS_SUCCESS)
                continue;

            KnownAxis* known = NULL;
            for (size_t k = 0; k < g_axes.size(); ++k) {
                if (g_axes[k].vid == vid && g_axes[k].pid == pid &&
                    g_axes[k].page == c.UsagePage && g_axes[k].usage == usage) {
                    known = &g_axes[k];
                    break;
                }
            }
            if (!known) {
                KnownAxis a = {vid, pid, c.UsagePage, usage, (LONG)value};
                g_axes.push_back(a);
                Say("  new axis  dev %04X:%04X  page %04X usage %04X = %ld\n",
                    vid, pid, c.UsagePage, usage, (long)value);
                continue;
            }
            // Only report real movement; idle jitter would bury the output.
            LONG delta = (LONG)value - known->last;
            if (delta < 0) delta = -delta;
            if (delta < 256) continue;
            Say("  move      dev %04X:%04X  page %04X usage %04X = %ld\n",
                vid, pid, c.UsagePage, usage, (long)value);
            known->last = (LONG)value;
        }
    }
}

static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        OnInput(l);
        return DefWindowProc(hwnd, msg, w, l);
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int main() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "g29diag.log");
    else strcpy(path, "g29diag.log");
    g_log = fopen(path, "w");

    Say("g29 pedal diagnostic\n");
    Say("log file: %s\n\n", path);

    ListDevices();

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "G29DiagWnd";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "g29diag", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        Say("CreateWindowEx failed, error %lu\n", GetLastError());
        return 1;
    }

    // RIDEV_PAGEONLY takes every usage on the generic desktop page, so the
    // wheel is caught no matter which usage it claims. The shipped app only
    // asks for joystick and gamepad; if reports show up here but the app sees
    // nothing, that difference is the bug.
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0;
    rid.dwFlags     = RIDEV_PAGEONLY | RIDEV_INPUTSINK;
    rid.hwndTarget  = hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        Say("RegisterRawInputDevices failed, error %lu\n", GetLastError());
        return 1;
    }
    Say("== Registered. Press and release each pedal, then the wheel. 30 seconds. ==\n");

    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < 30000) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(1);
    }

    Say("\n== Done. %d HID reports, %u distinct axes. ==\n", g_reports, (unsigned)g_axes.size());
    if (g_reports == 0)
        Say("No reports at all. The wheel is not delivering raw input to a background window.\n");
    Say("Send g29diag.log back.\n");

    if (g_log) fclose(g_log);
    printf("\nPress Enter to close.\n");
    getchar();
    return 0;
}
