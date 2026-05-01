/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "CommandColors.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Gfx/IGraphicsBackend.h"
#include "Rendering/Gfx/IVertexBuffer.h"
#include "Rendering/Gfx/Vulkan/VulkanGraphicsBackend.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GL/RenderBuffers.h"
#include "Rendering/Textures/Bitmap.h"
#include "MouseCursor.h"
#include "HwMouseCursor.h"
#include "System/Log/ILog.h"
#include "System/SpringMath.h"
#include "System/SafeUtil.h"
#include "System/StringUtil.h"
#include "System/UnorderedMap.hpp"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/SimpleParser.h"

#include "System/Misc/TracyDefs.h"

CMouseCursor::CMouseCursor(const std::string &name_, HotSpot hs)
	: name(name_), hotSpot(hs)
{
	frames.reserve(8);
	images.reserve(8);

	Build(name);

	for (FrameData &frame : frames)
	{
		frame.startTime = animPeriod;
		frame.endTime = (animPeriod += frame.length);

		xmaxsize = std::max(images[frame.imageIdx].xOrigSize, xmaxsize);
		ymaxsize = std::max(images[frame.imageIdx].yOrigSize, ymaxsize);
	}

	if (hotSpot == Center)
	{
		xofs = xmaxsize / 2;
		yofs = ymaxsize / 2;
	}
}

CMouseCursor::~CMouseCursor()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (hwCursor != nullptr)
	{
		hwCursor->Kill();
		IHardwareCursor::Free(hwCursor);
	}

	for (const auto &image : images)
	{
		glDeleteTextures(1, &image.texture);
	}
}

CMouseCursor &CMouseCursor::operator=(CMouseCursor &&mc) noexcept
{
	name = std::move(mc.name);
	images = std::move(mc.images);
	frames = std::move(mc.frames);

	if (mc.hwCursor != nullptr)
	{
		memcpy(hwCursorMem, mc.hwCursorMem, sizeof(hwCursorMem));
		memset(mc.hwCursorMem, 0, sizeof(hwCursorMem));

		hwCursor = reinterpret_cast<IHardwareCursor *>(hwCursorMem);
		mc.hwCursor = nullptr;
	}

	hotSpot = mc.hotSpot;

	animTime = mc.animTime;
	animPeriod = mc.animPeriod;
	currentFrame = mc.currentFrame;

	xmaxsize = mc.xmaxsize;
	ymaxsize = mc.ymaxsize;

	xofs = mc.xofs;
	yofs = mc.yofs;

	hwValid = mc.hwValid;
	return *this;
}

bool CMouseCursor::Build(const std::string &name)
{
	RECOIL_DETAILED_TRACY_ZONE;
	int lastFrame = 1000;

	hwCursor = IHardwareCursor::Alloc(hwCursorMem);
	hwCursor->Init(hotSpot);

	if (!BuildFromSpecFile(name, lastFrame))
		BuildFromFileNames(name, lastFrame);

	hwCursor->Finish();

	return (hwValid = hwCursor->IsValid(), IsValid());
}

bool CMouseCursor::BuildFromSpecFile(const std::string &name, int &lastFrame)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const std::string specFileName = "anims/" + name + ".txt";

	if (!CFileHandler::FileExists(specFileName, SPRING_VFS_RAW_FIRST))
		return false;

	CFileHandler specFile(specFileName);
	CSimpleParser specParser(specFile);

	const std::array<std::pair<std::string, int>, 3> commandsMap = {{{"frame", 0}, {"hotspot", 1}, {"lastframe", 2}}};
	spring::unsynced_map<std::string, int> imageIdxMap;

	for (std::string line = specParser.GetCleanLine(); !line.empty(); line = specParser.GetCleanLine())
	{
		const std::vector<std::string> &words = specParser.Tokenize(line, 2);
		const std::string &command = StringToLower(words[0]);

		const auto cmdPred = [&](const std::pair<std::string, int> &p)
		{ return (p.first == command); };
		const auto cmdIter = std::find_if(commandsMap.cbegin(), commandsMap.cend(), cmdPred);

		if (cmdIter == commandsMap.end())
		{
			LOG_L(L_ERROR, "[MouseCursor::%s] unknown command \"%s\" in file \"%s\"", __func__, command.c_str(), specFileName.c_str());
			continue;
		}

		if ((cmdIter->second == 0) && (words.size() >= 2))
		{
			const std::string &imageName = words[1];
			float length = MIN_FRAME_LENGTH;

			if (words.size() >= 3)
				length = std::max(length, float(std::atof(words[2].c_str())));

			const auto imageIdxIt = imageIdxMap.find(imageName);

			if (imageIdxIt != imageIdxMap.end())
			{
				frames.emplace_back(imageIdxIt->second, length);
				hwCursor->PushFrame(imageIdxIt->second, length);
				continue;
			}

			ImageData image;

			if (LoadCursorImage(imageName, image))
			{
				hwCursor->SetDelay(length);
				imageIdxMap[imageName] = images.size();

				images.push_back(image);
				frames.emplace_back(images.size() - 1, length);
			}

			continue;
		}

		if ((cmdIter->second == 1) && (!words.empty()))
		{
			if (words[1] == "topleft")
			{
				hwCursor->SetHotSpot(hotSpot = TopLeft);
				continue;
			}
			if (words[1] == "center")
			{
				hwCursor->SetHotSpot(hotSpot = Center);
				continue;
			}

			LOG_L(L_ERROR, "[MouseCursor::%s] unknown hotspot \"%s\" in file \"%s\"", __func__, words[1].c_str(), specFileName.c_str());
			continue;
		}

		if ((cmdIter->second == 2) && (!words.empty()))
		{
			lastFrame = atoi(words[1].c_str());
			continue;
		}
	}

	return (IsValid());
}

bool CMouseCursor::BuildFromFileNames(const std::string &name, int lastFrame)
{
	RECOIL_DETAILED_TRACY_ZONE;
	// find the image file type to use
	const char *ext = "";
	const char *exts[] = {"png", "tga", "bmp"};

	char animFrameStr[512];

	for (auto &e : exts)
	{
		memset(animFrameStr, 0, sizeof(animFrameStr));
		snprintf(animFrameStr, sizeof(animFrameStr) - 1, "anims/%s_%d.%s", name.c_str(), 0, ext = e);

		if (CFileHandler::FileExists(animFrameStr, SPRING_VFS_RAW_FIRST))
			break;
	}

	while (int(frames.size()) < lastFrame)
	{
		memset(animFrameStr, 0, sizeof(animFrameStr));
		snprintf(animFrameStr, sizeof(animFrameStr) - 1, "anims/%s_%d.%s", name.c_str(), static_cast<int>(frames.size()), ext);

		ImageData image;

		if (!LoadCursorImage(animFrameStr, image))
			break;

		images.push_back(image);
		frames.emplace_back(images.size() - 1, float(DEF_FRAME_LENGTH));
	}

	return (IsValid());
}

float4 CMouseCursor::CalcFrameMatrixParams(const float3 &winCoors, const float2 &winScale) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (winCoors.z > 1.0f || frames.empty())
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const FrameData &frame = frames[currentFrame];
	const ImageData &image = images[frame.imageIdx];

	const float qis = winScale.x;
	const float xs = image.xAlignedSize * qis;
	const float ys = image.yAlignedSize * qis;

	const float rxs = xs * globalRendering->pixelX;
	const float rys = ys * globalRendering->pixelY;

	// center on hotspot
	const float xp = (winCoors.x - (xofs * qis)) * globalRendering->pixelX;
	const bool isVulkan = (globalRendering != nullptr) &&
						  (globalRendering->graphicsBackend != nullptr) &&
						  (globalRendering->graphicsBackend->Type() != gfx::BackendType::OpenGL);

	// In Vulkan (Y-Down), we simply subtract the hotspot offset from the top.
	// In OpenGL (Y-Up), we had to find the bottom of the quad relative to the hotspot.
	const float yp = isVulkan ? (winCoors.y - (yofs * qis)) * globalRendering->pixelY
							  : (winCoors.y - winScale.y * (ys - (yofs * qis))) * globalRendering->pixelY;

	return {xp, yp, rxs, rys};
}

#if 0
}
bool CMouseCursor::LoadDummyImage()
{
	RECOIL_DETAILED_TRACY_ZONE;
	ImageData id;
	CBitmap bn;

	bn.Alloc(16, 16, 4);
	bn.Fill(SColor(255, 255 * 0, 255, 255));

	id.texture = bn.CreateTexture();
	id.xOrigSize = bn.xsize;
	id.yOrigSize = bn.ysize;
	id.xAlignedSize = bn.xsize;
	id.yAlignedSize = bn.ysize;

	images.emplace_back(id);
	frames.emplace_back(images.size() - 1, float(DEF_FRAME_LENGTH));

	hwCursor->PushImage(bn.xsize, bn.ysize, bn.GetRawMem());
}
#endif

bool CMouseCursor::LoadCursorImage(const std::string &name, ImageData &image)
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (!CFileHandler::FileExists(name, SPRING_VFS_RAW_FIRST))
		return false;

	CBitmap b;
	if (!b.Load(name))
	{
		LOG_L(L_ERROR, "[MouseCursor::%s] bad image file \"%s\"", __func__, name.c_str());
		return false;
	}

	const bool isVulkan = (globalRendering != nullptr) &&
						  (globalRendering->graphicsBackend != nullptr) &&
						  (globalRendering->graphicsBackend->Type() != gfx::BackendType::OpenGL);

	// hardcoded bmp transparency mask
	if (FileSystem::GetExtension(name) == "bmp")
		b.SetTransparent(SColor(84, 84, 252));

	if (hwCursor->NeedsYFlip())
	{
		// WINDOWS
		b.ReverseYAxis();
		hwCursor->PushImage(b.xsize, b.ysize, b.GetRawMem());
		if (isVulkan)
			b.ReverseYAxis();
	}
	else
	{
		// X11
		hwCursor->PushImage(b.xsize, b.ysize, b.GetRawMem());
		if (!isVulkan)
			b.ReverseYAxis();
	}

	const int nx = std::bit_ceil<uint32_t>(b.xsize);
	const int ny = std::bit_ceil<uint32_t>(b.ysize);

	if (b.xsize != nx || b.ysize != ny)
	{
		CBitmap bn;
		bn.Alloc(nx, ny);
		bn.CopySubImage(b, 0, ny - b.ysize);

		image.texture = bn.CreateTexture();
		image.xOrigSize = b.xsize;
		image.yOrigSize = b.ysize;
		image.xAlignedSize = bn.xsize;
		image.yAlignedSize = bn.ysize;
	}
	else
	{
		image.texture = b.CreateTexture();
		image.xOrigSize = b.xsize;
		image.yOrigSize = b.ysize;
		image.xAlignedSize = b.xsize;
		image.yAlignedSize = b.ysize;
	}

	return true;
}

void CMouseCursor::Draw(int x, int y, float scale) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (frames.empty())
		return;

	const FrameData &frame = frames[currentFrame];
	const ImageData &image = images[frame.imageIdx];

	const bool isVulkan = (globalRendering != nullptr) &&
						  (globalRendering->graphicsBackend != nullptr) &&
						  (globalRendering->graphicsBackend->Type() != gfx::BackendType::OpenGL);

	if (isVulkan)
	{
		auto *backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
		auto *vulkanBackend = dynamic_cast<gfx::VulkanGraphicsBackend *>(backend);
		if (vulkanBackend != nullptr)
		{
			static std::unique_ptr<gfx::IVertexBuffer> debugVertexBuffer;
			static std::unique_ptr<gfx::IVertexBuffer> debugIndexBuffer;

			if (debugVertexBuffer == nullptr)
			{
				gfx::BufferCreateInfo ci;
				ci.sizeBytes = 1u;
				ci.usage = gfx::BufferUsage::Dynamic;
				ci.memoryAccess = gfx::MemoryAccess::CpuToGpu;
				ci.debugName = "CursorDebugVertices";
				debugVertexBuffer = backend->CreateVertexBuffer(ci);
			}

			if (debugIndexBuffer == nullptr)
			{
				gfx::BufferCreateInfo ci;
				ci.sizeBytes = 1u;
				ci.usage = gfx::BufferUsage::Dynamic;
				ci.memoryAccess = gfx::MemoryAccess::CpuToGpu;
				ci.debugName = "CursorDebugIndices";
				debugIndexBuffer = backend->CreateVertexBuffer(ci);
			}

			if ((debugVertexBuffer != nullptr) && (debugIndexBuffer != nullptr))
			{
				const float size = std::max(6.0f, 6.0f * std::abs(scale));
				const float left = static_cast<float>(x) - size;
				const float right = static_cast<float>(x) + size;
				const float top = static_cast<float>(y) - size;
				const float bottom = static_cast<float>(y) + size;

				const SColor color(255, 255, 0, 255);

				std::array<VA_TYPE_TC, 4> vertices = {{
					{float3{left, top, 0.0f}, 0.0f, 0.0f, color},
					{float3{right, top, 0.0f}, 1.0f, 0.0f, color},
					{float3{right, bottom, 0.0f}, 1.0f, 1.0f, color},
					{float3{left, bottom, 0.0f}, 0.0f, 1.0f, color},
				}};

				std::array<std::uint16_t, 6> indices = {{0u, 1u, 2u, 0u, 2u, 3u}};

				const std::size_t vertexBytes = vertices.size() * sizeof(VA_TYPE_TC);
				const std::size_t indexBytes = indices.size() * sizeof(std::uint16_t);

				if (debugVertexBuffer->SizeBytes() < vertexBytes)
					debugVertexBuffer->Resize(vertexBytes, false);

				if (debugIndexBuffer->SizeBytes() < indexBytes)
					debugIndexBuffer->Resize(indexBytes, false);

				debugVertexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(vertices.data()), vertexBytes));
				debugIndexBuffer->Update(std::span<const std::byte>(reinterpret_cast<const std::byte *>(indices.data()), indexBytes));

				gfx::TexturedVertexLayout vertexLayout;
				vertexLayout.strideBytes = sizeof(VA_TYPE_TC);
				vertexLayout.positionOffsetBytes = offsetof(VA_TYPE_TC, pos);
				vertexLayout.texCoordOffsetBytes = offsetof(VA_TYPE_TC, s);
				vertexLayout.colorOffsetBytes = offsetof(VA_TYPE_TC, c);

				gfx::TexturedIndexedBatchDesc batch;
				batch.firstIndex = 0u;
				batch.indexCount = static_cast<std::uint32_t>(indices.size());

				gfx::TexturedBatchState state;
				state.depthTest = false;
				state.blend = true;
				state.premultipliedAlpha = false;
				state.useDefaultBlendFunc = true;

				vulkanBackend->DrawTexturedIndexedBatches(
					*debugVertexBuffer,
					*debugIndexBuffer,
					nullptr,
					std::span<const gfx::TexturedIndexedBatchDesc>(&batch, 1),
					vertexLayout,
					gfx::IndexElementType::UInt16,
					state);
			}
		}

		return;
	}

	auto &rb = RenderBuffer::GetTypedRenderBuffer<VA_TYPE_TC>();
	auto &sh = rb.GetShader();

	const float finalY = isVulkan ? (y * 1.0f) : (globalRendering->viewSizeY - y) * 1.0f;
	const float depthZ = isVulkan ? 0.5f : 0.0f;
	const float3 winCoors = {x * 1.0f, finalY, depthZ};
	const float2 winScale = {std::max(scale, -scale), 1.0f};
	const float4 &matParams = CalcFrameMatrixParams(winCoors, winScale);

	CMatrix44f cursorMat;
	cursorMat.Translate(matParams.x, matParams.y, 0.0f);
	cursorMat.Scale({matParams.z, matParams.w, 1.0f});

	rb.AddQuadTriangles(CURSOR_VERTS);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadMatrixf(cursorMat);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadMatrixf(CMatrix44f::ClipOrthoProj01(globalRendering->supportClipSpaceControl * 1.0f));

	const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindTexture(GL_TEXTURE_2D, image.texture);

	sh.Enable();
	sh.SetUniform("alphaCtrl", 0.01f, 1.0f, 0.0f, 0.0f); // test > 0.01

	rb.Submit(GL_TRIANGLES);

	sh.SetUniform("alphaCtrl", 0.0f, 0.0f, 0.0f, 1.0f); // no test
	sh.Disable();

	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glDisable(GL_BLEND);
	if (depthWasEnabled)
		glEnable(GL_DEPTH_TEST);
}

void CMouseCursor::Update()
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (frames.empty())
		return;

	animTime = math::fmod(animTime + globalRendering->lastFrameTime * 0.001f, animPeriod);

	if (animTime < frames[currentFrame].startTime)
	{
		currentFrame = 0;
		return;
	}

	while (animTime > frames[currentFrame].endTime)
	{
		currentFrame += 1;
		currentFrame %= frames.size();
	}
}

void CMouseCursor::BindTexture() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (frames.empty())
		return;

	const FrameData &frame = frames[currentFrame];
	const ImageData &image = images[frame.imageIdx];

	glBindTexture(GL_TEXTURE_2D, image.texture);
}

void CMouseCursor::BindHwCursor() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(hwCursor != nullptr);
	hwCursor->Bind();
}

void CMouseCursor::UpdateHwCursor() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(hwCursor != nullptr);
	hwCursor->Update(animTime);
}
