# Fork Contribution Rules — The Order

These rules govern **all** code written by The Order in this fork. They exist
to keep our additions cleanly separable from upstream OptiScaler, making future
merges, rebases, and cherry-picks safe and predictable.

---

## 1. File-Level Isolation

| Rule | Detail |
| :--- | :--- |
| **New code → new files** | Every new feature, module, or subsystem we add MUST live in its own dedicated source files. Never append new features into existing upstream `.cpp`/`.h` files when a new file is viable. |
| **Dedicated directory** | Large features (e.g., TAA Interceptor) MUST get their own subdirectory under `OptiScaler/` (e.g., `OptiScaler/taa_intercept/`). |
| **Namespace prefix** | All Order-authored code MUST live under the `ordo::` C++ namespace (or a child like `ordo::taa`). Upstream code uses flat or anonymous namespaces — ours is always `ordo::`. |
| **File naming** | Order-authored files SHOULD use `snake_case` naming to visually distinguish from upstream's `PascalCase_WithUnderscores` convention. Example: `taa_detector.cpp` vs `D3D12_Hooks.cpp`. |

## 2. Minimal Upstream Edits

When we **must** modify an upstream file (e.g., to register a hook or call an
init function), the edit MUST follow these rules:

```cpp
// --- [ORDO] TAA Interceptor registration ---------------------------------
#include <taa_intercept/taa_interceptor.h>
ordo::taa::Initialize(device);
// --- [ORDO] END ----------------------------------------------------------
```

| Rule | Detail |
| :--- | :--- |
| **Bracketed comments** | Every insertion into upstream code MUST be wrapped in `// --- [ORDO] <description> ---` and `// --- [ORDO] END ---` markers. |
| **Minimal footprint** | The insertion should be a single function call or `#include`. All logic lives in our own files. |
| **No reformatting** | Do NOT reformat, re-style, or reorganize upstream code. Touch only the lines you must. |
| **No upstream deletions** | Do NOT delete or comment-out upstream logic unless replacing it with a runtime toggle. Prefer wrapping with an `if (!ordo::taa::IsActive())` guard so upstream behavior is preserved by default. |

## 3. Configuration Isolation

| Rule | Detail |
| :--- | :--- |
| **INI section** | All Order-specific settings MUST go under a dedicated `[Ordo]` or `[OrdoTAA]` section in `OptiScaler.ini`. Never add our keys to upstream sections. |
| **Config class** | Our settings use our own config struct (e.g., `ordo::Config`) that reads from the `[Ordo*]` INI sections. We may read upstream `Config::Instance()` for shared state (render resolution, device pointer) but never write to it. |
| **Defaults = off** | Every Order feature MUST default to **disabled**. The fork should behave identically to upstream until the user explicitly enables an `[Ordo]` setting. |

## 4. Build Isolation

| Rule | Detail |
| :--- | :--- |
| **Compile flag** | Order code MUST compile behind a preprocessor guard: `#ifdef ORDO_FEATURES_ENABLED`. The flag is defined in CMake/MSBuild so a clean upstream build (without the flag) compiles without our code. |
| **No new external deps** | Do not add third-party libraries without explicit approval. Reuse what upstream already vendors (`Detours`, `MinHook`, `ImGui`, `magic_enum`, `ankerl`). |
| **PCH compliance** | Follow upstream's PCH rules from `CONTRIBUTING.md` — `.cpp` files include `"pch.h"` first, `.h` files never include it. |

## 5. Logging & Debugging

| Rule | Detail |
| :--- | :--- |
| **Log prefix** | All Order log lines MUST use the `[ORDO]` prefix: `LOG_INFO("[ORDO] TAA pass detected: {}x{}", w, h);` |
| **Log level** | Discovery/detection messages use `LOG_INFO`. Per-frame operational messages use `LOG_DEBUG` or `LOG_TRACE`. Never spam `LOG_INFO` per-frame. |
| **Debug overlay** | Any debug visualization (e.g., highlighting the detected TAA pass) MUST be behind a toggle in the `[Ordo]` INI section, defaulting to off. |

## 6. Merge-Forward Workflow

When pulling upstream changes:

1. `git fetch upstream && git merge upstream/main` (or rebase if linear history is preferred).
2. Conflicts should only appear in the few upstream files where we inserted `[ORDO]` blocks.
3. Re-apply our bracketed insertions after resolving conflicts.
4. Run the build with and without `ORDO_FEATURES_ENABLED` to verify both paths.

---

> **Summary**: Our code lives in `ordo::` namespaced files under dedicated
> directories. Upstream files get only thin `[ORDO]`-bracketed call-sites.
> Everything defaults to off. This keeps the fork trivially mergeable.
