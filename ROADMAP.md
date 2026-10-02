# Roadmap — OptiScaler Fork (The Order)

## Upstream Baseline
- [x] Upstream fork rebased to `v0.8.91` (`f45ccf3a761df91450959d325ac0166cba5364c9` - Compressed model input preview).

## Phase 1 — TAA Discovery Engine & Overlay UI ← CURRENT
- [x] Hook `ID3D12GraphicsCommandList::Dispatch` via Detours inside OptiScaler's
      existing D3D12 hook infrastructure.
- [x] Build `ordo::taa::TAADetector` that tracks bound descriptors per-dispatch
      and scores each dispatch against TAA heuristics (motion vector format,
      ping-pong color buffers, depth SRV, dispatch size vs render resolution).
- [x] Log candidate TAA passes with resource formats, dimensions, and frame
      timing to `OptiScaler.log` under `[ORDO]` prefix.
- [x] Add `[OrdoTAA]` INI section with `Enabled=false`, `LogDiscovery=true`,
      `ForceUpscaling=false`, and `QualityMode=1`.
- [x] Add ImGui Overlay UI (`ordo::taa::RenderMenu`) with manual Force Upscaling
      Override toggle, Quality presets (Ultra Quality to Ultra Performance + Custom),
      and TAA Interceptor telemetry status.
- [x] Enable top upscaler selection (DLSS / FSR / XeSS) even when games lack native
      upscaler features (like Elden Ring).
- [x] Extend interception to `DrawInstanced` and `DrawIndexedInstanced` for engines
      (such as FromSoftware's Dantelion engine) that execute TAA via full-screen
      pixel shader passes instead of compute dispatches.

## Phase 2 — TAA Suppression & Buffer Extraction ← CURRENT
- [x] Once a TAA pass is confirmed (stable detection across N frames), suppress
      the original pass when override is enabled.
- [x] Extract the SRV/RTV resources: current color, motion vectors, depth buffer,
      and render target output.
- [ ] Extract jitter offsets from constant buffer data.
- [x] Store extracted buffers and feed them to `ordo::taa::TAAInjector`.

## Phase 3 — Upscaler SR Injection
- [x] Call `NVSDK_NGX_D3D12_CreateFeature` through OptiScaler's NGX subsystem
      to create a SuperSampling feature instance.
- [x] Feed extracted TAA inputs to `NVSDK_NGX_D3D12_EvaluateFeature` on the
      same command list, writing output to the render target the game expects.
- [x] Expose quality mode picker (Ultra Performance → Ultra Quality) through the
      OptiScaler overlay via `[OrdoTAA]` config keys.
- [x] Strict indicator status enforcement: green `[CONFIRMED WORKING]` only when
      upscaler feature is actively initialized and evaluating.

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
