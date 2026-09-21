# G29 Pedal Keys

Maps the three Logitech G29 pedals to keyboard keys on Windows. Each pedal is a
threshold switch: past the configured depth the key goes down, below it the key
goes up.

## Build

A prebuilt `g29pedalkeys.exe` (x86-64, statically linked, no runtime needed) is
already in the project root, cross-compiled with mingw-w64. Copy it to the
Windows machine and run it. To rebuild from source on the Windows machine, from the project root:

```
build.bat
```

It uses MSVC if `cl` is on PATH (open the "x64 Native Tools Command Prompt for
VS"), otherwise MinGW-w64 `g++`. A CMake build is also provided:

```
cmake -B build -A x64
cmake --build build --config Release
```

From macOS or Linux with mingw-w64 installed, `./build-cross.sh` produces the
same exe.

The result is `g29pedalkeys.exe`. Settings are stored in `g29pedalkeys.ini`
next to the executable.

## Use

1. Plug in the G29. Leave G HUB installed; nothing needs to be configured there.
2. Run `g29pedalkeys.exe`.
3. For each pedal, click **Detect**, then press that pedal fully down and
   release it. Detection samples for four seconds and binds whichever HID axis
   moved the most. Inverted axes are handled automatically.
4. Click **Set key** and press the key you want that pedal to send.
5. Set the threshold slider (press depth, 5-95%).
6. Tick **Enabled** for the pedals you want active.
7. Click **Start**, or press **Ctrl+Alt+P**. The same hotkey stops it from
   inside a game.

Held keys are released when you stop, untick a pedal, or close the app.

## How it works

- Pedals are read with Raw Input (`RIDEV_INPUTSINK`), so reports keep arriving
  while a game has focus. Reports are decoded with the HID parser
  (`HidP_GetValueCaps` / `HidP_GetUsageValue`), not DirectInput.
- Bindings are stored as `(usage page, usage, vendor id, product id)`, which is
  stable across reboots, unlike raw input device handles.
- Keys are sent with `SendInput` using scancodes (`KEYEVENTF_SCANCODE`), which
  DirectInput-era games read. `KeyEmitter` in `src/emitter.h` is the backend
  interface; an Interception driver backend can be added behind it without
  touching the UI or pedal code.
- The release threshold sits 5% below the press threshold so a pedal resting on
  the threshold does not repeat the key.

## Anti-cheat

Easy Anti-Cheat (Fortnite), BattlEye and Vanguard can see `SendInput` keys as
injected and may ignore or flag them. This app does no automation or macros --
it is a 1:1 remap -- but that does not guarantee a given anti-cheat tolerates
injected input. Test in a private or creative mode before relying on it. A
kernel driver backend would bypass the injection flag but is itself the kind of
driver these anti-cheats scan for.

## Files

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Window, controls, detection state machine, message loop |
| `src/pedals.cpp` | Raw Input registration and HID report decoding |
| `src/emitter.cpp` | Key output backend and key naming |
| `src/config.cpp` | INI load/save and axis normalization |
