#include "pch.h"
#include "taa_menu.h"
#include "taa_config.h"
#include "taa_interceptor.h"
#include "taa_detector.h"

#include <Config.h>
#include <State.h>
#include <imgui/imgui.h>
#include "Logger.h"

namespace ordo::taa
{

static void HelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void SaveAllSettings(Config* config)
{
    TAAConfig::Instance().SaveToINI();
    if (config != nullptr)
        config->SaveIni();
    else if (Config::Instance() != nullptr)
        Config::Instance()->SaveIni();
}

void RenderMenu(Config* config, float menuResScale)
{
    auto& taaCfg = TAAConfig::Instance();
    static double s_lastSaveNotificationTime = -10.0;

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("ORDO - Force Upscaling & TAA Interceptor", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(16.0f);
        ImGui::Spacing();

        // -------------------------------------------------------------
        // 1. Force Upscaling Override Toggle (Default OFF for safe start)
        // -------------------------------------------------------------
        bool forceUpscaling = taaCfg.forceUpscaling;
        if (ImGui::Checkbox("Force Upscaling Override", &forceUpscaling))
        {
            taaCfg.forceUpscaling = forceUpscaling;
            SaveAllSettings(config);
            LOG_INFO("[ORDO] Force Upscaling Override toggled: {} and saved to INI", forceUpscaling ? "ON" : "OFF");
        }
        HelpMarker("Manually injects the selected upscaler backend into the game's render pipeline.\n"
                   "Kept OFF by default to avoid crashes on game launch.\n"
                   "Toggle ON once in-game to activate upscaler injection.");

        // Verification: Only green if confirmed working!
        const bool confirmedWorking = IsConfirmedWorking();

        if (forceUpscaling && confirmedWorking)
        {
            // Confirmed working -> GREEN
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("  [CONFIRMED WORKING] Upscaler injected and active");
            ImGui::PopStyleColor();
        }
        else if (forceUpscaling && !confirmedWorking)
        {
            // Armed but not confirmed working yet -> AMBER / YELLOW
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
            if (HasConfirmedPass())
                ImGui::Text("  [ARMED / TAA CONFIRMED] Pass confirmed - injecting upscaler...");
            else
                ImGui::Text("  [ARMED / SEARCHING] Override enabled - searching for TAA pass...");
            ImGui::PopStyleColor();

            size_t candCount = GetCandidateCount();
            ImGui::TextDisabled("  Candidates tracked: %zu | Pass confirmed: %s",
                                candCount, HasConfirmedPass() ? "Yes" : "No");
        }
        else
        {
            // Inactive safe mode -> GRAY
            ImGui::TextDisabled("  [STANDBY] Override inactive (safe mode, no injection)");
            if (HasConfirmedPass())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 1.0f, 1.0f));
                ImGui::Text("  [PASS CONFIRMED] TAA pass confirmed - toggle override ON to activate");
                ImGui::PopStyleColor();
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Quality Preset");

        // -------------------------------------------------------------
        // 2. Quality Mode Selector
        // -------------------------------------------------------------
        const char* qualityNames[] = {
            "Ultra Quality (1.30x)",
            "Quality (1.50x)",
            "Balanced (1.70x)",
            "Performance (2.00x)",
            "Ultra Performance (3.00x)",
            "Custom Ratio"
        };
        int currentQuality = static_cast<int>(taaCfg.qualityMode);
        if (currentQuality < 0 || currentQuality >= 6)
            currentQuality = 1;

        ImGui::PushItemWidth(210.0f * menuResScale);
        if (ImGui::Combo("Quality Mode", &currentQuality, qualityNames, IM_ARRAYSIZE(qualityNames)))
        {
            taaCfg.qualityMode = static_cast<uint32_t>(currentQuality);
            SaveAllSettings(config);
            LOG_INFO("[ORDO] Quality mode changed to: {} (ratio: {:.2f}x) and saved to INI",
                     qualityNames[currentQuality], TAAConfig::QualityModeRatio(taaCfg.qualityMode));
        }
        ImGui::PopItemWidth();
        HelpMarker("Sets the internal render scale relative to display resolution.\n"
                   "Quality (1.50x) renders at ~67% resolution and upscales to native.");

        if (taaCfg.qualityMode == 5) // Custom ratio
        {
            ImGui::PushItemWidth(210.0f * menuResScale);
            float customRatio = taaCfg.customScaleRatio;
            if (ImGui::SliderFloat("Scale Ratio", &customRatio, 1.0f, 4.0f, "%.2fx"))
            {
                taaCfg.customScaleRatio = customRatio;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                SaveAllSettings(config);
            }
            ImGui::PopItemWidth();
            HelpMarker("Custom scale ratio multiplier (e.g. 1.50 = 1.50x scaling)");
        }

        ImGui::Spacing();
        ImGui::SeparatorText("TAA Interception & Engine Status");

        // -------------------------------------------------------------
        // 3. TAA Interception Controls
        // -------------------------------------------------------------
        bool interceptorEnabled = taaCfg.enabled;
        if (ImGui::Checkbox("Enable TAA Interceptor", &interceptorEnabled))
        {
            taaCfg.enabled = interceptorEnabled;
            SaveAllSettings(config);
            LOG_INFO("[ORDO] TAA Interceptor enabled toggled: {} and saved to INI", interceptorEnabled);
        }
        HelpMarker("Monitors D3D12 Draw and Dispatch passes to identify and intercept the game's TAA pass.");

        std::string profileDisplay = taaCfg.profileName.empty() ? "Auto-detect (Elden Ring)" : taaCfg.profileName;
        ImGui::Text("Active Profile: %s", profileDisplay.c_str());

        bool logDiscovery = taaCfg.logDiscovery;
        if (ImGui::Checkbox("Log Discovery Telemetry", &logDiscovery))
        {
            taaCfg.logDiscovery = logDiscovery;
            SaveAllSettings(config);
        }
        HelpMarker("Outputs candidate heuristic scores and resource details to OptiScaler.log for debugging.");

        // -------------------------------------------------------------
        // 4. Persistence Controls
        // -------------------------------------------------------------
        ImGui::Spacing();
        if (ImGui::Button("Save ORDO Settings"))
        {
            SaveAllSettings(config);
            s_lastSaveNotificationTime = ImGui::GetTime();
        }
        HelpMarker("Saves all ORDO overrides and quality options to OptiScaler.ini immediately.");

        if (ImGui::GetTime() - s_lastSaveNotificationTime < 3.0)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Settings saved to OptiScaler.ini!");
        }

        ImGui::Spacing();
        ImGui::Unindent(16.0f);
    }
}

} // namespace ordo::taa
