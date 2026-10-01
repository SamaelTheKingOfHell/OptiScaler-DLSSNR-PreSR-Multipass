#include "pch.h"
#include "taa_profiles.h"
#include "taa_config.h"

#include <windows.h>
#include <algorithm>

namespace ordo::taa
{

static TAAProfiles s_instance;

TAAProfiles& TAAProfiles::Instance()
{
    return s_instance;
}

void TAAProfiles::LoadProfiles()
{
    _profiles.clear();

    // 1. Elden Ring (FromSoftware proprietary engine)
    TAAProfile eldenRing;
    eldenRing.profileId = "elden_ring";
    eldenRing.gameName = "ELDEN RING";
    eldenRing.exeName = "eldenring.exe";
    eldenRing.expectedMVFormat = DXGI_FORMAT_R16G16_FLOAT;
    eldenRing.expectedColorFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    eldenRing.expectedDepthFormat = DXGI_FORMAT_R32_FLOAT;
    eldenRing.invertedDepth = true;
    eldenRing.jitterInProjection = true;
    eldenRing.threadGroupSizeX = 8;
    eldenRing.threadGroupSizeY = 8;
    _profiles.push_back(eldenRing);

    // 2. Generic Unreal Engine 4 / 5
    TAAProfile ueGeneric;
    ueGeneric.profileId = "unreal_engine";
    ueGeneric.gameName = "Unreal Engine 4/5 Generic";
    ueGeneric.exeName = ""; // wildcard / fallback
    ueGeneric.expectedMVFormat = DXGI_FORMAT_R16G16_FLOAT;
    ueGeneric.expectedColorFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    ueGeneric.expectedDepthFormat = DXGI_FORMAT_R32_FLOAT;
    ueGeneric.invertedDepth = true;
    ueGeneric.jitterInProjection = true;
    ueGeneric.threadGroupSizeX = 8;
    ueGeneric.threadGroupSizeY = 8;
    _profiles.push_back(ueGeneric);

    LOG_INFO("[ORDO] Loaded {} built-in TAA profiles", _profiles.size());
}

bool TAAProfiles::AutoDetectGame()
{
    // If user explicitly configured a profile in [OrdoTAA] Profile=..., respect that
    const auto& customProfile = TAAConfig::Instance().profileName;
    if (!customProfile.empty())
    {
        if (SetActiveProfile(customProfile))
        {
            LOG_INFO("[ORDO] User override active: profile '{}' selected", customProfile);
            return true;
        }
    }

    // Determine current running executable
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return false;

    std::wstring wExePath(exePath);
    auto lastSlash = wExePath.find_last_of(L"\\/");
    std::wstring wFileName = (lastSlash != std::wstring::npos) ? wExePath.substr(lastSlash + 1) : wExePath;

    std::string currentExe;
    currentExe.resize(wFileName.length());
    std::transform(wFileName.begin(), wFileName.end(), currentExe.begin(), [](wchar_t c) {
        return static_cast<char>(std::tolower(c));
    });

    for (const auto& p : _profiles)
    {
        if (p.exeName.empty())
            continue;

        std::string targetExe = p.exeName;
        std::transform(targetExe.begin(), targetExe.end(), targetExe.begin(), [](char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (currentExe == targetExe)
        {
            _activeProfile = p;
            LOG_INFO("[ORDO] Auto-detected game '{}' (profile: '{}')", p.gameName, p.profileId);
            return true;
        }
    }

    LOG_DEBUG("[ORDO] No exact profile match for executable '{}'. Using dynamic heuristics.", currentExe);
    return false;
}

bool TAAProfiles::SetActiveProfile(const std::string& identifier)
{
    for (const auto& p : _profiles)
    {
        if (p.profileId == identifier || p.gameName == identifier)
        {
            _activeProfile = p;
            return true;
        }
    }
    return false;
}

std::optional<TAAProfile> TAAProfiles::GetActiveProfile() const
{
    return _activeProfile;
}

} // namespace ordo::taa
