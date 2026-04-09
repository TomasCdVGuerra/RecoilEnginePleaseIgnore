/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

/**
 * @brief EXT_framebuffer_object implementation
 * EXT_framebuffer_object class implementation
 */

#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "FBO.h"
#include "Rendering/Gfx/IFramebuffer.h"
#include "Rendering/Gfx/IGraphicsBackend.h"
#include "Rendering/Gfx/IRenderTarget.h"
#include "Rendering/Gfx/ITexture.h"
#include "System/ContainerUtil.h"
#include "System/Log/ILog.h"
#include "System/Config/ConfigHandler.h"

#include "System/Misc/TracyDefs.h"
#include "Rendering/GlobalRendering.h"

CONFIG(bool, AtiSwapRBFix).defaultValue(false);

std::vector<FBO*> FBO::activeFBOs;
spring::unordered_map<GLuint, FBO::TexData> FBO::fboTexData;

GLint FBO::maxAttachments = 0;
GLsizei FBO::maxSamples = -1;

namespace {

	class NativeRenderTarget final : public gfx::IRenderTarget {
	public:
		explicit NativeRenderTarget(GLuint id): targetId(id) {}

		[[nodiscard]] gfx::Extent3D GetExtent() const noexcept override { return {}; }
		[[nodiscard]] std::uint32_t GetSampleCount() const noexcept override { return 1; }
		[[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override { return static_cast<std::uintptr_t>(targetId); }

	private:
		GLuint targetId = 0;
	};

	class LegacyTextureHandle final : public gfx::ITexture {
	public:
		LegacyTextureHandle(GLuint id, GLenum legacyTarget):
			textureId(id),
			target(legacyTarget),
			dimension(TranslateDimension(legacyTarget))
		{}

		[[nodiscard]] gfx::TextureDimension Dimension() const noexcept override { return dimension; }
		[[nodiscard]] gfx::PixelFormat Format() const noexcept override { return gfx::PixelFormat::Unknown; }
		[[nodiscard]] gfx::Extent3D GetExtent() const noexcept override { return {}; }
		[[nodiscard]] std::uint32_t GetMipLevels() const noexcept override { return 1; }
		[[nodiscard]] std::uintptr_t GetNativeHandle() const noexcept override { return static_cast<std::uintptr_t>(textureId); }

		void Upload(std::uint32_t, std::uint32_t, std::span<const std::byte>, std::size_t, std::size_t) override {}
		void UploadSubRegion(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::span<const std::byte>, std::size_t) override {}
		void ApplySamplerState(const gfx::SamplerState&) override {}
		void GenerateMipmaps() override {}

	private:
		static gfx::TextureDimension TranslateDimension(GLenum legacyTarget)
		{
			switch (legacyTarget) {
				case GL_TEXTURE_3D:
					return gfx::TextureDimension::Tex3D;
				case GL_TEXTURE_2D_ARRAY:
					return gfx::TextureDimension::Tex2DArray;
				case GL_TEXTURE_CUBE_MAP:
				case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
				case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
				case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
				case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
				case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
				case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
					return gfx::TextureDimension::Cube;
				case GL_TEXTURE_1D:
				case GL_TEXTURE_2D:
				case GL_TEXTURE_RECTANGLE_ARB:
				default:
					return gfx::TextureDimension::Tex2D;
			}
		}

	private:
		GLuint textureId = 0;
		GLenum target = GL_TEXTURE_2D;
		gfx::TextureDimension dimension = gfx::TextureDimension::Tex2D;
	};

	static bool IsCubeFaceTarget(GLenum target)
	{
		return ((target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X) && (target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z));
	}

	static std::uint32_t CubeFaceLayer(GLenum target)
	{
		if (!IsCubeFaceTarget(target))
			return 0;

		return static_cast<std::uint32_t>(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
	}

	static bool TryTranslateAttachmentPoint(GLenum attachment, gfx::AttachmentPoint& point)
	{
		if ((attachment >= GL_COLOR_ATTACHMENT0_EXT) && (attachment <= (GL_COLOR_ATTACHMENT0_EXT + 7))) {
			const auto colorIndex = static_cast<std::uint32_t>(attachment - GL_COLOR_ATTACHMENT0_EXT);

			switch (colorIndex) {
				case 0u: point = gfx::AttachmentPoint::Color0; return true;
				case 1u: point = gfx::AttachmentPoint::Color1; return true;
				case 2u: point = gfx::AttachmentPoint::Color2; return true;
				case 3u: point = gfx::AttachmentPoint::Color3; return true;
				case 4u: point = gfx::AttachmentPoint::Color4; return true;
				case 5u: point = gfx::AttachmentPoint::Color5; return true;
				case 6u: point = gfx::AttachmentPoint::Color6; return true;
				case 7u: point = gfx::AttachmentPoint::Color7; return true;
				default: break;
			}
		}

		switch (attachment) {
			case GL_DEPTH_ATTACHMENT_EXT:
				point = gfx::AttachmentPoint::Depth;
				return true;
			case GL_STENCIL_ATTACHMENT_EXT:
				point = gfx::AttachmentPoint::Stencil;
				return true;
			case GL_DEPTH_STENCIL_ATTACHMENT:
				point = gfx::AttachmentPoint::DepthStencil;
				return true;
			default:
				return false;
		}
	}

	static gfx::FilterMode TranslateBlitFilter(uint32_t legacyFilter)
	{
		return (legacyFilter == GL_LINEAR) ? gfx::FilterMode::Linear : gfx::FilterMode::Nearest;
	}

	static GLenum TranslateFramebufferStatus(gfx::FramebufferStatus status)
	{
		switch (status) {
			case gfx::FramebufferStatus::Complete:
				return GL_FRAMEBUFFER_COMPLETE_EXT;
			case gfx::FramebufferStatus::MissingAttachment:
				return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
			case gfx::FramebufferStatus::IncompleteAttachment:
				return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
			case gfx::FramebufferStatus::Unsupported:
				return GL_FRAMEBUFFER_UNSUPPORTED_EXT;
		}

		return GL_FRAMEBUFFER_UNSUPPORTED_EXT;
	}

} // namespace


/**
 * Returns if the current gpu supports Framebuffer Objects
 */
bool FBO::IsSupported()
{
	return (GLAD_GL_EXT_framebuffer_object);
}

bool FBO::IsReady()
{
	return globalRendering->active;
}


FBO::FBO()
{
	Init(false);
}

FBO::FBO(bool noop)
{
	Init(noop);
}

FBO::~FBO()
{
	Kill();
}


uint32_t FBO::GetId() const
{
	if (backendFramebuffer == nullptr)
		return 0;

	const auto nativeHandle = backendFramebuffer->GetNativeHandle();
	constexpr std::uintptr_t maxHandle = static_cast<std::uintptr_t>(std::numeric_limits<uint32_t>::max());

	if (nativeHandle > maxHandle)
		return 0;

	return static_cast<uint32_t>(nativeHandle);
}


FBO::AttachmentBinding* FBO::FindAttachmentBinding(GLenum attachment)
{
	const auto it = std::find_if(attachmentBindings.begin(), attachmentBindings.end(), [attachment](const AttachmentBinding& binding) {
		return (binding.attachment == attachment);
	});

	if (it == attachmentBindings.end())
		return nullptr;

	return &(*it);
}

const FBO::AttachmentBinding* FBO::FindAttachmentBinding(GLenum attachment) const
{
	const auto it = std::find_if(attachmentBindings.begin(), attachmentBindings.end(), [attachment](const AttachmentBinding& binding) {
		return (binding.attachment == attachment);
	});

	if (it == attachmentBindings.end())
		return nullptr;

	return &(*it);
}


bool FBO::IsBackendAvailable() const
{
	return (
		(globalRendering != nullptr) &&
		(globalRendering->graphicsBackend != nullptr) &&
		(backendFramebuffer != nullptr)
	);
}


void FBO::AttachLegacyTexture(const AttachmentBinding& binding) const
{
	if (binding.objectId == 0)
		return;

	if (binding.useLayerBinding && IS_GL_FUNCTION_AVAILABLE(glFramebufferTextureLayerEXT)) {
		glFramebufferTextureLayerEXT(GL_FRAMEBUFFER_EXT, binding.attachment, binding.objectId, binding.mipLevel, binding.layer);
		return;
	}

	if (binding.texTarget == GL_TEXTURE_1D) {
		glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT, binding.attachment, GL_TEXTURE_1D, binding.objectId, binding.mipLevel);
		return;
	}

	if (binding.texTarget == GL_TEXTURE_3D) {
		glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT, binding.attachment, GL_TEXTURE_3D, binding.objectId, binding.mipLevel, binding.zSlice);
		return;
	}

	if ((binding.texTarget == GL_TEXTURE_CUBE_MAP || binding.texTarget == GL_TEXTURE_2D_ARRAY) && IS_GL_FUNCTION_AVAILABLE(glFramebufferTextureEXT)) {
		glFramebufferTextureEXT(GL_FRAMEBUFFER_EXT, binding.attachment, binding.objectId, binding.mipLevel);
		return;
	}

	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, binding.attachment, binding.texTarget, binding.objectId, binding.mipLevel);
}


void FBO::AttachLegacyRenderbuffer(const AttachmentBinding& binding) const
{
	if (binding.objectId == 0)
		return;

	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, binding.attachment, GL_RENDERBUFFER_EXT, binding.objectId);
}


void FBO::SyncAttachmentBindings()
{
	if (backendFramebuffer == nullptr)
		return;

	std::vector<LegacyTextureHandle> textureViews;
	std::vector<gfx::AttachmentViewDesc> backendAttachments;
	std::vector<const AttachmentBinding*> fallbackBindings;

	textureViews.reserve(attachmentBindings.size());
	backendAttachments.reserve(attachmentBindings.size());
	fallbackBindings.reserve(attachmentBindings.size());

	for (const AttachmentBinding& binding : attachmentBindings) {
		if (binding.objectId == 0)
			continue;

		if (binding.isRenderbuffer) {
			fallbackBindings.push_back(&binding);
			continue;
		}

		gfx::AttachmentPoint attachmentPoint;
		if (!TryTranslateAttachmentPoint(binding.attachment, attachmentPoint)) {
			fallbackBindings.push_back(&binding);
			continue;
		}

		if (binding.texTarget == GL_TEXTURE_1D || binding.texTarget == GL_TEXTURE_RECTANGLE_ARB) {
			fallbackBindings.push_back(&binding);
			continue;
		}

		textureViews.emplace_back(binding.objectId, binding.texTarget);

		gfx::AttachmentViewDesc view;
		view.point = attachmentPoint;
		view.texture = &textureViews.back();
		view.mipLevel = static_cast<std::uint32_t>(std::max(binding.mipLevel, 0));

		if (binding.useLayerBinding) {
			view.baseLayer = static_cast<std::uint32_t>(std::max(binding.layer, 0));
			view.layerCount = 1;
		} else if (binding.texTarget == GL_TEXTURE_3D) {
			view.baseLayer = static_cast<std::uint32_t>(std::max(binding.zSlice, 0));
			view.layerCount = 1;
		} else if (binding.texTarget == GL_TEXTURE_CUBE_MAP) {
			view.baseLayer = 0;
			view.layerCount = 6;
		} else if (IsCubeFaceTarget(binding.texTarget)) {
			view.baseLayer = CubeFaceLayer(binding.texTarget);
			view.layerCount = 1;
		} else if (binding.texTarget == GL_TEXTURE_2D_ARRAY) {
			view.baseLayer = 0;
			view.layerCount = 2;
		} else {
			view.baseLayer = 0;
			view.layerCount = 1;
		}

		backendAttachments.push_back(view);
	}

	backendFramebuffer->SetAttachments(backendAttachments);

	if (fallbackBindings.empty())
		return;

	GLint previousFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previousFBO);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, GetId());

	for (const AttachmentBinding* binding : fallbackBindings) {
		if (binding->isRenderbuffer)
			AttachLegacyRenderbuffer(*binding);
		else
			AttachLegacyTexture(*binding);
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previousFBO);
}


GLint FBO::GetCurrentBoundFBO()
{
	RECOIL_DETAILED_TRACY_ZONE;
	GLint curFBO;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curFBO);
	return curFBO;
}



/**
 * Detects the textureTarget just by the textureName/ID
 */
GLenum FBO::GetTextureTargetByID(const GLuint id, const unsigned int i)
{
	static constexpr std::array _targets = { GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_1D, GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY };
	GLint format;
	glBindTexture(_targets[i], id);
	glGetTexLevelParameteriv(_targets[i], 0, GL_TEXTURE_INTERNAL_FORMAT, &format);

	if (format != 1)
		return _targets[i];
	if (i < _targets.size() - 1)
		return GetTextureTargetByID(id, i + 1);
	return GL_INVALID_ENUM;
}


/**
 * Makes a copy of a texture/RBO in the system ram
 */
void FBO::DownloadAttachment(const GLenum attachment)
{
	RECOIL_DETAILED_TRACY_ZONE;
	GLuint target;
	GLuint id;

	glGetFramebufferAttachmentParameterivEXT(GL_FRAMEBUFFER_EXT, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT, (GLint*) &target);
	glGetFramebufferAttachmentParameterivEXT(GL_FRAMEBUFFER_EXT, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT, (GLint*) &id);

	if (target == GL_NONE || id == 0)
		return;

	if (fboTexData.find(id) != fboTexData.end())
		return;

	if (target == GL_TEXTURE) {
		target = GetTextureTargetByID(id);

		if (target == GL_INVALID_ENUM)
			return;
	}

	fboTexData.emplace(id, FBO::TexData{});
	FBO::TexData& tex = fboTexData[id];

	tex.id = id;
	tex.target = target;

	int bits = 0;

	if (target == GL_RENDERBUFFER_EXT) {
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, id);
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_WIDTH_EXT,  &tex.xsize);
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_HEIGHT_EXT, &tex.ysize);
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_INTERNAL_FORMAT_EXT, (GLint*)&tex.format);

		GLint _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_RED_SIZE_EXT, &_cbits); bits += _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_GREEN_SIZE_EXT, &_cbits); bits += _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_BLUE_SIZE_EXT, &_cbits); bits += _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_ALPHA_SIZE_EXT, &_cbits); bits += _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_DEPTH_SIZE_EXT, &_cbits); bits += _cbits;
		glGetRenderbufferParameterivEXT(GL_RENDERBUFFER_EXT, GL_RENDERBUFFER_STENCIL_SIZE_EXT, &_cbits); bits += _cbits;
	} else {
		glBindTexture(target, id);

		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &tex.xsize);
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &tex.ysize);
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_DEPTH, &tex.zsize);
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_INTERNAL_FORMAT, (GLint*)&tex.format);

		GLint _cbits;
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_RED_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_GREEN_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_BLUE_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_ALPHA_SIZE, &_cbits); bits += _cbits;
		glGetTexLevelParameteriv(target, 0, GL_TEXTURE_DEPTH_SIZE, &_cbits); bits += _cbits;
	}

	if (configHandler->GetBool("AtiSwapRBFix")) {
		if (tex.format == GL_RGBA) {
			tex.format = GL_BGRA;
		} else if (tex.format == GL_RGB) {
			tex.format = GL_BGR;
		}
	}

	if (bits < 32) /*FIXME*/
		bits = 32;

	switch (target) {
		case GL_TEXTURE_2D_ARRAY: [[fallthrough]];
		case GL_TEXTURE_3D:
			tex.pixels.resize(tex.xsize * tex.ysize * tex.zsize * (bits / 8));
			glGetTexImage(tex.target, 0, /*FIXME*/GL_RGBA, /*FIXME*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
			break;
		case GL_TEXTURE_1D:
			tex.pixels.resize(tex.xsize * (bits / 8));
			glGetTexImage(tex.target, 0, /*FIXME*/GL_RGBA, /*FIXME*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
			break;
		case GL_RENDERBUFFER_EXT:
			tex.pixels.resize(tex.xsize * tex.ysize * (bits / 8));
			glReadBuffer(attachment);
			glReadPixels(0, 0, tex.xsize, tex.ysize, /*FIXME*/GL_RGBA, /*FIXME*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
			break;
		default: //GL_TEXTURE_2D & GL_TEXTURE_RECTANGLE
			tex.pixels.resize(tex.xsize * tex.ysize * (bits / 8));
			glGetTexImage(tex.target, 0, /*FIXME*/GL_RGBA, /*FIXME*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
	}
}

/**
 * @brief GLContextLost
 */
void FBO::GLContextLost()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsSupported())
		return;

	GLint oldReadBuffer;

	for (FBO* fbo: activeFBOs) {
		if (!fbo->reloadOnAltTab)
			continue;

		const GLuint fboId = fbo->GetId();
		if (fboId == 0)
			continue;

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fboId);
		glGetIntegerv(GL_READ_BUFFER, &oldReadBuffer);

		for (int i = 0; i < maxAttachments; ++i) {
			DownloadAttachment(GL_COLOR_ATTACHMENT0_EXT + i);
		}
		DownloadAttachment(GL_DEPTH_ATTACHMENT_EXT);
		DownloadAttachment(GL_STENCIL_ATTACHMENT_EXT);

		glReadBuffer(oldReadBuffer);
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}


/**
 * @brief GLContextReinit
 */
void FBO::GLContextReinit()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsSupported())
		return;

	for (auto ti = fboTexData.begin(); ti != fboTexData.end(); ++ti) {
		const FBO::TexData& tex = ti->second;

		if (glIsTexture(tex.id)) {
			glBindTexture(tex.target, tex.id);

			// TODO regen mipmaps?
			switch (tex.target) {
				case GL_TEXTURE_2D_ARRAY: [[fallthrough]];
				case GL_TEXTURE_3D:
					// glTexSubImage3D(tex.target, 0, 0,0,0, tex.xsize, tex.ysize, tex.zsize, /*FIXME?*/GL_RGBA, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
					glTexImage3D(tex.target, 0, tex.format, tex.xsize, tex.ysize, tex.zsize, 0, /*FIXME?*/GL_RGBA, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
					break;
				case GL_TEXTURE_1D:
					// glTexSubImage1D(tex.target, 0, 0, tex.xsize, /*FIXME?*/GL_RGBA, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
					glTexImage1D(tex.target, 0, tex.format, tex.xsize, /*FIXME?*/GL_RGBA, 0, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
					break;
				default: // case GL_TEXTURE_2D & GL_TEXTURE_RECTANGLE
					// glTexSubImage2D(tex.target, 0, 0,0, tex.xsize, tex.ysize, /*FIXME?*/GL_RGBA, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
					glTexImage2D(tex.target, 0, tex.format, tex.xsize, tex.ysize, 0, /*FIXME?*/GL_RGBA, /*FIXME?*/GL_UNSIGNED_BYTE, &tex.pixels[0]);
			}
		} else if (glIsRenderbufferEXT(tex.id)) {
			// FIXME implement rendering buffer context init
		}
	}

	fboTexData.clear();
}


/**
 * Tests for support of the EXT_framebuffer_object
 * extension, and generates a framebuffer if supported
 */
void FBO::Init(bool noop)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (noop)
		return;
	if (!IsSupported())
		return;

	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &maxAttachments);

	GetMaxSamples();

	gfx::RenderTargetDesc desc;
	desc.extent = {1u, 1u, 1u};
	desc.sampleCount = 1u;
	desc.debugName = "Legacy-FBO";

	if (globalRendering != nullptr && globalRendering->graphicsBackend != nullptr)
		backendFramebuffer = globalRendering->graphicsBackend->CreateFramebuffer(desc);

	if (backendFramebuffer == nullptr)
		return;

	activeFBOs.push_back(this);

	valid = (GetId() != 0u);
}


/**
 * Unbinds the framebuffer and deletes it
 */
void FBO::Kill()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsSupported())
		return;

	{
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);

		for (auto ri = rboIDs.begin(); ri != rboIDs.end(); ++ri) {
			glDeleteRenderbuffersEXT(1, &(*ri));
		}

		rboIDs.clear();
	}

	attachmentBindings.clear();
	backendFramebuffer.reset();
	valid = false;

	spring::VectorErase(activeFBOs, this);

	if (!activeFBOs.empty())
		return;

	// we are the last fbo left, delete remaining alloc'ed stuff
	fboTexData.clear();
}


/**
 * Tests if we have a valid (generated and complete) framebuffer
 */
bool FBO::IsValid() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	return (backendFramebuffer != nullptr && GetId() != 0u && valid);
}


/**
 * Makes the framebuffer the active framebuffer context
 */
void FBO::Bind()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (IsBackendAvailable()) {
		globalRendering->graphicsBackend->BindFramebuffer(backendFramebuffer.get());
		return;
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, GetId());
}


/**
 * Unbinds the framebuffer from the current context
 */
void FBO::Unbind()
{
	RECOIL_DETAILED_TRACY_ZONE;
	// Bind is instance whereas Unbind is static (!),
	// this is cause Binding FBOs is a very expensive function
	// and so you want to save redundant FBO bindings when ever possible. e.g:
	// fbo1.Bind();
	//   do stuff
	// FBO::Unbind(); <- redundant!
	// fbo2.Bind();
	//   do stuff
	// FBO::Unbind(); <- not redundant!
	//   continue with screen FBO
	if (globalRendering != nullptr && globalRendering->graphicsBackend != nullptr) {
		globalRendering->graphicsBackend->BindFramebuffer(nullptr);
		return;
	}

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

bool FBO::Blit(int32_t fromID, int32_t toID, const std::array<int, 4>& srcRect, const std::array<int, 4>& dstRect, uint32_t mask, uint32_t filter)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!IsSupported())
		return false;

	if (globalRendering == nullptr || globalRendering->graphicsBackend == nullptr)
		return false;

	if (srcRect[2] - srcRect[0] <= 0 || srcRect[3] - srcRect[1] <= 0)
		return false;

	if (dstRect[2] - dstRect[0] <= 0 || dstRect[3] - dstRect[1] <= 0)
		return false;

	GLint currentFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &currentFBO);
	if (fromID < 0)
		fromID = currentFBO;

	gfx::IFramebuffer* blitter = nullptr;
	std::unique_ptr<gfx::IFramebuffer> fallbackBlitter;

	for (FBO* fbo : activeFBOs) {
		if (fbo->backendFramebuffer != nullptr) {
			blitter = fbo->backendFramebuffer.get();
			break;
		}
	}

	if (blitter == nullptr) {
		gfx::RenderTargetDesc desc;
		desc.extent = {1u, 1u, 1u};
		desc.sampleCount = 1u;
		desc.debugName = "Legacy-FBO-Blit";

		fallbackBlitter = globalRendering->graphicsBackend->CreateFramebuffer(desc);
		blitter = fallbackBlitter.get();
	}

	if (blitter == nullptr)
		return false;

	NativeRenderTarget srcTarget(static_cast<GLuint>(fromID));
	NativeRenderTarget dstTarget(static_cast<GLuint>(toID));

	gfx::FramebufferBlitDesc blitDesc;
	blitDesc.src = &srcTarget;
	blitDesc.dst = &dstTarget;
	blitDesc.srcRect = srcRect;
	blitDesc.dstRect = dstRect;
	blitDesc.mask = mask;
	blitDesc.filter = TranslateBlitFilter(filter);

	return blitter->Blit(blitDesc);
}


/**
 * Tests if the framebuffer is a complete and
 * legitimate framebuffer
 */
bool FBO::CheckStatus(const char* name)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	const GLenum status = GetStatus();

	switch (status) {
		case GL_FRAMEBUFFER_COMPLETE_EXT:
			return (valid = true);
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
			LOG_L(L_WARNING, "FBO-%s: None/Unsupported textures/buffers attached!", name);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
			LOG_L(L_WARNING, "FBO-%s: Missing a required texture/buffer attachment!", name);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
			LOG_L(L_WARNING, "FBO-%s: Has mismatched texture/buffer dimensions!", name);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
			LOG_L(L_WARNING, "FBO-%s: Incomplete buffer formats!", name);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
			LOG_L(L_WARNING, "FBO-%s: Incomplete draw buffers!", name);
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
			LOG_L(L_WARNING, "FBO-%s: Incomplete read buffer!", name);
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
			LOG_L(L_WARNING, "FBO-%s: GL_FRAMEBUFFER_UNSUPPORTED_EXT", name);
			break;
		default:
			LOG_L(L_WARNING, "FBO-%s: error code 0x%X", name, status);
			break;
	}

	return (valid = false);
}


/**
 * Returns the current framebuffer status
 */
GLenum FBO::GetStatus()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif

	if (backendFramebuffer != nullptr)
		return TranslateFramebufferStatus(backendFramebuffer->Validate());

	return glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
}


/**
 * Attaches a GL texture to the framebuffer
 */
void FBO::AttachTexture(const GLuint texId, const GLenum texTarget, const GLenum attachment, const int mipLevel, const int zSlice )
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	AttachmentBinding* binding = FindAttachmentBinding(attachment);

	if (binding == nullptr) {
		attachmentBindings.emplace_back();
		binding = &attachmentBindings.back();
	}

	binding->attachment = attachment;
	binding->isRenderbuffer = false;
	binding->objectId = texId;
	binding->texTarget = texTarget;
	binding->mipLevel = mipLevel;
	binding->zSlice = zSlice;
	binding->layer = 0;
	binding->useLayerBinding = false;

	SyncAttachmentBindings();
}

void FBO::AttachTextureLayer(const GLuint texId, const GLenum attachment, const int mipLevel, const int layer)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	AttachmentBinding* binding = FindAttachmentBinding(attachment);

	if (binding == nullptr) {
		attachmentBindings.emplace_back();
		binding = &attachmentBindings.back();
	}

	GLenum detectedTarget = GetTextureTargetByID(texId);
	if (detectedTarget == GL_INVALID_ENUM)
		detectedTarget = GL_TEXTURE_2D_ARRAY;

	binding->attachment = attachment;
	binding->isRenderbuffer = false;
	binding->objectId = texId;
	binding->texTarget = detectedTarget;
	binding->mipLevel = mipLevel;
	binding->layer = layer;
	binding->zSlice = layer;
	binding->useLayerBinding = true;

	SyncAttachmentBindings();
}


/**
 * Attaches a GL RenderBuffer to the framebuffer
 */
void FBO::AttachRenderBuffer(const GLuint rboId, const GLenum attachment)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	AttachmentBinding* binding = FindAttachmentBinding(attachment);

	if (binding == nullptr) {
		attachmentBindings.emplace_back();
		binding = &attachmentBindings.back();
	}

	binding->attachment = attachment;
	binding->isRenderbuffer = true;
	binding->objectId = rboId;
	binding->texTarget = GL_TEXTURE_2D;
	binding->mipLevel = 0;
	binding->layer = 0;
	binding->zSlice = 0;
	binding->useLayerBinding = false;

	SyncAttachmentBindings();
}


/**
 * Detaches an attachment from the framebuffer
 */
void FBO::Detach(const GLenum attachment)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	const AttachmentBinding* binding = FindAttachmentBinding(attachment);

	if (binding != nullptr && binding->isRenderbuffer) {
		spring::VectorEraseIf(rboIDs, [&](GLuint& rboID) {
			if (rboID != binding->objectId)
				return false;

			glDeleteRenderbuffersEXT(1, &rboID);
			return true;
		});
	}

	spring::VectorEraseIf(attachmentBindings, [attachment](const AttachmentBinding& b) {
		return (b.attachment == attachment);
	});

	SyncAttachmentBindings();
}


/**
 * Detaches any attachments from the framebuffer
 */
void FBO::DetachAll()
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	for (GLuint rboID : rboIDs)
		glDeleteRenderbuffersEXT(1, &rboID);

	rboIDs.clear();
	attachmentBindings.clear();
	SyncAttachmentBindings();
}


/**
 * Creates and attaches a RBO
 */
void FBO::CreateRenderBuffer(const GLenum attachment, const GLenum format, const GLsizei width, const GLsizei height)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	GLuint rbo;
	glGenRenderbuffersEXT(1, &rbo);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rbo);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, format, width, height);
	rboIDs.push_back(rbo);

	AttachRenderBuffer(rbo, attachment);
}


/**
 * Creates and attaches a multisampled RBO
 */
void FBO::CreateRenderBufferMultisample(const GLenum attachment, const GLenum format, const GLsizei width, const GLsizei height, GLsizei samples)
{
	RECOIL_DETAILED_TRACY_ZONE;
#ifndef HEADLESS
	assert(GetCurrentBoundFBO() == static_cast<GLint>(GetId()));
#endif
	assert(maxSamples > 0);
	samples = std::min(samples, maxSamples);

	GLuint rbo;
	glGenRenderbuffersEXT(1, &rbo);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rbo);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, samples, format, width, height);
	rboIDs.push_back(rbo);

	AttachRenderBuffer(rbo, attachment);
}

GLsizei FBO::GetMaxSamples()
{
	if (maxSamples >= 0)
		return maxSamples;
#ifdef HEADLESS
	maxSamples = 1;
#else
	// set maxSamples once
	if (maxSamples == -1) {
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);
		maxSamples = std::max(0, maxSamples);
	}
#endif // HEADLESS
	return maxSamples;
}
