# Engine Architecture Map

This document is a definitive, single-file map of the RecoilEngine codebase, organized by subsystem and annotated for the OpenGL-to-Vulkan migration.

## Engine Knowledge Base (Provided)

1. Input & Interaction (The CMouseHandler System)

rts/Game/UI/MouseHandler.cpp & .h: The absolute command center for mouse input. It intercepts raw OS/SDL coordinates. Key architectural quirk: It natively assumes an OpenGL "bottom-up" coordinate system, meaning it actively intercepts raw y coordinates and mirrors them (viewSizeY - y) before passing them to the UI or the 3D raycaster.

rts/Game/UI/MouseCursor.cpp & .h: Manages both hardware and software cursors.

Visuals: Uses legacy OpenGL immediate-mode-style rendering (RenderBuffer<VA_TYPE_TC>) which completely drops the draw calls when the Vulkan backend is active.

Logic: Contains complex "hotspot" math (CalcFrameMatrixParams) to center the cursor image on the click point, heavily reliant on OpenGL's Y-up projection.

2. Textures & Asset Loading

rts/Rendering/Textures/Bitmap.cpp & .h: The raw image data handler. Key architectural quirk: Its CreateTexture() method explicitly returns raw OpenGL integer IDs. In non-OpenGL backends (like Vulkan), it returns 0, causing the engine to try and draw with null textures. Modern backends require using CreateBackendTexture() to return a gfx::ITexture pointer instead.

3. Rendering Pipeline & Backends

rts/Rendering/GL/RenderBuffers.cpp & .h: The legacy OpenGL drawing pipeline. Any UI element (like the software cursor) that relies on RenderBuffer will simply not render in Vulkan without a rewrite.

rts/Rendering/Gfx/IGraphicsBackend.h: The modern interface designed to bridge OpenGL and Vulkan. Contains the modern pipeline methods we want to use, specifically DrawTexturedIndexedBatches.

rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp & VulkanTexturedBatchShaders.h: The actual Vulkan implementation. It expects UI elements to be batched and submitted via VulkanTexturedBatch rather than drawn frame-by-frame in immediate mode.

4. UI Framework & Typography

rts/aGui/GuiElement.cpp: The aGui UI layer uses a classic GuiElement hierarchy with per-widget draw paths. It has a gfx backend path for textured batches and a legacy GL RenderBuffers fallback.

rts/Rendering/Fonts/glFontRenderer.cpp: Handles text. It requires specific yOffset math adjustments when switching between OpenGL (bottom-up baseline) and Vulkan (top-down baseline) to keep text centered inside button hitboxes.

## Entry Points And Lifecycle

- [rts/System/SpringApp.cpp](rts/System/SpringApp.cpp): Main application entry point, bootstraps subsystems and orchestrates engine startup/shutdown.
- [rts/Rendering/GlobalRendering.cpp](rts/Rendering/GlobalRendering.cpp): Central graphics initialization and per-frame rendering state, including backend selection and window flags.
- [rts/Game/Game.cpp](rts/Game/Game.cpp): Core game loop and orchestration of sim, rendering, and UI per frame.
- [rts/Net/GameServer.cpp](rts/Net/GameServer.cpp): Dedicated/server-side loop and authoritative simulation hosting.

## Input Handling

- [rts/Game/UI/MouseHandler.cpp](rts/Game/UI/MouseHandler.cpp): Raw mouse processing, coordinate transforms, selection box logic, and camera raycasting hooks.
⚠️ Vulkan Porting Note: Y inversion is OpenGL-centric; Vulkan should consume raw Y-down coordinates and only flip when explicitly needed by legacy GL paths.

- [rts/Game/UI/MouseHandler.h](rts/Game/UI/MouseHandler.h): Interface and state for cursor position, selection, and button state.

- [rts/Game/UI/MouseCursor.cpp](rts/Game/UI/MouseCursor.cpp): Software/hardware cursor loading, hotspot math, and draw submission.
⚠️ Vulkan Porting Note: The RenderBuffer-based GL draw path must be replaced with backend batches (DrawTexturedIndexedBatches) and Vulkan-ready textures.

- [rts/Game/UI/MouseCursor.h](rts/Game/UI/MouseCursor.h): Cursor data layout (frames, hotspots, sizes) and rendering API.

- [rts/Game/UI/ScanCodes.h](rts/Game/UI/ScanCodes.h): Scan-code key mapping provider implementing `IKeys` for input routing.

- [rts/Game/UI/KeySet.h](rts/Game/UI/KeySet.h): Key-set and key-chain definitions for binding and chord parsing.

- [rts/Game/UI/KeySet.cpp](rts/Game/UI/KeySet.cpp): Parses key strings, tracks modifiers, and builds key chains for binding resolution.

- [rts/Game/UI/SelectionKeyHandler.h](rts/Game/UI/SelectionKeyHandler.h): Input receiver for selection key groups and related shortcuts.

- [rts/Game/UI/InfoConsole.h](rts/Game/UI/InfoConsole.h): In-game info console; also acts as an `ILogSink` and `CEventClient` for log/event display.

- [rts/Game/UI/ResourceBar.h](rts/Game/UI/ResourceBar.h): Input receiver for the resource bar overlay.

## Rendering Pipeline And Backends

- [rts/Rendering/Gfx/IGraphicsBackend.h](rts/Rendering/Gfx/IGraphicsBackend.h): Backend-agnostic rendering interface for buffers, textures, frame lifecycle, and batched draws.
- [rts/Rendering/Gfx/GL/GLGraphicsBackend.h](rts/Rendering/Gfx/GL/GLGraphicsBackend.h): OpenGL backend adapter implementing IGraphicsBackend.
- [rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp](rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp): OpenGL command translation and draw submission.
- [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h): Vulkan backend API, swapchain management, and textured batch pipeline.
- [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp): Vulkan rendering implementation, including deferred UI batches and swapchain resources.
⚠️ Vulkan Porting Note: The Vulkan backend may require explicit depth testing disables (glDisable(GL_DEPTH_TEST) or backend-equivalent pipeline states) for 2D UI overlays to prevent Z-fighting or clipping against the 3D world.
- [rts/Rendering/Gfx/Vulkan/VulkanTexturedBatchShaders.h](rts/Rendering/Gfx/Vulkan/VulkanTexturedBatchShaders.h): Precompiled SPIR-V shader blobs for Vulkan UI textured batches.
- [rts/Rendering/GL/RenderBuffers.h](rts/Rendering/GL/RenderBuffers.h): Legacy GL render buffer abstraction (VAO/VBO/IBO) and GL shader helper.
⚠️ Vulkan Porting Note: RenderBuffers are GL-only and call glDraw*; Vulkan must use gfx::IVertexBuffer + DrawTexturedIndexedBatches or DrawIndexed paths.

- [rts/Rendering/GL/RenderBuffers.cpp](rts/Rendering/GL/RenderBuffers.cpp): Static registry and lifecycle of typed render buffers.
⚠️ Vulkan Porting Note: Any new UI or debug path should avoid RenderBuffers and use backend-neutral buffers instead.

- [rts/Rendering/GL/VertexArrayTypes.h](rts/Rendering/GL/VertexArrayTypes.h): Legacy vertex layouts and attribute definitions used by GL pipelines.
⚠️ Vulkan Porting Note: Many structures assume GL attribute conventions; migrate call sites to gfx::TexturedVertexLayout and backend buffers.

## UI And Fonts

- [rts/aGui/Gui.cpp](rts/aGui/Gui.cpp): Lightweight UI framework root, manages element tree and event routing.
- [rts/aGui/GuiElement.cpp](rts/aGui/GuiElement.cpp): Base UI element behavior and draw helpers, with a gfx backend path (IGraphicsBackend + IVertexBuffer/ITexture) and a legacy GL RenderBuffers fallback.
⚠️ Vulkan Porting Note: The UI path is a classic GuiElement hierarchy, not entt::poly. Vulkan batching hooks live in the per-widget draw paths and in GuiElement helpers.
- [rts/aGui/GuiElement.h](rts/aGui/GuiElement.h): Core UI element interface and layout hooks.
- [rts/aGui/Button.cpp](rts/aGui/Button.cpp): Button widget rendering and interaction.
- [rts/aGui/Button.h](rts/aGui/Button.h): Button widget interface.
- [rts/aGui/Window.cpp](rts/aGui/Window.cpp): Window/container widget rendering.
- [rts/aGui/Window.h](rts/aGui/Window.h): Window widget interface.
- [rts/aGui/TextElement.cpp](rts/aGui/TextElement.cpp): UI text element rendering and font binding.
- [rts/aGui/TextElement.h](rts/aGui/TextElement.h): Text element interface.
- [rts/aGui/Picture.cpp](rts/aGui/Picture.cpp): UI image rendering with backend textured batches.
- [rts/aGui/Picture.h](rts/aGui/Picture.h): Picture widget interface; bridges to gfx texture/vertex buffer usage.
- [rts/aGui/HorizontalLayout.cpp](rts/aGui/HorizontalLayout.cpp): Horizontal layout container.
- [rts/aGui/HorizontalLayout.h](rts/aGui/HorizontalLayout.h): Horizontal layout interface.
- [rts/aGui/VerticalLayout.cpp](rts/aGui/VerticalLayout.cpp): Vertical layout container.
- [rts/aGui/VerticalLayout.h](rts/aGui/VerticalLayout.h): Vertical layout interface.
- [rts/aGui/Layout.h](rts/aGui/Layout.h): Shared layout helpers and sizing rules.
- [rts/aGui/LineEdit.cpp](rts/aGui/LineEdit.cpp): Text input widget drawing and caret handling; uses GL + fonts.
- [rts/aGui/LineEdit.h](rts/aGui/LineEdit.h): Line edit widget interface.
- [rts/aGui/List.cpp](rts/aGui/List.cpp): List widget rendering and input handling; uses GL state and font rendering.
- [rts/aGui/List.h](rts/aGui/List.h): List widget interface and data model.
- [rts/aGui/CMakeLists.txt](rts/aGui/CMakeLists.txt): aGui source registration for build integration.

- [rts/Game/UI/QuitBox.cpp](rts/Game/UI/QuitBox.cpp): Quit confirmation UI with GL rendering and mouse/key handling; depends on global game state and player/team info.
⚠️ Vulkan Porting Note: GL draw calls (`myGL`/`glExtra`) make this overlay GL-only until ported to `IGraphicsBackend` batches.

- [rts/Game/UI/CursorIcons.cpp](rts/Game/UI/CursorIcons.cpp): Builds and renders contextual cursor icons using GL + font rendering; relies on `MouseHandler` and command metadata.
⚠️ Vulkan Porting Note: Uses GL state directly, so Vulkan needs a backend-neutral draw path.

- [rts/Game/UI/GameSetupDrawer.cpp](rts/Game/UI/GameSetupDrawer.cpp): Pre-game setup overlay rendering and countdown UI; consumes game setup data and fonts.

- [rts/Game/UI/PlayerRosterDrawer.cpp](rts/Game/UI/PlayerRosterDrawer.cpp): Draws the player roster HUD overlay; depends on player/team info, fonts, and global rendering state.

- [rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp](rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp): RmlUi GL3 renderer backend implementation.
⚠️ Vulkan Porting Note: Replace GL3 draw calls with gfx::IGraphicsBackend submissions and Vulkan-ready textures.

- [rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.h](rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.h): RmlUi renderer interface for the GL3 backend.
⚠️ Vulkan Porting Note: Update interface to allow backend-neutral render submission.

- [rts/Rml/Backends/RmlUi_Backend.cpp](rts/Rml/Backends/RmlUi_Backend.cpp): RmlUi backend glue that binds renderer/system/file interfaces.
- [rts/Rml/Backends/RmlUi_Backend.h](rts/Rml/Backends/RmlUi_Backend.h): Backend abstraction for RmlUi.
- [rts/Rml/Backends/RmlUi_SystemInterface.cpp](rts/Rml/Backends/RmlUi_SystemInterface.cpp): RmlUi system services (timing, logging, clipboard).
- [rts/Rml/Backends/RmlUi_SystemInterface.h](rts/Rml/Backends/RmlUi_SystemInterface.h): System interface declaration for RmlUi.
- [rts/Rml/Backends/RmlUi_VFSFileInterface.cpp](rts/Rml/Backends/RmlUi_VFSFileInterface.cpp): RmlUi file access through VFS.
- [rts/Rml/Backends/RmlUi_VFSFileInterface.h](rts/Rml/Backends/RmlUi_VFSFileInterface.h): File interface for RmlUi VFS integration.
- [rts/Rml/RmlInputReceiver.h](rts/Rml/RmlInputReceiver.h): Input bridge from engine events to RmlUi.
- [rts/Rml/Components/ElementLuaTexture.cpp](rts/Rml/Components/ElementLuaTexture.cpp): RmlUi element that draws Lua-provided textures.
- [rts/Rml/Components/ElementLuaTexture.h](rts/Rml/Components/ElementLuaTexture.h): Lua texture element interface.

- [rts/Rendering/Fonts/CFontTexture.cpp](rts/Rendering/Fonts/CFontTexture.cpp): Font atlas creation, glyph packing, and atlas uploads.
⚠️ Vulkan Porting Note: Ensure atlas uploads use backend textures (gfx::ITexture) and correct pixel formats for Vulkan.

- [rts/Rendering/Fonts/CFontTexture.h](rts/Rendering/Fonts/CFontTexture.h): Font atlas data model and glyph cache.
- [rts/Rendering/Fonts/glFontRenderer.cpp](rts/Rendering/Fonts/glFontRenderer.cpp): Font renderer path with GL shaders and Vulkan batch fallback.
⚠️ Vulkan Porting Note: Y-baseline math and UV normalization must be consistent with Y-down coordinates.

- [rts/Rendering/Fonts/glFontRenderer.h](rts/Rendering/Fonts/glFontRenderer.h): Font renderer interface and buffer ownership.

## Textures And Asset Loading

- [rts/Rendering/Textures/Bitmap.cpp](rts/Rendering/Textures/Bitmap.cpp): Bitmap loading, conversion, and texture creation.
⚠️ Vulkan Porting Note: CreateTexture returns GL IDs; Vulkan paths must use CreateBackendTexture and gfx::ITexture handles.

- [rts/Rendering/Textures/Bitmap.h](rts/Rendering/Textures/Bitmap.h): Bitmap API and texture creation entry points.
⚠️ Vulkan Porting Note: Prefer CreateBackendTexture in backend-neutral code paths.

- [rts/Rendering/Textures/Texture.hpp](rts/Rendering/Textures/Texture.hpp): GL texture wrapper types that store backend ITexture handles.
⚠️ Vulkan Porting Note: Avoid assuming GetGLId is valid in non-OpenGL backends.

## Simulation (Core RTS Logic)

- [rts/Sim/CMakeLists.txt](rts/Sim/CMakeLists.txt): Build definition for the simulation library and its link dependencies.

- [rts/Sim/Misc/CollisionVolume.cpp](rts/Sim/Misc/CollisionVolume.cpp): Collision volume math, shape initialization, and distance queries used by units and features.

- [rts/Sim/Misc/CollisionHandler.cpp](rts/Sim/Misc/CollisionHandler.cpp): Collision testing and intersection routines for volumes, footprints, and ray tests.

- [rts/Sim/Misc/QuadField.cpp](rts/Sim/Misc/QuadField.cpp): Spatial partitioning grid for units, features, projectiles, and collision queries; key for performance.

- [rts/Sim/Misc/TeamBase.cpp](rts/Sim/Misc/TeamBase.cpp): Shared team data (colors, custom values) and serialization metadata.

- [rts/Sim/MoveTypes/MoveType.h](rts/Sim/MoveTypes/MoveType.h): Base class for unit movement state and update hooks.
- [rts/Sim/MoveTypes/GroundMoveType.cpp](rts/Sim/MoveTypes/GroundMoveType.cpp): Ground movement behavior and path following.
- [rts/Sim/MoveTypes/HoverAirMoveType.cpp](rts/Sim/MoveTypes/HoverAirMoveType.cpp): Hover/air movement behavior.
- [rts/Sim/MoveTypes/ScriptMoveType.cpp](rts/Sim/MoveTypes/ScriptMoveType.cpp): Script-driven movement (Lua or external AI).
- [rts/Sim/MoveTypes/MoveDefHandler.cpp](rts/Sim/MoveTypes/MoveDefHandler.cpp): Move definition loading and caching.
- [rts/Sim/MoveTypes/Systems/GeneralMoveSystem.cpp](rts/Sim/MoveTypes/Systems/GeneralMoveSystem.cpp): ECS-style movement system updates.
- [rts/Sim/MoveTypes/Systems/GroundMoveSystem.cpp](rts/Sim/MoveTypes/Systems/GroundMoveSystem.cpp): Ground movement system specialization.

## Networking And Multiplayer

- [rts/Net/GameServer.cpp](rts/Net/GameServer.cpp): Dedicated server implementation and authoritative game state broadcast.
- [rts/Net/GameServer.h](rts/Net/GameServer.h): GameServer interface and lifecycle.
- [rts/Net/GameParticipant.cpp](rts/Net/GameParticipant.cpp): Player/AI participant state and identity management.
- [rts/Net/GameParticipant.h](rts/Net/GameParticipant.h): Participant data model.
- [rts/Net/NetCommands.cpp](rts/Net/NetCommands.cpp): Serialization and handling of network commands.
- [rts/Net/Protocol/NetProtocol.cpp](rts/Net/Protocol/NetProtocol.cpp): Protocol implementation for network packet flow.
- [rts/Net/Protocol/NetProtocol.h](rts/Net/Protocol/NetProtocol.h): Protocol interface and message flow definitions.
- [rts/Net/Protocol/NetMessageTypes.h](rts/Net/Protocol/NetMessageTypes.h): Protocol message enums and packet IDs.
- [rts/Net/AutohostInterface.h](rts/Net/AutohostInterface.h): Autohost control interface for reporting game events, player state, and chat.
- [rts/Net/AutohostInterface.cpp](rts/Net/AutohostInterface.cpp): UDP-based autohost message sender; uses `BaseNetProtocol` and socket helpers.
- [rts/Net/GameSkirmishAI.h](rts/Net/GameSkirmishAI.h): Network-visible wrapper for skirmish AI participation.
- [rts/Net/CMakeLists.txt](rts/Net/CMakeLists.txt): Defines net-related source groupings for engine build targets.

## Lua Scripting

- [rts/Lua/LuaIntro.cpp](rts/Lua/LuaIntro.cpp): Lua intro screen script integration.
- [rts/Lua/LuaHandleSynced.cpp](rts/Lua/LuaHandleSynced.cpp): Synced Lua state management for deterministic simulation.
- [rts/Lua/LuaSyncedCtrl.cpp](rts/Lua/LuaSyncedCtrl.cpp): Synced Lua control API exposed to scripts.
- [rts/Lua/LuaSyncedCtrl.h](rts/Lua/LuaSyncedCtrl.h): Synced control interface.
- [rts/Lua/LuaUnsyncedRead.h](rts/Lua/LuaUnsyncedRead.h): Unsynced Lua read accessors (UI and client-only data).
- [rts/Lua/LuaShaders.cpp](rts/Lua/LuaShaders.cpp): Lua-facing shader program creation, linking, uniform updates, and engine uniform buffer integration; ties into `LuaOpenGL` and rendering globals.
⚠️ Vulkan Porting Note: This path relies on OpenGL shader/program IDs and GL uniform APIs; Vulkan needs a backend-neutral shader interface.

- [rts/Lua/LuaMaterial.cpp](rts/Lua/LuaMaterial.cpp): Lua material parsing and execution for model rendering, including shader state, uniforms, and deferred-pass handling.
⚠️ Vulkan Porting Note: Uses OpenGL material and uniform semantics; Vulkan needs a parallel material execution path or a translation layer.
- [rts/Lua/LuaTextures.cpp](rts/Lua/LuaTextures.cpp): Lua-driven texture loading and binding.
⚠️ Vulkan Porting Note: GL texture IDs and OpenGL-specific texture state must be replaced with gfx::ITexture accessors.

- [rts/Lua/LuaVBO.h](rts/Lua/LuaVBO.h): Lua-managed VBO interfaces for custom rendering.
⚠️ Vulkan Porting Note: GL buffer APIs must be wrapped or replaced with backend-neutral buffers.

- [rts/Lua/LuaOpenGLUtils.h](rts/Lua/LuaOpenGLUtils.h): GL helper utilities for Lua rendering.
⚠️ Vulkan Porting Note: Vulkan backend needs an alternative API or a compatibility layer for GL-only helpers.

## Map And World

- [rts/Map/MapInfo.cpp](rts/Map/MapInfo.cpp): Map metadata parsing and accessors.
- [rts/Map/MapInfo.h](rts/Map/MapInfo.h): Map metadata interface.
- [rts/Map/ReadMap.cpp](rts/Map/ReadMap.cpp): Map loading orchestration and shared map services.
- [rts/Map/ReadMap.h](rts/Map/ReadMap.h): ReadMap interface.
- [rts/Map/Ground.cpp](rts/Map/Ground.cpp): Ground height queries and terrain interaction.
- [rts/Map/Ground.h](rts/Map/Ground.h): Terrain access interface.
- [rts/Map/BaseGroundDrawer.cpp](rts/Map/BaseGroundDrawer.cpp): Ground rendering orchestration for map tiles.
⚠️ Vulkan Porting Note: Likely GL draw calls; migrate to backend-neutral draw paths or Vulkan terrain pipeline.

- [rts/Map/BaseGroundDrawer.h](rts/Map/BaseGroundDrawer.h): Ground drawer interface and shared state.
- [rts/Map/SMF/SMFGroundDrawer.cpp](rts/Map/SMF/SMFGroundDrawer.cpp): SMF map renderer implementation.
⚠️ Vulkan Porting Note: Replace GL-specific rendering with gfx::IGraphicsBackend submissions.

- [rts/Map/MapParser.cpp](rts/Map/MapParser.cpp): Map definition parser and format handling.
- [rts/Map/MapParser.h](rts/Map/MapParser.h): Map parser interface.
- [rts/Map/MapTexture.h](rts/Map/MapTexture.h): Map texture ownership and updates.
- [rts/Map/Generation/BlankMapGenerator.cpp](rts/Map/Generation/BlankMapGenerator.cpp): Procedural blank map generator for fallback/debug.
- [rts/Map/Generation/BlankMapGenerator.h](rts/Map/Generation/BlankMapGenerator.h): Blank map generator interface.

## External AI Integration

- [rts/ExternalAI/AILibraryManager.cpp](rts/ExternalAI/AILibraryManager.cpp): AI library discovery and dynamic loading.
- [rts/ExternalAI/AIInterfaceLibrary.cpp](rts/ExternalAI/AIInterfaceLibrary.cpp): AI interface library implementation.
- [rts/ExternalAI/AIInterfaceLibrary.h](rts/ExternalAI/AIInterfaceLibrary.h): AI interface library API.
- [rts/ExternalAI/AIInterfaceKey.cpp](rts/ExternalAI/AIInterfaceKey.cpp): AI interface key parsing and comparison.
- [rts/ExternalAI/AIInterfaceKey.h](rts/ExternalAI/AIInterfaceKey.h): AI interface key definition.
- [rts/ExternalAI/SkirmishAIHandler.h](rts/ExternalAI/SkirmishAIHandler.h): Skirmish AI lifecycle and dispatch.
- [rts/ExternalAI/SkirmishAIData.cpp](rts/ExternalAI/SkirmishAIData.cpp): Per-AI configuration and metadata.
- [rts/ExternalAI/SkirmishAIData.h](rts/ExternalAI/SkirmishAIData.h): Skirmish AI data model.
- [rts/ExternalAI/SkirmishAIWrapper.cpp](rts/ExternalAI/SkirmishAIWrapper.cpp): Runtime AI wrapper and bridge to engine callbacks.
- [rts/ExternalAI/SSkirmishAICallbackImpl.cpp](rts/ExternalAI/SSkirmishAICallbackImpl.cpp): Implementation of AI callbacks into engine.
- [rts/ExternalAI/SSkirmishAICallbackImpl.h](rts/ExternalAI/SSkirmishAICallbackImpl.h): AI callback interface.

## System And Serialization

- [rts/System/creg/creg.cpp](rts/System/creg/creg.cpp): Core reflection/serialization registry.
- [rts/System/creg/Serializer.cpp](rts/System/creg/Serializer.cpp): Object serialization and save/load pipeline.
- [rts/System/creg/Serializer.h](rts/System/creg/Serializer.h): Serializer interface and configuration.
- [rts/System/creg/SerializeLuaState.cpp](rts/System/creg/SerializeLuaState.cpp): Lua state serialization glue for saved games.
- [rts/System/creg/SerializeLuaState.h](rts/System/creg/SerializeLuaState.h): Lua serialization declarations.

## Notes For Ongoing Vulkan Port

- Prioritize removal of GL-only draw calls in UI and Lua rendering APIs.
- Prefer gfx::IVertexBuffer and gfx::ITexture for backend-neutral code paths.
- Keep Y-down coordinate conventions consistent across UI, fonts, and mouse input to avoid mirrored rendering.

## Phase 2: rts/Rendering/Gfx Deep Dive

### 3. File-by-File Analysis

- [rts/Rendering/Gfx/GfxTypes.h](rts/Rendering/Gfx/GfxTypes.h): Core type system for backend-neutral rendering (enums for `BackendType`, `TextureUsage`, `ShaderStage`, `PrimitiveTopology`, and structs like `VertexLayoutDesc`, `TextureCreateInfo`, `RenderTargetDesc`, `UniformParameter`). Used by every Gfx interface and implementation; both GL and Vulkan backends rely on its enums for translation. Architectural role: the shared contract between legacy OpenGL and Vulkan pipelines. Risks: changing enum values or struct layouts silently breaks GL/Vulkan translations, shader parameter packing, and attachment semantics across the engine.

- [rts/Rendering/Gfx/IRenderTarget.h](rts/Rendering/Gfx/IRenderTarget.h): Minimal render target interface with `GetExtent`, `GetSampleCount`, and `GetNativeHandle`. Implemented by `GLFramebuffer` and `VulkanFramebuffer`, consumed by `IGraphicsBackend::BindFramebuffer`. Architectural role: backend-neutral render target handle. Risks: the meaning of `GetNativeHandle` differs by backend (GLuint vs VkFramebuffer or VkImageView); cross-backend misuse is easy.

- [rts/Rendering/Gfx/IFramebuffer.h](rts/Rendering/Gfx/IFramebuffer.h): Render target specialization supporting attachments, color draw targets, validation, and blit. Implemented by [rts/Rendering/Gfx/GL/GLFramebuffer.cpp](rts/Rendering/Gfx/GL/GLFramebuffer.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp](rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp). Architectural role: offscreen render target bridge for GL and Vulkan. Risks: Vulkan `Blit` is stubbed, and attachment semantics differ between GL and Vulkan; changing attachment rules can break one backend while passing in the other.

- [rts/Rendering/Gfx/IVertexBuffer.h](rts/Rendering/Gfx/IVertexBuffer.h): Vertex/index buffer abstraction with resize, update, and map/unmap. Implemented by [rts/Rendering/Gfx/GL/GLVertexBuffer.cpp](rts/Rendering/Gfx/GL/GLVertexBuffer.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.cpp](rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.cpp). Architectural role: buffer ownership for both legacy GL and Vulkan pipelines. Risks: Vulkan UI batching maps buffers via `MapWrite`; changing map semantics or mappability can break Vulkan UI draws.

- [rts/Rendering/Gfx/IVertexArray.h](rts/Rendering/Gfx/IVertexArray.h): Lightweight vertex array abstraction with `Bind`/`Unbind`. Implemented by [rts/Rendering/Gfx/GL/GLVertexArray.cpp](rts/Rendering/Gfx/GL/GLVertexArray.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanVertexArray.cpp](rts/Rendering/Gfx/Vulkan/VulkanVertexArray.cpp). Architectural role: VAO wrapper for GL, binding cache for Vulkan. Risks: Vulkan uses this as a state cache only; adding GL-style state assumptions will not carry to Vulkan.

- [rts/Rendering/Gfx/ITexture.h](rts/Rendering/Gfx/ITexture.h): Texture abstraction for upload, subregion upload, sampler state, and mip generation. Implemented by [rts/Rendering/Gfx/GL/GLTexture.cpp](rts/Rendering/Gfx/GL/GLTexture.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanTexture.cpp](rts/Rendering/Gfx/Vulkan/VulkanTexture.cpp). Architectural role: texture ownership for both pipelines. Risks: `GetNativeHandle` is backend-specific; Vulkan returns an image view pointer, not a GL ID. Vulkan mip generation is not implemented.

- [rts/Rendering/Gfx/IShader.h](rts/Rendering/Gfx/IShader.h): Shader object abstraction with stage, format, validation, and log. Implemented by [rts/Rendering/Gfx/GL/GLShader.cpp](rts/Rendering/Gfx/GL/GLShader.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanShader.cpp](rts/Rendering/Gfx/Vulkan/VulkanShader.cpp). Architectural role: shader compilation/loading boundary. Risks: Vulkan path only accepts SPIR-V; GLSL inputs log an error and return invalid.

- [rts/Rendering/Gfx/IShaderProgram.h](rts/Rendering/Gfx/IShaderProgram.h): Shader program interface with link, uniform parameters, and texture binding. Implemented by [rts/Rendering/Gfx/GL/GLShaderProgram.cpp](rts/Rendering/Gfx/GL/GLShaderProgram.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.cpp](rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.cpp). Architectural role: cross-backend shader program interface. Risks: Vulkan program defers pipeline creation and stores parameters; changing parameter semantics can break Vulkan even if GL still works.

- [rts/Rendering/Gfx/IGraphicsBackend.h](rts/Rendering/Gfx/IGraphicsBackend.h): The primary backend abstraction, including resource creation, render target binding, and draw calls (`DrawTexturedIndexedBatches` and indexed draw paths). Implemented by [rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp](rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp) and [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp). Architectural role: the main orchestration interface between legacy GL and modern Vulkan. Risks: any signature or behavior change must be mirrored in both backends; `DrawTexturedIndexedBatches` is the critical Vulkan UI path.

- [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h): Vulkan backend API and state owner for swapchain, pipelines, transient upload buffers, and deferred UI batch submission. Consumed by `GlobalRendering` and UI render paths that call `IGraphicsBackend`. Architectural role: modern Vulkan pipeline backend. Risks: command-buffer lifecycle, swapchain state, and deferred UI queueing (`pendingTexturedBatchDraws`) are fragile; mismatched vertex layouts or texture descriptor states will silently drop UI draws.

- [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp): Full Vulkan implementation: instance/surface/device setup, swapchain creation, textured batch pipeline, and deferred submission. Draws UI batches via transient buffers and a dedicated textured batch pipeline. Architectural role: Vulkan batch renderer. Risks: `DrawLineBatches` and `DrawIndexedInstanced` throw `not implemented`; `DrawTexturedIndexedBatches` assumes 2D position/UV layout and uses CPU-side remapping of indices, so layout changes can corrupt UI.

- [rts/Rendering/Gfx/Vulkan/VulkanTexturedBatchShaders.h](rts/Rendering/Gfx/Vulkan/VulkanTexturedBatchShaders.h): Embedded SPIR-V blobs for the Vulkan textured batch pipeline. Architectural role: shader payload for UI batching. Risks: descriptor layout changes in `VulkanShaderProgram` or pipeline layout changes must match these blobs.

- [rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.h](rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.h): Vulkan `IFramebuffer` implementation with render-pass and framebuffer handles. Depends on `VulkanTexture`. Architectural role: Vulkan render target support. Risks: expects `VulkanTexture` via `dynamic_cast`; mixing GL textures will fail validation.

- [rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp](rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp): Constructs render passes and framebuffers based on `AttachmentViewDesc`, selects layouts, and validates completeness. `Blit` is not implemented. Architectural role: Vulkan render target assembly. Risks: subtle attachment layout rules; `Blit` always false; errors are mapped to `FramebufferStatus::Unsupported`.

- [rts/Rendering/Gfx/Vulkan/VulkanShader.h](rts/Rendering/Gfx/Vulkan/VulkanShader.h): Vulkan shader wrapper that stores a `VkShaderModule` and `ShaderStage`. Architectural role: Vulkan shader handle. Risks: GLSL is rejected; only SPIR-V accepted.

- [rts/Rendering/Gfx/Vulkan/VulkanShader.cpp](rts/Rendering/Gfx/Vulkan/VulkanShader.cpp): SPIR-V-only loader with validation and log recording. Architectural role: Vulkan shader loading. Risks: SPIR-V byte size must be multiple of 4; invalid modules invalidate the program.

- [rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.h](rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.h): Program wrapper that caches attached shaders and descriptor layout scaffolding. Architectural role: Vulkan program state container. Risks: `Link` builds descriptor set layout and pipeline layout but defers pipeline creation; consumers may assume the program is fully ready when it is not.

- [rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.cpp](rts/Rendering/Gfx/Vulkan/VulkanShaderProgram.cpp): Implements the Vulkan program lifecycle, descriptor scaffolding, texture binding tracking, and global bound program state. Architectural role: Vulkan program state. Risks: descriptor layout is currently fixed (uniform buffer + 16 sampled images); changes ripple into pipeline creation and UI batching.

- [rts/Rendering/Gfx/Vulkan/VulkanVertexArray.h](rts/Rendering/Gfx/Vulkan/VulkanVertexArray.h): Vulkan VAO analogue that caches layout and buffer bindings. Architectural role: Vulkan draw binding state. Risks: no real Vulkan object exists; assumes buffers are Vulkan-backed and validated elsewhere.

- [rts/Rendering/Gfx/Vulkan/VulkanVertexArray.cpp](rts/Rendering/Gfx/Vulkan/VulkanVertexArray.cpp): Stores `VulkanVertexBuffer` bindings and index buffer references. Architectural role: Vulkan binding cache. Risks: `dynamic_cast` failures silently drop bindings; no validation for mismatched layout.

- [rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.h](rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.h): Vulkan buffer wrapper with resize and mapping. Architectural role: Vulkan buffer allocation/mapping. Risks: buffer memory type selection and mapping behavior are critical for UI and batch uploads.

- [rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.cpp](rts/Rendering/Gfx/Vulkan/VulkanVertexBuffer.cpp): Implements Vulkan buffer allocation, update, and map/unmap. Architectural role: Vulkan buffer operations. Risks: mapping a GPU-only buffer is invalid; resizing can drop data if not preserved.

- [rts/Rendering/Gfx/Vulkan/VulkanTexture.h](rts/Rendering/Gfx/Vulkan/VulkanTexture.h): Vulkan texture wrapper (image, view, sampler) supporting uploads and layout transitions. Architectural role: Vulkan texture ownership. Risks: no native handle wrapping; mipmap generation is a stub; layout transitions must match usage flags.

- [rts/Rendering/Gfx/Vulkan/VulkanTexture.cpp](rts/Rendering/Gfx/Vulkan/VulkanTexture.cpp): Creates images, views, and samplers; uploads via staging buffers; transitions layouts per usage. Architectural role: Vulkan texture data path. Risks: uses a per-upload command buffer with `vkQueueWaitIdle`, which can stall; some dimensions (Tex3D subregion uploads) are not implemented.

- [rts/Rendering/Gfx/GL/GLGraphicsBackend.h](rts/Rendering/Gfx/GL/GLGraphicsBackend.h): OpenGL backend adapter implementing `IGraphicsBackend`. Architectural role: legacy OpenGL pipeline entry point. Risks: function parity with Vulkan must be maintained.

- [rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp](rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp): GL implementation of draw calls using client-state arrays and immediate-mode-era patterns (`glVertexPointer`, `glTexCoordPointer`, `glColorPointer`). Architectural role: legacy OpenGL draw submission. Risks: heavy reliance on global GL state; any state leakage or shader pipeline changes can break unrelated rendering.

- [rts/Rendering/Gfx/GL/GLFramebuffer.h](rts/Rendering/Gfx/GL/GLFramebuffer.h): OpenGL framebuffer wrapper for attachments and blit. Architectural role: legacy GL render target support. Risks: relies on EXT functions and native handle IDs from textures; assumes FBO features are available.

- [rts/Rendering/Gfx/GL/GLFramebuffer.cpp](rts/Rendering/Gfx/GL/GLFramebuffer.cpp): Implements attachment binding, validation, and blit with FBOs. Architectural role: GL render target management. Risks: attachment operations mutate GL global FBO state and require careful restore; missing GL functions cause no-op behavior.

- [rts/Rendering/Gfx/GL/GLShader.h](rts/Rendering/Gfx/GL/GLShader.h): GL shader wrapper for GLSL sources. Architectural role: legacy shader compilation. Risks: GLSL only; shader compile errors populate logs and set `valid=false`.

- [rts/Rendering/Gfx/GL/GLShader.cpp](rts/Rendering/Gfx/GL/GLShader.cpp): Compiles GLSL with optional definitions and records compile logs. Architectural role: GL shader compilation. Risks: assumes GLSL; any SPIR-V input is rejected.

- [rts/Rendering/Gfx/GL/GLShaderProgram.h](rts/Rendering/Gfx/GL/GLShaderProgram.h): GL program wrapper with uniform caches and texture unit tracking. Architectural role: legacy shader program state. Risks: uniform cache relies on the program being bound; texture unit tracking can desync if external code binds textures.

- [rts/Rendering/Gfx/GL/GLShaderProgram.cpp](rts/Rendering/Gfx/GL/GLShaderProgram.cpp): Links programs, validates, caches uniforms, binds textures via GL IDs. Architectural role: GL shader program execution. Risks: uniform updates are skipped if cached raw words match; changing uniform packing rules can break rendering.

- [rts/Rendering/Gfx/GL/GLTexture.h](rts/Rendering/Gfx/GL/GLTexture.h): GL texture wrapper with upload and sampler state. Architectural role: legacy texture management. Risks: GL upload paths modify unpack state; requires careful state restoration.

- [rts/Rendering/Gfx/GL/GLTexture.cpp](rts/Rendering/Gfx/GL/GLTexture.cpp): Allocates GL textures, handles mipmap allocation, and uploads data with `glTexSubImage*`. Architectural role: GL texture data path. Risks: uses `GL_UNPACK_*` state changes and assumes `TextureDimension` mapping to GL targets.

- [rts/Rendering/Gfx/GL/GLVertexBuffer.h](rts/Rendering/Gfx/GL/GLVertexBuffer.h): GL buffer wrapper with resize and map/unmap. Architectural role: legacy buffer management. Risks: map/unmap flags assume unsynchronized writes and can corrupt if used incorrectly.

- [rts/Rendering/Gfx/GL/GLVertexBuffer.cpp](rts/Rendering/Gfx/GL/GLVertexBuffer.cpp): Implements GL buffer creation, resize, update, and map/unmap with error logging. Architectural role: GL buffer data path. Risks: mapping state is global; map range and size checks must stay correct to prevent GL errors.

- [rts/Rendering/Gfx/GL/GLVertexArray.h](rts/Rendering/Gfx/GL/GLVertexArray.h): GL VAO wrapper. Architectural role: legacy vertex array management. Risks: assumes GL context supports VAOs.

- [rts/Rendering/Gfx/GL/GLVertexArray.cpp](rts/Rendering/Gfx/GL/GLVertexArray.cpp): Manages VAO lifetime and bind/unbind. Architectural role: GL VAO lifecycle. Risks: global GL state changes can conflict with other code paths.

### 4. Dependency & Relationship Mapping

- **Primary orchestration layer:** [rts/Rendering/Gfx/IGraphicsBackend.h](rts/Rendering/Gfx/IGraphicsBackend.h) defines the shared draw and resource API. Both backends implement it, and higher-level systems (UI widgets, rendering passes) target this interface.
- **Legacy OpenGL coupling:** [rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp](rts/Rendering/Gfx/GL/GLGraphicsBackend.cpp) and all GL resource wrappers are tightly bound to global GL state and client-side arrays. Any caller relying on `gl*` side effects or GL IDs is coupled to legacy OpenGL.
- **Modern Vulkan path:** [rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp](rts/Rendering/Gfx/Vulkan/VulkanGraphicsBackend.cpp) is the modern pipeline core. It depends on the Vulkan resource wrappers and on `GfxTypes` layout conventions to translate UI batches.
- **Dangerous edit points:** `GfxTypes` enums/structs (breaks GL/Vulkan translation), `IGraphicsBackend` signatures (breaks both backends), `VulkanGraphicsBackend::DrawTexturedIndexedBatches` (UI batching and deferred draw queue), and `GLGraphicsBackend::DrawTexturedIndexedBatches` (legacy GL state assumptions).
- **Brittle abstractions:** `GetNativeHandle` is backend-specific; GL expects a `GLuint`, Vulkan expects `Vk*` handles. Mixing resources from different backends leads to `dynamic_cast` failures or silent no-ops, especially in [rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp](rts/Rendering/Gfx/Vulkan/VulkanFramebuffer.cpp).
- **Circular or fragile dependencies:** There are no explicit circular includes in Gfx, but GL implementations rely on `Rendering/GL/myGL.h` and Vulkan implementations rely on Vulkan headers plus SDL. The most brittle area is the UI batch path: it uses `IVertexBuffer::MapWrite` on backend-owned buffers, making buffer mapping semantics a hard dependency.
