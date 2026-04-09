# Vulkan Migration Plan (MoltenVK on macOS)

## Scope
This document captures the OpenGL surface area in CMake and the incremental steps for introducing a Vulkan backend (MoltenVK on macOS) without breaking existing OpenGL and Headless paths.

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

## Phase 2: Abstraction Strategy
This phase introduces backend-neutral interfaces so existing render systems can depend on abstractions instead of OpenGL wrappers directly.

### Coupling Observed In Current GL Wrappers
1. `VBO.h`
- Public API and storage state use OpenGL types (`GLenum`, `GLuint`, `GLsizeiptr`, `GLbitfield`) directly.
- Behavior is target-centric (`GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`) and range-binding uses GL binding points.
- Mapping/upload workflow is explicitly GL map/unmap + bind/unbind oriented.

2. `FBO.h`
- Attachment APIs are GL-attachment/token based (`GL_COLOR_ATTACHMENT*`, texture targets, renderbuffers).
- Context-loss handling persists attachment contents with GL readback/reload mechanics.
- Framebuffer validity/status is represented with GL status enums.

3. `RenderBuffers.h`
- Render submission path directly calls GL draw and vertex attribute functions (`glDraw*`, `glVertexAttribPointer`, etc.).
- Stream-buffer creation is keyed by GL buffer targets and GL-centric lifecycle.
- Shader-path details and VAO setup are tightly coupled to current GL attribute semantics.

### Phase 2 Goals
1. Remove OpenGL typedefs and tokens from public rendering abstractions.
2. Keep ownership RAII-based (`std::unique_ptr`) and data flow span-based (`std::span`).
3. Allow an OpenGL backend adapter first, then a Vulkan backend, with identical high-level contracts.
4. Keep this phase non-invasive: introduce interfaces and adapters before call-site rewrites.

### Proposed Core Interfaces (Draft)
```cpp
namespace gfx {

enum class BackendType {
	OpenGL,
	Vulkan,
};

enum class BufferUsage {
	Static,
	Dynamic,
	Stream,
};

enum class MemoryAccess {
	GpuOnly,
	CpuToGpu,
	GpuToCpu,
};

enum class TextureDimension {
	Tex2D,
	Tex2DArray,
	Tex3D,
	Cube,
};

enum class PixelFormat {
	Unknown,
	RGBA8_UNorm,
	BGRA8_UNorm,
	R8_UNorm,
	D24S8,
	D32_SFloat,
};

enum class TextureUsage : uint32_t {
	Sampled      = 1u << 0,
	RenderTarget = 1u << 1,
	DepthStencil = 1u << 2,
	TransferSrc  = 1u << 3,
	TransferDst  = 1u << 4,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
{
	return static_cast<TextureUsage>(
		static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
	);
}

struct Extent3D {
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t depth = 1;
};

struct BufferCreateInfo {
	size_t sizeBytes = 0;
	BufferUsage usage = BufferUsage::Dynamic;
	MemoryAccess memoryAccess = MemoryAccess::CpuToGpu;
	bool readable = false;
	std::string debugName;
};

struct TextureCreateInfo {
	TextureDimension dimension = TextureDimension::Tex2D;
	PixelFormat format = PixelFormat::RGBA8_UNorm;
	Extent3D extent;
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
	TextureUsage usage = TextureUsage::Sampled;
	std::string debugName;
};

class IVertexBuffer {
public:
	virtual ~IVertexBuffer() = default;

	[[nodiscard]] virtual size_t SizeBytes() const noexcept = 0;
	[[nodiscard]] virtual BufferUsage Usage() const noexcept = 0;
	[[nodiscard]] virtual bool IsMappable() const noexcept = 0;

	virtual void Resize(size_t newSizeBytes, bool preserveData) = 0;
	virtual void Update(std::span<const std::byte> src, size_t dstOffsetBytes = 0) = 0;

	[[nodiscard]] virtual std::span<std::byte> MapWrite(size_t offsetBytes, size_t sizeBytes) = 0;
	virtual void UnmapWrite() = 0;
};

class ITexture {
public:
	virtual ~ITexture() = default;

	[[nodiscard]] virtual TextureDimension Dimension() const noexcept = 0;
	[[nodiscard]] virtual PixelFormat Format() const noexcept = 0;
	[[nodiscard]] virtual Extent3D GetExtent() const noexcept = 0;
	[[nodiscard]] virtual uint32_t GetMipLevels() const noexcept = 0;

	virtual void Upload(
		uint32_t mipLevel,
		uint32_t arrayLayer,
		std::span<const std::byte> pixels,
		size_t rowPitchBytes = 0,
		size_t slicePitchBytes = 0
	) = 0;

	virtual void GenerateMipmaps() = 0;
};

class IGraphicsBackend {
public:
	virtual ~IGraphicsBackend() = default;

	[[nodiscard]] virtual BackendType Type() const noexcept = 0;
	[[nodiscard]] virtual std::string_view Name() const noexcept = 0;
	[[nodiscard]] virtual bool SupportsPersistentMapping() const noexcept = 0;

	[[nodiscard]] virtual std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo& ci) = 0;
	[[nodiscard]] virtual std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo& ci) = 0;

	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void DeviceWaitIdle() = 0;
};

} // namespace gfx
```

### Incremental Adoption Path
1. Add interfaces and backend-agnostic enums/types in a new rendering abstraction module.
2. Implement OpenGL adapters (`GLGraphicsBackend`, `GLVertexBuffer`, `GLTexture`) over existing `VBO`/texture code.
3. Update selected call sites to request resources via `IGraphicsBackend` rather than constructing GL wrappers directly.
4. Introduce Vulkan implementations matching the same contracts.
5. Defer command-buffer and render-pass abstraction details until backend-independent resource ownership is proven in-engine.

## Integrated Roadmap Update (MoltenVK on macOS)

This update augments the existing plan starting at Phase 3 and forward, without repeating the already documented introduction, scope, and Phase 1/2 technical details.

### Execution Status Snapshot
1. `[COMPLETE]` Phase 1: Scaffolding.
2. `[COMPLETE]` Phase 2: Abstraction Strategy and Interfaces.
3. `[COMPLETE]` Phase 3: Backend Injection.
4. `[PENDING]` Phase 4: Bottom-Up Migration (Leaf Nodes).
5. `[PENDING]` Phase 5: Mid-Level Systems Migration.
6. `[PENDING]` Phase 6: Core Rendering Systems Migration.
7. `[PENDING]` Phase 7: Vulkan Backend Implementation.
8. `[PENDING]` Phase 8: Build System Flip and Validation.

### [COMPLETE] Phase 3: Backend Injection
Goal: Make the new abstraction available to the engine safely.

1. `[x]` Inject `std::unique_ptr<gfx::IGraphicsBackend> graphicsBackend;` into `GlobalRendering`.
2. `[x]` Instantiate `GLGraphicsBackend` after valid OpenGL context creation.
3. `[x]` Handle clean teardown/reset during shutdown paths.

Verification completed for this phase:
1. `cmake --build build --target engine-headless --parallel $(sysctl -n hw.logicalcpu)`
2. `cmake --build build --target engine_smoketest --parallel $(sysctl -n hw.logicalcpu)`
3. `ctest --test-dir build --output-on-failure -R smoketest`

### [PENDING] Phase 4: Bottom-Up Migration (Leaf Nodes)
Goal: Migrate the simplest rendering subsystems (least dependencies) to use `graphicsBackend` instead of direct OpenGL wrappers.

Coupling observed:
1. Debug drawers (`LineDrawer.cpp`, `DebugVisibilityDrawer.cpp`): directly construct `VBO` objects and call `glDrawArrays` / `glDrawElements`; relatively isolated from lighting/shadow passes.
2. Font rendering (`glFontRenderer.cpp`, `CFontTexture.cpp`): direct texture atlas management via `glTexImage2D` and `glBindTexture`; mostly 2D/orthographic.
3. UI backend (`RmlUi_Renderer_GL3_Recoil.cpp`): hardcoded GL3 render interface path; self-contained geometry + texture sampling.

Incremental adoption path:
1. Update `LineDrawer` to allocate a `gfx::IVertexBuffer` from `globalRendering->graphicsBackend`.
2. Introduce `gfx::IIndexBuffer` (parallel to `IVertexBuffer`) for indexed UI geometry and batched draws.
3. Rewrite the RmlUi renderer backend path so `RenderGeometry` uses `gfx` interfaces only.

#### [COMPLETE] Phase 4.1: LineDrawer Migration
Scouting findings:
1. `LineDrawer` does not currently use `VBO.h`; it uses CPU-side float arrays and legacy client-state OpenGL submission.
2. Submission path is fixed-function and directly issues `glColorPointer`, `glVertexPointer`, and `glDrawArrays` in `DrawAll()`.
3. Stippled and non-stippled line batches are emitted in separate passes with direct GL state toggles.

Proposed refactor shape:
1. Keep the existing path-building API (`StartPath`, `DrawLine`, `Break`, `Restart`) unchanged to minimize call-site churn.
2. Replace per-batch split arrays (`verts` + `colors`) with packed transient upload buffers during `DrawAll()`, using an internal packed vertex layout: `position(float3) + color(float4)`.
3. Add two backend-owned dynamic buffers in `LineDrawer`:
	- `lineVertexBuffer` for solid line batches
	- `stippleVertexBuffer` for stippled line batches
4. Lazily instantiate each via `globalRendering->graphicsBackend->CreateVertexBuffer(...)` with dynamic CPU-to-GPU usage settings.
5. On each `DrawAll()`:
	- flatten queued `LinePair` data into packed contiguous CPU vectors
	- call `Resize(...)` only if capacity is insufficient
	- upload with either `MapWrite/UnmapWrite` or `Update(...)` depending on mappability and frame size
6. Preserve line-strip vs line-list behavior by tracking batch ranges and draw modes during flattening.

Required abstraction extension (to avoid GL leakage at call-sites):
1. Introduce a minimal line draw submission entry-point on `gfx::IGraphicsBackend`, for example a `DrawLineBatches(...)` style method that accepts:
	- vertex buffer resource reference
	- batch spans (mode, first vertex, count)
	- line-stipple enable flag and stipple params
2. Implement this in `GLGraphicsBackend` using existing OpenGL calls internally.
3. Keep all GL state toggles and fixed-function compatibility inside the backend implementation, not inside `LineDrawer`.

Migration acceptance criteria for this step:
1. `LineDrawer` no longer calls raw `glDrawArrays`, `glColorPointer`, or `glVertexPointer` directly.
2. `LineDrawer` owns only `gfx` abstractions and CPU staging vectors.
3. Visual parity remains for both normal and stippled command lines.

#### [COMPLETE] Phase 4.2: Font Rendering Migration
Target files:
1. `rts/Rendering/Fonts/CFontTexture.cpp`
2. `rts/Rendering/Fonts/glFontRenderer.cpp`

Texture-coupling findings (`CFontTexture.cpp`):
1. Texture lifecycle is raw OpenGL handle based (`glyphAtlasTextureID`), created in `CreateTexture(...)` with `glGenTextures` and destroyed in `~CFontTexture()` via `glDeleteTextures`.
2. Atlas texture state setup is direct GL: `glBindTexture`, `glTexParameteri`, `glTexParameterfv`, `glTexParameteriv`, and format-specific `glTexImage2D` allocation.
3. Glyph atlas upload path is full-texture replacement in `UploadGlyphAtlasTextureImpl()` using `glTexImage2D` (not sub-region upload), after CPU-side atlas merge/update bookkeeping.

Atlas update mechanism findings:
1. `LoadGlyph(...)` rasterizes glyphs to CPU bitmaps and queues atlas entries via allocator metadata (`atlasAlloc`, `glyphNameToIdx`, `atlasGlyphs`).
2. `LoadWantedGlyphs(...)` composes pending glyph bitmaps into `atlasUpdate` and `atlasUpdateShadow`, tracks blur rectangles for outline expansion, and increments update generation counters.
3. `UpdateGlyphAtlasTexture()` merges shadow atlas into main atlas and marks upload-required state (`needsTextureUpload`), then `UploadGlyphAtlasTexture()` triggers renderer-mediated upload.

Vertex/draw submission findings (`glFontRenderer.cpp`):
1. Shader path uses `TypedRenderBuffer<VA_TYPE_TC>` and issues indexed draws via GL-backed render-buffer wrappers (`DrawElements(GL_TRIANGLES)`).
2. Legacy/no-shader path uses raw CPU vectors and fixed-function client arrays (`glVertexPointer`, `glTexCoordPointer`, `glColorPointer`, `glDrawRangeElements`).
3. Font renderer state is GL-coupled (`glBindTexture`, blend/depth toggles, client state enable/disable, program bind/restore).

Phase 4.2 implication summary:
1. Texture creation can migrate to `graphicsBackend->CreateTexture(...)`, but incremental atlas updates require a region/offset-aware upload API extension beyond current `ITexture::Upload(...)` semantics.
2. Font draw submission will require backend API support for textured indexed geometry (and texture binding/state control) to remove residual GL calls from font renderer implementations.

#### [IN PROGRESS] Phase 4.3: UI Backend Scouting
Target files:
1. `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.h`
2. `rts/Rml/Backends/RmlUi_Renderer_GL3_Recoil.cpp`

Geometry buffering findings (`CompileGeometry`, `RenderGeometry`, `ReleaseGeometry`):
1. `CompileGeometry(...)` allocates per-handle GL resources (`VAO`, `VBO` array buffer, `VBO` element buffer), uploads static data with `GL_STATIC_DRAW`, and configures vertex attributes with `glEnableVertexAttribArray` and `glVertexAttribPointer`.
2. `RenderGeometry(...)` binds shader program variants (`ProgramId::Texture` / `ProgramId::Color`), binds texture via raw `GLuint` cast from `Rml::TextureHandle`, binds VAO, and submits `glDrawElements(GL_TRIANGLES, ..., GL_UNSIGNED_INT, nullptr)`.
3. `ReleaseGeometry(...)` explicitly destroys the VAO/VBO/IBO wrappers and frees the compiled handle.

Texture management findings (`LoadTexture`, `GenerateTexture`, `ReleaseTexture`):
1. `LoadTexture(...)` delegates to `CBitmap::Load` and `CBitmap::CreateTexture`, returning raw OpenGL texture ids through `Rml::TextureHandle`.
2. `GenerateTexture(...)` directly uses `glGenTextures`, `glBindTexture`, `glTexImage2D`, `glTexParameteri`, and returns the generated `GLuint` as `Rml::TextureHandle`.
3. `ReleaseTexture(...)` directly destroys texture ids with `glDeleteTextures`; `SaveLayerAsTexture()` and postprocess paths also manipulate raw texture ids via `glCopyTexSubImage2D` and framebuffer color attachments.

Shader and state management findings:
1. Shader programs are GL-coupled: `CreateShaders(...)` compiles/links GLSL via `ShaderHandler`, and `UseProgram(...)` toggles program enable/disable plus uniform submission (`SubmitTransformUniform(...)`).
2. Frame begin/end explicitly backup and restore broad GL state (`glIsEnabled`, `glGetIntegerv`, blend/stencil/scissor/depth state), and resolve MSAA using `glBlitFramebuffer`.
3. Clip/scissor behavior is directly GL-driven via `glScissor`, `glEnable(GL_SCISSOR_TEST)`, `glStencilFunc`, and `glStencilOp`; postprocess filters and layers rely on GL framebuffer/texture binding transitions.

Phase 4.3 implication summary:
1. UI textures require handle indirection: map `Rml::TextureHandle` to backend-owned texture records rather than passing raw `GLuint` ids.
2. Compiled geometry maps naturally to backend buffers (vertex + index), with `Rml::Vertex` layout translated into `TexturedVertexLayout` and index type `UInt32`.
3. Short-lived fullscreen/filter quads should use a shared dynamic streaming path (or cached reusable geometry) to avoid frequent create/free churn of static GPU resources.
4. Full removal of GL from this backend also requires framebuffer/render-target abstractions beyond current scope; those dependencies should be staged with Phase 5/6 interfaces.

### [PENDING] Phase 5: Mid-Level Systems Migration
Goal: Migrate complex geometry, texture streaming, and particle systems.

Sub-status:
1. `[COMPLETE]` Phase 5.3: Particle System Scouting.

Coupling observed:
1. Textures (`Texture.cpp`, `Bitmap.cpp`, `AtlasedTexture.cpp`): heavy usage of `glGenerateMipmap`, `glTexParameteri`, and PBO-based upload paths.
2. Models (`3DModel.cpp`, `LocalModel.cpp`): tight VAO coupling for attribute layouts (position/normal/UV).
3. Particles (`ProjectileDrawer.cpp`): large dynamic VBO streaming with strict CPU->GPU throughput requirements.

Incremental adoption path:
1. Introduce `gfx::IVertexArray` or backend-neutral vertex layout descriptors to replace direct `glVertexAttribPointer` paths.
2. Refactor `Texture.cpp` toward manager-style ownership returning `gfx::ITexture` instances instead of raw GL handles.
3. Validate particle streaming throughput and frame pacing using `gfx::IVertexBuffer::MapWrite` across stress scenarios.

#### [COMPLETE] Phase 5.1: Texture Management Migration
Target files:
1. `rts/Rendering/Textures/Texture.hpp` (header counterpart to requested `Texture.h`)
2. `rts/Rendering/Textures/Texture.cpp`
3. `rts/Rendering/Textures/Bitmap.cpp`

OpenGL coupling findings (`Texture.cpp`, `Texture.hpp`):
1. Texture object lifecycle is GL-id based: `glGenTextures` during initialization and `glDeleteTextures` in `TextureBase` destruction/move ownership paths.
2. Binding model is explicit GL state mutation (`glActiveTexture`, `glBindTexture`) in `ScopedBind`, `Bind`, and `Unbind` methods.
3. Allocation/upload path is direct GL: `glTexImage2D` and `glTexImage3D` (fallback path) or `glTexStorage2D`/`glTexStorage3D` when available; sub-updates use `glTexSubImage2D`/`glTexSubImage3D`.
4. Mipmap generation is explicit `glGenerateMipmap` with AMD workaround toggles.
5. Sampler parameters are directly applied with `glTexParameteri`, `glTexParameterf`, and `glTexParameterfv` (wrap, min/mag filters, border color, lod bias, anisotropy).

OpenGL coupling findings (`Bitmap.cpp`):
1. `CBitmap::CreateTexture(...)` creates/binds raw texture ids, applies wrap/filter/lod/anisotropy with `glTexParameteri`/`glTexParameterf`, and uploads through `RecoilBuildMipmaps(...)`.
2. `RecoilBuildMipmaps(...)` (in `myGL.cpp`) uploads base and mip levels via `glTexImage2D`, sets base/max level via `glTexParameteri`, then calls `glGenerateMipmap`.
3. DDS upload path (`CreateDDSTexture`) binds target-specific texture objects and applies per-target lod bias/anisotropy plus optional mipmap generation.

PBO usage assessment:
1. No direct `GL_PIXEL_UNPACK_BUFFER` usage appears in `Texture.cpp`, `Texture.hpp`, or `Bitmap.cpp`.
2. Current upload paths are CPU pointer uploads (`glTexImage*` / `glTexSubImage*`) and helper wrappers; async unpack-buffer staging is not currently wired in these target files.

Phase 5.1 implication summary:
1. Texture abstractions still embed binding-state behavior and sampler configuration in GL-specific classes, requiring decomposition into backend resource + sampler state descriptors.
2. A complete migration will need backend-neutral texture parameter application (filtering/wrap/lod/aniso) in addition to upload/mipmap APIs.
3. Optional async upload optimization can be introduced later through backend staging mechanisms rather than preserving direct PBO dependencies.

#### [COMPLETE] Phase 5.2: 3D Model Migration
Target files:
1. `rts/Rendering/Models/3DModel.hpp` (header counterpart to requested `3DModel.h`)
2. `rts/Rendering/Models/3DModel.cpp`
3. `rts/Rendering/Models/LocalModel.hpp` (header counterpart to requested `LocalModel.h`)
4. `rts/Rendering/Models/LocalModel.cpp`

Container-level coupling findings (`3DModel.cpp`, `LocalModel.cpp`):
1. `S3DModel::DrawStatic()` is still legacy-path coupled via `S3DModelHelpers::BindLegacyAttrVBOs()` / `UnbindLegacyAttrVBOs()` and per-piece legacy draws (`DrawStaticLegacy`).
2. `LocalModel.cpp` itself contains no direct VAO/VBO setup calls; draw fanout goes through `LocalModelPiece::Draw()` / `DrawLOD()`.
3. Actual OpenGL submission and vertex-state setup for these models is delegated to model-subsystem helpers (`3DModelPiece.cpp`, `LocalModelPiece.cpp`, `3DModelVAO.cpp`) rather than in the two container files directly.

VAO/VBO lifecycle and binding findings (model subsystem path):
1. `S3DModelVAO` owns persistent model buffers (`vertVBO`, `indxVBO`, `instVBO`) and a `VAO`; constructor preallocates instance-buffer storage with `VBO::New(..., GL_STREAM_DRAW)`.
2. `S3DModelVAO::CreateVAO()` binds VAO + VBO/IBO, programs attribute state, then unbinds and disables attrib arrays.
3. Raw object creation/binding is wrapped but still OpenGL-bound: `VAO::Generate()` -> `glGenVertexArrays`, `VAO::Bind()` -> `glBindVertexArray`; `VBO::Generate()` -> `glGenBuffers`, `VBO::Bind()` -> `glBindBuffer`.

Vertex attribute layout findings (`3DModelVAO::EnableAttribs`):
1. Vertex stream (`SVertexData`, divisor 0):
	- location 0: `float3` position (`pos`)
	- location 1: `float3` normal (`normal`)
	- location 2: `float3` tangent (`sTangent`)
	- location 3: `float3` bitangent (`tTangent`)
	- location 4: `float4` packed UV channels (`texCoords[0].xy` + `texCoords[1].xy`)
	- location 5: integer packed bone block (`uvec3` via `glVertexAttribIPointer`, sourced at `boneIDsLow` offset)
2. Instance stream (`SInstanceData`, divisor 1):
	- location 6: integer `uvec4` payload (`matOffset`, `uniOffset`, packed info bytes, `bposeMatOffset`)

Draw submission findings:
1. Piece draw path resolves to `S3DModelVAO::DrawElements()` -> `glDrawElements(prim, count, GL_UNSIGNED_INT, ...)`.
2. Batched model submission uses `glMultiDrawElementsIndirect(..., GL_UNSIGNED_INT, ...)` for both batched and immediate paths.
3. `LocalModelPiece::DrawLOD()` still supports display-list fallback (`glCallList`) when an LOD list exists; otherwise it uses the same indexed path.
4. No `glDrawArrays` usage was found in this model geometry path.

Texture and shader binding findings:
1. No direct `glBindTexture` or shader program bind/use calls are present in `3DModel.cpp`, `3DModel.hpp`, `LocalModel.cpp`, or `LocalModel.hpp`.
2. Model classes are primarily geometry/transform containers; texture and shader binding responsibility remains in higher-level rendering state/drawer systems.

Phase 5.2 implication summary:
1. The critical GL lock-in for model migration is VAO attribute-state declaration and indirect indexed submission APIs.
2. Existing `S3DModelVAO` already centralizes geometry ownership, making it a practical pivot point for introducing backend-neutral vertex-layout and draw-command abstractions.
3. Legacy fixed-function helpers (`BindLegacyAttrVBOs`, matrix stack usage in piece draw calls, display-list LOD fallback) must be staged behind backend-compatible compatibility paths during migration.

#### [COMPLETE] Phase 5.3: Particle System Scouting
Target files:
1. `rts/Rendering/Env/Particles/ProjectileDrawer.h`
2. `rts/Rendering/Env/Particles/ProjectileDrawer.cpp`
3. `rts/Sim/Projectiles/ExpGenSpawnable.cpp`
4. `rts/Rendering/GL/RenderBuffers.h`
5. `rts/Rendering/GL/StreamBuffer.h`

OpenGL coupling findings (vertex data upload path):
1. Particle/projectile quads are generated on CPU each frame through `CProjectile::Draw()` and derived classes, funneled via `CExpGenSpawnable::AddEffectsQuadImpl(...)` into `TypedRenderBuffer<VA_TYPE_PROJ>` (`CExpGenSpawnable::GetPrimaryRenderBuffer()`).
2. Submission in `ProjectileDrawer` is centralized: `DrawAlpha()`, `DrawShadowTransparent()`, and `DrawGroundFlashes()` all flush the same buffer using `rb.DrawElements(GL_TRIANGLES)`.
3. `TypedRenderBuffer<VA_TYPE_PROJ>::DrawElements()` calls `UploadVBO()` and `UploadEBO()` before drawing; these call `IStreamBuffer::Map(...)` and `Unmap()` each frame for newly appended ranges.
4. `IStreamBuffer` implementation is selected by `SB_AUTODETECT` and runtime capabilities:
	- `PersistentMapImpl` / `MapAndSyncImpl` / `MapAndOrphanImpl` paths use `glMapBufferRange` (+ flush/unmap/fencing where needed).
	- fallback `BufferSubDataImpl` uses `glBufferSubData`.
5. This is not a legacy client-array path for particle geometry; there is no `glVertexPointer`/`glColorPointer` in the `VA_TYPE_PROJ` submission flow.

OpenGL coupling findings (draw mechanism):
1. Main particle/projectile rendering uses indexed triangle draws (`glDrawElements`) through `TypedRenderBuffer<VA_TYPE_PROJ>::DrawElements(...)`.
2. Minimap projectile overlays use non-indexed draws (`glDrawArrays`) through `TypedRenderBuffer<VA_TYPE_C>::DrawArrays(...)`.
3. Model-based projectiles still go through model draw paths (`DrawProjectileModel`), including legacy matrix-stack and model-helper rendering for pieces/weapon models.

OpenGL coupling findings (texture binding and blend state):
1. `DrawAlpha()` binds atlas texture on unit 0 (`textureAtlas->BindTexture()`), optionally binds copied depth texture on unit 15 for soft particles, and uses shader flags/uniforms to control soft clipping.
2. Transparent particle pass blend state is explicit: `BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`.
3. `DrawGroundFlashes()` binds `groundFXAtlas`, uses additive blending (`BlendFunc(GL_SRC_ALPHA, GL_ONE)`), and conditionally toggles depth test/depth write per flash while flushing batched geometry between state changes.
4. `DrawShadowTransparent()` uses multiplicative blend for shadow color filtering (`BlendFunc(GL_ZERO, GL_SRC_COLOR)`) with atlas-bound textured quads.
5. Additional GL texture management remains in this subsystem for perlin/noise updates (`glTexSubImage2D`, `glBindTexture`) and perlin blend textures setup.

Phase 5.3 implication summary:
1. Particle rendering already has a centralized batched stream (`TypedRenderBuffer<VA_TYPE_PROJ>`), making it a good migration seam for replacing GL stream buffers with `gfx::IVertexBuffer` updates.
2. Current draw path requires an indexed textured-batch backend API equivalent to `DrawElements` over dynamic vertex+index streams (multiple submit points per frame).
3. Blend/depth/texture-unit state is currently interleaved with submission in `ProjectileDrawer`; migration should separate state descriptors from geometry submission to keep backend boundaries clean.

### [PENDING] Phase 6: Core Rendering Systems Migration
Goal: Abstract the most tightly coupled OpenGL systems: framebuffers, shaders, and map rendering.

Sub-status:
1. `[COMPLETE]` Phase 6.1: Framebuffer Abstraction.
2. `[COMPLETE]` Phase 6.2: Shader Abstraction.

Coupling observed:
1. Framebuffers (`FBO.h`, `RenderBuffers.cpp`): hardcoded `GL_COLOR_ATTACHMENT*`, depth/stencil setup, and `glBlitFramebuffer` logic.
2. Shaders (`Shader.cpp`, `ShaderHandler.cpp`): direct GLSL compile/link path (`glCreateShader`) and uniform lookups (`glGetUniformLocation`).
3. Map rendering (`SMFGroundDrawer.cpp`, `WaterRendering.cpp`): multi-pass heavy GL-state usage and engine-specific state macros.

Incremental adoption path:
1. Design `gfx::IFramebuffer` and `gfx::IRenderTarget` and provide OpenGL adapters first.
2. Introduce `gfx::IShaderProgram` with backend-specific compilation/reflect paths.
3. Ensure shader abstraction supports GLSL source for GL backend and SPIR-V bytecode for Vulkan backend.
4. Refactor map rendering to render against `gfx::IFramebuffer` surfaces instead of direct `FBO` dependencies.

#### [COMPLETE] Phase 6.1: Framebuffer Abstraction
Target files:
1. `rts/Rendering/GL/FBO.h`
2. `rts/Rendering/GL/FBO.cpp`

OpenGL coupling findings (framebuffer creation and binding):
1. `FBO::Init(...)` directly queries `GL_MAX_COLOR_ATTACHMENTS_EXT`, allocates GL objects via `glGenFramebuffersEXT`, and performs first-time bind/unbind through `glBindFramebufferEXT`.
2. `FBO::Bind()` and static `FBO::Unbind()` directly mutate GL framebuffer binding state (`glBindFramebufferEXT(..., fboId/0)`).
3. `FBO::GetCurrentBoundFBO()` reads current draw framebuffer binding through `glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, ...)`.

OpenGL coupling findings (attachment APIs):
1. Texture attachment paths are direct GL calls (`glFramebufferTexture1DEXT`, `glFramebufferTexture2DEXT`, `glFramebufferTexture3DEXT`, `glFramebufferTexture`, `glFramebufferTextureLayerEXT`) and use raw texture ids (`GLuint`).
2. Renderbuffer attachment paths directly use `glFramebufferRenderbufferEXT` and `glRenderbufferStorage*EXT` after `glGenRenderbuffersEXT`.
3. Attachment teardown (`Detach`, `DetachAll`) inspects and clears attachment object types via `glGetFramebufferAttachmentParameterivEXT` and unbinds either texture or renderbuffer attachments.

OpenGL coupling findings (MRT draw-target setup):
1. `FBO` manages attachment objects and limits (`maxAttachments`) but does not configure active color draw targets via `glDrawBuffers`.
2. MRT routing is currently configured by higher-level passes after attachments are set (for example, `rts/Rendering/GL/GeometryBuffer.cpp` calls `glDrawBuffers(...)`).

OpenGL coupling findings (status checks and blit/resolve):
1. FBO validity checks are GL-status based (`glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT)`) with direct mapping of incomplete/unsupported codes in `FBO::CheckStatus(...)`.
2. `FBO::Blit(...)` uses explicit read/draw bindings (`glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, ...)`, `glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, ...)`) and resolve/copy through `glBlitFramebufferEXT`.
3. Blit path is hard-gated by extension availability (`GLAD_GL_EXT_framebuffer_blit`) and rectangle validity checks, then restores prior framebuffer binding.

Phase 6.1 implication summary:
1. Framebuffer creation, attachment, and blit behavior can be abstracted behind backend-neutral interfaces, but current APIs must stop exposing raw `GLuint` texture/renderbuffer ownership.
2. MRT configuration should become an explicit backend API call on a render-target descriptor rather than a direct `glDrawBuffers` call in pass code.
3. Context-loss restore logic in `FBO` (attachment readback/reupload) should be reviewed as a separate responsibility from core framebuffer binding/attachment API during interface design.

#### [COMPLETE] Phase 6.2: Shader Abstraction
Target files:
1. `rts/Rendering/Shaders/Shader.h`
2. `rts/Rendering/Shaders/Shader.cpp`
3. `rts/Rendering/Shaders/ShaderHandler.h`
4. `rts/Rendering/Shaders/ShaderHandler.cpp`

OpenGL coupling findings (program creation/link/bind):
1. Shader object compilation is direct GLSL runtime text compile: `glCreateShader`, `glShaderSource`, and `glCompileShader` in `GLSLShaderObject::CompileShaderObject()`.
2. Program objects are created with `glCreateProgram` and linked by attaching compiled shader objects (`glAttachShader`) followed by `glLinkProgram` in `GLSLProgramObject::Reload(...)`.
3. Program binding/unbinding is direct global GL state mutation via `glUseProgram(objID)` / `glUseProgram(0)` in `GLSLProgramObject::EnableRaw()` / `DisableRaw()`.
4. `CShaderHandler` currently hardcodes GLSL construction paths (`new Shader::GLSLProgramObject`, `new Shader::GLSLShaderObject`) and keeps a GL object-ID cache keyed by source hash.

OpenGL coupling findings (uniform model):
1. Uniform locations are resolved with `glGetUniformLocation` and cached in `UniformState` entries keyed by hashed uniform names.
2. Uniform uploads are immediate `glUniform*` calls (`glUniform1i/f`, `glUniform2/3/4*`, matrix variants), guarded by `UniformState` change checks to avoid redundant submission.
3. Legacy index-based uniform APIs are still present (`SetUniformLocation` + `SetUniformXi/Xf` by index) and map through `uniformLocs` into the same cached-uniform path.
4. There is no active UBO binding/upload path in these files (no `glGetUniformBlockIndex`, `glUniformBlockBinding`, or `glBindBufferBase` usage for submission); UBO presence is only detected during validation via `GL_UNIFORM_BLOCK_INDEX` so warnings can ignore block uniforms.

OpenGL coupling findings (vertex attributes and outputs):
1. Attribute bindings are name/index maps collected before link and applied by `glBindAttribLocation`.
2. Fragment-output bindings are similarly applied by `glBindFragDataLocation` before link.
3. Debug validation checks attribute location results through `glGetAttribLocation` after successful link.

OpenGL coupling findings (sampler texture binding):
1. Texture units are managed in-program via `IProgramObject::AddTextureBinding` and stored as `LuaMatTexture` bindings indexed by texture unit.
2. Runtime binding/unbinding uses `glActiveTexture(GL_TEXTURE0 + relSlot)` plus `LuaMatTexture::Bind/Unbind`, then restores active unit 0.
3. Sampler uniform assignment still relies on regular uniform APIs (`SetUniform*`) and is not represented as a backend-neutral descriptor set model.

Phase 6.2 implication summary:
1. Shader and program lifecycle must move behind a backend abstraction while keeping current runtime-GLSL compile support for OpenGL.
2. Uniform handling should remain API-compatible short-term (name-based and index-based entry points) but be internally redirected toward backend-neutral parameter/block submission.
3. `CShaderHandler` should remain as a high-level shader registry/reload/cache manager, but its concrete object creation should be delegated to `gfx::IGraphicsBackend` instead of hardcoded GLSL types.

### [PENDING] Phase 7: Vulkan Backend Implementation
Goal: Implement Vulkan-native versions of `gfx` interfaces to run on macOS via MoltenVK.

Implementation strategy:
1. Initialization: implement `VulkanGraphicsBackend` for `VkInstance`, `VkDevice`, swapchain/surface setup (including MoltenVK/macOS integration).
2. Memory management: integrate VMA and map `gfx::BufferUsage` and `MemoryAccess` policies to Vulkan memory and staging strategy.
3. Buffers and textures: implement `VulkanVertexBuffer` and `VulkanTexture`, including image layout transitions and staging copies.
4. Command recording: introduce `gfx::ICommandBuffer` abstraction (no-op / implicit behavior for GL backend, explicit for Vulkan).
5. Pipelines and shaders: map `gfx::IShaderProgram` to Vulkan PSO concepts; compile GLSL to SPIR-V (runtime or build-time pipeline).

### [PENDING] Phase 8: Build System Flip and Validation
Goal: Make Vulkan the primary rendering path on Apple Silicon while keeping rollback-safe behavior.

1. Wire `ENABLE_VULKAN=ON` path to instantiate `VulkanGraphicsBackend` in `GlobalRendering`.
2. Validate headless and CI compliance under Vulkan-enabled configurations.
3. Benchmark performance and frame pacing against OpenGL baseline.
4. After stabilization, deprecate and remove legacy `rts/Rendering/GL/` wrappers in controlled cleanup waves.

### Risks and Safeguards
1. Keep OpenGL path as known-good fallback until Vulkan reaches feature parity for gameplay-critical render paths.
2. Preserve deterministic behavior for synced gameplay code by isolating rendering-only abstractions from simulation state.
3. Gate large migrations behind focused compile/test loops (`engine-headless`, `engine_smoketest`, `ctest -R smoketest`) at each phase boundary.
