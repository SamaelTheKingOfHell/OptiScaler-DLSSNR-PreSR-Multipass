#include "pch.h"
#include "taa_interceptor.h"
#include "taa_config.h"
#include "taa_detector.h"
#include "taa_frame_context.h"
#include "taa_profiles.h"
#include "taa_injector.h"

#include <Config.h>
#include <State.h>
#include <resource_tracking/ResTrack_Dx12.h>

#include <detours/detours.h>

#include <atomic>

#pragma intrinsic(_ReturnAddress)

namespace ordo::taa
{

// --- Static state ---
static TAADetector s_detector;
static FrameContext s_frameContext;
static std::atomic<uint64_t> s_frameCounter { 0 };
static std::atomic<bool> s_initialized { false };

static ID3D12Device* s_device = nullptr;

// --- Dispatch hook state ---
// Dispatch vtable index = 14 on ID3D12GraphicsCommandList
using PFN_Dispatch = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
static PFN_Dispatch o_Dispatch = nullptr;

// --- Resource extraction helpers ---

/// Try to resolve a GPU descriptor handle to a ResourceInfo using
/// OptiScaler's existing heap tracking infrastructure.
/// Returns true if the resource was found and outInfo is populated.
///
/// ```cpp
/// ResourceInfo info;
/// if (TryResolveDescriptor(gpuHandle, info)) {
///     LOG_DEBUG("Found resource: {}x{} fmt={}", info.width, info.height, info.format);
/// }
/// ```
static bool TryResolveDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, ResourceInfo& outInfo)
{
    if (gpuHandle.ptr == 0)
        return false;

    auto heap = ResTrack_Dx12::GetHeapByGpuHandleCR(gpuHandle.ptr);
    if (heap != nullptr && heap->active.load(std::memory_order_acquire))
    {
        return heap->GetByGpuHandle(gpuHandle.ptr, outInfo) && outInfo.buffer != nullptr;
    }

    return false;
}

// --- Resource extraction helpers ---

/// Build a DispatchSnapshot from the tracked resources on the command list.
static void BuildSnapshot(ID3D12GraphicsCommandList* cmdList,
                          bool isRaster,
                          UINT paramA, UINT paramB, UINT paramC,
                          DispatchSnapshot& snap)
{
    snap = {};
    snap.isRaster = isRaster;

    if (isRaster)
    {
        snap.vertexCount = paramA;
        snap.indexCount = paramB;
        snap.instanceCount = paramC;
    }
    else
    {
        snap.threadGroupCountX = paramA;
        snap.threadGroupCountY = paramB;
        snap.threadGroupCountZ = paramC;
    }

    std::vector<ResourceInfo> srvs, uavs, rtvs;
    ResourceInfo dsvInfo {};

    if (ResTrack_Dx12::GetCurrentBindings(cmdList, isRaster, srvs, uavs, rtvs, &dsvInfo))
    {
        for (const auto& r : srvs)
        {
            BoundResource b;
            b.resource = r.buffer;
            b.format = r.format;
            b.width = r.width;
            b.height = r.height;
            b.type = ResourceType::SRV;
            snap.srvs.push_back(b);
        }

        for (const auto& r : uavs)
        {
            BoundResource b;
            b.resource = r.buffer;
            b.format = r.format;
            b.width = r.width;
            b.height = r.height;
            b.type = ResourceType::UAV;
            snap.uavs.push_back(b);
        }

        for (const auto& r : rtvs)
        {
            BoundResource b;
            b.resource = r.buffer;
            b.format = r.format;
            b.width = r.width;
            b.height = r.height;
            b.type = ResourceType::RTV;
            snap.rtvs.push_back(b);
        }

        if (dsvInfo.buffer != nullptr)
        {
            snap.dsv.resource = dsvInfo.buffer;
            snap.dsv.format = dsvInfo.format;
            snap.dsv.width = dsvInfo.width;
            snap.dsv.height = dsvInfo.height;
        }
    }
}

// --- Public API ---

void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    if (s_initialized.exchange(true))
        return; // Already initialized

    s_device = device;

    // Load configuration from OptiScaler.ini
    std::string iniPath;
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        std::wstring wExePath(exePath);
        auto lastSlash = wExePath.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos)
        {
            std::wstring wDir = wExePath.substr(0, lastSlash + 1);
            std::wstring wIniPath = wDir + L"OptiScaler.ini";

            int size = WideCharToMultiByte(CP_UTF8, 0, wIniPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (size > 0)
            {
                iniPath.resize(size - 1);
                WideCharToMultiByte(CP_UTF8, 0, wIniPath.c_str(), -1, iniPath.data(), size, nullptr, nullptr);
            }
        }
    }

    TAAConfig::Instance().LoadFromINI(iniPath);

    TAAInjector::Instance().Initialize(device);

    if (!TAAConfig::Instance().enabled)
    {
        LOG_INFO("[ORDO] TAA interception is currently inactive in INI. Standing by for activation.");
    }

    // Load game profile if available
    TAAProfiles::Instance().LoadProfiles();
    TAAProfiles::Instance().AutoDetectGame();

    LOG_INFO("[ORDO] TAA Interceptor initialized. Monitoring compute dispatches and raster passes...");
}

void Shutdown()
{
    if (!s_initialized.exchange(false))
        return;

    TAAInjector::Instance().Shutdown();
    s_detector.Reset();
    s_frameContext.Reset();
    s_device = nullptr;

    LOG_INFO("[ORDO] TAA Interceptor shut down");
}

bool OnDispatch(ID3D12GraphicsCommandList* commandList,
                UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ)
{
    if (!s_initialized.load(std::memory_order_acquire) || !TAAConfig::Instance().enabled)
        return false;

    const auto& config = TAAConfig::Instance();
    uint64_t frame = s_frameCounter.load(std::memory_order_relaxed);

    // Quick dimension filter — TAA is always a full-screen 2D dispatch
    if (threadGroupCountZ != 1)
        return false;

    uint32_t inferredW = threadGroupCountX * 8;
    uint32_t inferredH = threadGroupCountY * 8;

    if (inferredW < config.minDispatchWidth || inferredH < config.minDispatchHeight)
        return false;

    static std::atomic<uint32_t> s_dispatchCallCount { 0 };
    uint32_t callNum = s_dispatchCallCount.fetch_add(1);

    // Discovery mode — build a snapshot from active command list bindings and score it
    DispatchSnapshot snapshot;
    BuildSnapshot(commandList, false, threadGroupCountX, threadGroupCountY, threadGroupCountZ, snapshot);

    if (callNum < 10)
    {
        LOG_INFO("[ORDO] OnDispatch #{} (thread groups: {}x{}x{}, inferred: {}x{}): UAVs={}, RTVs={}, SRVs={}",
                 callNum, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
                 inferredW, inferredH,
                 snapshot.uavs.size(), snapshot.rtvs.size(), snapshot.srvs.size());
    }

    // Skip dispatches with too few bound resources
    if (snapshot.srvs.empty() && snapshot.uavs.empty() && snapshot.rtvs.empty())
        return false;

    auto result = s_detector.ScoreDispatch(snapshot);

    if (callNum < 15 || (callNum % 200 == 0) || result.score >= 0.3f)
    {
        LOG_INFO("[ORDO] OnDispatch #{} (thread groups: {}x{}x{}, inferred: {}x{}, SRVs={}, UAVs={}): Score={:.2f}, frame={}",
                 callNum, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
                 inferredW, inferredH, snapshot.srvs.size(), snapshot.uavs.size(),
                 result.score, frame);
    }

    if (result.score >= s_detector.GetConfidenceThreshold())
    {
        s_detector.TrackCandidate(snapshot, result, frame);
    }

    if (s_detector.IsConfirmedPass(snapshot))
    {
        if (TAAConfig::Instance().forceUpscaling)
        {
            if (TAAInjector::Instance().InjectUpscaler(commandList, snapshot, result))
                return true;
        }
        else
        {
            TAAInjector::Instance().ReleaseFeature();
        }
    }

    return false;
}

bool OnDrawInstanced(ID3D12GraphicsCommandList* commandList,
                     UINT vertexCountPerInstance, UINT instanceCount,
                     UINT startVertexLocation, UINT startInstanceLocation)
{
    if (!s_initialized.load(std::memory_order_acquire) || !TAAConfig::Instance().enabled)
        return false;

    // Full-screen triangle is 3 vertices, full-screen quad strip is 4, quad list is 6
    if (vertexCountPerInstance != 3 && vertexCountPerInstance != 4 && vertexCountPerInstance != 6)
        return false;

    if (instanceCount != 1)
        return false;

    uint64_t frame = s_frameCounter.load(std::memory_order_relaxed);

    static std::atomic<uint32_t> s_drawCallCount { 0 };
    uint32_t callNum = s_drawCallCount.fetch_add(1);

    DispatchSnapshot snapshot;
    BuildSnapshot(commandList, true, vertexCountPerInstance, 0, instanceCount, snapshot);

    if (callNum < 10)
    {
        LOG_INFO("[ORDO] OnDrawInstanced #{} (verts={}, instances={}): RTVs={}, SRVs={}, DSV={}",
                 callNum, vertexCountPerInstance, instanceCount,
                 snapshot.rtvs.size(), snapshot.srvs.size(), snapshot.dsv.resource != nullptr);
    }

    if (snapshot.srvs.empty() && snapshot.rtvs.empty())
        return false;

    auto result = s_detector.ScoreDispatch(snapshot);

    if (callNum < 20 || (callNum % 100 == 0) || result.score >= 0.3f)
    {
        LOG_INFO("[ORDO] OnDrawInstanced #{} (verts={}, RTVs={}, SRVs={}): Score={:.2f}, frame={}",
                 callNum, vertexCountPerInstance, snapshot.rtvs.size(), snapshot.srvs.size(),
                 result.score, frame);
    }

    if (result.score >= s_detector.GetConfidenceThreshold())
    {
        s_detector.TrackCandidate(snapshot, result, frame);
    }

    if (s_detector.IsConfirmedPass(snapshot))
    {
        if (TAAConfig::Instance().forceUpscaling)
        {
            if (TAAInjector::Instance().InjectUpscaler(commandList, snapshot, result))
                return true;
        }
        else
        {
            TAAInjector::Instance().ReleaseFeature();
        }
    }

    return false;
}

bool OnDrawIndexedInstanced(ID3D12GraphicsCommandList* commandList,
                            UINT indexCountPerInstance, UINT instanceCount,
                            UINT startIndexLocation, INT baseVertexLocation,
                            UINT startInstanceLocation)
{
    if (!s_initialized.load(std::memory_order_acquire) || !TAAConfig::Instance().enabled)
        return false;

    // Full-screen indexed quad is 4 or 6 indices, full-screen triangle is 3
    if (indexCountPerInstance != 3 && indexCountPerInstance != 4 && indexCountPerInstance != 6)
        return false;

    if (instanceCount != 1)
        return false;

    uint64_t frame = s_frameCounter.load(std::memory_order_relaxed);

    static std::atomic<uint32_t> s_drawIndexedCount { 0 };
    uint32_t callNum = s_drawIndexedCount.fetch_add(1);

    DispatchSnapshot snapshot;
    BuildSnapshot(commandList, true, 0, indexCountPerInstance, instanceCount, snapshot);

    if (callNum < 10)
    {
        LOG_INFO("[ORDO] OnDrawIndexedInstanced #{} (indices={}, instances={}): RTVs={}, SRVs={}, DSV={}",
                 callNum, indexCountPerInstance, instanceCount,
                 snapshot.rtvs.size(), snapshot.srvs.size(), snapshot.dsv.resource != nullptr);
    }

    if (snapshot.srvs.empty() && snapshot.rtvs.empty())
        return false;

    auto result = s_detector.ScoreDispatch(snapshot);

    if (callNum < 20 || (callNum % 100 == 0) || result.score >= 0.3f)
    {
        LOG_INFO("[ORDO] OnDrawIndexedInstanced #{} (indices={}, RTVs={}, SRVs={}): Score={:.2f}, frame={}",
                 callNum, indexCountPerInstance, snapshot.rtvs.size(), snapshot.srvs.size(),
                 result.score, frame);
    }

    if (result.score >= s_detector.GetConfidenceThreshold())
    {
        s_detector.TrackCandidate(snapshot, result, frame);
    }

    if (s_detector.IsConfirmedPass(snapshot))
    {
        if (TAAConfig::Instance().forceUpscaling)
        {
            if (TAAInjector::Instance().InjectUpscaler(commandList, snapshot, result))
                return true;
        }
        else
        {
            TAAInjector::Instance().ReleaseFeature();
        }
    }

    return false;
}

void OnPresent()
{
    auto f = s_frameCounter.fetch_add(1, std::memory_order_relaxed);
    if (f == 0 || f == 60 || (f % 600 == 0))
    {
        LOG_INFO("[ORDO] Present frame {}, TAA status: confirmed={}, candidates={}",
                 f, s_detector.HasConfirmedPass(), s_detector.GetCandidateCount());
    }
}

bool IsActive()
{
    return s_initialized.load(std::memory_order_acquire) &&
           TAAConfig::Instance().enabled &&
           s_detector.HasConfirmedPass();
}

bool HasConfirmedPass()
{
    return s_detector.HasConfirmedPass();
}

size_t GetCandidateCount()
{
    return s_detector.GetCandidateCount();
}

bool IsConfirmedWorking()
{
    // Must have a positively identified and confirmed TAA pass
    if (!HasConfirmedPass())
        return false;

    // If force upscaling override is enabled, it is ONLY confirmed working
    // if the upscaler feature has actually been created, initialized, and is actively evaluating
    if (TAAConfig::Instance().forceUpscaling)
    {
        return TAAInjector::Instance().IsFeatureActive();
    }

    return false;
}

} // namespace ordo::taa
