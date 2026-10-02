#pragma once

// --- [ORDO] TAA Intercept Configuration ---
// Reads settings from the [OrdoTAA] section of OptiScaler.ini.
// All settings default to disabled so the fork behaves identically
// to upstream unless explicitly configured.

#include <string>
#include <cstdint>
#include <SimpleIni.h>

namespace ordo::taa
{

/// Runtime configuration for the TAA interception system.
/// Loaded once at init from OptiScaler.ini [OrdoTAA] section.
///
/// Example INI:
/// ```ini
/// [OrdoTAA]
/// Enabled=true
/// LogDiscovery=true
/// ConfirmFrames=30
/// MinDispatchWidth=256
/// ```
struct TAAConfig
{
    // --- Master toggle ---
    bool enabled = true;

    // --- Discovery / logging ---
    bool logDiscovery = true;       // Log TAA candidate dispatches to OptiScaler.log
    bool logPerFrame  = false;      // Log every frame (very verbose, use for debugging)

    // --- Detection thresholds ---
    uint32_t confirmFrames    = 30;   // Consecutive frames a candidate must appear to confirm
    uint32_t minDispatchWidth = 256;  // Ignore dispatches smaller than this
    uint32_t minDispatchHeight = 128;

    // --- Motion vector heuristics ---
    // Expected DXGI_FORMAT for motion vectors (default: R16G16_FLOAT = 34)
    uint32_t motionVectorFormat = 34; // DXGI_FORMAT_R16G16_FLOAT

    // --- Profile override ---
    std::string profileName;  // If set, skip auto-detect and use this profile

    // --- Force Upscaling Override ---
    bool forceUpscaling = false;          // Manual override, default OFF to avoid startup crashes
    uint32_t qualityMode = 1;             // 0: Ultra Quality, 1: Quality, 2: Balanced, 3: Performance, 4: Ultra Performance, 5: Custom
    float customScaleRatio = 1.5f;        // Custom scale ratio

    static const char* QualityModeName(uint32_t mode);
    static float QualityModeRatio(uint32_t mode);

    // --- Stored INI path ---
    std::string iniFilePath;

    // --- Singleton access ---
    static TAAConfig& Instance();

    /// Load config from an INI file path.
    /// Reads only the [OrdoTAA] section; ignores everything else.
    ///
    /// ```cpp
    /// TAAConfig::Instance().LoadFromINI("D:\\Game\\OptiScaler.ini");
    /// ```
    void LoadFromINI(const std::string& iniPath);

    /// Load config directly from a CSimpleIniA instance.
    void LoadFromSimpleIni(CSimpleIniA& ini);

    /// Save current configuration directly to a CSimpleIniA instance.
    void SaveToSimpleIni(CSimpleIniA& ini);

    /// Save current configuration to an INI file under [OrdoTAA].
    /// If iniPath is empty, uses iniFilePath or OptiScaler's active INI path.
    ///
    /// ```cpp
    /// TAAConfig::Instance().SaveToINI();
    /// ```
    bool SaveToINI(const std::string& iniPath = "");
};

} // namespace ordo::taa
