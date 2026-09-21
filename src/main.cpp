#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "config.h"
#include "pedals.h"
#include "emitter.h"

#define ID_BASE(row)      (1000 + (row) * 20)
#define ID_PROGRESS(row)  (ID_BASE(row) + 1)
#define ID_PERCENT(row)   (ID_BASE(row) + 2)
#define ID_AXIS(row)      (ID_BASE(row) + 3)
#define ID_DETECT(row)    (ID_BASE(row) + 4)
#define ID_SETKEY(row)    (ID_BASE(row) + 5)
#define ID_TRACK(row)     (ID_BASE(row) + 6)
#define ID_THRTEXT(row)   (ID_BASE(row) + 7)
#define ID_ENABLE(row)    (ID_BASE(row) + 8)
#define ID_KEYTEXT(row)   (ID_BASE(row) + 9)

#define ID_ARM     900
#define ID_STATUS  901
#define HOTKEY_ARM 1

#define TIMER_UI     1
#define DETECT_MS    4000
#define HYSTERESIS   0.05

struct RowUi {
    HWND progress, percent, axis, detect, setkey, track, thrText, enable, keyText;
};

struct DetectAxis {
    USHORT usagePage, usage;
    LONG first, min, max;
};

static AppCfg      g_cfg;
static RowUi       g_ui[PEDAL_COUNT];
static HidReader   g_reader;
static SendInputEmitter g_emitter;

static LONG  g_raw[PEDAL_COUNT]  = {0, 0, 0};
static bool  g_seen[PEDAL_COUNT] = {false, false, false};
static bool  g_held[PEDAL_COUNT] = {false, false, false};

static bool  g_armed = false;
static int   g_captureRow = -1;
static int   g_detectRow = -1;
static DWORD g_detectStart = 0;
static DeviceId g_detectDevice = {0, 0};
static std::vector<DetectAxis> g_detectAxes;

static HWND g_arm = NULL, g_status = NULL;

static void ReleaseAll();

static void SetStatus(const char* text) {
    if (g_status) SetWindowTextA(g_status, text);
}

// ---------------------------------------------------------------- UI refresh

static void RefreshRow(int row) {
    const PedalCfg& p = g_cfg.pedals[row];

    char buf[128];
    if (p.usage == 0) {
        SetWindowTextA(g_ui[row].axis, "axis: not set");
    } else {
        sprintf(buf, "axis %04X:%04X  dev %04X:%04X",
                p.usagePage, p.usage, p.vendorId, p.productId);
        SetWindowTextA(g_ui[row].axis, buf);
    }

    char key[64];
    KeyName(p.vk, key, sizeof(key));
    sprintf(buf, "key: %s", key);
    SetWindowTextA(g_ui[row].keyText, buf);

    sprintf(buf, "%d%%", p.thresholdPct);
    SetWindowTextA(g_ui[row].thrText, buf);

    SendMessage(g_ui[row].track, TBM_SETPOS, TRUE, p.thresholdPct);
    SendMessage(g_ui[row].enable, BM_SETCHECK, p.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void RefreshLive() {
    for (int row = 0; row < PEDAL_COUNT; ++row) {
        int pct = 0;
        if (g_seen[row]) pct = (int)(Normalize(g_cfg.pedals[row], g_raw[row]) * 100.0 + 0.5);
        SendMessage(g_ui[row].progress, PBM_SETPOS, pct, 0);

        char buf[32];
        if (g_seen[row]) sprintf(buf, "%d%%", pct);
        else             strcpy(buf, "--");
        SetWindowTextA(g_ui[row].percent, buf);
    }
}

// ------------------------------------------------------------- key emission

static void ReleaseAll() {
    for (int row = 0; row < PEDAL_COUNT; ++row) {
        if (g_held[row]) {
            g_emitter.Emit(g_cfg.pedals[row].vk, false);
            g_held[row] = false;
        }
    }
}

static void SetArmed(bool armed) {
    if (armed == g_armed) return;
    g_armed = armed;
    if (!g_armed) ReleaseAll();
    SetWindowTextA(g_arm, g_armed ? "Stop (Ctrl+Alt+P)" : "Start (Ctrl+Alt+P)");
    SetStatus(g_armed ? "Armed. Pedals are sending keys."
                      : "Stopped. No keys are being sent.");
}

// Decides the key state for one pedal. The release point sits below the press
// point so a pedal resting near the threshold does not machine-gun the key.
static void Evaluate(int row) {
    PedalCfg& p = g_cfg.pedals[row];
    if (!p.enabled || p.vk == 0 || p.usage == 0) return;

    double n = Normalize(p, g_raw[row]);
    double press = p.thresholdPct / 100.0;
    double release = press - HYSTERESIS;
    if (release < 0.0) release = 0.0;

    bool down = g_held[row] ? (n > release) : (n >= press);
    if (down == g_held[row]) return;

    g_held[row] = down;
    if (g_armed) g_emitter.Emit(p.vk, down);
}

// ----------------------------------------------------------------- detection

static void BeginDetect(int row) {
    // Detection swallows reports, so nothing would ever release a held key.
    ReleaseAll();
    g_detectRow = row;
    g_detectStart = GetTickCount();
    g_detectAxes.clear();
    g_detectDevice.vendorId = 0;
    g_detectDevice.productId = 0;

    char buf[160];
    sprintf(buf, "Detecting %s: press the pedal all the way down, then release. %d seconds.",
            PedalName(row), DETECT_MS / 1000);
    SetStatus(buf);
}

static void FeedDetect(const std::vector<AxisSample>& samples, const DeviceId& id) {
    if (g_detectDevice.vendorId == 0) g_detectDevice = id;
    // Ignore reports from other devices once the first one is locked in.
    if (id.vendorId != g_detectDevice.vendorId || id.productId != g_detectDevice.productId)
        return;

    for (size_t i = 0; i < samples.size(); ++i) {
        const AxisSample& s = samples[i];
        DetectAxis* found = NULL;
        for (size_t j = 0; j < g_detectAxes.size(); ++j) {
            if (g_detectAxes[j].usagePage == s.usagePage && g_detectAxes[j].usage == s.usage) {
                found = &g_detectAxes[j];
                break;
            }
        }
        if (!found) {
            DetectAxis a;
            a.usagePage = s.usagePage;
            a.usage = s.usage;
            a.first = a.min = a.max = s.value;
            g_detectAxes.push_back(a);
            continue;
        }
        if (s.value < found->min) found->min = s.value;
        if (s.value > found->max) found->max = s.value;
    }
}

static void FinishDetect() {
    int row = g_detectRow;
    g_detectRow = -1;

    // The pedal is whichever axis moved the most while the user pressed it.
    DetectAxis* best = NULL;
    LONG bestRange = 0;
    for (size_t i = 0; i < g_detectAxes.size(); ++i) {
        LONG range = g_detectAxes[i].max - g_detectAxes[i].min;
        if (range > bestRange) {
            bestRange = range;
            best = &g_detectAxes[i];
        }
    }

    if (!best || bestRange < 16) {
        SetStatus("Detect failed: no axis moved. Is the wheel plugged in and the pedal pressed?");
        return;
    }

    PedalCfg& p = g_cfg.pedals[row];
    p.usagePage = best->usagePage;
    p.usage     = best->usage;
    p.vendorId  = g_detectDevice.vendorId;
    p.productId = g_detectDevice.productId;
    // first is where the axis sat when detection started, i.e. the released
    // position. Full travel is whichever extreme is further from it, which is
    // what makes an inverted pedal axis work without a separate invert flag.
    p.rest = best->first;
    LONG distLow  = best->first - best->min;
    LONG distHigh = best->max - best->first;
    p.full = (distHigh >= distLow) ? best->max : best->min;

    g_seen[row] = true;
    g_raw[row] = p.rest;

    char buf[160];
    sprintf(buf, "%s bound to axis %04X:%04X, rest %ld, full %ld.",
            PedalName(row), p.usagePage, p.usage, p.rest, p.full);
    SetStatus(buf);
    RefreshRow(row);
    SaveConfig(g_cfg);
}

// ------------------------------------------------------------ raw input feed

static void OnRawInput(LPARAM lParam) {
    static std::vector<AxisSample> samples;
    DeviceId id;
    if (!g_reader.Parse(lParam, samples, id)) return;

    if (g_detectRow >= 0) {
        FeedDetect(samples, id);
        return;
    }

    for (int row = 0; row < PEDAL_COUNT; ++row) {
        PedalCfg& p = g_cfg.pedals[row];
        if (p.usage == 0) continue;
        if (p.vendorId && (p.vendorId != id.vendorId || p.productId != id.productId)) continue;

        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].usagePage != p.usagePage || samples[i].usage != p.usage) continue;
            g_raw[row] = samples[i].value;
            g_seen[row] = true;
            Evaluate(row);
            break;
        }
    }
}

// ------------------------------------------------------------- window layout

static HWND MakeControl(HWND parent, const char* cls, const char* text, DWORD style,
                        int x, int y, int w, int h, int id) {
    return CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id,
                           (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), NULL);
}

static void BuildUi(HWND hwnd) {
    for (int row = 0; row < PEDAL_COUNT; ++row) {
        int top = 12 + row * 72;
        RowUi& u = g_ui[row];

        MakeControl(hwnd, "STATIC", PedalName(row), 0, 10, top + 3, 64, 18, -1);
        u.progress = MakeControl(hwnd, PROGRESS_CLASSA, "", 0, 78, top, 160, 20, ID_PROGRESS(row));
        SendMessage(u.progress, PBM_SETRANGE32, 0, 100);
        u.percent  = MakeControl(hwnd, "STATIC", "--", 0, 244, top + 3, 44, 18, ID_PERCENT(row));
        u.axis     = MakeControl(hwnd, "STATIC", "", 0, 294, top + 3, 130, 18, ID_AXIS(row));
        u.detect   = MakeControl(hwnd, "BUTTON", "Detect", BS_PUSHBUTTON, 430, top - 2, 74, 24, ID_DETECT(row));
        u.setkey   = MakeControl(hwnd, "BUTTON", "Set key", BS_PUSHBUTTON, 510, top - 2, 74, 24, ID_SETKEY(row));

        MakeControl(hwnd, "STATIC", "Thr", 0, 78, top + 32, 26, 18, -1);
        u.track    = MakeControl(hwnd, TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS,
                                 104, top + 28, 180, 26, ID_TRACK(row));
        SendMessage(u.track, TBM_SETRANGE, TRUE, MAKELPARAM(5, 95));
        u.thrText  = MakeControl(hwnd, "STATIC", "", 0, 290, top + 32, 44, 18, ID_THRTEXT(row));
        u.enable   = MakeControl(hwnd, "BUTTON", "Enabled", BS_AUTOCHECKBOX, 344, top + 32, 80, 20, ID_ENABLE(row));
        u.keyText  = MakeControl(hwnd, "STATIC", "", 0, 430, top + 32, 154, 18, ID_KEYTEXT(row));

        RefreshRow(row);
    }

    int bottom = 12 + PEDAL_COUNT * 72;
    g_arm    = MakeControl(hwnd, "BUTTON", "Start (Ctrl+Alt+P)", BS_PUSHBUTTON, 10, bottom, 140, 28, ID_ARM);
    g_status = MakeControl(hwnd, "STATIC", "Stopped.", 0, 160, bottom + 6, 424, 36, ID_STATUS);
}

// ------------------------------------------------------------------ messages

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            BuildUi(hwnd);
            if (!g_reader.Register(hwnd))
                SetStatus("Failed to register for raw input. Nothing will be read.");
            if (!RegisterHotKey(hwnd, HOTKEY_ARM, MOD_CONTROL | MOD_ALT, 'P'))
                SetStatus("Ctrl+Alt+P is taken by another app. Use the Start button.");
            SetTimer(hwnd, TIMER_UI, 50, NULL);
            return 0;

        case WM_INPUT:
            OnRawInput(lParam);
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_INPUT_DEVICE_CHANGE:
            if (wParam == GIDC_REMOVAL) g_reader.Forget((HANDLE)lParam);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_UI) {
                RefreshLive();
                if (g_detectRow >= 0 && GetTickCount() - g_detectStart > DETECT_MS)
                    FinishDetect();
            }
            return 0;

        case WM_HOTKEY:
            if (wParam == HOTKEY_ARM) SetArmed(!g_armed);
            return 0;

        case WM_HSCROLL:
            for (int row = 0; row < PEDAL_COUNT; ++row) {
                if ((HWND)lParam != g_ui[row].track) continue;
                g_cfg.pedals[row].thresholdPct = (int)SendMessage(g_ui[row].track, TBM_GETPOS, 0, 0);
                char buf[32];
                sprintf(buf, "%d%%", g_cfg.pedals[row].thresholdPct);
                SetWindowTextA(g_ui[row].thrText, buf);
                SaveConfig(g_cfg);
                break;
            }
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_ARM) {
                SetArmed(!g_armed);
                return 0;
            }
            for (int row = 0; row < PEDAL_COUNT; ++row) {
                if (id == ID_DETECT(row)) {
                    BeginDetect(row);
                    return 0;
                }
                if (id == ID_SETKEY(row)) {
                    g_captureRow = row;
                    char buf[96];
                    sprintf(buf, "Press the key to bind to %s. Esc cancels.", PedalName(row));
                    SetStatus(buf);
                    return 0;
                }
                if (id == ID_ENABLE(row)) {
                    bool on = SendMessage(g_ui[row].enable, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    g_cfg.pedals[row].enabled = on;
                    if (!on && g_held[row]) {
                        g_emitter.Emit(g_cfg.pedals[row].vk, false);
                        g_held[row] = false;
                    }
                    SaveConfig(g_cfg);
                    return 0;
                }
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            ReleaseAll();
            SaveConfig(g_cfg);
            UnregisterHotKey(hwnd, HOTKEY_ARM);
            KillTimer(hwnd, TIMER_UI);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Key binding is captured in the message loop rather than the window proc:
// whichever button has focus would otherwise eat the keystroke.
static bool CaptureKey(const MSG& msg) {
    if (g_captureRow < 0) return false;
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;

    int row = g_captureRow;
    g_captureRow = -1;

    if (msg.wParam == VK_ESCAPE) {
        SetStatus("Key binding cancelled.");
        return true;
    }

    if (g_held[row]) {
        g_emitter.Emit(g_cfg.pedals[row].vk, false);
        g_held[row] = false;
    }
    g_cfg.pedals[row].vk = (UINT)msg.wParam;

    char key[64], buf[128];
    KeyName(g_cfg.pedals[row].vk, key, sizeof(key));
    sprintf(buf, "%s bound to %s.", PedalName(row), key);
    SetStatus(buf);
    RefreshRow(row);
    SaveConfig(g_cfg);
    return true;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    LoadConfig(g_cfg);

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "G29PedalKeysWnd";
    if (!RegisterClassA(&wc)) return 1;

    RECT rc = {0, 0, 600, 12 + PEDAL_COUNT * 72 + 52};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "G29 Pedal Keys", style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (CaptureKey(msg)) continue;
        if (IsDialogMessage(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
