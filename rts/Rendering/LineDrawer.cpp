/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// TODO: move this out of Sim, this is rendering code!

#include "LineDrawer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Rendering/GlobalRendering.h"
#include "Game/UI/CommandColors.h"
#include "System/Log/ILog.h"

namespace
{

	std::uint32_t ToUint32(std::size_t value, const char *context)
	{
		constexpr std::size_t maxValue = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
		if (value > maxValue)
		{
			LOG_L(L_WARNING, "[CLineDrawer::%s] value (%zu) exceeds uint32 max (%zu), clamping", context, value, maxValue);
			return std::numeric_limits<std::uint32_t>::max();
		}

		return static_cast<std::uint32_t>(value);
	}

} // namespace

CLineDrawer lineDrawer;

CLineDrawer::CLineDrawer()
	: lineStipple(false), useColorRestarts(false), useRestartColor(false), restartAlpha(0.0f), restartColor(nullptr), lastPos(ZeroVector), lastColor(nullptr), stippleTimer(0.0f)
{
	lines.reserve(32);
	stippled.reserve(32);
	lineVertices.reserve(256);
	stippledVertices.reserve(256);
	lineBatches.reserve(32);
	stippledBatches.reserve(32);
}

void CLineDrawer::UpdateLineStipple()
{
	stippleTimer += (globalRendering->lastFrameTime * 0.001f * cmdColors.StippleSpeed());
	stippleTimer = std::fmod(stippleTimer, (16.0f / 20.0f));
}

void CLineDrawer::SetupLineStipple()
{
	const unsigned int stipPat = (0xffff & cmdColors.StipplePattern());
	if ((stipPat != 0x0000) && (stipPat != 0xffff))
	{
		lineStipple = true;
	}
	else
	{
		lineStipple = false;
		stippleState.enabled = false;
		return;
	}

	const unsigned int fullPat = (stipPat << 16) | (stipPat & 0x0000ffff);
	const int shiftBits = 15 - (int(stippleTimer * 20.0f) % 16);

	stippleState.enabled = true;
	stippleState.factor = std::max(1u, cmdColors.StippleFactor());
	stippleState.pattern = static_cast<std::uint16_t>(fullPat >> shiftBits);
}

void CLineDrawer::DrawAll()
{
	if (lines.empty() && stippled.empty())
		return;

	auto *backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	if (backend == nullptr)
	{
		LOG_L(L_WARNING, "[CLineDrawer::%s] graphicsBackend is null, dropping queued lines", __func__);
		lines.clear();
		stippled.clear();
		return;
	}

	auto buildUploadData = [](const std::vector<LinePair> &linePairs, std::vector<gfx::LineVertexPC> &vertices, std::vector<gfx::LineBatchDesc> &batches)
	{
		vertices.clear();
		batches.clear();

		for (const LinePair &pair : linePairs)
		{
			const std::size_t vertsCount = pair.verts.size() / 3u;
			const std::size_t colorsCount = pair.colors.size() / 4u;
			const std::size_t numVertices = std::min(vertsCount, colorsCount);

			if (numVertices == 0)
				continue;

			const std::size_t firstVertex = vertices.size();
			vertices.resize(firstVertex + numVertices);

			for (std::size_t i = 0; i < numVertices; ++i)
			{
				auto &dst = vertices[firstVertex + i];

				const std::size_t vi = i * 3u;
				const std::size_t ci = i * 4u;

				dst.px = pair.verts[vi + 0u];
				dst.py = pair.verts[vi + 1u];
				dst.pz = pair.verts[vi + 2u];

				dst.r = pair.colors[ci + 0u];
				dst.g = pair.colors[ci + 1u];
				dst.b = pair.colors[ci + 2u];
				dst.a = pair.colors[ci + 3u];
			}

			gfx::LineBatchDesc batch;
			batch.primitive = pair.type;
			batch.firstVertex = ToUint32(firstVertex, "BuildLineUploadData::firstVertex");
			batch.vertexCount = ToUint32(numVertices, "BuildLineUploadData::vertexCount");

			batches.push_back(batch);
		}
	};

	buildUploadData(lines, lineVertices, lineBatches);
	buildUploadData(stippled, stippledVertices, stippledBatches);

	auto uploadVertices = [backend](std::unique_ptr<gfx::IVertexBuffer> &vertexBuffer, const std::vector<gfx::LineVertexPC> &vertices, const char *debugName) -> bool
	{
		if (vertices.empty())
			return false;

		const std::size_t sizeBytes = vertices.size() * sizeof(gfx::LineVertexPC);

		if (!vertexBuffer)
		{
			gfx::BufferCreateInfo ci;
			ci.sizeBytes = sizeBytes;
			ci.usage = gfx::BufferUsage::Dynamic;
			ci.memoryAccess = gfx::MemoryAccess::CpuToGpu;
			ci.readable = false;
			ci.debugName = debugName;

			vertexBuffer = backend->CreateVertexBuffer(ci);
			if (!vertexBuffer)
			{
				LOG_L(L_WARNING, "[CLineDrawer::%s] Failed to create vertex buffer (%s)", __func__, debugName);
				return false;
			}
		}

		if (vertexBuffer->SizeBytes() < sizeBytes)
		{
			vertexBuffer->Resize(sizeBytes, false);
		}

		const std::byte *rawPtr = reinterpret_cast<const std::byte *>(vertices.data());
		std::span<const std::byte> uploadData(rawPtr, sizeBytes);

		bool uploaded = false;

		if (vertexBuffer->IsMappable())
		{
			auto mappedData = vertexBuffer->MapWrite(0u, sizeBytes);

			if (mappedData.size() >= sizeBytes)
			{
				std::memcpy(mappedData.data(), rawPtr, sizeBytes);
				uploaded = true;
			}

			vertexBuffer->UnmapWrite();
		}

		if (!uploaded)
		{
			vertexBuffer->Update(uploadData, 0u);
		}

		return true;
	};

	const gfx::LineStippleState noStipple = {};

	if (!lineBatches.empty() && uploadVertices(lineVertexBuffer, lineVertices, "LineDrawer::SolidLines"))
	{
		backend->DrawLineBatches(*lineVertexBuffer, std::span<const gfx::LineBatchDesc>(lineBatches.data(), lineBatches.size()), noStipple);
	}

	if (!stippledBatches.empty() && uploadVertices(stippledVertexBuffer, stippledVertices, "LineDrawer::StippledLines"))
	{
		backend->DrawLineBatches(*stippledVertexBuffer, std::span<const gfx::LineBatchDesc>(stippledBatches.data(), stippledBatches.size()), stippleState);
	}

	lines.clear();
	stippled.clear();
	lineVertices.clear();
	stippledVertices.clear();
	lineBatches.clear();
	stippledBatches.clear();
}
