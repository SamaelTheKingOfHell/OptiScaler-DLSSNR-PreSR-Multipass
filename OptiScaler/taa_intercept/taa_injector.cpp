#include "pch.h"
#include "taa_injector.h"
#include "taa_config.h"
#include "taa_detector.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include "Logger.h"
#include "resource.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>

namespace ordo::taa
{

TAAInjector& TAAInjector::Instance()
{
    static TAAInjector s_injector;
    return s_injector;
}

void TAAInjector::Initialize(ID3D12Device* device)
{
    if (device != nullptr)
        _device = device;
}

void TAAInjector::Shutdown()
{
    ReleaseFeature();
    if (_params != nullptr)
    {
        NVSDK_NGX_D3D12_DestroyParameters(reinterpret_cast<NVSDK_NGX_Parameter*>(_params));
        _params = nullptr;
    }
    _device = nullptr;
}

void TAAInjector::ReleaseFeature()
{
    if (_featureHandle != nullptr)
    {
        LOG_INFO("[ORDO] Releasing upscaler feature handle {:X}",
                 reinterpret_cast<NVSDK_NGX_Handle*>(_featureHandle)->Id);
        NVSDK_NGX_D3D12_ReleaseFeature(reinterpret_cast<NVSDK_NGX_Handle*>(_featureHandle));
        _featureHandle = nullptr;
    }
    _evaluatedFrames = 0;
    _isEvaluating = false;
    _createdQualityMode = 999;
    _createdBackend = -1;
    _createdRenderWidth = 0;
    _createdRenderHeight = 0;
    _createdDisplayWidth = 0;
    _createdDisplayHeight = 0;
}

bool TAAInjector::IsFeatureActive() const
{
    if (!_isEvaluating || _featureHandle == nullptr)
        return false;

    auto feat = State::Instance().currentFeature;
    return feat != nullptr && feat->IsInited() && !feat->IsFrozen();
}

bool TAAInjector::EnsureNGXInitialized()
{
    if (State::Instance().nvngxDx12Inited)
        return true;

    if (_device == nullptr)
    {
        if (State::Instance().currentD3D12Device != nullptr)
            _device = State::Instance().currentD3D12Device;
        else
            return false;
    }

    NVSDK_NGX_FeatureCommonInfo fcInfo {};
    auto exePath = Util::ExePath().remove_filename();

    auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
        OPTI_GUID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, OPTI_VERSION, exePath.c_str(), _device, &fcInfo,
        State::Instance().NVNGX_Version == 0 ? NVSDK_NGX_Version_API : State::Instance().NVNGX_Version);

    if (nvResult != NVSDK_NGX_Result_Success)
    {
        LOG_ERROR("[ORDO] NVSDK_NGX_D3D12_Init_with_ProjectID failed: 0x{:X}", static_cast<UINT>(nvResult));
        return false;
    }

    LOG_INFO("[ORDO] NVSDK NGX D3D12 initialized successfully");
    return true;
}

bool TAAInjector::EnsureFeatureCreated(ID3D12GraphicsCommandList* commandList,
                                      uint32_t renderWidth, uint32_t renderHeight,
                                      uint32_t displayWidth, uint32_t displayHeight)
{
    const auto& config = TAAConfig::Instance();
    int currentBackend = static_cast<int>(Config::Instance()->Dx12Upscaler.value_or_default());

    // Check if current feature is still valid
    if (_featureHandle != nullptr)
    {
        if (_createdRenderWidth == renderWidth &&
            _createdRenderHeight == renderHeight &&
            _createdDisplayWidth == displayWidth &&
            _createdDisplayHeight == displayHeight &&
            _createdQualityMode == config.qualityMode &&
            _createdBackend == currentBackend)
        {
            return true;
        }

        LOG_INFO("[ORDO] Feature configuration changed, recreating upscaler feature...");
        ReleaseFeature();
    }

    if (!EnsureNGXInitialized())
        return false;

    // Allocate parameters if not yet allocated
    if (_params == nullptr)
    {
        NVSDK_NGX_Parameter* p = nullptr;
        auto pRes = NVSDK_NGX_D3D12_AllocateParameters(&p);
        if (pRes != NVSDK_NGX_Result_Success || p == nullptr)
        {
            LOG_ERROR("[ORDO] NVSDK_NGX_D3D12_AllocateParameters failed: 0x{:X}", static_cast<UINT>(pRes));
            return false;
        }
        _params = p;
    }

    auto* params = reinterpret_cast<NVSDK_NGX_Parameter*>(_params);

    // Map quality mode to NGX enum
    NVSDK_NGX_PerfQuality_Value perfQuality;
    switch (config.qualityMode)
    {
    case 0: perfQuality = NVSDK_NGX_PerfQuality_Value_UltraQuality; break;
    case 1: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break;
    case 2: perfQuality = NVSDK_NGX_PerfQuality_Value_Balanced; break;
    case 3: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf; break;
    case 4: perfQuality = NVSDK_NGX_PerfQuality_Value_UltraPerformance; break;
    default: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break;
    }

    UINT initFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                     NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
                     NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    params->Set(NVSDK_NGX_Parameter_Width, renderWidth);
    params->Set(NVSDK_NGX_Parameter_Height, renderHeight);
    params->Set(NVSDK_NGX_Parameter_OutWidth, displayWidth);
    params->Set(NVSDK_NGX_Parameter_OutHeight, displayHeight);
    params->Set(NVSDK_NGX_Parameter_PerfQualityValue, perfQuality);
    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    NVSDK_NGX_Handle* handle = nullptr;
    auto createRes = NVSDK_NGX_D3D12_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &handle);
    if (createRes != NVSDK_NGX_Result_Success || handle == nullptr)
    {
        LOG_ERROR("[ORDO] NVSDK_NGX_D3D12_CreateFeature failed: 0x{:X}", static_cast<UINT>(createRes));
        return false;
    }

    _featureHandle = handle;
    _createdRenderWidth = renderWidth;
    _createdRenderHeight = renderHeight;
    _createdDisplayWidth = displayWidth;
    _createdDisplayHeight = displayHeight;
    _createdQualityMode = config.qualityMode;
    _createdBackend = currentBackend;
    _evaluatedFrames = 0;

    LOG_INFO("[ORDO] Created upscaler feature! Handle ID: {}, Render: {}x{}, Display: {}x{}, Quality: {}",
             handle->Id, renderWidth, renderHeight, displayWidth, displayHeight,
             TAAConfig::QualityModeName(config.qualityMode));

    return true;
}

bool TAAInjector::InjectUpscaler(ID3D12GraphicsCommandList* commandList,
                                const DispatchSnapshot& snapshot,
                                const DetectionResult& result)
{
    if (commandList == nullptr)
        return false;

    // Acquire device if needed
    if (_device == nullptr)
    {
        commandList->GetDevice(IID_PPV_ARGS(&_device));
    }

    // Extract resources
    ID3D12Resource* colorRes = nullptr;
    ID3D12Resource* mvRes = nullptr;
    ID3D12Resource* depthRes = nullptr;
    ID3D12Resource* outputRes = nullptr;

    if (result.colorInputIndex >= 0 && result.colorInputIndex < static_cast<int>(snapshot.srvs.size()))
        colorRes = snapshot.srvs[result.colorInputIndex].resource;

    if (result.motionVectorIndex >= 0 && result.motionVectorIndex < static_cast<int>(snapshot.srvs.size()))
        mvRes = snapshot.srvs[result.motionVectorIndex].resource;

    if (result.depthIndex >= 0 && result.depthIndex < static_cast<int>(snapshot.srvs.size()))
        depthRes = snapshot.srvs[result.depthIndex].resource;

    if (snapshot.isRaster)
    {
        if (result.outputIndex >= 0 && result.outputIndex < static_cast<int>(snapshot.rtvs.size()))
            outputRes = snapshot.rtvs[result.outputIndex].resource;
    }
    else
    {
        if (result.outputIndex >= 0 && result.outputIndex < static_cast<int>(snapshot.uavs.size()))
            outputRes = snapshot.uavs[result.outputIndex].resource;
    }

    if (colorRes == nullptr || outputRes == nullptr)
    {
        static uint32_t s_missingResLog = 0;
        if (s_missingResLog++ < 5)
        {
            LOG_WARN("[ORDO] Missing required resource for upscaler: color={:p}, output={:p}",
                     (void*) colorRes, (void*) outputRes);
        }
        return false;
    }

    // Determine display dimensions
    uint32_t displayWidth = snapshot.isRaster && result.outputIndex >= 0 ? static_cast<uint32_t>(snapshot.rtvs[result.outputIndex].width) : 0;
    uint32_t displayHeight = snapshot.isRaster && result.outputIndex >= 0 ? static_cast<uint32_t>(snapshot.rtvs[result.outputIndex].height) : 0;

    if (displayWidth == 0 || displayHeight == 0)
    {
        displayWidth = 3840;
        displayHeight = 2160;
    }

    // Determine render dimensions based on quality mode
    float ratio = TAAConfig::QualityModeRatio(TAAConfig::Instance().qualityMode);
    uint32_t renderWidth = static_cast<uint32_t>(displayWidth / ratio);
    uint32_t renderHeight = static_cast<uint32_t>(displayHeight / ratio);

    // If color input has valid dimensions smaller than display, use it
    if (result.colorInputIndex >= 0)
    {
        uint32_t cw = static_cast<uint32_t>(snapshot.srvs[result.colorInputIndex].width);
        uint32_t ch = static_cast<uint32_t>(snapshot.srvs[result.colorInputIndex].height);
        if (cw > 0 && ch > 0 && cw <= displayWidth && ch <= displayHeight)
        {
            renderWidth = cw;
            renderHeight = ch;
        }
    }

    if (!EnsureFeatureCreated(commandList, renderWidth, renderHeight, displayWidth, displayHeight))
        return false;

    auto* params = reinterpret_cast<NVSDK_NGX_Parameter*>(_params);
    auto* handle = reinterpret_cast<NVSDK_NGX_Handle*>(_featureHandle);

    params->Set(NVSDK_NGX_Parameter_Color, colorRes);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, mvRes);
    params->Set(NVSDK_NGX_Parameter_Depth, depthRes);
    params->Set(NVSDK_NGX_Parameter_Output, outputRes);
    params->Set(NVSDK_NGX_Parameter_Width, renderWidth);
    params->Set(NVSDK_NGX_Parameter_Height, renderHeight);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, renderWidth);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, renderHeight);
    params->Set(NVSDK_NGX_Parameter_Reset, (_evaluatedFrames == 0) ? 1 : 0);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0);

    auto evalRes = NVSDK_NGX_D3D12_EvaluateFeature(commandList, handle, params, nullptr);
    if (evalRes != NVSDK_NGX_Result_Success)
    {
        static uint32_t s_evalErrorLog = 0;
        if (s_evalErrorLog++ < 5)
        {
            LOG_ERROR("[ORDO] NVSDK_NGX_D3D12_EvaluateFeature failed: 0x{:X}", static_cast<UINT>(evalRes));
        }
        return false;
    }

    _evaluatedFrames++;
    _isEvaluating = true;

    if (_evaluatedFrames == 1 || (_evaluatedFrames % 600 == 0))
    {
        LOG_INFO("[ORDO] Upscaler evaluated successfully! Frame count: {}, Resolution: {}x{} -> {}x{}",
                 _evaluatedFrames, renderWidth, renderHeight, displayWidth, displayHeight);
    }

    return true; // Suppress original TAA pass!
}

} // namespace ordo::taa
