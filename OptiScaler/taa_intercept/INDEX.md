# TAA Intercept Module [Order]

All code in this directory is authored by The Order and follows the isolation
rules defined in `FORK_RULES.md`. Everything lives under the `ordo::taa`
namespace.

## Purpose

Detects and intercepts Temporal Anti-Aliasing (TAA) compute dispatches in D3D12
games, extracts the input buffers (color, motion vectors, depth), and provides
them to OptiScaler's upscaler evaluation pipeline so games without native
DLSS/FSR support can use DLSS Super Resolution.

## Files

| File | Purpose |
| :--- | :--- |
| `INDEX.md` | This file. |
| `taa_config.h` | Configuration struct for `[OrdoTAA]` INI section. |
| `taa_config.cpp` | INI parsing and runtime config loading. |
| `taa_detector.h` | Heuristic engine that scores compute dispatches as TAA candidates. |
| `taa_detector.cpp` | Detection implementation using resource format/dimension analysis. |
| `taa_frame_context.h` | Per-frame captured buffer state for the detected TAA pass. |
| `taa_interceptor.h` | Public API — `Initialize()`, `Shutdown()`, `OnDispatch()` entry points. |
| `taa_interceptor.cpp` | Dispatch hook orchestration and TAA pass suppression. |
| `taa_menu.h` | ImGui overlay menu declarations for forced upscaling override and quality mode. |
| `taa_menu.cpp` | ImGui UI rendering for the ORDO forced upscaling override and TAA interceptor section. |
| `taa_profiles.h` | Per-game TAA signature profiles (Elden Ring, UE4/5, etc.). |
| `taa_profiles.cpp` | Profile loading and auto-detection by executable name. |
