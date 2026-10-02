# Hooks Module

## Purpose

Contains API and driver hook implementations for DirectX 12, DirectX 11, Vulkan, and Windows graphics interfaces. Intercepts device creation, command list recording, root signature creation, pipeline states, and descriptor heaps to integrate OptiScaler upscaling and ORDO TAA interception.

## Files

| File | Purpose |
| :--- | :--- |
| `INDEX.md` | This file. |
| `D3D12_Hooks.h` | Header declarations for D3D12 device and command list hooking functions. |
| `D3D12_Hooks.cpp` | Core D3D12 hook implementation using Microsoft Detours. Hooks `CreateDevice`, root signatures, and pipeline states, and delegates to ORDO TAA and ResTrack. |
| `Hook_Utils.h` / `Hook_Utils.cpp` | Utilities for detouring function tables and signatures safely. |
| `DXGI_Hooks.h` / `DXGI_Hooks.cpp` | DXGI swap chain and presentation hooks. |
