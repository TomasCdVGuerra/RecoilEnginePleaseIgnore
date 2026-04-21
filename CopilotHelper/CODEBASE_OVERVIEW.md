# RecoilEngine Codebase Overview

This document is a source-of-truth overview of repository structure, build and CI behavior, and day-to-day development constraints. It is aligned with current files in this workspace and with the priorities described in `copilot-context.md`.

## 1) Project Mission And Core Stack

RecoilEngine is an open-source real-time strategy engine, maintained as a continuation of the Spring RTS 105.x lineage. The repository includes the core simulation engine, rendering and UI systems, tooling, game content packaging, and testing infrastructure.

Core technical stack:

- Language: C++ (configured in `CMakeLists.txt` with `CMAKE_CXX_STANDARD 23` in current tree).
- Build system: CMake (minimum 3.27).
- Primary engine source root: `rts/`.
- Tooling and packaging: `tools/`, `installer/`, `docker-build-v2/`.
- Tests: CTest + Catch2-based unit tests and smoke tests under `test/`.
- CI: GitHub Actions workflows (notably `.github/workflows/macos.yml`).

Current platform priorities reflected in code and CI:

- macOS support is actively improved (headless path, portability checks, sanitizers).
- Cross-platform platform layer is split into Linux/macOS/Windows implementations under `rts/System/Platform/`.
- Headless builds are first-class and can be configured independently of legacy graphical builds.

## 2) Repository Map

Top-level directory responsibilities:

- `rts/`: Core engine code and build variants.
- `AI/`: AI interfaces, wrappers, and skirmish AI modules.
- `cont/`: Base content, Lua UI scripts, cursors, fonts, freedesktop assets.
- `tools/`: Engine-adjacent utilities (unitsync, pr-downloader, demo/tooling helpers).
- `test/`: Unit tests, smoke tests, validation suites, and test tooling.
- `doc/`: Engine docs, changelog, key format references, and website content.
- `installer/`: Installer scripts and packaging assets.
- `docker-build-v2/`: Containerized build scripts and images.
- `rts/lib/`: Third-party and vendored dependencies used by engine targets.
- `build/`: Generated build output (not source of truth).

Important build graph anchors:

- Root `CMakeLists.txt` orchestrates the global configuration, dependency discovery, and subdirectory order.
- `rts/CMakeLists.txt` assembles engine sources and shared libraries.
- `rts/builds/` defines product variants (`legacy`, `dedicated`, `headless`).
- `test/CMakeLists.txt` defines the CTest integration and per-target test binaries.

## 3) Rendering Architecture

RecoilEngine rendering is now organized around a backend abstraction layer in `rts/Rendering/Gfx/`, replacing the prior monolithic OpenGL-only execution model.

### Gfx abstraction (`rts/Rendering/Gfx/`)

- `rts/Rendering/Gfx/IGraphicsBackend.h` defines the backend contract used by engine rendering systems.
- Resource creation and frame lifecycle operations flow through `IGraphicsBackend` (buffers, textures, framebuffers, shaders/programs, frame begin/end, and presentation).
- Backend implementations are provided by:
  - `rts/Rendering/Gfx/GL/GLGraphicsBackend.h`
  - `rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h`

### Vulkan backend

The Vulkan path is implemented under `rts/Rendering/Gfx/Vulkan/`, with `VulkanGraphicsBackend` coordinating Vulkan initialization and runtime execution.

Key supporting classes include:

- `rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.h`
- `rts/Rendering/Gfx/Vulkan/VulkanTexture.h`
- `rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.h`

These classes provide backend-specific implementations for the shared `gfx` interfaces while preserving backend-neutral call sites.

### Migration state (GL + Vulkan)

- The engine currently supports both OpenGL and Vulkan backends.
- Backend compilation/selection is controlled by the `ENABLE_VULKAN` CMake option in `CMakeLists.txt`.
- On macOS, Vulkan is the preferred path when enabled, using MoltenVK.

### Initialization flow (`rts/Rendering/GlobalRendering.cpp`)

- During window/context creation, the SDL window flag is selected conditionally:
  - `SDL_WINDOW_VULKAN` when `ENABLE_VULKAN` is active.
  - `SDL_WINDOW_OPENGL` otherwise.
- Backend selection is conditional in the same flow:
  - Vulkan builds instantiate `VulkanGraphicsBackend` (with the SDL window handle for surface/swapchain setup).
  - Non-Vulkan builds instantiate `GLGraphicsBackend`.
- Presentation follows the selected backend path (Vulkan swapchain presentation vs OpenGL swap-window flow).

### Vulkan UI Bridge And Startup Stabilization (macOS/MoltenVK)

The Vulkan startup and menu/UI path now includes explicit containment and safety mechanisms to avoid legacy OpenGL coupling and MoltenVK driver faults.

#### Deferred UI queue hardening

- Deferred UI draws in `VulkanGraphicsBackend` no longer keep raw texture pointers for later submission.
- The deferred path now resolves descriptor image data immediately and stores `VkDescriptorImageInfo` by value, removing the use-after-free risk that previously caused SIGSEGV crashes.
- Descriptor image data is validated again before submission, and fallback descriptor recovery is applied when needed to keep descriptor bindings valid at `vkQueueSubmit` time.

#### Legacy OpenGL gating (containment)

- Legacy startup systems that assume OpenGL are now gated behind `if (HasOpenGLBackend())` so Vulkan startup does not execute GL-only code paths.
- Gated components include:
  - SelMenuVFS archive background scanning in `SelectMenu.cpp`.
  - LuaVFSDownload initialization in `SpringApp.cpp`.

#### agui primitive bridge and white fallback

- `agui` solid-color primitives (for example `GuiElement::DrawBox`) are routed through the Vulkan textured batch bridge via `DrawTexturedIndexedBatches`.
- A UI white-pixel fallback texture is created and used when needed so descriptor submission always has a valid texture.
- This fallback behavior prevents MoltenVK device-loss failures (`VK_ERROR_DEVICE_LOST`, `-4`) caused by invalid or missing texture descriptors.

#### Apple Silicon / MoltenVK strictness

- MoltenVK on macOS is strict about resource-state correctness and draw parameter consistency.
- Index buffer element types must exactly match the bound Vulkan index type.
- 32-bit index streams must be bound as `VK_INDEX_TYPE_UINT32`; mismatches can trigger `_ioGPUResourceListAddResourceEntry` abort traps.

#### Current frontier (known issue)

- UI rendering is now stable and native on Vulkan, but the active blocker is font atlas corruption that appears as "TV static".
- The current hypothesis is a CPU-to-GPU pixel-format mismatch during `CFontTexture` upload, likely involving 1-byte luminance-style data being interpreted through a 4-byte RGBA path.

## 4) Submodule Ecosystem (13 Entries)

The repository currently declares 13 top-level Git submodules in `.gitmodules`:

1. `tools/unitsync/python` -> `https://github.com/spring/pyunitsync.git`
Role: Python bindings/integration around unitsync tooling.
2. `tools/pr-downloader` -> `https://github.com/beyond-all-reason/pr-downloader`
Role: Content/package downloader utility used by engine workflows.
3. `AI/Skirmish/BARb` -> `https://github.com/rlcevg/CircuitAI.git` (branch `barbarian`)
Role: BAR-specific skirmish AI module.
4. `AI/Skirmish/CircuitAI` -> `https://github.com/rlcevg/CircuitAI.git` (branch `zk`)
Role: Circuit AI integration for skirmish play.
5. `rts/lib/tracy` -> `https://github.com/wolfpld/tracy.git`
Role: Runtime profiling/tracing.
6. `rts/lib/gflags` -> `https://github.com/gflags/gflags`
Role: Command-line flag parsing support.
7. `rts/lib/entt` -> `https://github.com/skypjack/entt`
Role: Entity/component and utility containers used by engine systems.
8. `rts/lib/cereal` -> `https://github.com/USCiLab/cereal.git`
Role: Serialization framework.
9. `rts/lib/RmlUi` -> `https://github.com/mikke89/RmlUi.git`
Role: HTML/CSS-style UI framework.
10. `rts/lib/lunasvg` -> `https://github.com/sammycage/lunasvg.git`
Role: SVG rendering support.
11. `rts/lib/fastgltf` -> `https://github.com/spnda/fastgltf`
Role: glTF asset loading.
12. `rts/lib/simdjson` -> `https://github.com/simdjson/simdjson`
Role: High-performance JSON parsing.
13. `rts/lib/nowide` -> `https://github.com/boostorg/nowide` (branch `standalone`)
Role: UTF-8 portability helpers (especially relevant on Windows).

Operational notes:

- Always clone and update recursively:
  - `git clone --recursive ...`
  - `git submodule update --init --recursive`
- Some dependencies are vendored directly in tree (for example Lua/minizip/glad/jsoncpp/7zip integrations) and are not all represented as top-level submodules.

## 5) Build System And CI Strategy

### Build orchestration

Root CMake flow (high-level):

1. Configure global toolchain/options and platform flags.
2. Add core libraries first (`rts/lib`).
3. Add optional AI tree (`AI/`) depending on `AI_TYPES`.
4. Add shared engine systems (`rts/System`).
5. Add tools/docs/content (`tools`, `doc`, `cont`).
6. Add engine runtime tree (`rts`).
7. Enable testing and add `test/`.

### Build variants

Build variants are configured in `rts/builds/`:

- `spring-legacy`: full graphical engine path.
- `spring-dedicated`: dedicated server profile.
- `spring-headless`: graphics-light profile suited for CI and server workflows.

Recent headless architecture detail:

- `rts/builds/headless/CMakeLists.txt` defines `GameHeadless` from shared game sources (`sources_engine_Game`) directly.
- This decouples headless from requiring the legacy build target, so `BUILD_spring-legacy=OFF` remains a valid configuration.

### Key build options in active use

- `BUILD_spring-legacy=OFF`: disable legacy graphical target.
- `AI_TYPES=NONE`: skip AI interface builds when not needed.
- `ENABLE_STREFLOP=OFF` (common on macOS CI path).
- `RECOIL_WERROR=ON`: treat warnings as errors for engine-owned targets.
- `ENGINE_ENABLE_SANITIZERS=ON`: AddressSanitizer + UndefinedBehaviorSanitizer.
- `ENGINE_ENABLE_TSAN=ON`: ThreadSanitizer (mutually exclusive with ASan).

### macOS portability and CI

Current macOS workflow (`.github/workflows/macos.yml`) runs on `macos-13` (Intel runner) and includes:

- Main headless build and smoke test.
- Portability verification:
  - `LC_RPATH` inspection (`@loader_path` / `@executable_path` expectations).
  - codesign verification (ad-hoc signature check).
- Dedicated sanitizer jobs:
  - ASan + UBSan smoke test loops.
  - TSan smoke test loops.

macOS headless target behavior includes:

- RPATH setup (`BUILD_RPATH` and `INSTALL_RPATH`) for relocation-friendly binaries.
- Post-build ad-hoc signing (`codesign --sign -`) when available.

### Testing layers

- `test/`: broad Catch2-based unit tests across engine systems.
- `test/smoke/`: lightweight startup/platform smoke tests, including:
  - platform RAM/pagefile probes,
  - filesystem case behavior checks,
  - multithreaded platform-call checks,
  - repeated stress loop with RSS-growth guard (non-Windows path).

## 6) Developer Environment Notes

### Recommended bootstrap

1. Clone recursively:
   - `git clone --recursive https://github.com/beyond-all-reason/RecoilEngine`
2. If needed later:
   - `git submodule update --init --recursive`

### macOS build prerequisites (from current README)

- `xcode-select --install`
- `brew install cmake sdl2 devil freetype zlib curl`

### Known configure pitfalls and fixes

- `pr-downloader` / libcurl pkg-config discovery on macOS may require explicit `PKG_CONFIG_PATH` including Homebrew curl and zlib pkgconfig dirs.
- With CMake 4.x toolchains, configure can require `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for compatibility with older third-party CMake files.
- SevenZip lookup now supports `7zz` (in addition to `7z`/`7za`) via `rts/build/cmake/FindSevenZip.cmake`.

### VS Code submodule warning

If VS Code reports that some submodules will not auto-open, this is usually an editor setting threshold (`git.detectSubmodulesLimit`) rather than missing submodule checkout.

### Platform/threading context relevant to ongoing macOS work

- macOS thread affinity uses QoS intent (`pthread_set_qos_class_self_np`) because macOS does not expose `pthread_setaffinity_np`.
- Apple Silicon topology support in `rts/System/Platform/Mac/CpuTopology.cpp` uses `hw.perflevel*` sysctl data for P-core/E-core masks.
- macOS thread names are truncated to kernel limits in `rts/System/Platform/Threading.cpp`.

### Practical headless-first command pattern

```bash
PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:$(brew --prefix zlib)/lib/pkgconfig:${PKG_CONFIG_PATH}" \
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DBUILD_spring-legacy=OFF \
  -DAI_TYPES=NONE \
  -DENABLE_STREFLOP=OFF

cmake --build build --target engine-headless --parallel $(sysctl -n hw.logicalcpu)
cmake --build build --target engine_smoketest
ctest --test-dir build --output-on-failure -R smoketest
```
