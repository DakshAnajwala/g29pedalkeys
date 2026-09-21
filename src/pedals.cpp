#include "pedals.h"

bool HidReader::Register(HWND hwnd) {
    // Every usage on the generic desktop page, not just joystick and gamepad:
    // wheels do not agree on which usage they claim, and a wrong guess here
    // means no reports at all. Reports that are not HID are dropped in Parse.
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0;
    rid.dwFlags     = RIDEV_PAGEONLY | RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid.hwndTarget  = hwnd;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

void HidReader::Forget(HANDLE device) {
    for (size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].handle == device) {
            cache_.erase(cache_.begin() + i);
            return;
        }
    }
}

HidReader::DeviceCache* HidReader::Lookup(HANDLE device) {
    for (size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].handle == device) return &cache_[i];
    }

    UINT size = 0;
    if (GetRawInputDeviceInfoA(device, RIDI_PREPARSEDDATA, NULL, &size) != 0 || size == 0)
        return NULL;

    DeviceCache entry;
    entry.handle = device;
    entry.preparsed.resize(size);
    if (GetRawInputDeviceInfoA(device, RIDI_PREPARSEDDATA, entry.preparsed.data(), &size) == (UINT)-1)
        return NULL;

    PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)entry.preparsed.data();
    HIDP_CAPS caps;
    if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0)
        return NULL;

    entry.valueCaps.resize(caps.NumberInputValueCaps);
    USHORT count = caps.NumberInputValueCaps;
    if (HidP_GetValueCaps(HidP_Input, entry.valueCaps.data(), &count, pp) != HIDP_STATUS_SUCCESS)
        return NULL;
    entry.valueCaps.resize(count);

    RID_DEVICE_INFO info;
    info.cbSize = sizeof(info);
    UINT infoSize = sizeof(info);
    entry.id.vendorId = 0;
    entry.id.productId = 0;
    if (GetRawInputDeviceInfoA(device, RIDI_DEVICEINFO, &info, &infoSize) != (UINT)-1 &&
        info.dwType == RIM_TYPEHID) {
        entry.id.vendorId  = (USHORT)info.hid.dwVendorId;
        entry.id.productId = (USHORT)info.hid.dwProductId;
    }

    cache_.push_back(entry);
    return &cache_.back();
}

bool HidReader::Parse(LPARAM lParam, std::vector<AxisSample>& out, DeviceId& id) {
    out.clear();

    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) != 0)
        return false;
    if (buffer_.size() < size) buffer_.resize(size);
    if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer_.data(), &size,
                        sizeof(RAWINPUTHEADER)) != size)
        return false;

    RAWINPUT* raw = (RAWINPUT*)buffer_.data();
    if (raw->header.dwType != RIM_TYPEHID) return false;
    if (raw->data.hid.dwCount == 0 || raw->data.hid.dwSizeHid == 0) return false;

    DeviceCache* dev = Lookup(raw->header.hDevice);
    if (!dev) return false;
    id = dev->id;

    PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)dev->preparsed.data();
    // Only the newest report in the batch matters; older ones are stale pedal
    // positions we would immediately overwrite.
    const DWORD last = raw->data.hid.dwCount - 1;
    PCHAR report = (PCHAR)(raw->data.hid.bRawData + last * raw->data.hid.dwSizeHid);
    const ULONG reportLen = raw->data.hid.dwSizeHid;

    for (size_t i = 0; i < dev->valueCaps.size(); ++i) {
        const HIDP_VALUE_CAPS& vc = dev->valueCaps[i];
        USHORT usageMin = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
        USHORT usageMax = vc.IsRange ? vc.Range.UsageMax : vc.NotRange.Usage;
        for (USHORT usage = usageMin; usage <= usageMax; ++usage) {
            ULONG value = 0;
            if (HidP_GetUsageValue(HidP_Input, vc.UsagePage, 0, usage, &value, pp,
                                   report, reportLen) != HIDP_STATUS_SUCCESS)
                continue;
            AxisSample s;
            s.usagePage = vc.UsagePage;
            s.usage     = usage;
            s.value     = (LONG)value;
            out.push_back(s);
        }
    }
    return !out.empty();
}
