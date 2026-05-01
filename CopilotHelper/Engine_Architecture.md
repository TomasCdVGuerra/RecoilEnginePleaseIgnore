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

rts/aGui/GuiElement.cpp (and EnTT / poly.md): The UI elements (buttons, minimaps) do not use traditional class inheritance. The engine uses a modern Entity-Component-System (ECS) library called EnTT, leveraging entt::poly for static polymorphism. UI elements are standalone structs/classes that the engine wraps and calls draw() on.

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
- [rts/aGui/GuiElement.cpp](rts/aGui/GuiElement.cpp): Base UI element behavior and draw helpers, including backend batch submission for primitives.
⚠️ Vulkan Porting Note: Because UI elements use entt::poly for static polymorphism, look for standalone draw() functions within individual widget structs rather than a traditional virtual class hierarchy. Vulkan batching must be injected into these specific draw() implementations.
- [rts/aGui/GuiElement.h](rts/aGui/GuiElement.h): Core UI element interface and layout hooks.
- [rts/aGui/Button.cpp](rts/aGui/Button.cpp): Button widget rendering and interaction.
- [rts/aGui/Button.h](rts/aGui/Button.h): Button widget interface.
- [rts/aGui/Window.cpp](rts/aGui/Window.cpp): Window/container widget rendering.
- [rts/aGui/Window.h](rts/aGui/Window.h): Window widget interface.
- [rts/aGui/TextElement.cpp](rts/aGui/TextElement.cpp): UI text element rendering and font binding.
- [rts/aGui/TextElement.h](rts/aGui/TextElement.h): Text element interface.
- [rts/aGui/Picture.cpp](rts/aGui/Picture.cpp): UI image rendering with backend textured batches.
- [rts/aGui/HorizontalLayout.cpp](rts/aGui/HorizontalLayout.cpp): Horizontal layout container.
- [rts/aGui/HorizontalLayout.h](rts/aGui/HorizontalLayout.h): Horizontal layout interface.
- [rts/aGui/VerticalLayout.cpp](rts/aGui/VerticalLayout.cpp): Vertical layout container.
- [rts/aGui/VerticalLayout.h](rts/aGui/VerticalLayout.h): Vertical layout interface.
- [rts/aGui/Layout.h](rts/aGui/Layout.h): Shared layout helpers and sizing rules.

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

## Lua Scripting

- [rts/Lua/LuaIntro.cpp](rts/Lua/LuaIntro.cpp): Lua intro screen script integration.
- [rts/Lua/LuaHandleSynced.cpp](rts/Lua/LuaHandleSynced.cpp): Synced Lua state management for deterministic simulation.
- [rts/Lua/LuaSyncedCtrl.cpp](rts/Lua/LuaSyncedCtrl.cpp): Synced Lua control API exposed to scripts.
- [rts/Lua/LuaSyncedCtrl.h](rts/Lua/LuaSyncedCtrl.h): Synced control interface.
- [rts/Lua/LuaUnsyncedRead.h](rts/Lua/LuaUnsyncedRead.h): Unsynced Lua read accessors (UI and client-only data).
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
