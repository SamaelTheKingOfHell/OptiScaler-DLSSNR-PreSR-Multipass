# Roadmap — OptiScaler Fork (The Order)

## Phase 1 — TAA Discovery Engine ← CURRENT
- [ ] Hook `ID3D12GraphicsCommandList::Dispatch` via Detours inside OptiScaler's
      existing D3D12 hook infrastructure.
- [ ] Build `ordo::taa::TAADetector` that tracks bound descriptors per-dispatch
      and scores each dispatch against TAA heuristics (motion vector format,
      ping-pong color buffers, depth SRV, dispatch size vs render resolution).
- [ ] Log candidate TAA passes with resource formats, dimensions, and frame
      timing to `OptiScaler.log` under `[ORDO]` prefix.
- [ ] Add `[OrdoTAA]` INI section with `Enabled=false`, `LogDiscovery=true`.

## Phase 2 — TAA Suppression & Buffer Extraction
- [ ] Once a TAA pass is confirmed (stable detection across N frames), suppress
      the original `Dispatch` call.
- [ ] Extract the SRV resources: current color, history color, motion vectors,
      depth buffer.
- [ ] Extract jitter offsets from constant buffer data.
- [ ] Store extracted buffers in `ordo::taa::FrameContext` for the upscaler.

## Phase 3 — DLSS SR Injection
- [ ] Call `NVSDK_NGX_D3D12_CreateFeature` through OptiScaler's existing
      `FeatureProvider_Dx12` to create a DLSS SR feature instance.
- [ ] Feed extracted TAA inputs to `NVSDK_NGX_D3D12_EvaluateFeature` on the
      same command list, writing output to the render target the game expects.
- [ ] Expose quality mode picker (Ultra Performance → DLAA) through the
      OptiScaler overlay via `[OrdoTAA]` config keys.

## Phase 4 — Per-Game Profiles
- [ ] Create a JSON/INI profile database for known TAA signatures.
- [ ] Auto-detect game executable and load matching profile.
- [ ] Profile for Elden Ring (FromSoft engine).
- [ ] Profile for at least one Unreal Engine 4/5 title.

## Phase 5 — Frame Generation & Neural Rendering
- [ ] Once SR injection is stable, enable OptiScaler's FG pipeline on top.
- [ ] Test DLSS Neural Rendering with the intercepted pipeline.

## Future
- [ ] Vulkan TAA interception (parallel to D3D12 path).
- [ ] Community profile submission system.
