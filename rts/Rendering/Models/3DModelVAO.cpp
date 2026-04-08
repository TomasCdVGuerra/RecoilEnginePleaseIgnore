/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "3DModelVAO.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <iterator>

#include "3DModel.hpp"
#include "3DModelPiece.hpp"
#include "IModelParser.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Gfx/GL/GLVertexBuffer.h"
#include "Rendering/ModelsDataUploader.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Features/Feature.h"

#include "System/Log/ILog.h"
#include "System/Misc/TracyDefs.h"


namespace
{

	gfx::PrimitiveTopology ToPrimitiveTopology(GLenum prim)
	{
		switch (prim) {
			case GL_TRIANGLES: return gfx::PrimitiveTopology::Triangles;
			case GL_LINES:     return gfx::PrimitiveTopology::Lines;
			case GL_LINE_STRIP:return gfx::PrimitiveTopology::LineStrip;
			default: {
				LOG_L(L_WARNING, "[S3DModelVAO::ToPrimitiveTopology] Unsupported primitive %u, defaulting to triangles", prim);
				return gfx::PrimitiveTopology::Triangles;
			}
		}
	}

	GLuint GetGLBufferId(const std::unique_ptr<gfx::IVertexBuffer>& buffer)
	{
		auto* glBuffer = dynamic_cast<gfx::GLVertexBuffer*>(buffer.get());
		return (glBuffer != nullptr) ? glBuffer->GetBufferId() : 0u;
	}

	gfx::VertexLayoutDesc BuildVertexLayoutDesc()
	{
		gfx::VertexLayoutDesc layout;
		layout.bindings = {
			{0u, static_cast<std::uint32_t>(sizeof(SVertexData)), 0u, gfx::VertexInputRate::PerVertex},
			{1u, static_cast<std::uint32_t>(sizeof(SInstanceData)), 0u, gfx::VertexInputRate::PerInstance},
		};

		layout.attributes = {
			{0u, 0u, gfx::VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SVertexData, pos))},
			{1u, 0u, gfx::VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SVertexData, normal))},
			{2u, 0u, gfx::VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SVertexData, sTangent))},
			{3u, 0u, gfx::VertexFormat::Float3, static_cast<std::uint32_t>(offsetof(SVertexData, tTangent))},
			{4u, 0u, gfx::VertexFormat::Float4, static_cast<std::uint32_t>(offsetof(SVertexData, texCoords[0]))},
			{5u, 0u, gfx::VertexFormat::UInt3, static_cast<std::uint32_t>(offsetof(SVertexData, boneIDsLow))},
			{6u, 1u, gfx::VertexFormat::UInt4, static_cast<std::uint32_t>(offsetof(SInstanceData, matOffset))},
		};

		return layout;
	}

} // namespace


S3DModelVAO::S3DModelVAO()
	: legacyVertVBO{GL_ARRAY_BUFFER, false}
	, legacyIndxVBO{GL_ELEMENT_ARRAY_BUFFER, false}
{
	RECOIL_DETAILED_TRACY_ZONE;
	vertData.reserve(VERT_SIZE0);
	indxData.reserve(INDX_SIZE0);

	auto* backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	assert(backend != nullptr);

	if (backend == nullptr)
		return;

	backendVerts = backend->CreateVertexBuffer(gfx::BufferCreateInfo{
		.sizeBytes = 0,
		.usage = gfx::BufferUsage::Static,
		.memoryAccess = gfx::MemoryAccess::CpuToGpu,
		.readable = false,
		.debugName = "S3DModelVAO::backendVerts",
	});

	backendIndx = backend->CreateVertexBuffer(gfx::BufferCreateInfo{
		.sizeBytes = 0,
		.usage = gfx::BufferUsage::Static,
		.memoryAccess = gfx::MemoryAccess::CpuToGpu,
		.readable = false,
		.debugName = "S3DModelVAO::backendIndx",
	});

	backendInst = backend->CreateVertexBuffer(gfx::BufferCreateInfo{
		.sizeBytes = INSTANCE_BUFFER_NUM_ELEMS * sizeof(SInstanceData),
		.usage = gfx::BufferUsage::Stream,
		.memoryAccess = gfx::MemoryAccess::CpuToGpu,
		.readable = false,
		.debugName = "S3DModelVAO::backendInst",
	});

	CreateVAO();
}

std::unique_ptr<S3DModelVAO> S3DModelVAO::instance = nullptr;

void S3DModelVAO::ProcessVertices(const S3DModel* model)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(model);
	assert(model->loadStatus == S3DModel::LoadStatus::LOADING);

	if (const auto* root = model->GetRootPiece(); root->vertIndex != ~0u)
		return;

	uint32_t vertIndex = static_cast<uint32_t>(vertData.size());
	for (auto* modelPiece : model->pieceObjects) {
		modelPiece->vertIndex = vertIndex;
		const auto& modelPieceVerts = modelPiece->GetVerticesVec();
		vertIndex += modelPieceVerts.size();
		vertData.insert(vertData.end(), modelPieceVerts.begin(), modelPieceVerts.end()); //append
	}
}

void S3DModelVAO::ProcessIndicies(S3DModel* model)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(model);
	if (model->indxStart != ~0u)
		return;

	//models should know their index offset
	model->indxStart = static_cast<uint32_t>(std::distance(indxData.cbegin(), indxData.cend()));

	for (auto* modelPiece : model->pieceObjects) {
		if (!modelPiece->HasGeometryData()) {
			modelPiece->indxStart = static_cast<uint32_t>(indxData.size());
			modelPiece->indxCount = 0;
			continue;
		}

		const auto& modelPieceIndcs = modelPiece->GetIndicesVec();
		indxData.insert(indxData.end(), modelPieceIndcs.begin(), modelPieceIndcs.end()); //append

		const auto endIdx = indxData.end();
		const auto begIdx = endIdx - modelPieceIndcs.size();

		std::for_each(begIdx, endIdx, [offset = modelPiece->vertIndex](uint32_t& indx) { indx += offset; }); // add per piece vertex offset to indices

		//model pieces should know their index offset
		modelPiece->indxStart = static_cast<uint32_t>(std::distance(indxData.begin(), begIdx));

		//model pieces should know their index count
		modelPiece->indxCount = static_cast<uint32_t>(modelPieceIndcs.size());
	}
	//models should know their index count
	model->indxCount = static_cast<uint32_t>(indxData.size() - model->indxStart);

	//add shatter indices to the end of indxData
	for (const auto* modelPiece : model->pieceObjects) {
		if (!modelPiece->HasGeometryData())
			continue;

		const auto& mdlPcsShatIndcs = modelPiece->GetShatterIndicesVec();

		indxData.insert(indxData.end(), mdlPcsShatIndcs.begin(), mdlPcsShatIndcs.end()); //append

		const auto endIdx = indxData.end();
		const auto begIdx = endIdx - mdlPcsShatIndcs.size();

		std::for_each(begIdx, endIdx, [offset = modelPiece->vertIndex](uint32_t& indx) { indx += offset; }); // add per piece vertex offset to indices
	}
}

void S3DModelVAO::CreateVAO()
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	assert(backend != nullptr);

	if ((backend == nullptr) || (backendVerts == nullptr) || (backendIndx == nullptr) || (backendInst == nullptr))
		return;

	const std::array<gfx::VertexArrayBufferBinding, 2> vertexBuffers = {{
		{0u, backendVerts.get()},
		{1u, backendInst.get()},
	}};

	backendVAO = backend->CreateVertexArray(BuildVertexLayoutDesc(), vertexBuffers, backendIndx.get());
	assert(backendVAO != nullptr);

	RefreshLegacyVBOViews();
}

void S3DModelVAO::UploadVBOs()
{
	RECOIL_DETAILED_TRACY_ZONE;
	static constexpr size_t MEM_STEP = 8 * 1024 * 1024;
	bool reinitVAO = (backendVAO == nullptr);
	bool refreshLegacyViews = false;

	if ((backendVerts == nullptr) || (backendIndx == nullptr))
		return;

	if (vertData.size() > vertUploadIndex) {
		assert(!safeToDeleteVectors);
		const size_t reqSize = AlignUp(std::max(vertData.size(), S3DModelVAO::VERT_SIZE0) * sizeof(SVertexData), MEM_STEP);
		reinitVAO |= (reqSize > backendVerts->SizeBytes());
		backendVerts->Resize(reqSize, true);

		const std::span<const SVertexData> uploadData(
			vertData.data() + vertUploadIndex,
			vertData.size() - vertUploadIndex);
		backendVerts->Update(std::as_bytes(uploadData), vertUploadIndex * sizeof(SVertexData));

		refreshLegacyViews = true;
		vertUploadIndex = vertData.size();
		vertUploadSize = vertUploadIndex;
	}

	if (indxData.size() > indxUploadIndex) {
		assert(!safeToDeleteVectors);
		const size_t reqSize = AlignUp(std::max(indxData.size(), S3DModelVAO::INDX_SIZE0) * sizeof(   uint32_t), MEM_STEP);
		reinitVAO |= (reqSize > backendIndx->SizeBytes());
		backendIndx->Resize(reqSize, true);

		const std::span<const uint32_t> uploadData(
			indxData.data() + indxUploadIndex,
			indxData.size() - indxUploadIndex);
		backendIndx->Update(std::as_bytes(uploadData), indxUploadIndex * sizeof(uint32_t));

		refreshLegacyViews = true;
		indxUploadIndex = indxData.size();
		indxUploadSize = indxUploadIndex;
	}

	if (reinitVAO)
		CreateVAO();
	else if (refreshLegacyViews)
		RefreshLegacyVBOViews();

	if (safeToDeleteVectors && !vertData.empty()) {
		// all models have been uploaded in the calls above
		// safe to clear CPU copy of the data
		vertData.clear();
		indxData.clear();
		vertUploadIndex = 0;
		indxUploadIndex = 0;
	}
}

void S3DModelVAO::Init()
{
	RECOIL_DETAILED_TRACY_ZONE;
	Kill();
	instance = std::make_unique<S3DModelVAO>();
}

void S3DModelVAO::Kill()
{
	RECOIL_DETAILED_TRACY_ZONE;
	instance = nullptr;
}

void S3DModelVAO::RefreshLegacyVBOViews()
{
	RECOIL_DETAILED_TRACY_ZONE;

	const GLsizeiptr vertSize = (backendVerts != nullptr) ? static_cast<GLsizeiptr>(backendVerts->SizeBytes()) : 0;
	const GLsizeiptr indxSize = (backendIndx != nullptr) ? static_cast<GLsizeiptr>(backendIndx->SizeBytes()) : 0;

	legacyVertVBO.AttachExternal(GetGLBufferId(backendVerts), vertSize, GL_ARRAY_BUFFER, GL_STATIC_DRAW);
	legacyIndxVBO.AttachExternal(GetGLBufferId(backendIndx), indxSize, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW);
}

void S3DModelVAO::Bind() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(backendVAO != nullptr);

	if (backendVAO != nullptr)
		backendVAO->Bind();
}

void S3DModelVAO::Unbind() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(backendVAO != nullptr);

	if (backendVAO != nullptr)
		backendVAO->Unbind();
}

void S3DModelVAO::BindLegacyVertexAttribsAndVBOs() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(legacyVertVBO.GetIdRaw() != 0u);
	assert(legacyIndxVBO.GetIdRaw() != 0u);

	legacyVertVBO.Bind();
	legacyIndxVBO.Bind();
	legacyAttribsBound = true;

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, pos)));

	glEnableClientState(GL_NORMAL_ARRAY);
	glNormalPointer(GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, normal)));

	glClientActiveTexture(GL_TEXTURE0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, texCoords[0])));

	glClientActiveTexture(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, texCoords[1])));

	glClientActiveTexture(GL_TEXTURE5);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(3, GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, sTangent)));

	glClientActiveTexture(GL_TEXTURE6);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(3, GL_FLOAT, sizeof(SVertexData), legacyVertVBO.GetPtr(offsetof(SVertexData, tTangent)));
}

void S3DModelVAO::UnbindLegacyVertexAttribsAndVBOs() const
{
	RECOIL_DETAILED_TRACY_ZONE;
	legacyAttribsBound = false;

	glClientActiveTexture(GL_TEXTURE6);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glClientActiveTexture(GL_TEXTURE5);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);

	if (legacyIndxVBO.bound)
		legacyIndxVBO.Unbind();

	if (legacyVertVBO.bound)
		legacyVertVBO.Unbind();
}

void S3DModelVAO::DrawElements(GLenum prim, uint32_t vboIndxStart, uint32_t vboIndxCount) const
{
	RECOIL_DETAILED_TRACY_ZONE;
	if (legacyAttribsBound) {
		glDrawElements(prim, vboIndxCount, GL_UNSIGNED_INT, legacyIndxVBO.GetPtr(vboIndxStart * sizeof(uint32_t)));
		return;
	}

	auto* backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	if ((backend == nullptr) || (backendVAO == nullptr))
		return;

	backend->DrawIndexed(
		*backendVAO,
		ToPrimitiveTopology(prim),
		gfx::IndexedDrawDesc{vboIndxCount, vboIndxStart, 0},
		gfx::IndexElementType::UInt32);
}

template<typename TObj>
bool S3DModelVAO::AddToSubmissionImpl(const TObj* obj, uint32_t indexStart, uint32_t indexCount, uint8_t teamID, uint8_t drawFlags)
{
	RECOIL_DETAILED_TRACY_ZONE;
	const auto traIndex = transformsUploader.GetElemOffset(obj);
	if (traIndex == TransformsMemStorage::INVALID_INDEX)
		return false;

	const auto uniIndex = modelUniformsStorage.GetObjOffset(obj); //doesn't need to exist for defs and models. Don't check for validity

	uint16_t numPieces = 0;
	size_t bposeIndex = 0;
	if constexpr (std::is_same<TObj, S3DModel>::value) {
		numPieces = static_cast<uint16_t>(obj->numPieces);
		bposeIndex = transformsUploader.GetElemOffset(obj);
	}
	else {
		numPieces = static_cast<uint16_t>(obj->model->numPieces);
		bposeIndex = transformsUploader.GetElemOffset(obj->model);
	}

	if (bposeIndex == TransformsMemStorage::INVALID_INDEX)
		return false;

	auto& modelInstanceData = modelDataToInstance[SIndexAndCount{ indexStart, indexCount }];
	modelInstanceData.emplace_back(SInstanceData(
		static_cast<uint32_t>(traIndex),
		teamID,
		drawFlags,
		numPieces,
		static_cast<uint32_t>(uniIndex),
		static_cast<uint32_t>(bposeIndex)
	));

	return true;
}

bool S3DModelVAO::AddToSubmission(const S3DModel* model, uint8_t teamID, uint8_t drawFlags)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(model);

	return AddToSubmissionImpl(model, model->indxStart, model->indxCount, teamID, drawFlags);
}

bool S3DModelVAO::AddToSubmission(const CUnit* unit)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(unit);

	const S3DModel* model = unit->model;
	assert(model);

	return AddToSubmissionImpl(unit, model->indxStart, model->indxCount, unit->team, unit->drawFlag);
}

bool S3DModelVAO::AddToSubmission(const CFeature* feature)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(feature);

	const S3DModel* model = feature->model;
	assert(model);

	return AddToSubmissionImpl(feature, model->indxStart, model->indxCount, feature->team, feature->drawFlag);
}

bool S3DModelVAO::AddToSubmission(const UnitDef* unitDef, uint8_t teamID)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(unitDef);

	const S3DModel* model = unitDef->model;
	assert(model);

	return AddToSubmissionImpl(unitDef, model->indxStart, model->indxCount, teamID, 0);
}


void S3DModelVAO::Submit(GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	if ((backend == nullptr) || (backendVAO == nullptr) || (backendInst == nullptr))
		return;

	static std::vector<gfx::IndexedIndirectDrawCommand> submitCmds;
	submitCmds.clear();

	batchedBaseInstance = 0u;

	static std::vector<SInstanceData> allRenderModelData;
	allRenderModelData.reserve(INSTANCE_BUFFER_NUM_BATCHED);
	allRenderModelData.clear();

	for (const auto& [indxCount, renderModelData] : modelDataToInstance) {
		if (allRenderModelData.size() + renderModelData.size() >= INSTANCE_BUFFER_NUM_BATCHED)
			continue;

		submitCmds.emplace_back(gfx::IndexedIndirectDrawCommand{
			.indexCount = indxCount.count,
			.instanceCount = static_cast<uint32_t>(renderModelData.size()),
			.firstIndex = indxCount.index,
			.baseVertex = 0,
			.firstInstance = batchedBaseInstance,
		});

		allRenderModelData.insert(allRenderModelData.end(), renderModelData.cbegin(), renderModelData.cend());
		batchedBaseInstance += renderModelData.size();
	}

	if (submitCmds.empty())
		return;

	backendInst->Update(std::as_bytes(std::span<const SInstanceData>(allRenderModelData)), 0u);

	if (bindUnbind)
		Bind();

	backend->MultiDrawIndexedIndirect(*backendVAO, ToPrimitiveTopology(mode), submitCmds, gfx::IndexElementType::UInt32);

	if (bindUnbind)
		Unbind();

	modelDataToInstance.clear();
}

template<typename TObj>
bool S3DModelVAO::SubmitImmediatelyImpl(const TObj* obj, uint32_t indexStart, uint32_t indexCount, uint8_t teamID, uint8_t drawFlags, GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	auto* backend = (globalRendering != nullptr) ? globalRendering->graphicsBackend.get() : nullptr;
	if ((backend == nullptr) || (backendVAO == nullptr) || (backendInst == nullptr))
		return false;

	std::size_t traIndex = transformsUploader.GetElemOffset(obj);
	if (traIndex == TransformsMemStorage::INVALID_INDEX)
		return false;

	const auto uniIndex = modelUniformsStorage.GetObjOffset(obj); //doesn't need to exist for defs. Don't check for validity

	uint16_t numPieces = 0;
	size_t bposeIndex = 0;
	if constexpr (std::is_same<TObj, S3DModel>::value) {
		numPieces = static_cast<uint16_t>(obj->numPieces);
		bposeIndex = transformsUploader.GetElemOffset(obj);
	}
	else {
		numPieces = static_cast<uint16_t>(obj->model->numPieces);
		bposeIndex = transformsUploader.GetElemOffset(obj->model);
	}

	SInstanceData instanceData(static_cast<uint32_t>(traIndex), teamID, drawFlags, numPieces, uniIndex, bposeIndex);
	const uint32_t immediateBaseInstanceAbs = INSTANCE_BUFFER_NUM_BATCHED + immediateBaseInstance;

	backendInst->Update(
		std::as_bytes(std::span<const SInstanceData>(&instanceData, 1)),
		immediateBaseInstanceAbs * sizeof(SInstanceData));

	immediateBaseInstance = (immediateBaseInstance + 1) % INSTANCE_BUFFER_NUM_IMMEDIATE;

	if (bindUnbind)
		Bind();

	if (backend->Type() == gfx::BackendType::OpenGL) {
		// As of 01.05.2023 AMD Windows drivers do not support baseInstance field of SDrawElementsIndirectCommand.
		// At the same time AMD Windows drivers sometimes crash on glDrawElementsInstancedBaseInstance.
		// Revert to multi-draw indirect as it works reliably.
		const std::array<gfx::IndexedIndirectDrawCommand, 1> submitCmds = {{
			gfx::IndexedIndirectDrawCommand{
				.indexCount = indexCount,
				.instanceCount = 1,
				.firstIndex = indexStart,
				.baseVertex = 0,
				.firstInstance = immediateBaseInstanceAbs,
			}
		}};

		backend->MultiDrawIndexedIndirect(*backendVAO, ToPrimitiveTopology(mode), submitCmds, gfx::IndexElementType::UInt32);
	}
	else {
		backend->DrawIndexedInstanced(
			*backendVAO,
			ToPrimitiveTopology(mode),
			gfx::IndexedInstancedDrawDesc{
				.indexCount = indexCount,
				.firstIndex = indexStart,
				.baseVertex = 0,
				.instanceCount = 1,
				.firstInstance = immediateBaseInstanceAbs,
			},
			gfx::IndexElementType::UInt32);
	}

	if (bindUnbind)
		Unbind();

	return true;
}

bool S3DModelVAO::SubmitImmediately(const S3DModel* model, uint8_t teamID, uint8_t drawFlags, GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(model);
	return SubmitImmediatelyImpl(model, model->indxStart, model->indxCount, teamID, drawFlags, mode, bindUnbind);
}

bool S3DModelVAO::SubmitImmediately(const CUnit* unit, const GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(unit);

	const S3DModel* model = unit->model;
	assert(model);

	return SubmitImmediatelyImpl(unit, model->indxStart, model->indxCount, unit->team, unit->drawFlag, mode, bindUnbind);
}

bool S3DModelVAO::SubmitImmediately(const CFeature* feature, GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(feature);

	const S3DModel* model = feature->model;
	assert(model);

	return SubmitImmediatelyImpl(feature, model->indxStart, model->indxCount, feature->team, feature->drawFlag, mode, bindUnbind);
}

bool S3DModelVAO::SubmitImmediately(const UnitDef* unitDef, int teamID, GLenum mode, bool bindUnbind)
{
	RECOIL_DETAILED_TRACY_ZONE;
	assert(unitDef);

	const S3DModel* model = unitDef->model;
	assert(model);

	return SubmitImmediatelyImpl(unitDef, model->indxStart, model->indxCount, teamID, 0, mode, bindUnbind);
}
