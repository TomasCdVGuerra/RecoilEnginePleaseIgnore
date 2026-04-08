#include "Texture.hpp"

#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "System/Log/ILog.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace {
	gfx::FilterMode TranslateFilterMode(uint32_t filterMode)
	{
		switch (filterMode) {
			case GL_NEAREST:
				return gfx::FilterMode::Nearest;
			case GL_LINEAR:
				return gfx::FilterMode::Linear;
			case GL_NEAREST_MIPMAP_NEAREST:
				return gfx::FilterMode::NearestMipmapNearest;
			case GL_LINEAR_MIPMAP_NEAREST:
				return gfx::FilterMode::LinearMipmapNearest;
			case GL_NEAREST_MIPMAP_LINEAR:
				return gfx::FilterMode::NearestMipmapLinear;
			case GL_LINEAR_MIPMAP_LINEAR:
				return gfx::FilterMode::LinearMipmapLinear;
			default:
				return gfx::FilterMode::Linear;
		}
	}

	gfx::WrapMode TranslateWrapMode(int32_t wrapMode)
	{
		switch (wrapMode) {
			case GL_REPEAT:
				return gfx::WrapMode::Repeat;
			case GL_MIRRORED_REPEAT:
				return gfx::WrapMode::MirroredRepeat;
			case GL_CLAMP_TO_EDGE:
				return gfx::WrapMode::ClampToEdge;
			case GL_CLAMP_TO_BORDER:
				return gfx::WrapMode::ClampToBorder;
			case GL_MIRROR_CLAMP_TO_EDGE:
				return gfx::WrapMode::MirrorClampToEdge;
			default:
				return gfx::WrapMode::ClampToEdge;
		}
	}

	gfx::SamplerState BuildSamplerState(const GL::TextureCreationParams& tcp, int32_t numLevels)
	{
		gfx::SamplerState samplerState;
		samplerState.minFilter = TranslateFilterMode(tcp.GetMinFilter(numLevels));
		samplerState.magFilter = TranslateFilterMode(tcp.GetMagFilter());
		samplerState.anisotropy = tcp.aniso;
		samplerState.lodBias = tcp.lodBias;

		if (tcp.wrapModes.has_value()) {
			const auto& wrapModes = tcp.wrapModes.value();
			samplerState.wrapS = TranslateWrapMode(wrapModes[0]);
			samplerState.wrapT = TranslateWrapMode(wrapModes[1]);
			samplerState.wrapR = TranslateWrapMode(wrapModes[2]);
		}
		else {
			const auto wrapMode = TranslateWrapMode(static_cast<int32_t>(tcp.GetWrapMode()));
			samplerState.wrapS = wrapMode;
			samplerState.wrapT = wrapMode;
			samplerState.wrapR = wrapMode;
		}

		return samplerState;
	}

	gfx::PixelFormat TranslateInternalFormat(uint32_t intFormat)
	{
		switch (intFormat) {
			case GL_R8:
				return gfx::PixelFormat::R8_UNorm;
			case GL_RG8:
				return gfx::PixelFormat::RG8_UNorm;
			case GL_RGB8:
				return gfx::PixelFormat::RGB8_UNorm;
			case GL_RGBA8:
				return gfx::PixelFormat::RGBA8_UNorm;
			case GL_R16:
				return gfx::PixelFormat::R16_UNorm;
			case GL_RG16:
				return gfx::PixelFormat::RG16_UNorm;
			case GL_RGB16:
				return gfx::PixelFormat::RGB16_UNorm;
			case GL_RGBA16:
				return gfx::PixelFormat::RGBA16_UNorm;
			case GL_R32F:
				return gfx::PixelFormat::R32_SFloat;
			case GL_RG32F:
				return gfx::PixelFormat::RG32_SFloat;
			case GL_RGB32F:
				return gfx::PixelFormat::RGB32_SFloat;
			case GL_RGBA32F:
				return gfx::PixelFormat::RGBA32_SFloat;
			case GL_RGB10_A2:
				return gfx::PixelFormat::RGB10A2_UNorm;
			case GL_DEPTH24_STENCIL8:
				return gfx::PixelFormat::D24S8;
			case GL_DEPTH_COMPONENT32F:
				return gfx::PixelFormat::D32_SFloat;
			default:
				LOG_L(L_WARNING, "[Texture::TranslateInternalFormat] Unsupported internal format %u", intFormat);
				return gfx::PixelFormat::Unknown;
		}
	}

	std::size_t CalcUploadSizeBytes(uint32_t intFormat, int width, int height)
	{
		if ((width <= 0) || (height <= 0))
			return 0;

		const auto numChannels = GL::GetNumChannelsFromInternalFormat(intFormat);
		const auto dataType = GL::GetDataTypeFromInternalFormat(intFormat);
		const auto dataSize = GL::GetDataTypeSize(dataType);

		if ((numChannels == 0) || (dataSize == 0))
			return 0;

		return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
			       static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(dataSize);
	}

	std::size_t CalcRowPitchBytes(uint32_t intFormat, int width)
	{
		if (width <= 0)
			return 0;

		const auto numChannels = GL::GetNumChannelsFromInternalFormat(intFormat);
		const auto dataType = GL::GetDataTypeFromInternalFormat(intFormat);
		const auto dataSize = GL::GetDataTypeSize(dataType);

		if ((numChannels == 0) || (dataSize == 0))
			return 0;

		return static_cast<std::size_t>(width) * static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(dataSize);
	}

	std::uint32_t ToU32(int value, const char* ctx)
	{
		if (value < 0) {
			LOG_L(L_WARNING, "[Texture::%s] Negative integer (%d) converted to 0", ctx, value);
			return 0;
		}

		return static_cast<std::uint32_t>(value);
	}
}

namespace GL {
	TextureBase::~TextureBase() = default;

	auto TextureBase::GetGLId() const -> uint32_t
	{
		if (backendTexture == nullptr)
			return 0;

		const auto nativeHandle = backendTexture->GetNativeHandle();
		constexpr auto maxHandle = static_cast<std::uintptr_t>(std::numeric_limits<uint32_t>::max());

		if (nativeHandle > maxHandle) {
			LOG_L(
				L_WARNING,
				"[TextureBase::GetGLId] Native handle (%zu) exceeds uint32 range (%zu)",
				static_cast<std::size_t>(nativeHandle),
				static_cast<std::size_t>(maxHandle)
			);
			return 0;
		}

		return static_cast<uint32_t>(nativeHandle);
	}

	GL::TexBind TextureBase::ScopedBind()
	{
		auto scopedBinding = GL::TexBind(texTarget, GetGLId());
		lastBoundSlot = scopedBinding.GetLastActiveTextureSlot();
		return scopedBinding;
	}

	GL::TexBind TextureBase::ScopedBind(uint32_t relSlot)
	{
		lastBoundSlot = GL_TEXTURE0 + relSlot;
		return GL::TexBind(relSlot, texTarget, GetGLId());
	}

	void TextureBase::ScopedBind(const GL::TexBind& existingScopedBinding)
	{
		glActiveTexture(existingScopedBinding.GetLastActiveTextureSlot());
		glBindTexture(texTarget, GetGLId());
	}

	void TextureBase::Bind()
	{
		lastBoundSlot = GL::FetchActiveTextureSlot();
		glBindTexture(texTarget, GetGLId());
	}

	void TextureBase::Bind(uint32_t relSlot)
	{
		lastBoundSlot = GL_TEXTURE0 + relSlot;
		glActiveTexture(GL_TEXTURE0 + relSlot);
		glBindTexture(texTarget, GetGLId());
	}

	void TextureBase::Unbind()
	{
		glBindTexture(texTarget, 0);
		lastBoundSlot = 0;
	}

	void TextureBase::Unbind(uint32_t relSlot)
	{
		glActiveTexture(GL_TEXTURE0 + relSlot);
		glBindTexture(texTarget, 0);
		lastBoundSlot = 0;
	}

	TextureBase& TextureBase::operator=(TextureBase&& other) noexcept
	{
		backendTexture = std::move(other.backendTexture);
		std::swap(intFormat, other.intFormat);
		std::swap(numLevels, other.numLevels);
		std::swap(lastBoundSlot, other.lastBoundSlot);
		std::swap(texTarget, other.texTarget);

		return *this;
	}

	Texture2D::Texture2D(uint32_t xsize_, uint32_t ysize_, uint32_t intFormat_, const TextureCreationParams& tcp, bool wantCompress)
		: Texture2D()
	{
		(void)wantCompress;

		size = int2(xsize_, ysize_);
		intFormat = intFormat_;

		numLevels = tcp.reqNumLevels <= 0
			? static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(std::max({size.x, size.y}))))
			: tcp.reqNumLevels;
		numLevels = std::max<int32_t>(1, numLevels);
		lastBoundSlot = GL::FetchActiveTextureSlot();

		if ((globalRendering == nullptr) || (globalRendering->graphicsBackend == nullptr)) {
			LOG_L(L_WARNING, "[Texture2D::Texture2D] graphics backend unavailable");
			return;
		}

		gfx::TextureCreateInfo ci;
		ci.dimension = gfx::TextureDimension::Tex2D;
		ci.format = TranslateInternalFormat(intFormat);
		ci.extent.width = static_cast<std::uint32_t>(std::max(size.x, 1));
		ci.extent.height = static_cast<std::uint32_t>(std::max(size.y, 1));
		ci.extent.depth = 1u;
		ci.mipLevels = static_cast<std::uint32_t>(numLevels);
		ci.arrayLayers = 1u;
		ci.usage = gfx::TextureUsage::Sampled;
		ci.samplerState = BuildSamplerState(tcp, numLevels);
		ci.nativeHandle = tcp.texID;
		ci.debugName = "GL::Texture2D";

		backendTexture = globalRendering->graphicsBackend->CreateTexture(ci);
		if (backendTexture == nullptr)
			LOG_L(L_WARNING, "[Texture2D::Texture2D] Failed to create backend texture");
	}

	Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
	{
		TextureBase::operator=(static_cast<TextureBase&&>(other));
		std::swap(size, other.size);

		return *this;
	}

	void Texture2D::UploadSubImage(const void* data, int xOffset, int yOffset, int width, int height, int level) const
	{
		assert(lastBoundSlot >= GL_TEXTURE0);
		if ((backendTexture == nullptr) || (data == nullptr))
			return;

		const std::size_t uploadSizeBytes = CalcUploadSizeBytes(intFormat, width, height);
		if (uploadSizeBytes == 0)
			return;

		const std::size_t rowPitchBytes = CalcRowPitchBytes(intFormat, width);
		const auto* bytes = static_cast<const std::byte*>(data);

		backendTexture->UploadSubRegion(
			ToU32(level, "Texture2D::UploadSubImage::level"),
			0u,
			ToU32(xOffset, "Texture2D::UploadSubImage::xOffset"),
			ToU32(yOffset, "Texture2D::UploadSubImage::yOffset"),
			ToU32(width, "Texture2D::UploadSubImage::width"),
			ToU32(height, "Texture2D::UploadSubImage::height"),
			std::span<const std::byte>(bytes, uploadSizeBytes),
			rowPitchBytes
		);
	}

	void Texture2D::ProduceMipmaps() const
	{
		assert(lastBoundSlot >= GL_TEXTURE0);
		if (backendTexture == nullptr)
			return;

		backendTexture->GenerateMipmaps();
	}

	Texture2DArray::Texture2DArray(uint32_t xsize_, uint32_t ysize_, uint32_t numPages_, uint32_t intFormat_, const TextureCreationParams& tcp, bool wantCompress)
		: Texture2DArray()
	{
		(void)wantCompress;

		size = int2(xsize_, ysize_);
		numPages = numPages_;
		intFormat = intFormat_;

		numLevels = tcp.reqNumLevels <= 0
			? static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(std::max({size.x, size.y}))))
			: tcp.reqNumLevels;
		numLevels = std::max<int32_t>(1, numLevels);
		lastBoundSlot = GL::FetchActiveTextureSlot();

		if ((globalRendering == nullptr) || (globalRendering->graphicsBackend == nullptr)) {
			LOG_L(L_WARNING, "[Texture2DArray::Texture2DArray] graphics backend unavailable");
			return;
		}

		gfx::TextureCreateInfo ci;
		ci.dimension = gfx::TextureDimension::Tex2DArray;
		ci.format = TranslateInternalFormat(intFormat);
		ci.extent.width = static_cast<std::uint32_t>(std::max(size.x, 1));
		ci.extent.height = static_cast<std::uint32_t>(std::max(size.y, 1));
		ci.extent.depth = 1u;
		ci.mipLevels = static_cast<std::uint32_t>(numLevels);
		ci.arrayLayers = std::max<std::uint32_t>(1u, numPages);
		ci.usage = gfx::TextureUsage::Sampled;
		ci.samplerState = BuildSamplerState(tcp, numLevels);
		ci.nativeHandle = tcp.texID;
		ci.debugName = "GL::Texture2DArray";

		backendTexture = globalRendering->graphicsBackend->CreateTexture(ci);
		if (backendTexture == nullptr)
			LOG_L(L_WARNING, "[Texture2DArray::Texture2DArray] Failed to create backend texture");
	}

	Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept
	{
		TextureBase::operator=(static_cast<TextureBase&&>(other));
		std::swap(size, other.size);
		std::swap(numPages, other.numPages);

		return *this;
	}

	void Texture2DArray::UploadSubImage(const void* data, int layer, int xOffset, int yOffset, int width, int height, int level) const
	{
		assert(lastBoundSlot >= GL_TEXTURE0);
		if ((backendTexture == nullptr) || (data == nullptr))
			return;

		const std::size_t uploadSizeBytes = CalcUploadSizeBytes(intFormat, width, height);
		if (uploadSizeBytes == 0)
			return;

		const std::size_t rowPitchBytes = CalcRowPitchBytes(intFormat, width);
		const auto* bytes = static_cast<const std::byte*>(data);

		backendTexture->UploadSubRegion(
			ToU32(level, "Texture2DArray::UploadSubImage::level"),
			ToU32(layer, "Texture2DArray::UploadSubImage::layer"),
			ToU32(xOffset, "Texture2DArray::UploadSubImage::xOffset"),
			ToU32(yOffset, "Texture2DArray::UploadSubImage::yOffset"),
			ToU32(width, "Texture2DArray::UploadSubImage::width"),
			ToU32(height, "Texture2DArray::UploadSubImage::height"),
			std::span<const std::byte>(bytes, uploadSizeBytes),
			rowPitchBytes
		);
	}

	void Texture2DArray::ProduceMipmaps() const
	{
		assert(lastBoundSlot >= GL_TEXTURE0);
		if (backendTexture == nullptr)
			return;

		backendTexture->GenerateMipmaps();
	}
}
