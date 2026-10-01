#pragma once

// --- [ORDO] TAA Interceptor — Public API ---
// This is the entry point for the TAA interception system.
// D3D12_Hooks.cpp calls into this via thin [ORDO] brackets.

#include <d3d12.h>

namespace ordo::taa
{

/// Initialize the TAA interception system.
/// Call once after the D3D12 device is created and command list hooks are set up.
/// Loads config from OptiScaler.ini and sets up the Dispatch hook.
///
/// ```cpp
/// // In D3D12_Hooks.cpp HookToDevice():
/// ordo::taa::Initialize(device, commandList);
/// ```
void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

/// Shut down the TAA interception system.
/// Unhooks Dispatch and releases resources.
///
/// ```cpp
/// ordo::taa::Shutdown();
/// ```
void Shutdown();

/// Called by the hooked Dispatch function.
/// Analyzes the dispatch, scores it, and if it matches the confirmed TAA pass,
/// optionally suppresses it. Returns true if the dispatch was intercepted
/// (caller should skip the original Dispatch call).
///
/// ```cpp
/// // In the Dispatch hook:
/// if (ordo::taa::OnDispatch(cmdList, X, Y, Z))
///     return; // TAA dispatch suppressed, DLSS will handle it
/// ```
bool OnDispatch(ID3D12GraphicsCommandList* commandList,
                UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ);

/// Called once per present to advance the frame counter.
/// ```cpp
/// ordo::taa::OnPresent();
/// ```
void OnPresent();

/// Check if the TAA interception system is active and has found a TAA pass.
/// ```cpp
/// if (ordo::taa::IsActive()) { /* TAA is being intercepted */ }
/// ```
bool IsActive();

} // namespace ordo::taa
