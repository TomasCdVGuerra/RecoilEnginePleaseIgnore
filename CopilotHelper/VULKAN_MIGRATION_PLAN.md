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
