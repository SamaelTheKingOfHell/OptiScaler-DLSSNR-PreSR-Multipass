#pragma once

// --- [ORDO] Per-frame captured buffer context ---
// Stores the D3D12 resources extracted from a detected TAA dispatch.
// These resources are what OptiScaler's upscaler needs as input.

#include <d3d12.h>
#include <cstdint>

namespace ordo::taa
{

/// Captured resources from a single TAA dispatch.
/// The detector fills this when a confirmed TAA pass is intercepted.
///
/// ```cpp
/// FrameContext ctx;
/// if (ctx.isValid) {
///     // Feed ctx.colorInput, ctx.motionVectors, etc. to DLSS evaluate
/// }
/// ```
struct FrameContext
{
    bool isValid = false;

    // --- Input SRVs extracted from the TAA dispatch ---
    ID3D12Resource* colorInput      = nullptr;  // Current frame color (pre-TAA)
    ID3D12Resource* colorHistory    = nullptr;  // Previous frame resolved color (ping-pong)
    ID3D12Resource* motionVectors   = nullptr;  // Screen-space velocity (R16G16_FLOAT)
    ID3D12Resource* depthBuffer     = nullptr;  // Scene depth

    // --- Output UAV ---
    ID3D12Resource* outputTarget    = nullptr;  // Where TAA writes its result

    // --- Dimensions ---
    uint32_t renderWidth  = 0;
    uint32_t renderHeight = 0;

    // --- Formats (for verification) ---
    uint32_t colorFormat        = 0;  // DXGI_FORMAT
    uint32_t motionVectorFormat = 0;  // Should be R16G16_FLOAT (34)
    uint32_t depthFormat        = 0;

    // --- Frame tracking ---
    uint64_t frameIndex = 0;

    /// Reset all fields to invalid state.
    /// ```cpp
    /// ctx.Reset();
    /// assert(!ctx.isValid);
    /// ```
    void Reset()
    {
        isValid = false;
        colorInput = nullptr;
        colorHistory = nullptr;
        motionVectors = nullptr;
        depthBuffer = nullptr;
        outputTarget = nullptr;
        renderWidth = 0;
        renderHeight = 0;
        colorFormat = 0;
        motionVectorFormat = 0;
        depthFormat = 0;
        frameIndex = 0;
    }
};

} // namespace ordo::taa
