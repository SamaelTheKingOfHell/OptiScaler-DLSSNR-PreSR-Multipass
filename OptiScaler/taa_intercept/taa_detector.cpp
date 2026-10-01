#include "pch.h"
#include "taa_detector.h"
#include "taa_config.h"

#include <algorithm>
#include <cmath>

namespace ordo::taa
{

// --- Format classification helpers ---

/// Check if a DXGI format is a plausible motion vector format.
/// Motion vectors are typically R16G16_FLOAT or R16G16_SNORM.
///
/// ```cpp
/// bool isMV = IsMotionVectorFormat(DXGI_FORMAT_R16G16_FLOAT); // true
/// ```
static bool IsMotionVectorFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16_FLOAT:    // Most common
    case DXGI_FORMAT_R16G16_SNORM:    // Some engines
    case DXGI_FORMAT_R32G32_FLOAT:    // High precision (rare)
        return true;
    default:
        return false;
    }
}

/// Check if a DXGI format is a plausible HDR color buffer.
/// TAA operates on HDR linear color before tonemapping.
///
/// ```cpp
/// bool isColor = IsColorFormat(DXGI_FORMAT_R11G11B10_FLOAT); // true
/// ```
static bool IsColorFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

/// Check if a DXGI format is a depth or depth-stencil format.
///
/// ```cpp
/// bool isDepth = IsDepthFormat(DXGI_FORMAT_D32_FLOAT); // true
/// ```
static bool IsDepthFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT:        // Often used as depth SRV
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return true;
    default:
        return false;
    }
}

// --- Score sub-components ---

float TAADetector::ScoreMotionVectors(const DispatchSnapshot& snapshot, DetectionResult& result) const
{
    // Look for an SRV with a motion vector format
    auto expectedFormat = static_cast<DXGI_FORMAT>(TAAConfig::Instance().motionVectorFormat);

    for (int i = 0; i < static_cast<int>(snapshot.srvs.size()); i++)
    {
        const auto& srv = snapshot.srvs[i];

        // Exact format match gets highest score
        if (srv.format == expectedFormat)
        {
            result.motionVectorIndex = i;
            return 0.35f; // Strong signal
        }

        // Any motion vector format gets partial score
        if (IsMotionVectorFormat(srv.format))
        {
            if (result.motionVectorIndex < 0)
                result.motionVectorIndex = i;
            return 0.25f;
        }
    }

    return 0.0f; // No motion vectors = not TAA
}

float TAADetector::ScoreColorBuffers(const DispatchSnapshot& snapshot, DetectionResult& result) const
{
    // TAA needs at least 2 color-format SRVs (current + history) with matching dimensions
    std::vector<int> colorIndices;

    for (int i = 0; i < static_cast<int>(snapshot.srvs.size()); i++)
    {
        if (IsColorFormat(snapshot.srvs[i].format))
            colorIndices.push_back(i);
    }

    if (colorIndices.size() < 2)
        return 0.0f;

    // Look for a pair with matching dimensions (ping-pong pattern)
    for (size_t a = 0; a < colorIndices.size(); a++)
    {
        for (size_t b = a + 1; b < colorIndices.size(); b++)
        {
            const auto& srvA = snapshot.srvs[colorIndices[a]];
            const auto& srvB = snapshot.srvs[colorIndices[b]];

            if (srvA.width == srvB.width && srvA.height == srvB.height && srvA.format == srvB.format)
            {
                result.colorInputIndex = colorIndices[a];
                result.colorHistoryIndex = colorIndices[b];
                result.inferredWidth = static_cast<uint32_t>(srvA.width);
                result.inferredHeight = srvA.height;
                return 0.25f; // Matching ping-pong pair found
            }
        }
    }

    // Even without exact match, having 2+ color SRVs is a mild signal
    result.colorInputIndex = colorIndices[0];
    if (colorIndices.size() > 1)
        result.colorHistoryIndex = colorIndices[1];
    return 0.10f;
}

float TAADetector::ScoreDepthBuffer(const DispatchSnapshot& snapshot, DetectionResult& result) const
{
    for (int i = 0; i < static_cast<int>(snapshot.srvs.size()); i++)
    {
        if (IsDepthFormat(snapshot.srvs[i].format))
        {
            result.depthIndex = i;
            return 0.15f;
        }
    }
    return 0.0f;
}

float TAADetector::ScoreDispatchDimensions(const DispatchSnapshot& snapshot, DetectionResult& result) const
{
    const auto& config = TAAConfig::Instance();

    // TAA typically dispatches with 8x8 thread groups covering the full render resolution
    // So dispatch dimensions ~= (width+7)/8, (height+7)/8
    uint32_t inferredW = snapshot.threadGroupCountX * 8;
    uint32_t inferredH = snapshot.threadGroupCountY * 8;

    // Too small to be a meaningful TAA pass
    if (inferredW < config.minDispatchWidth || inferredH < config.minDispatchHeight)
        return 0.0f;

    // Z must be 1 for a 2D full-screen pass
    if (snapshot.threadGroupCountZ != 1)
        return 0.0f;

    // If we inferred dimensions from color buffers, check consistency
    if (result.inferredWidth > 0 && result.inferredHeight > 0)
    {
        // The dispatch should cover the render resolution (within 8-pixel rounding)
        int32_t diffW = static_cast<int32_t>(inferredW) - static_cast<int32_t>(result.inferredWidth);
        int32_t diffH = static_cast<int32_t>(inferredH) - static_cast<int32_t>(result.inferredHeight);

        if (std::abs(diffW) <= 8 && std::abs(diffH) <= 8)
            return 0.20f; // Dimensions match the color buffers
    }
    else
    {
        // No color buffer reference, but reasonable full-screen dispatch
        result.inferredWidth = inferredW;
        result.inferredHeight = inferredH;
    }

    return 0.10f; // Reasonable dimensions but no buffer correlation
}

float TAADetector::ScoreResourceCount(const DispatchSnapshot& snapshot) const
{
    // TAA typically binds 3-6 SRVs and 1-2 UAVs
    auto srvCount = snapshot.srvs.size();
    auto uavCount = snapshot.uavs.size();

    if (srvCount >= 3 && srvCount <= 8 && uavCount >= 1 && uavCount <= 3)
        return 0.05f;

    return 0.0f;
}

// --- Main scoring ---

DetectionResult TAADetector::ScoreDispatch(const DispatchSnapshot& snapshot) const
{
    DetectionResult result {};

    // Accumulate sub-scores (weights sum to ~1.0)
    result.score += ScoreMotionVectors(snapshot, result);
    result.score += ScoreColorBuffers(snapshot, result);
    result.score += ScoreDepthBuffer(snapshot, result);
    result.score += ScoreDispatchDimensions(snapshot, result);
    result.score += ScoreResourceCount(snapshot);

    // Find the output UAV (first UAV with color format matching render dims)
    for (int i = 0; i < static_cast<int>(snapshot.uavs.size()); i++)
    {
        const auto& uav = snapshot.uavs[i];
        if (IsColorFormat(uav.format))
        {
            result.outputIndex = i;
            break;
        }
    }

    return result;
}

// --- Candidate tracking ---

void TAADetector::TrackCandidate(const DispatchSnapshot& snapshot, const DetectionResult& result, uint64_t frameIndex)
{
    if (result.score < GetConfidenceThreshold())
        return;

    std::lock_guard<std::mutex> lock(_mutex);

    // Look for an existing candidate with the same pipeline state
    for (auto& candidate : _candidates)
    {
        if (candidate.pipelineState == snapshot.pipelineState)
        {
            if (frameIndex == candidate.lastSeenFrame + 1)
            {
                candidate.consecutiveHits++;
            }
            else if (frameIndex > candidate.lastSeenFrame + 3)
            {
                // Gap too large, reset counter but keep tracking
                candidate.consecutiveHits = 1;
            }

            candidate.lastSeenFrame = frameIndex;

            if (result.score > candidate.bestScore)
            {
                candidate.bestScore = result.score;
                candidate.bestResult = result;
                candidate.bestSnapshot = snapshot;
            }

            // Check confirmation threshold
            if (!candidate.confirmed &&
                candidate.consecutiveHits >= TAAConfig::Instance().confirmFrames)
            {
                candidate.confirmed = true;
                _confirmedIndex = static_cast<int>(&candidate - _candidates.data());

                LOG_INFO("[ORDO] TAA pass CONFIRMED! Pipeline: {:X}, Score: {:.2f}, "
                         "Resolution: {}x{}, ConsecutiveFrames: {}",
                         reinterpret_cast<uintptr_t>(candidate.pipelineState),
                         candidate.bestScore,
                         candidate.bestResult.inferredWidth,
                         candidate.bestResult.inferredHeight,
                         candidate.consecutiveHits);
            }
            return;
        }
    }

    // New candidate
    CandidateTracker newCandidate {};
    newCandidate.pipelineState = snapshot.pipelineState;
    newCandidate.firstSeenFrame = frameIndex;
    newCandidate.lastSeenFrame = frameIndex;
    newCandidate.consecutiveHits = 1;
    newCandidate.bestScore = result.score;
    newCandidate.bestResult = result;
    newCandidate.bestSnapshot = snapshot;

    if (TAAConfig::Instance().logDiscovery)
    {
        LOG_INFO("[ORDO] New TAA candidate: Pipeline={:X}, Score={:.2f}, "
                 "SRVs={}, UAVs={}, Dispatch={}x{}x{}, InferredRes={}x{}",
                 reinterpret_cast<uintptr_t>(snapshot.pipelineState),
                 result.score,
                 snapshot.srvs.size(), snapshot.uavs.size(),
                 snapshot.threadGroupCountX, snapshot.threadGroupCountY, snapshot.threadGroupCountZ,
                 result.inferredWidth, result.inferredHeight);

        // Log resource details
        for (size_t i = 0; i < snapshot.srvs.size(); i++)
        {
            const auto& srv = snapshot.srvs[i];
            const char* role = "";
            if (static_cast<int>(i) == result.motionVectorIndex) role = " [MOTION_VECTORS]";
            else if (static_cast<int>(i) == result.colorInputIndex) role = " [COLOR_INPUT]";
            else if (static_cast<int>(i) == result.colorHistoryIndex) role = " [COLOR_HISTORY]";
            else if (static_cast<int>(i) == result.depthIndex) role = " [DEPTH]";

            LOG_INFO("[ORDO]   SRV[{}]: {}x{} fmt={}{}", i, srv.width, srv.height,
                     static_cast<uint32_t>(srv.format), role);
        }

        for (size_t i = 0; i < snapshot.uavs.size(); i++)
        {
            const auto& uav = snapshot.uavs[i];
            const char* role = (static_cast<int>(i) == result.outputIndex) ? " [OUTPUT]" : "";
            LOG_INFO("[ORDO]   UAV[{}]: {}x{} fmt={}{}", i, uav.width, uav.height,
                     static_cast<uint32_t>(uav.format), role);
        }
    }

    _candidates.push_back(std::move(newCandidate));
}

bool TAADetector::HasConfirmedPass() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _confirmedIndex >= 0 && _confirmedIndex < static_cast<int>(_candidates.size());
}

const CandidateTracker& TAADetector::GetConfirmedPass() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _candidates[_confirmedIndex];
}

void TAADetector::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _candidates.clear();
    _confirmedIndex = -1;
    LOG_INFO("[ORDO] TAA detector reset");
}

} // namespace ordo::taa
