/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Picture.h"

#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Gfx/IGraphicsBackend.h"
#include "Rendering/Gfx/ITexture.h"
#include "Rendering/Gfx/IVertexBuffer.h"
#ifdef ENABLE_VULKAN
#include "Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h"
#endif
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/Textures/Bitmap.h"
#include "System/Log/ILog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace
{
	bool HasOpenGLBackend()
	{
		return ((globalRendering != nullptr) &&
				(globalRendering->graphicsBackend != nullptr) &&
				(globalRendering->graphicsBackend->Type() == gfx::BackendType::OpenGL));
	}

	gfx::IGraphicsBackend *GetGraphicsBackend()
	{
		if ((globalRendering == nullptr) || (globalRendering->graphicsBackend == nullptr))
			return nullptr;

		return globalRendering->graphicsBackend.get();
	}

#ifndef HEADLESS
	void ResolveUiViewportSize(float &width, float &height)
	{
		width = 1.0f;
		height = 1.0f;

		if (globalRendering == nullptr)
			return;

		int sx = globalRendering->viewSizeX;
		int sy = globalRendering->viewSizeY;

		if ((sx <= 16) || (sy <= 16))
		{
			sx = globalRendering->winSizeX;
			sy = globalRendering->winSizeY;
		}

		if ((sx > 16) && (sy > 16))
		{
			width = static_cast<float>(sx);
			height = static_cast<float>(sy);
		}
	}
#endif

	struct PictureBatchVertex
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float u = 0.0f;
		float v = 0.0f;
		std::uint8_t r = 255u;
		std::uint8_t g = 255u;
		std::uint8_t b = 255u;
		std::uint8_t a = 255u;
	};
}

namespace agui
{

	Picture::Picture(GuiElement *parent)
		: GuiElement(parent), texture(0)
	{
	}

	Picture::~Picture()
	{
		if (texture && HasOpenGLBackend())
		{
			glDeleteTextures(1, &texture);
		}
	}

	void Picture::Load(const std::string &_file)
	{
		file = _file;

		if (!HasOpenGLBackend())
		{
			auto *backend = GetGraphicsBackend();
			if (backend == nullptr)
			{
				backendTexture.reset();
				texture = 0;
				return;
			}

#ifdef ENABLE_VULKAN
			if (backendTexture != nullptr)
			{
				if (auto *vulkanBackend = dynamic_cast<gfx::VulkanGraphicsBackend *>(backend); vulkanBackend != nullptr)
					vulkanBackend->ClearPendingTexturedBatchDraws();
			}
#endif

			backendTexture.reset();

			CBitmap bmp;
			if (bmp.Load(file))
			{
				backendTexture = bmp.CreateBackendTexture();
				if (backendTexture == nullptr)
					LOG_L(L_WARNING, "[Picture::%s] failed to create backend texture for '%s'", __func__, file.c_str());
			}
			else
			{
				LOG_L(L_WARNING, "Failed to load: %s", file.c_str());
			}

			texture = 0;
			return;
		}

		backendTexture.reset();

		CBitmap bmp;
		if (bmp.Load(file))
		{
			texture = bmp.CreateTexture();
		}
		else
		{
			LOG_L(L_WARNING, "Failed to load: %s", file.c_str());
			texture = 0;
		}
	}

#ifdef HEADLESS
	void Picture::DrawSelf() {}
#else
	void Picture::DrawSelf()
	{
		if (!HasOpenGLBackend())
		{
			auto *backend = GetGraphicsBackend();
			if ((backend == nullptr) || (backendTexture == nullptr))
				return;

			if (backendVertexBuffer == nullptr)
			{
				gfx::BufferCreateInfo ci;
				ci.sizeBytes = 1;
				ci.usage = gfx::BufferUsage::Dynamic;
				ci.memoryAccess = gfx::MemoryAccess::CpuToGpu;
				ci.debugName = "aGuiPictureVertices";
				backendVertexBuffer = backend->CreateVertexBuffer(ci);
			}

			if (backendIndexBuffer == nullptr)
			{
				gfx::BufferCreateInfo ci;
				ci.sizeBytes = 1;
				ci.usage = gfx::BufferUsage::Dynamic;
				ci.memoryAccess = gfx::MemoryAccess::CpuToGpu;
				ci.debugName = "aGuiPictureIndices";
				backendIndexBuffer = backend->CreateVertexBuffer(ci);
			}

			if ((backendVertexBuffer == nullptr) || (backendIndexBuffer == nullptr))
				return;

			float viewWidth = 1.0f;
			float viewHeight = 1.0f;
			ResolveUiViewportSize(viewWidth, viewHeight);

			const float leftPx = pos[0] * viewWidth;
			const float rightPx = (pos[0] + size[0]) * viewWidth;
			const float topPx = viewHeight - ((pos[1] + size[1]) * viewHeight);
			const float bottomPx = viewHeight - (pos[1] * viewHeight);

			const std::array<PictureBatchVertex, 4> vertices = {{
				{leftPx, topPx, 0.0f, 0.0f, 1.0f, 255u, 255u, 255u, 255u},
				{rightPx, topPx, 0.0f, 1.0f, 1.0f, 255u, 255u, 255u, 255u},
				{rightPx, bottomPx, 0.0f, 1.0f, 0.0f, 255u, 255u, 255u, 255u},
				{leftPx, bottomPx, 0.0f, 0.0f, 0.0f, 255u, 255u, 255u, 255u},
			}};

			const std::array<std::uint16_t, 6> indices = {
				3u,
				0u,
				1u,
				3u,
				1u,
				2u,
			};

			const std::size_t vertexBytes = vertices.size() * sizeof(PictureBatchVertex);
			const std::size_t indexBytes = indices.size() * sizeof(std::uint16_t);

			if (backendVertexBuffer->SizeBytes() < vertexBytes)
				backendVertexBuffer->Resize(vertexBytes, false);

			if (backendIndexBuffer->SizeBytes() < indexBytes)
				backendIndexBuffer->Resize(indexBytes, false);

			backendVertexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(vertices.data()), vertexBytes));
			backendIndexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(indices.data()), indexBytes));

			gfx::TexturedVertexLayout vertexLayout;
			vertexLayout.strideBytes = sizeof(PictureBatchVertex);
			vertexLayout.positionOffsetBytes = offsetof(PictureBatchVertex, x);
			vertexLayout.texCoordOffsetBytes = offsetof(PictureBatchVertex, u);
			vertexLayout.colorOffsetBytes = offsetof(PictureBatchVertex, r);

			gfx::TexturedIndexedBatchDesc batch;
			batch.firstIndex = 0;
			batch.indexCount = static_cast<std::uint32_t>(indices.size());

			gfx::TexturedBatchState state;
			state.depthTest = false;
			state.blend = true;
			state.premultipliedAlpha = false;
			state.useDefaultBlendFunc = true;

			backend->DrawTexturedIndexedBatches(
				*backendVertexBuffer,
				*backendIndexBuffer,
				*backendTexture,
				std::span<const gfx::TexturedIndexedBatchDesc>(&batch, 1),
				vertexLayout,
				gfx::IndexElementType::UInt16,
				state);

			return;
		}

		if (texture)
		{
			auto &rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DTC>();
			auto &sh = rb.GetShader();
			const SColor color = {1.0f, 1.0f, 1.0f, 1.0f};

			rb.AddQuadTriangles(
				{pos[0], pos[1], 0.0f, 1.0f, color},
				{pos[0] + size[0], pos[1], 1.0f, 1.0f, color},
				{pos[0] + size[0], pos[1] + size[1], 1.0f, 0.0f, color},
				{pos[0], pos[1] + size[1], 0.0f, 0.0f, color});

			glBindTexture(GL_TEXTURE_2D, texture);
			sh.Enable();
			rb.DrawElements(GL_TRIANGLES);
			sh.Disable();
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}
#endif

} // namespace agui
