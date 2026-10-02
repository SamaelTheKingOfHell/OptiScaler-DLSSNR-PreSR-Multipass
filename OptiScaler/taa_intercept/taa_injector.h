#pragma once

// --- [ORDO] TAA Upscaler Injector ---
// Responsible for initializing NGX, creating the upscaler feature,
// binding captured TAA frame buffers, evaluating the upscaler, and
// suppressing the native TAA draw/dispatch pass.

#include <d3d12.h>
#include <cstdint>
#include "taa_detector.h"

namespace ordo::taa
{

/// Manages upscaler feature creation, parameter mapping, and evaluation
/// for intercepted TAA render passes.
///
/// ```cpp
/// if (TAAInjector::Instance().InjectUpscaler(cmdList, snapshot, result)) {
///     // Original TAA pass was successfully replaced and evaluated
///     return true; // suppress original draw
/// }
/// ```
class TAAInjector
{
public:
    static TAAInjector& Instance();

    /// Initialize injector with the D3D12 device.
    ///
    /// ```cpp
    /// TAAInjector::Instance().Initialize(device);
    /// ```
    void Initialize(ID3D12Device* device);

    /// Shut down and release active features.
    void Shutdown();

    /// Attempt to inject upscaler evaluation on the given command list.
    /// Returns true if the upscaler evaluated successfully and the original pass should be suppressed.
    ///
    /// ```cpp
    /// bool suppressed = injector.InjectUpscaler(cmdList, snapshot, result);
    /// ```
    bool InjectUpscaler(ID3D12GraphicsCommandList* commandList,
                        const DispatchSnapshot& snapshot,
                        const DetectionResult& result);

    /// Check if an upscaler feature has been created and is actively evaluating.
    bool IsFeatureActive() const;

    /// Force release and cleanup of current feature.
    void ReleaseFeature();

private:
    TAAInjector() = default;
    ~TAAInjector() = default;

    bool EnsureNGXInitialized();
    bool EnsureFeatureCreated(ID3D12GraphicsCommandList* commandList,
                              uint32_t renderWidth, uint32_t renderHeight,
                              uint32_t displayWidth, uint32_t displayHeight);

    ID3D12Device* _device = nullptr;
    void* _params = nullptr;          // NVSDK_NGX_Parameter*
    void* _featureHandle = nullptr;   // NVSDK_NGX_Handle*

    uint32_t _createdRenderWidth = 0;
    uint32_t _createdRenderHeight = 0;
    uint32_t _createdDisplayWidth = 0;
    uint32_t _createdDisplayHeight = 0;
    uint32_t _createdQualityMode = 999;
    int _createdBackend = -1;

    uint64_t _evaluatedFrames = 0;
    bool _isEvaluating = false;
};

} // namespace ordo::taa
