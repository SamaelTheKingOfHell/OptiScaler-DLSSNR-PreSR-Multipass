# Objective — OptiScaler Fork (The Order)

**Mission**: Build a general-purpose TAA interceptor that detects and replaces
Temporal Anti-Aliasing passes in any D3D12 game, feeding captured buffers
(color, motion vectors, depth) into OptiScaler's upscaler evaluation pipeline
so games without native DLSS/FSR support gain full DLSS Super Resolution,
Frame Generation, and Neural Rendering — starting with Elden Ring.

## Scope

- **In scope**: TAA detection heuristics, compute dispatch interception, buffer
  extraction, DLSS SR evaluate injection, quality mode selection via overlay,
  per-game profile database.
- **Out of scope**: Modifications to upstream OptiScaler's upscaler backends,
  overlay UI framework, or swapchain wrapping. We consume their APIs.

## Success Criteria

1. Elden Ring launches with our fork, TAA pass is detected and replaced with
   DLSS SR at user-selected quality, visible in the OptiScaler overlay.
2. The fork builds cleanly with and without `ORDO_FEATURES_ENABLED`.
3. At least one additional non-FromSoft D3D12 game has a working TAA profile.
