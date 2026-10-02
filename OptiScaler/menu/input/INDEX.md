# Input Sub-system Module

## Purpose

Provides input interception, blocking, and forwarding for the ImGui overlay and game automation. Hooks Win32 window messages, DirectInput8, XInput, and RawInput to prevent game input leakage when the overlay is open, and provides auto-progression pulsing for headless/automated test validation.

## Key Files

| File | Purpose |
| :--- | :--- |
| `INDEX.md` | This file. |
| `input_system.h` / `input_system.cpp` | Core input system manager and state coordination. |
| `input_system_directinput.h` / `input_system_directinput.cpp` | DirectInput8 device hooks (`GetDeviceState`, `GetDeviceData`), blocking, and ORDO title-screen auto-progression key pulsing. |
| `input_system_xinput.h` / `input_system_xinput.cpp` | XInput controller hooks (`XInputGetState`, `XInputGetKeystroke`) and gamepad button simulation. |
| `input_system_windows.h` / `input_system_windows.cpp` | Win32 message hooks, subclassing, cursor clipping, and keyboard state hooks. |
| `input_system_raw.h` / `input_system_raw.cpp` | RawInput registration and message processing. |
