# Resource Tracking Module

## Purpose

Tracks D3D12 resource allocations, descriptor heaps, root signatures, and command-list bindings to enable hudless HUD-fix and provide binding introspection for the ORDO TAA interceptor.

## Files

| File | Purpose |
| :--- | :--- |
| `INDEX.md` | This file. |
| `ResTrack_Dx12.h` | Declarations for `ResTrack_Dx12` class, heap tracking, descriptor caching, and binding state introspection. |
| `ResTrack_Dx12.cpp` | Implementation of D3D12 device and command list hooks for descriptor heaps, root signatures, render targets, draw calls, and compute dispatches. |
