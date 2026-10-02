#include "pch.h"
#include "taa_config.h"

#include <fstream>
#include <algorithm>
#include <SimpleIni.h>
#include <Config.h>
#include "Util.h"

namespace ordo::taa
{

// Singleton instance
static TAAConfig s_config;

TAAConfig& TAAConfig::Instance()
{
    return s_config;
}

/// Trim leading/trailing whitespace from a string.
/// ```cpp
/// auto trimmed = Trim("  hello  "); // "hello"
/// ```
static std::string Trim(const std::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// Case-insensitive string comparison.
/// ```cpp
/// bool eq = IEquals("True", "true"); // true
/// ```
static bool IEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](char ca, char cb) { return std::tolower(ca) == std::tolower(cb); });
}

/// Parse a boolean from common INI string values.
/// Accepts "true", "1", "yes" as true; everything else is false.
/// ```cpp
/// bool val = ParseBool("true"); // true
/// ```
static bool ParseBool(const std::string& val)
{
    auto v = Trim(val);
    return IEquals(v, "true") || v == "1" || IEquals(v, "yes");
}

/// Parse an unsigned integer from a string, returning a default on failure.
/// ```cpp
/// uint32_t n = ParseUint("128", 64); // 128
/// ```
static uint32_t ParseUint(const std::string& val, uint32_t defaultVal)
{
    try
    {
        return static_cast<uint32_t>(std::stoul(Trim(val)));
    }
    catch (...)
    {
        return defaultVal;
    }
}

void TAAConfig::LoadFromINI(const std::string& iniPath)
{
    iniFilePath = iniPath;
    std::ifstream file(iniPath);
    if (!file.is_open())
    {
        LOG_INFO("[ORDO] TAAConfig: Could not open INI file: {}", iniPath);
        return;
    }

    bool inOrdoSection = false;
    std::string line;

    while (std::getline(file, line))
    {
        auto trimmed = Trim(line);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
            continue;

        // Section header
        if (trimmed[0] == '[')
        {
            auto closing = trimmed.find(']');
            if (closing != std::string::npos)
            {
                auto section = trimmed.substr(1, closing - 1);
                inOrdoSection = IEquals(section, "OrdoTAA");
            }
            continue;
        }

        if (!inOrdoSection)
            continue;

        // Key=Value parsing
        auto eq = trimmed.find('=');
        if (eq == std::string::npos)
            continue;

        auto key = Trim(trimmed.substr(0, eq));
        auto value = Trim(trimmed.substr(eq + 1));

        if (IEquals(key, "Enabled"))
            enabled = ParseBool(value);
        else if (IEquals(key, "LogDiscovery"))
            logDiscovery = ParseBool(value);
        else if (IEquals(key, "LogPerFrame"))
            logPerFrame = ParseBool(value);
        else if (IEquals(key, "ConfirmFrames"))
            confirmFrames = ParseUint(value, confirmFrames);
        else if (IEquals(key, "MinDispatchWidth"))
            minDispatchWidth = ParseUint(value, minDispatchWidth);
        else if (IEquals(key, "MinDispatchHeight"))
            minDispatchHeight = ParseUint(value, minDispatchHeight);
        else if (IEquals(key, "MotionVectorFormat"))
            motionVectorFormat = ParseUint(value, motionVectorFormat);
        else if (IEquals(key, "Profile"))
            profileName = value;
        else if (IEquals(key, "ForceUpscaling"))
            forceUpscaling = ParseBool(value);
        else if (IEquals(key, "QualityMode"))
            qualityMode = ParseUint(value, qualityMode);
        else if (IEquals(key, "CustomScaleRatio"))
        {
            try { customScaleRatio = std::stof(value); } catch (...) {}
        }
    }

    LOG_INFO("[ORDO] TAAConfig loaded: Enabled={}, ForceUpscaling={}, QualityMode={}({}), ConfirmFrames={}, Profile='{}'",
             enabled, forceUpscaling, qualityMode, QualityModeName(qualityMode), confirmFrames, profileName);
}

const char* TAAConfig::QualityModeName(uint32_t mode)
{
    switch (mode)
    {
    case 0: return "Ultra Quality (1.3x)";
    case 1: return "Quality (1.5x)";
    case 2: return "Balanced (1.7x)";
    case 3: return "Performance (2.0x)";
    case 4: return "Ultra Performance (3.0x)";
    case 5: return "Custom";
    default: return "Quality (1.5x)";
    }
}

float TAAConfig::QualityModeRatio(uint32_t mode)
{
    switch (mode)
    {
    case 0: return 1.30f;
    case 1: return 1.50f;
    case 2: return 1.70f;
    case 3: return 2.00f;
    case 4: return 3.00f;
    default: return 1.50f;
    }
}

bool TAAConfig::SaveToINI(const std::string& iniPath)
{
    std::string targetPath = iniPath.empty() ? iniFilePath : iniPath;
    if (targetPath.empty())
    {
        if (Config::Instance() != nullptr)
        {
            auto p = Config::Instance()->AbsoluteFileName();
            if (!p.empty())
                targetPath = p.string();
        }
    }

    if (targetPath.empty())
    {
        targetPath = (Util::DllPath().parent_path() / L"OptiScaler.ini").string();
    }

    if (targetPath.empty())
    {
        LOG_WARN("[ORDO] TAAConfig::SaveToINI: No INI file path available");
        return false;
    }

    CSimpleIniA ini;
    ini.SetUnicode();
    // Load existing INI file so all comments and existing sections are preserved
    if (ini.LoadFile(targetPath.c_str()) < 0)
    {
        LOG_WARN("[ORDO] TAAConfig::SaveToINI: Could not load INI file at {}", targetPath);
        return false;
    }

    ini.SetBoolValue("OrdoTAA", "Enabled", enabled);
    ini.SetBoolValue("OrdoTAA", "ForceUpscaling", forceUpscaling);
    ini.SetLongValue("OrdoTAA", "QualityMode", static_cast<long>(qualityMode));
    ini.SetDoubleValue("OrdoTAA", "CustomScaleRatio", static_cast<double>(customScaleRatio));
    ini.SetBoolValue("OrdoTAA", "LogDiscovery", logDiscovery);
    ini.SetBoolValue("OrdoTAA", "LogPerFrame", logPerFrame);
    ini.SetLongValue("OrdoTAA", "ConfirmFrames", static_cast<long>(confirmFrames));
    ini.SetLongValue("OrdoTAA", "MinDispatchWidth", static_cast<long>(minDispatchWidth));
    ini.SetLongValue("OrdoTAA", "MinDispatchHeight", static_cast<long>(minDispatchHeight));
    ini.SetLongValue("OrdoTAA", "MotionVectorFormat", static_cast<long>(motionVectorFormat));
    if (!profileName.empty())
        ini.SetValue("OrdoTAA", "Profile", profileName.c_str());

    if (ini.SaveFile(targetPath.c_str()) >= 0)
    {
        LOG_INFO("[ORDO] TAAConfig::SaveToINI: Successfully saved [OrdoTAA] to {}", targetPath);
        return true;
    }

    LOG_ERROR("[ORDO] TAAConfig::SaveToINI: Failed saving [OrdoTAA] to {}", targetPath);
    return false;
}

} // namespace ordo::taa
