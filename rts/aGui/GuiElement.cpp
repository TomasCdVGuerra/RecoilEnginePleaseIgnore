/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GuiElement.h"
#include "System/SafeUtil.h"

#include "Rendering/GlobalRendering.h"
#include "Rendering/Gfx/IGraphicsBackend.h"
#include "Rendering/Gfx/ITexture.h"
#include "Rendering/Gfx/IVertexBuffer.h"
#ifdef ENABLE_VULKAN
#include "Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h"
#endif
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Shaders/Shader.h"
#include "System/Log/ILog.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace agui
{

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

		struct GuiBatchVertex
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

		void AppendQuad(
			std::vector<GuiBatchVertex> &vertices,
			std::vector<std::uint16_t> &indices,
			float left,
			float top,
			float right,
			float bottom,
			const SColor &color)
		{
			if (vertices.size() > (static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) - 4u))
				return;

			const std::uint16_t base = static_cast<std::uint16_t>(vertices.size());

			vertices.push_back({left, top, 0.0f, 0.0f, 1.0f, color.r, color.g, color.b, color.a});
			vertices.push_back({right, top, 0.0f, 1.0f, 1.0f, color.r, color.g, color.b, color.a});
			vertices.push_back({right, bottom, 0.0f, 1.0f, 0.0f, color.r, color.g, color.b, color.a});
			vertices.push_back({left, bottom, 0.0f, 0.0f, 0.0f, color.r, color.g, color.b, color.a});

			indices.push_back(base + 0u);
			indices.push_back(base + 1u);
			indices.push_back(base + 2u);
			indices.push_back(base + 2u);
			indices.push_back(base + 3u);
			indices.push_back(base + 0u);
		}

		bool DrawBackendQuads(
			gfx::IGraphicsBackend *backend,
			gfx::ITexture *texture,
			const std::vector<GuiBatchVertex> &vertices,
			const std::vector<std::uint16_t> &indices)
		{
			if ((backend == nullptr) || vertices.empty() || indices.empty())
				return false;

			if ((indices.size() % 3u) != 0u)
				return false;

			for (const std::uint16_t index : indices)
			{
				if (index >= vertices.size())
					return false;
			}

			gfx::BufferCreateInfo vertexCI;
			vertexCI.sizeBytes = vertices.size() * sizeof(GuiBatchVertex);
			vertexCI.usage = gfx::BufferUsage::Dynamic;
			vertexCI.memoryAccess = gfx::MemoryAccess::CpuToGpu;
			vertexCI.debugName = "aGuiDrawBoxVB";

			auto vertexBuffer = backend->CreateVertexBuffer(vertexCI);
			if (vertexBuffer == nullptr)
				return false;

			gfx::BufferCreateInfo indexCI;
			indexCI.sizeBytes = indices.size() * sizeof(std::uint16_t);
			indexCI.usage = gfx::BufferUsage::Dynamic;
			indexCI.memoryAccess = gfx::MemoryAccess::CpuToGpu;
			indexCI.debugName = "aGuiDrawBoxIB";

			auto indexBuffer = backend->CreateVertexBuffer(indexCI);
			if (indexBuffer == nullptr)
				return false;

			vertexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(vertices.data()), vertexCI.sizeBytes));
			indexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(indices.data()), indexCI.sizeBytes));

			gfx::TexturedVertexLayout layout;
			layout.strideBytes = sizeof(GuiBatchVertex);
			layout.positionOffsetBytes = offsetof(GuiBatchVertex, x);
			layout.texCoordOffsetBytes = offsetof(GuiBatchVertex, u);
			layout.colorOffsetBytes = offsetof(GuiBatchVertex, r);

			gfx::TexturedIndexedBatchDesc batch;
			batch.firstIndex = 0u;
			batch.indexCount = static_cast<std::uint32_t>(indices.size());

			gfx::TexturedBatchState state;
			state.depthTest = false;
			state.blend = true;
			state.premultipliedAlpha = false;
			state.useDefaultBlendFunc = true;

#ifdef ENABLE_VULKAN
			if (auto *vulkanBackend = dynamic_cast<gfx::VulkanGraphicsBackend *>(backend); vulkanBackend != nullptr)
			{
				vulkanBackend->DrawTexturedIndexedBatches(
					*vertexBuffer,
					*indexBuffer,
					texture,
					std::span<const gfx::TexturedIndexedBatchDesc>(&batch, 1),
					layout,
					gfx::IndexElementType::UInt16,
					state);

				return true;
			}
#endif

			if (texture == nullptr)
				return false;

			backend->DrawTexturedIndexedBatches(
				*vertexBuffer,
				*indexBuffer,
				*texture,
				std::span<const gfx::TexturedIndexedBatchDesc>(&batch, 1),
				layout,
				gfx::IndexElementType::UInt16,
				state);

			return true;
		}
	}

	int GuiElement::screensize[2];
	int GuiElement::screenoffset[2];

	GuiElement::GuiElement(GuiElement *_parent) : parent(_parent), fixedSize(false), weight(1)
	{
		size[0] = size[1] = 0.0f;
		pos[0] = pos[1] = 0.0f;

		if (parent)
			parent->AddChild(this);
	}

	GuiElement::~GuiElement()
	{
		for (auto &ch : children)
			spring::SafeDelete(ch);
	}

	void GuiElement::Draw()
	{
		DrawSelf();
		for (auto ch : children)
		{
			ch->Draw();
		}
	}

	bool GuiElement::HandleEvent(const SDL_Event &ev)
	{
		if (HandleEventSelf(ev))
			return true;
		for (auto ch : children)
		{
			if (ch->HandleEvent(ev))
				return true;
		}
		return false;
	}

	bool GuiElement::MouseOver(int x, int y) const
	{
		float mouse[2] = {PixelToGlX(x), PixelToGlY(y)};
		return (mouse[0] >= pos[0] && mouse[0] <= pos[0] + size[0]) && (mouse[1] >= pos[1] && mouse[1] <= pos[1] + size[1]);
	}

	bool GuiElement::MouseOver(float x, float y) const
	{
		return (x >= pos[0] && x <= pos[0] + size[0]) && (y >= pos[1] && y <= pos[1] + size[1]);
	}

	void GuiElement::UpdateDisplayGeo(int x, int y, int xOffset, int yOffset)
	{
		screensize[0] = x;
		screensize[1] = y;
		screenoffset[0] = xOffset;
		screenoffset[1] = yOffset;
	}

	float GuiElement::PixelToGlX(int x)
	{
		return float(x - screenoffset[0]) / float(screensize[0]);
	}

	float GuiElement::PixelToGlY(int y)
	{
		return 1.0f - float(y - screenoffset[1]) / float(screensize[1]);
	}

	float GuiElement::GlToPixelX(float x)
	{
		return x * float(screensize[0]) + float(screenoffset[0]);
	}

	float GuiElement::GlToPixelY(float y)
	{
		return y * float(screensize[1]) + float(screenoffset[1]);
	}

	void GuiElement::AddChild(GuiElement *elem)
	{
		children.push_back(elem);
		elem->SetPos(pos[0], pos[1]);
		elem->SetSize(size[0], size[1]);
		GeometryChange();
	}

	void GuiElement::SetPos(float x, float y)
	{
		pos[0] = x;
		pos[1] = y;
		GeometryChange();
	}

	void GuiElement::SetSize(float x, float y, bool fixed)
	{
		size[0] = x;
		size[1] = y;
		fixedSize = fixed;
		GeometryChange();
	}

	void GuiElement::GeometryChange()
	{
		GeometryChangeSelf();
		for (auto ch : children)
			ch->GeometryChange();
	}

	float GuiElement::DefaultOpacity() const
	{
		return 0.8f;
	}

	void GuiElement::Move(float x, float y)
	{
		pos[0] += x;
		pos[1] += y;
		for (auto ch : children)
			ch->Move(x, y);
	}

#ifdef HEADLESS
	void GuiElement::DrawBox(int primType, const SColor &color) {}
#else
	void GuiElement::DrawBox(int primType, const SColor &color)
	{
		if (!HasOpenGLBackend())
		{
			auto *backend = GetGraphicsBackend();
			if (backend == nullptr)
				return;

			float viewWidth = 1.0f;
			float viewHeight = 1.0f;
			ResolveUiViewportSize(viewWidth, viewHeight);

			const float leftPx = pos[0] * viewWidth;
			const float rightPx = (pos[0] + size[0]) * viewWidth;
			const float topPx = viewHeight - ((pos[1] + size[1]) * viewHeight);
			const float bottomPx = viewHeight - (pos[1] * viewHeight);

			std::vector<GuiBatchVertex> vertices;
			std::vector<std::uint16_t> indices;

			switch (primType)
			{
			case GL_QUADS:
			{
				vertices.reserve(4u);
				indices.reserve(6u);
				AppendQuad(vertices, indices, leftPx, topPx, rightPx, bottomPx, color);
			}
			break;

			case GL_LINE_LOOP:
			{
				const float thicknessPx = 1.0f;
				vertices.reserve(16u);
				indices.reserve(24u);

				AppendQuad(vertices, indices, leftPx, topPx, rightPx, std::min(topPx + thicknessPx, bottomPx), color);	  // top
				AppendQuad(vertices, indices, leftPx, std::max(bottomPx - thicknessPx, topPx), rightPx, bottomPx, color); // bottom
				AppendQuad(vertices, indices, leftPx, topPx, std::min(leftPx + thicknessPx, rightPx), bottomPx, color);	  // left
				AppendQuad(vertices, indices, std::max(rightPx - thicknessPx, leftPx), topPx, rightPx, bottomPx, color);  // right
			}
			break;

			default:
				return;
			}

			if ((vertices.size() % 4u) != 0u)
				return;

			if (indices.size() != ((vertices.size() / 4u) * 6u))
				return;

			// Solid-color UI boxes are rendered as white-texture quads tinted by vertex color.
			DrawBackendQuads(backend, nullptr, vertices, indices);
			return;
		}

		auto &rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_2DC>();
		auto &sh = rb.GetShader();

		sh.Enable();
		switch (primType)
		{
		case GL_QUADS:
		{
			rb.AddQuadTriangles(
				{pos[0], pos[1], color},
				{pos[0] + size[0], pos[1], color},
				{pos[0] + size[0], pos[1] + size[1], color},
				{pos[0], pos[1] + size[1], color});
			rb.DrawElements(GL_TRIANGLES);
		}
		break;
		case GL_LINE_LOOP:
		{
			rb.AddVertices({{pos[0], pos[1], color},
							{pos[0] + size[0], pos[1], color},
							{pos[0] + size[0], pos[1] + size[1], color},
							{pos[0], pos[1] + size[1], color}});
			rb.DrawArrays(primType);
		}
		break;
		default:
			assert(false);
			break;
		}
		sh.Disable();
	}
#endif // !HEADLESS

}
