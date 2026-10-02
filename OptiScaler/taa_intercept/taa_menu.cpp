#include "pch.h"
#include "taa_menu.h"
#include "taa_config.h"
#include "taa_interceptor.h"
#include "taa_detector.h"

#include <Config.h>
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

void RenderMenu(Config* config, float menuResScale)
{
    auto& taaCfg = TAAConfig::Instance();

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
            LOG_INFO("[ORDO] Force Upscaling Override toggled: {}", forceUpscaling ? "ON" : "OFF");
        }
        HelpMarker("Manually injects the selected upscaler backend into the game's render pipeline.\n"
                   "Kept OFF by default to avoid crashes on game launch.\n"
                   "Toggle ON once in-game to activate upscaler injection.");

        if (forceUpscaling)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("  [ARMED] Force upscaling override active");
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled("  [STANDBY] Override inactive (safe mode, no injection)");
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
            LOG_INFO("[ORDO] Quality mode changed to: {} (ratio: {:.2f}x)",
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
            LOG_INFO("[ORDO] TAA Interceptor enabled toggled: {}", interceptorEnabled);
        }
        HelpMarker("Monitors D3D12 Draw and Dispatch passes to identify and intercept the game's TAA pass.");

        std::string profileDisplay = taaCfg.profileName.empty() ? "Auto-detect (Elden Ring)" : taaCfg.profileName;
        ImGui::Text("Active Profile: %s", profileDisplay.c_str());

        bool logDiscovery = taaCfg.logDiscovery;
        if (ImGui::Checkbox("Log Discovery Telemetry", &logDiscovery))
        {
            taaCfg.logDiscovery = logDiscovery;
        }
        HelpMarker("Outputs candidate heuristic scores and resource details to OptiScaler.log for debugging.");

        ImGui::Spacing();
        ImGui::Unindent(16.0f);
    }
}

} // namespace ordo::taa
