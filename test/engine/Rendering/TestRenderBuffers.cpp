/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <catch_amalgamated.hpp>

#include <array>
#include <string>
#include <vector>

#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRenderingInfo.h"
#include "Rendering/Shaders/Shader.h"
#include "Rendering/GL/RenderBuffers.h"

// Provide storage for globalRenderingInfo in unit test environment
GlobalRenderingInfo globalRenderingInfo;

namespace Shader {

void IProgramObject::Enable() {}
void IProgramObject::Disable() {}
void IProgramObject::Release() {}

UniformState* IProgramObject::GetNewUniformState(const char* name)
{
	const auto hash = hashString(name);
	const auto it = uniformStates.emplace(hash, UniformState{name});
	UniformState* us = &(it.first->second);
	us->SetLocation(GetUniformLoc(name));
	return us;
}

IProgramObject::IProgramObject(const std::string& poName)
	: name(poName)
	, objID(0)
	, logReporting(true)
	, valid(false)
	, bound(false)
{
}

} // namespace Shader

namespace {

class MockProgramObject : public Shader::IProgramObject {
public:
	MockProgramObject() : Shader::IProgramObject("MockProgram") {}

	std::string lastUniformName;
	bool lastTranspose = true;
	std::array<float, 16> lastMatrix = {0.0f};
	int uploadCount = 0;

	void Link() override {}
	bool Validate() override { return true; }
	void Reload(bool, bool) override {}

	int GetUniformLoc(const char* name) override
	{
		lastUniformName = name;
		return 42;
	}

	void SetUniformMatrix4fv(int idx, bool transp, const float* v) override
	{
		lastTranspose = transp;
		std::copy(v, v + 16, lastMatrix.begin());
		uploadCount++;
	}

	int GetUniformType(const int) override { return 0; }

	void SetUniform1i(int, int) override {}
	void SetUniform2i(int, int, int) override {}
	void SetUniform3i(int, int, int, int) override {}
	void SetUniform4i(int, int, int, int, int) override {}
	void SetUniform1f(int, float) override {}
	void SetUniform2f(int, float, float) override {}
	void SetUniform3f(int, float, float, float) override {}
	void SetUniform4f(int, float, float, float, float) override {}

	void SetUniform2iv(int, const int*) override {}
	void SetUniform3iv(int, const int*) override {}
	void SetUniform4iv(int, const int*) override {}
	void SetUniform2fv(int, const float*) override {}
	void SetUniform3fv(int, const float*) override {}
	void SetUniform4fv(int, const float*) override {}

	void SetUniform1iv(int, GLsizei, const int*) override {}
	void SetUniform2iv(int, GLsizei, const int*) override {}
	void SetUniform3iv(int, GLsizei, const int*) override {}
	void SetUniform4iv(int, GLsizei, const int*) override {}
	void SetUniform1fv(int, GLsizei, const float*) override {}
	void SetUniform2fv(int, GLsizei, const float*) override {}
	void SetUniform3fv(int, GLsizei, const float*) override {}
	void SetUniform4fv(int, GLsizei, const float*) override {}
};

static float s_mockModelView[16] = {0.0f};
static float s_mockProjection[16] = {0.0f};
static int s_glGetFloatvCalls = 0;

static void APIENTRY Intercept_glGetFloatv(GLenum pname, GLfloat* params)
{
	s_glGetFloatvCalls++;
	if (pname == GL_MODELVIEW_MATRIX) {
		std::copy(s_mockModelView, s_mockModelView + 16, params);
	} else if (pname == GL_PROJECTION_MATRIX) {
		std::copy(s_mockProjection, s_mockProjection + 16, params);
	} else {
		std::fill(params, params + 16, 0.0f);
	}
}

struct RenderBuffersTestFixture {
	decltype(glad_glGetFloatv) prev_glGetFloatv;

	RenderBuffersTestFixture()
	{
		prev_glGetFloatv = glad_glGetFloatv;
		glad_glGetFloatv = Intercept_glGetFloatv;
		s_glGetFloatvCalls = 0;
		std::fill(std::begin(s_mockModelView), std::end(s_mockModelView), 0.0f);
		std::fill(std::begin(s_mockProjection), std::end(s_mockProjection), 0.0f);
	}

	~RenderBuffersTestFixture()
	{
		glad_glGetFloatv = prev_glGetFloatv;
	}
};

} // namespace

TEST_CASE_METHOD(RenderBuffersTestFixture, "PlatformShaderProfile: UploadCoreMvpMatrix null program safety", "[RenderBuffers]")
{
	PlatformShaderProfile::UploadCoreMvpMatrix(nullptr);
	CHECK(s_glGetFloatvCalls == 0);
}

TEST_CASE_METHOD(RenderBuffersTestFixture, "PlatformShaderProfile: UploadCoreMvpMatrix identity transformation", "[RenderBuffers]")
{
	// Set both ModelView and Projection to Identity 4x4 (column-major)
	s_mockModelView[0]  = 1.0f; s_mockModelView[5]  = 1.0f; s_mockModelView[10] = 1.0f; s_mockModelView[15] = 1.0f;
	s_mockProjection[0] = 1.0f; s_mockProjection[5] = 1.0f; s_mockProjection[10] = 1.0f; s_mockProjection[15] = 1.0f;

	MockProgramObject mockProgram;
	PlatformShaderProfile::UploadCoreMvpMatrix(&mockProgram);

	CHECK(mockProgram.uploadCount == 1);
	CHECK(mockProgram.lastUniformName == "u_mvpMatrix");
	CHECK_FALSE(mockProgram.lastTranspose);

	// Result should be identity matrix
	for (int i = 0; i < 16; ++i) {
		float expected = (i % 5 == 0) ? 1.0f : 0.0f;
		CHECK(mockProgram.lastMatrix[i] == Catch::Approx(expected).margin(0.0001f));
	}
}

TEST_CASE_METHOD(RenderBuffersTestFixture, "PlatformShaderProfile: UploadCoreMvpMatrix affine transform multiplication", "[RenderBuffers]")
{
	// ModelView: Translation by (10, 20, 30)
	// Column 0
	s_mockModelView[0] = 1.0f; s_mockModelView[1] = 0.0f; s_mockModelView[2] = 0.0f; s_mockModelView[3] = 0.0f;
	// Column 1
	s_mockModelView[4] = 0.0f; s_mockModelView[5] = 1.0f; s_mockModelView[6] = 0.0f; s_mockModelView[7] = 0.0f;
	// Column 2
	s_mockModelView[8] = 0.0f; s_mockModelView[9] = 0.0f; s_mockModelView[10] = 1.0f; s_mockModelView[11] = 0.0f;
	// Column 3 (translation)
	s_mockModelView[12] = 10.0f; s_mockModelView[13] = 20.0f; s_mockModelView[14] = 30.0f; s_mockModelView[15] = 1.0f;

	// Projection: Non-uniform scale by (2, 3, 4)
	// Column 0
	s_mockProjection[0] = 2.0f; s_mockProjection[1] = 0.0f; s_mockProjection[2] = 0.0f; s_mockProjection[3] = 0.0f;
	// Column 1
	s_mockProjection[4] = 0.0f; s_mockProjection[5] = 3.0f; s_mockProjection[6] = 0.0f; s_mockProjection[7] = 0.0f;
	// Column 2
	s_mockProjection[8] = 0.0f; s_mockProjection[9] = 0.0f; s_mockProjection[10] = 4.0f; s_mockProjection[11] = 0.0f;
	// Column 3
	s_mockProjection[12] = 0.0f; s_mockProjection[13] = 0.0f; s_mockProjection[14] = 0.0f; s_mockProjection[15] = 1.0f;

	MockProgramObject mockProgram;
	PlatformShaderProfile::UploadCoreMvpMatrix(&mockProgram);

	CHECK(mockProgram.uploadCount == 1);
	CHECK(mockProgram.lastUniformName == "u_mvpMatrix");
	CHECK_FALSE(mockProgram.lastTranspose);

	// Expected MVP = Projection * ModelView:
	// Column 0: (2, 0, 0, 0)
	CHECK(mockProgram.lastMatrix[0] == Catch::Approx(2.0f));
	CHECK(mockProgram.lastMatrix[1] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[2] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[3] == Catch::Approx(0.0f));

	// Column 1: (0, 3, 0, 0)
	CHECK(mockProgram.lastMatrix[4] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[5] == Catch::Approx(3.0f));
	CHECK(mockProgram.lastMatrix[6] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[7] == Catch::Approx(0.0f));

	// Column 2: (0, 0, 4, 0)
	CHECK(mockProgram.lastMatrix[8] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[9] == Catch::Approx(0.0f));
	CHECK(mockProgram.lastMatrix[10] == Catch::Approx(4.0f));
	CHECK(mockProgram.lastMatrix[11] == Catch::Approx(0.0f));

	// Column 3: (2 * 10, 3 * 20, 4 * 30, 1) = (20, 60, 120, 1)
	CHECK(mockProgram.lastMatrix[12] == Catch::Approx(20.0f));
	CHECK(mockProgram.lastMatrix[13] == Catch::Approx(60.0f));
	CHECK(mockProgram.lastMatrix[14] == Catch::Approx(120.0f));
	CHECK(mockProgram.lastMatrix[15] == Catch::Approx(1.0f));
}

TEST_CASE("PlatformShaderProfile: RequiresCoreShaderHeader predicate", "[RenderBuffers]")
{
	SECTION("Non-core context never requires core shader header") {
		globalRenderingInfo.glContextIsCore = false;
		CHECK_FALSE(PlatformShaderProfile::RequiresCoreShaderHeader());
	}

	SECTION("Core context requires core shader header on Apple platforms") {
		globalRenderingInfo.glContextIsCore = true;
#if defined(__APPLE__)
		CHECK(PlatformShaderProfile::RequiresCoreShaderHeader());
#else
		CHECK_FALSE(PlatformShaderProfile::RequiresCoreShaderHeader());
#endif
	}
}

TEST_CASE("RenderBuffers: Shader template definitions", "[RenderBuffers]")
{
	const std::string coreSrc = vsRenderBufferSrcCore;
	const std::string compatSrc = vsRenderBufferSrcCompat;

	SECTION("Core vertex shader declares u_mvpMatrix and does not use gl_ModelViewProjectionMatrix") {
		CHECK(coreSrc.find("uniform mat4 u_mvpMatrix;") != std::string::npos);
		CHECK(coreSrc.find("gl_Position = u_mvpMatrix *") != std::string::npos);
		CHECK(coreSrc.find("gl_ModelViewProjectionMatrix") == std::string::npos);
	}

	SECTION("Compatibility vertex shader retains gl_ModelViewProjectionMatrix") {
		CHECK(compatSrc.find("gl_ModelViewProjectionMatrix") != std::string::npos);
		CHECK(compatSrc.find("u_mvpMatrix") == std::string::npos);
	}
}
