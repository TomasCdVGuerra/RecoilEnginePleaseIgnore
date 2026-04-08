# Vulkan Migration Plan (Phase 1 Scaffolding)

## Scope
This document captures the OpenGL surface area in CMake and the first scaffold steps for introducing a Vulkan backend (MoltenVK on macOS) without removing existing OpenGL paths.

## OpenGL CMake Surface Area (Current)
1. `rts/builds/legacy/CMakeLists.txt`
- `set(OpenGL_GL_PREFERENCE LEGACY)`
- `find_package_static(OpenGL 3.0 REQUIRED)`
- `target_link_libraries(... OpenGL::GL)`

2. `rts/builds/headless/CMakeLists.txt`
- `set(OpenGL_GL_PREFERENCE LEGACY)`
- `find_package(OpenGL 3.0 REQUIRED)`

3. `rts/Rendering/CMakeLists.txt`
- Explicit GL source set under `rts/Rendering/GL/*` (FBO, VAO, VBO, State, myGL, etc.)

## Core CMake Files To Refactor
1. `CMakeLists.txt` (root)
- Backend feature flags (`ENABLE_VULKAN`), package discovery, macOS MoltenVK hints.

2. `rts/CMakeLists.txt`
- Backend-level target wiring and feature propagation into build variants.

3. `rts/builds/legacy/CMakeLists.txt`
- Conditional linkage selection (`OpenGL::GL` vs Vulkan backend libs/targets).

4. `rts/builds/headless/CMakeLists.txt`
- Current OpenGL requirement should eventually become backend-conditional.

5. `rts/Rendering/CMakeLists.txt`
- Split GL-specific sources from backend-neutral rendering code.

6. `rts/lib/CMakeLists.txt`
- Central location for future Vulkan-supporting dependencies and feature toggles.

## Major Rendering Directories Likely Impacted
1. `rts/Rendering/GL`
2. `rts/Rendering/Shaders`
3. `rts/Rendering/Textures`
4. `rts/Rendering/Models`
5. `rts/Rendering/Env`
6. `rts/Rml/Backends` (contains GL3 backend implementation)
7. `rts/Map/SMF` (GL-facing map rendering paths)

## Phase 1 Scaffolding Checklist
1. Add `ENABLE_VULKAN` CMake option defaulting to `OFF`.
2. Add `find_package(Vulkan REQUIRED)` behind `ENABLE_VULKAN`.
3. Add macOS-specific MoltenVK ICD and Vulkan loader/framework hinting.
4. Keep all OpenGL code paths intact for now.

## Notes
- This phase is scaffolding only. No OpenGL source removal or render-path behavior change is included.
- Headless and current OpenGL builds remain the default behavior while `ENABLE_VULKAN` is `OFF`.
