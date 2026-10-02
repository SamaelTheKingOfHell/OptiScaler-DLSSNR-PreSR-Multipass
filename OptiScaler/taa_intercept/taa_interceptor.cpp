#include "pch.h"
#include "taa_interceptor.h"
#include "taa_config.h"
#include "taa_detector.h"
#include "taa_frame_context.h"
#include "taa_profiles.h"

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

/// Build a DispatchSnapshot from the tracked resources.
/// Uses OptiScaler's tracked descriptor heaps and resource tracking.
///
/// ```cpp
/// DispatchSnapshot snap;
/// BuildSnapshot(cmdList, 240, 135, 1, snap);
/// ```
static void BuildSnapshot(ID3D12GraphicsCommandList* cmdList,
                          UINT tgX, UINT tgY, UINT tgZ,
                          DispatchSnapshot& snap)
{
    snap.threadGroupCountX = tgX;
    snap.threadGroupCountY = tgY;
    snap.threadGroupCountZ = tgZ;
    snap.srvs.clear();
    snap.uavs.clear();

    const uint64_t currentFrame = s_frameCounter.load(std::memory_order_relaxed);

    std::scoped_lock lock(_trackedResourcesMutex);
    for (const auto& [resource, slots] : _trackedResources)
    {
        if (resource == nullptr || slots.empty())
            continue;

        for (const auto& slot : slots)
        {
            auto heap = slot.heap.lock();
            if (!heap || !heap->active.load(std::memory_order_acquire))
                continue;

            SIZE_T gpuHandle = heap->gpuStart + static_cast<SIZE_T>(slot.index) * heap->increment;
            ResourceInfo info {};
            if (heap->GetByGpuHandle(gpuHandle, info) && info.buffer != nullptr)
            {
                BoundResource bound;
                bound.resource = info.buffer;
                bound.format = info.format;
                bound.width = info.width;
                bound.height = info.height;
                bound.type = info.type;

                if (info.type == ResourceType::SRV)
                    snap.srvs.push_back(bound);
                else if (info.type == ResourceType::UAV)
                    snap.uavs.push_back(bound);

                break;
            }
        }
    }
}

// --- Dispatch hook ---

/// The hooked Dispatch function. Called for every compute dispatch in the game.
/// We analyze the dispatch, score it, and if configured, suppress confirmed
/// TAA passes.
static void STDMETHODCALLTYPE hkDispatch(ID3D12GraphicsCommandList* cmdList,
                                          UINT ThreadGroupCountX,
                                          UINT ThreadGroupCountY,
                                          UINT ThreadGroupCountZ)
{
    static std::atomic<uint32_t> s_totalDispatches { 0 };
    uint32_t total = s_totalDispatches.fetch_add(1);

    if (total < 10 || (total % 1000 == 0))
    {
        LOG_INFO("[ORDO] hkDispatch #{} called: {}x{}x{}", total, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    }

    bool intercepted = false;

    if (s_initialized.load(std::memory_order_acquire) &&
        TAAConfig::Instance().enabled)
    {
        intercepted = OnDispatch(cmdList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    }

    if (o_Dispatch)
    {
        o_Dispatch(cmdList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
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

    if (!TAAConfig::Instance().enabled)
    {
        LOG_INFO("[ORDO] TAA interception is DISABLED. Set [OrdoTAA] Enabled=true to activate.");
        s_initialized.store(false);
        return;
    }

    // Load game profile if available
    TAAProfiles::Instance().LoadProfiles();
    TAAProfiles::Instance().AutoDetectGame();

    // Hook Dispatch on the command list vtable
    if (commandList != nullptr)
    {
        PVOID* pVTable = *(PVOID**)commandList;
        o_Dispatch = (PFN_Dispatch)pVTable[14]; // Dispatch = vtable index 14

        if (o_Dispatch != nullptr)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)o_Dispatch, hkDispatch);

            if (DetourTransactionCommit() == NO_ERROR)
            {
                LOG_INFO("[ORDO] Dispatch hook installed successfully");
            }
            else
            {
                LOG_ERROR("[ORDO] Failed to install Dispatch hook");
                o_Dispatch = nullptr;
                s_initialized.store(false);
                return;
            }
        }
        else
        {
            LOG_ERROR("[ORDO] Could not find Dispatch in command list vtable");
            s_initialized.store(false);
            return;
        }
    }

    LOG_INFO("[ORDO] TAA Interceptor initialized. Monitoring compute dispatches...");
}

void Shutdown()
{
    if (!s_initialized.exchange(false))
        return;

    // Unhook Dispatch
    if (o_Dispatch != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)o_Dispatch, hkDispatch);
        DetourTransactionCommit();
        o_Dispatch = nullptr;
        LOG_INFO("[ORDO] Dispatch hook removed");
    }

    s_detector.Reset();
    s_frameContext.Reset();
    s_device = nullptr;

    LOG_INFO("[ORDO] TAA Interceptor shut down");
}

bool OnDispatch(ID3D12GraphicsCommandList* commandList,
                UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ)
{
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

    if (callNum < 15 || (callNum % 500 == 0))
    {
        LOG_INFO("[ORDO] OnDispatch #{} (thread groups: {}x{}x{}, inferred: {}x{}), frame={}",
                 callNum, threadGroupCountX, threadGroupCountY, threadGroupCountZ,
                 inferredW, inferredH, frame);
    }

    // If we already have a confirmed pass, check if this is the same pipeline
    if (s_detector.HasConfirmedPass())
    {
        if (config.logPerFrame)
        {
            LOG_DEBUG("[ORDO] Confirmed TAA pass dispatched: {}x{}x{}",
                      threadGroupCountX, threadGroupCountY, threadGroupCountZ);
        }
        return false; // Phase 1: don't suppress
    }

    // Discovery mode — build a snapshot and score it
    DispatchSnapshot snapshot;
    BuildSnapshot(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ, snapshot);

    if (callNum < 15 || (callNum % 500 == 0))
    {
        LOG_INFO("[ORDO]   Snapshot #{} has {} SRVs, {} UAVs (total tracked: {})",
                 callNum, snapshot.srvs.size(), snapshot.uavs.size(), _trackedResources.size());
    }

    // Skip dispatches with too few bound resources
    if (snapshot.srvs.size() < 2 || snapshot.uavs.empty())
        return false;

    auto result = s_detector.ScoreDispatch(snapshot);

    if (callNum < 15 || (callNum % 500 == 0) || result.score >= 0.3f)
    {
        LOG_INFO("[ORDO]   Score for #{}: {:.2f}", callNum, result.score);
    }

    if (result.score >= s_detector.GetConfidenceThreshold())
    {
        s_detector.TrackCandidate(snapshot, result, frame);
    }

    return false; // Phase 1: never suppress
}

void OnPresent()
{
    s_frameCounter.fetch_add(1, std::memory_order_relaxed);
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
    // 1. TAA interception is active on a confirmed pass
    if (IsActive())
        return true;

    // 2. An upscaler feature is currently active, initialized, and not frozen
    if (TAAConfig::Instance().forceUpscaling)
    {
        auto feat = State::Instance().currentFeature;
        if (feat != nullptr && feat->IsInited() && !feat->IsFrozen())
            return true;
    }

    return false;
}

} // namespace ordo::taa
