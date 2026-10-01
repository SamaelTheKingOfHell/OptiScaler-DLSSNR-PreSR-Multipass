#include "pch.h"
#include "taa_config.h"

#include <fstream>
#include <algorithm>

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
    }

    LOG_INFO("[ORDO] TAAConfig loaded: Enabled={}, LogDiscovery={}, ConfirmFrames={}, Profile='{}'",
             enabled, logDiscovery, confirmFrames, profileName);
}

} // namespace ordo::taa
