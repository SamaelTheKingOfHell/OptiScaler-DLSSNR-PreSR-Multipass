# Menu Module

## Purpose

Provides the ImGui in-game overlay menu and rendering integrations across DirectX 11, DirectX 12, and Vulkan. Renders settings controls, upscaler selections, frame generation options, and the ORDO Force Upscaling and TAA Interceptor panel.

## Key Files

| File | Purpose |
| :--- | :--- |
| `INDEX.md` | This file. |
| `menu_common.h` / `menu_common.cpp` | Core menu layout, tab navigation, settings controls, and upscaler selection UI. |
| `menu_dx12.h` / `menu_dx12.cpp` | DirectX 12 ImGui overlay backend rendering and swapchain integration. |
| `menu_dx11.h` / `menu_dx11.cpp` | DirectX 11 ImGui overlay backend rendering. |
| `menu_overlay_vk.h` / `menu_overlay_vk.cpp` | Vulkan overlay rendering implementation. |
| `input/` | Input hooking sub-system (Win32 messages, DirectInput, XInput). |
