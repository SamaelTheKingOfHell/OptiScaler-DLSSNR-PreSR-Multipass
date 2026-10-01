# OptiScaler Neural Rendering (PreSR & Multipass)

Fork of OptiScaler integrating NVIDIA Neural Rendering (`nvngx_dlssnr.dll`), Pre-SR neural passes, and multi-pass AI lighting/detail enhancement for DirectX 12 games.

## Repository Overview

| Path / File | Purpose |
| :--- | :--- |
| `INDEX.md` | Index of repository structure and integration notes. |
| `FORK_RULES.md` | **[Order]** Isolation rules for fork contributions — namespacing, file layout, upstream edit conventions, merge-forward workflow. |
| `OBJECTIVE.md` | **[Order]** Fork mission: general-purpose TAA interception for DLSS injection. |
| `ROADMAP.md` | **[Order]** Phased roadmap from TAA discovery through SR injection and per-game profiles. |
| `README.md` | Upstream documentation and overview of Neural Rendering features. |
| `CONTRIBUTING.md` | Upstream PCH and build rules. |
| `INSTALL-DLSSNR.md` | Runtime requirements, GPU compatibility matrix (RTX 20/30/40/50), and installation steps. |
| `OptiScaler.sln` | Visual Studio C++ solution for building OptiScaler and backend wrappers. |
| `OptiScaler/` | Core C++ codebase for hooks, renderers, upscaler backends, and DLSS-NR pipeline. |
| `external/` | External third-party libraries (Detours, MinHook, ImGui, NVSDK headers). |
| `OptiScaler.ini` | Master runtime configuration file. |
| `setup_windows.bat` | Automated proxy installer (deploys `dxgi.dll` or `dbghelp.dll`). |
