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
    // Walk OptiScaler's tracked heaps to find the resource at this GPU handle
    auto& heapMap = ResTrack_Dx12::GetHeapMap();
    auto lock = ResTrack_Dx12::LockHeapMapShared();

    for (const auto& [heapPtr, heapInfo] : heapMap)
    {
        if (!heapInfo || !heapInfo->active.load(std::memory_order_acquire))
            continue;

        if (heapInfo->GetByGpuHandle(gpuHandle.ptr, outInfo))
            return true;
    }

    return false;
}

/// Build a DispatchSnapshot from the current command list root state.
/// Uses OptiScaler's tracked descriptor heaps and root signature bindings.
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

    // We can't directly enumerate all bound descriptors without tracking
    // every SetComputeRootDescriptorTable call. For Phase 1 (discovery),
    // we use a simpler approach: scan the tracked heaps for resources that
    // were recently used (within this frame) and match render-resolution
    // dimensions.
    //
    // This is a heuristic discovery pass — it casts a wide net.
    // Phase 2 will refine this with exact root signature tracking.

    const uint64_t currentFrame = s_frameCounter.load(std::memory_order_relaxed);

    auto& heapMap = ResTrack_Dx12::GetHeapMap();
    auto lock = ResTrack_Dx12::LockHeapMapShared();

    for (const auto& [heapPtr, heapInfo] : heapMap)
    {
        if (!heapInfo || !heapInfo->active.load(std::memory_order_acquire))
            continue;

        // Only scan CBV_SRV_UAV heaps (type 0)
        if (heapInfo->type != 0)
            continue;

        for (UINT i = 0; i < heapInfo->numDescriptors; i++)
        {
            const auto& info = heapInfo->info[i];
            if (info.buffer == nullptr)
                continue;

            // Only look at resources used in the current frame
            if (info.lastUsedFrame == 0 || 
                static_cast<uint64_t>(info.lastUsedFrame) < currentFrame - 1)
                continue;

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
    // Always call the original first in Phase 1 (discovery mode).
    // We only observe and log — we don't suppress yet.
    // Phase 2 will add suppression for confirmed passes.

    bool intercepted = false;

    if (s_initialized.load(std::memory_order_acquire) &&
        TAAConfig::Instance().enabled)
    {
        intercepted = OnDispatch(cmdList, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    }

    // In Phase 1, always call original regardless of interception result
    // Phase 2 will skip the original when intercepted = true
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
    // OptiScaler sets its DLL path which we can derive the INI location from
    auto& state = State::Instance();
    std::string iniPath;

    // Try to find OptiScaler.ini next to the game exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring wExePath(exePath);
    auto lastSlash = wExePath.find_last_of(L'\\');
    if (lastSlash != std::wstring::npos)
    {
        std::wstring wDir = wExePath.substr(0, lastSlash + 1);
        std::wstring wIniPath = wDir + L"OptiScaler.ini";

        // Convert to narrow string for our parser
        int size = WideCharToMultiByte(CP_UTF8, 0, wIniPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0)
        {
            iniPath.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, wIniPath.c_str(), -1, iniPath.data(), size, nullptr, nullptr);
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

    // If we already have a confirmed pass, check if this is the same pipeline
    if (s_detector.HasConfirmedPass())
    {
        // Phase 1: Just log that we see it. Phase 2 will suppress + extract.
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

    // Skip dispatches with too few bound resources
    if (snapshot.srvs.size() < 2 || snapshot.uavs.empty())
        return false;

    auto result = s_detector.ScoreDispatch(snapshot);

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

} // namespace ordo::taa
