#pragma once

// --- [ORDO] TAA Interceptor and Force Upscaling Menu Header ---
// Provides the ImGui UI section for manual force upscaling override,
// quality mode selection, and TAA intercept status.

#include <Config.h>

namespace ordo::taa
{

/// Renders the ORDO Force Upscaling and TAA Interception menu section in the ImGui overlay.
///
/// ```cpp
/// ordo::taa::RenderMenu(config, menuResScale);
/// ```
void RenderMenu(Config* config, float menuResScale);

} // namespace ordo::taa
