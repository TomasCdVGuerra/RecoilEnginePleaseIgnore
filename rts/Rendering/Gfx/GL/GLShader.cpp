/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLShader.h"

#include "System/Log/ILog.h"

#include <utility>

namespace
{

    std::string GetShaderLog(GLuint shaderId)
    {
        GLint maxLength = 0;
        GLint logLength = 0;

        glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &maxLength);

        if (maxLength <= 1)
            return {};

        std::string shaderLog;
        shaderLog.resize(static_cast<std::size_t>(maxLength));

        glGetShaderInfoLog(shaderId, maxLength, &logLength, shaderLog.data());

        if (logLength <= 0)
            return {};

        shaderLog.resize(static_cast<std::size_t>(logLength));
        return shaderLog;
    }

} // namespace

namespace gfx
{

    GLShader::GLShader(const ShaderCreateInfo &ci)
        : stage(ci.stage), sourceFormat(ci.sourceFormat)
    {
        if (ci.sourceFormat != ShaderSourceFormat::GLSL)
        {
            log = "GL backend currently supports GLSL shader sources only";
            return;
        }

        CompileFromGLSL(ci);
    }

    GLShader::~GLShader()
    {
        if (shaderId != 0u)
        {
            glDeleteShader(shaderId);
            shaderId = 0u;
        }
    }

    GLShader::GLShader(GLShader &&other) noexcept
    {
        MoveFrom(std::move(other));
    }

    GLShader &GLShader::operator=(GLShader &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (shaderId != 0u)
            glDeleteShader(shaderId);

        MoveFrom(std::move(other));
        return *this;
    }

    GLenum GLShader::TranslateStage(ShaderStage value) const noexcept
    {
        switch (value)
        {
        case ShaderStage::Vertex:
            return GL_VERTEX_SHADER;
        case ShaderStage::TessControl:
            return GL_TESS_CONTROL_SHADER;
        case ShaderStage::TessEvaluation:
            return GL_TESS_EVALUATION_SHADER;
        case ShaderStage::Geometry:
            return GL_GEOMETRY_SHADER;
        case ShaderStage::Fragment:
            return GL_FRAGMENT_SHADER;
        case ShaderStage::Compute:
            return GL_COMPUTE_SHADER;
        }

        return GL_VERTEX_SHADER;
    }

    void GLShader::CompileFromGLSL(const ShaderCreateInfo &ci)
    {
        const GLenum shaderType = TranslateStage(ci.stage);
        shaderId = glCreateShader(shaderType);

        if (shaderId == 0u)
        {
            log = "glCreateShader returned 0";
            return;
        }

        std::string source = ci.sourceCode;

        if (!ci.definitions.empty())
        {
            source = ci.definitions + "\n" + source;
        }

        if (source.empty())
        {
            log = "GLSL shader source is empty";
            return;
        }

        const GLchar *sourcePtr = source.c_str();
        glShaderSource(shaderId, 1, &sourcePtr, nullptr);
        glCompileShader(shaderId);

        GLint compileStatus = GL_FALSE;
        glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compileStatus);

        log = GetShaderLog(shaderId);
        valid = (compileStatus == GL_TRUE);

        if (!valid)
        {
            LOG_L(
                L_WARNING,
                "[GLShader::CompileFromGLSL] Failed to compile shader \"%s\"",
                ci.debugName.c_str());
        }
    }

    void GLShader::MoveFrom(GLShader &&other) noexcept
    {
        shaderId = other.shaderId;
        stage = other.stage;
        sourceFormat = other.sourceFormat;
        valid = other.valid;
        log = std::move(other.log);

        other.shaderId = 0u;
        other.valid = false;
        other.log.clear();
    }

} // namespace gfx
