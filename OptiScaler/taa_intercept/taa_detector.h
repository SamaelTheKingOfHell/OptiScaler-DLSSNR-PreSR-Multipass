#pragma once

// --- [ORDO] TAA Detection Heuristics Engine ---
// Analyzes D3D12 compute dispatches to identify TAA passes by scoring
// bound resource signatures against known TAA patterns.

#include <d3d12.h>
#include <cstdint>
#include <vector>
#include <mutex>

#include <hudfix/Hudfix_Dx12.h>  // For ResourceInfo

namespace ordo::taa
{

/// A single resource binding observed during a compute dispatch.
/// Extracted from the descriptor heap via OptiScaler's resource tracking.
///
/// ```cpp
/// BoundResource res;
/// res.resource = pD3D12Resource;
/// res.format   = DXGI_FORMAT_R16G16_FLOAT;
/// res.width    = 1920;
/// res.height   = 1080;
/// res.type     = ResourceType::SRV;
/// ```
struct BoundResource
{
    ID3D12Resource* resource = nullptr;
    DXGI_FORMAT format       = DXGI_FORMAT_UNKNOWN;
    uint64_t width           = 0;
    uint32_t height          = 0;
    ResourceType type        = ResourceType::SRV;
};

/// Snapshot of all resources bound during a single Dispatch call.
/// The interceptor populates this from the tracked root state.
///
/// ```cpp
/// DispatchSnapshot snap;
/// snap.threadGroupCountX = 240;  // (1920+7)/8
/// snap.threadGroupCountY = 135;  // (1080+7)/8
/// snap.srvs.push_back(colorSRV);
/// snap.uavs.push_back(outputUAV);
/// ```
struct DispatchSnapshot
{
    uint32_t threadGroupCountX = 0;
    uint32_t threadGroupCountY = 0;
    uint32_t threadGroupCountZ = 0;

    std::vector<BoundResource> srvs;
    std::vector<BoundResource> uavs;

    ID3D12PipelineState* pipelineState = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
};

/// Result of scoring a dispatch against TAA heuristics.
///
/// ```cpp
/// DetectionResult result = detector.ScoreDispatch(snapshot);
/// if (result.score >= detector.GetConfidenceThreshold()) {
///     LOG_INFO("[ORDO] TAA candidate found! Score: {}", result.score);
/// }
/// ```
struct DetectionResult
{
    float score = 0.0f;           // 0.0 = not TAA, 1.0 = definitely TAA

    int motionVectorIndex = -1;   // Index into srvs for the motion vector SRV
    int colorInputIndex   = -1;   // Index into srvs for current color
    int colorHistoryIndex = -1;   // Index into srvs for history color
    int depthIndex        = -1;   // Index into srvs for depth
    int outputIndex       = -1;   // Index into uavs for output

    uint32_t inferredWidth  = 0;
    uint32_t inferredHeight = 0;
};

/// Tracks a TAA candidate across frames to build confidence.
struct CandidateTracker
{
    ID3D12PipelineState* pipelineState = nullptr;
    uint64_t firstSeenFrame  = 0;
    uint64_t lastSeenFrame   = 0;
    uint32_t consecutiveHits = 0;
    float bestScore          = 0.0f;
    DetectionResult bestResult {};
    DispatchSnapshot bestSnapshot {};
    bool confirmed = false;
};

/// The TAA detection engine. Scores dispatches and tracks candidates
/// across frames until one reaches the confirmation threshold.
///
/// ```cpp
/// TAADetector detector;
/// auto result = detector.ScoreDispatch(snapshot);
/// detector.TrackCandidate(snapshot, result, frameIndex);
/// if (detector.HasConfirmedPass()) {
///     auto& pass = detector.GetConfirmedPass();
/// }
/// ```
class TAADetector
{
  public:
    TAADetector() = default;

    /// Score a single dispatch snapshot against TAA heuristics.
    /// Returns a DetectionResult with score and identified resource indices.
    ///
    /// ```cpp
    /// auto result = detector.ScoreDispatch(snapshot);
    /// // result.score > 0.7 = strong TAA candidate
    /// ```
    DetectionResult ScoreDispatch(const DispatchSnapshot& snapshot) const;

    /// Track a scored dispatch across frames. Call once per frame per candidate.
    /// When consecutiveHits reaches confirmFrames, the candidate is confirmed.
    ///
    /// ```cpp
    /// detector.TrackCandidate(snapshot, result, frameCounter);
    /// ```
    void TrackCandidate(const DispatchSnapshot& snapshot, const DetectionResult& result, uint64_t frameIndex);

    /// Check if we have a confirmed TAA pass.
    bool HasConfirmedPass() const;

    /// Get the number of candidate passes currently tracked.
    size_t GetCandidateCount() const;

    /// Get the confirmed TAA pass tracker. Only valid if HasConfirmedPass() is true.
    const CandidateTracker& GetConfirmedPass() const;

    /// Reset all tracking state. Used when re-detecting after a resolution change.
    void Reset();

    /// Confidence threshold for a dispatch to be considered a TAA candidate.
    float GetConfidenceThreshold() const { return 0.6f; }

  private:
    mutable std::mutex _mutex;
    std::vector<CandidateTracker> _candidates;
    int _confirmedIndex = -1;

    // --- Heuristic sub-scores ---
    float ScoreMotionVectors(const DispatchSnapshot& snapshot, DetectionResult& result) const;
    float ScoreColorBuffers(const DispatchSnapshot& snapshot, DetectionResult& result) const;
    float ScoreDepthBuffer(const DispatchSnapshot& snapshot, DetectionResult& result) const;
    float ScoreDispatchDimensions(const DispatchSnapshot& snapshot, DetectionResult& result) const;
    float ScoreResourceCount(const DispatchSnapshot& snapshot) const;
};

} // namespace ordo::taa
