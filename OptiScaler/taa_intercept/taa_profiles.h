#pragma once

// --- [ORDO] Per-game TAA signature profiles ---
// Defines known characteristics of engine-specific TAA passes (motion vector
// formats, depth buffer inversion, constant buffer jitter layouts) to accelerate
// detection and provide tailored handling.

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <dxgiformat.h>

namespace ordo::taa
{

/// Profile describing a specific game or engine's TAA implementation.
///
/// ```cpp
/// TAAProfile profile;
/// profile.gameName = "ELDEN RING";
/// profile.exeName = "eldenring.exe";
/// profile.expectedMVFormat = DXGI_FORMAT_R16G16_FLOAT;
/// ```
struct TAAProfile
{
    std::string profileId;
    std::string gameName;
    std::string exeName;

    DXGI_FORMAT expectedMVFormat = DXGI_FORMAT_R16G16_FLOAT;
    DXGI_FORMAT expectedColorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT expectedDepthFormat = DXGI_FORMAT_UNKNOWN;

    bool invertedDepth = true;       // Modern engines almost universally use reverse-Z
    bool jitterInProjection = true;  // Halton jitter applied directly in projection matrix
    uint32_t threadGroupSizeX = 8;
    uint32_t threadGroupSizeY = 8;
};

/// Registry and auto-detection system for per-game TAA profiles.
///
/// ```cpp
/// auto& registry = TAAProfiles::Instance();
/// registry.LoadProfiles();
/// registry.AutoDetectGame();
/// if (auto p = registry.GetActiveProfile()) {
///     LOG_INFO("[ORDO] Active profile: {}", p->gameName);
/// }
/// ```
class TAAProfiles
{
  public:
    static TAAProfiles& Instance();

    /// Populate built-in profiles (Elden Ring / FromSoft engine, Unreal Engine 4/5 generic, etc.).
    void LoadProfiles();

    /// Detect game from the current process executable name and activate matching profile.
    bool AutoDetectGame();

    /// Manually activate a profile by profileId or gameName.
    bool SetActiveProfile(const std::string& identifier);

    /// Get current active profile if one is selected.
    std::optional<TAAProfile> GetActiveProfile() const;

  private:
    std::vector<TAAProfile> _profiles;
    std::optional<TAAProfile> _activeProfile;
};

} // namespace ordo::taa
