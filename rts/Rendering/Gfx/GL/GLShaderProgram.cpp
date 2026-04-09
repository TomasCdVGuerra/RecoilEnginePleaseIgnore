/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLShaderProgram.h"

#include "GLShader.h"
#include "GLTexture.h"

#include "System/Log/ILog.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

namespace
{

    std::string GetProgramLog(GLuint programId)
    {
        GLint maxLength = 0;
        GLint logLength = 0;

        glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &maxLength);

        if (maxLength <= 1)
            return {};

        std::string programLog;
        programLog.resize(static_cast<std::size_t>(maxLength));

        glGetProgramInfoLog(programId, maxLength, &logLength, programLog.data());

        if (logLength <= 0)
            return {};

        programLog.resize(static_cast<std::size_t>(logLength));
        return programLog;
    }

    template <typename TValue>
    void CopyScalarWord(TValue value, std::array<std::uint32_t, 16> &rawWords, std::uint8_t &rawWordCount)
    {
        static_assert(sizeof(TValue) == sizeof(std::uint32_t));

        rawWords.fill(0u);

        std::uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(std::uint32_t));
        rawWords[0] = bits;
        rawWordCount = 1u;
    }

    template <typename TValue, std::size_t N>
    void CopyArrayWords(
        const std::array<TValue, N> &value,
        std::array<std::uint32_t, 16> &rawWords,
        std::uint8_t &rawWordCount)
    {
        static_assert(sizeof(TValue) == sizeof(std::uint32_t));

        rawWords.fill(0u);

        const std::size_t clampedCount = std::min<std::size_t>(N, rawWords.size());

        for (std::size_t i = 0; i < clampedCount; ++i)
        {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, &value[i], sizeof(std::uint32_t));
            rawWords[i] = bits;
        }

        rawWordCount = static_cast<std::uint8_t>(clampedCount);
    }

    template <std::size_t N>
    std::array<GLint, N> ToSignedArray(const std::array<std::uint32_t, N> &value)
    {
        std::array<GLint, N> out = {};

        for (std::size_t i = 0; i < N; ++i)
            out[i] = static_cast<GLint>(value[i]);

        return out;
    }

} // namespace

namespace gfx
{

    GLShaderProgram::GLShaderProgram(const ShaderProgramCreateInfo &ci)
        : debugName(ci.debugName)
    {
        programId = glCreateProgram();

        if (programId == 0u)
            log = "glCreateProgram returned 0";
    }

    GLShaderProgram::~GLShaderProgram()
    {
        if (bound)
        {
            glUseProgram(0u);
            bound = false;
        }

        if (programId != 0u)
        {
            glDeleteProgram(programId);
            programId = 0u;
        }
    }

    void GLShaderProgram::AttachShader(IShader &shader)
    {
        if (programId == 0u)
            return;

        auto *glShader = dynamic_cast<GLShader *>(&shader);
        if (glShader == nullptr)
        {
            LOG_L(L_WARNING, "[GLShaderProgram::AttachShader] Non-GL shader object attached to GL program");
            return;
        }

        if (!glShader->IsValid())
        {
            LOG_L(L_WARNING, "[GLShaderProgram::AttachShader] Attempting to attach invalid shader to program \"%s\"", debugName.c_str());
            return;
        }

        const ShaderStage stage = glShader->Stage();
        const GLuint shaderId = glShader->GetShaderId();

        const auto existingIt = attachedShaderIds.find(stage);
        if (existingIt != attachedShaderIds.end() && existingIt->second != 0u)
            glDetachShader(programId, existingIt->second);

        glAttachShader(programId, shaderId);
        attachedShaderIds[stage] = shaderId;
    }

    void GLShaderProgram::BindAttribLocation(std::string_view name, std::uint32_t index)
    {
        if (programId == 0u || name.empty())
            return;

        const std::string attributeName(name);
        glBindAttribLocation(programId, index, attributeName.c_str());
    }

    void GLShaderProgram::BindOutputLocation(std::string_view name, std::uint32_t index)
    {
        if (programId == 0u || name.empty())
            return;

        if (!IS_GL_FUNCTION_AVAILABLE(glBindFragDataLocation))
            return;

        const std::string outputName(name);
        glBindFragDataLocation(programId, index, outputName.c_str());
    }

    bool GLShaderProgram::Link()
    {
        if (programId == 0u)
            return false;

        glLinkProgram(programId);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(programId, GL_LINK_STATUS, &linkStatus);

        valid = (linkStatus == GL_TRUE);
        log = GetProgramLog(programId);

        if (!valid)
        {
            LOG_L(L_WARNING, "[GLShaderProgram::Link] Failed to link program \"%s\"", debugName.c_str());
            return false;
        }

        ClearUniformCaches();
        return true;
    }

    bool GLShaderProgram::Validate()
    {
        if (programId == 0u)
            return false;

        glValidateProgram(programId);

        GLint validateStatus = GL_FALSE;
        glGetProgramiv(programId, GL_VALIDATE_STATUS, &validateStatus);

        const bool validated = (validateStatus == GL_TRUE);

        const std::string validationLog = GetProgramLog(programId);
        if (!validationLog.empty())
        {
            if (!log.empty())
                log += "\n";

            log += validationLog;
        }

        valid = valid && validated;
        return validated;
    }

    void GLShaderProgram::Bind()
    {
        if (programId == 0u)
            return;

        glUseProgram(programId);
        bound = true;
    }

    void GLShaderProgram::Unbind()
    {
        if (!bound)
            return;

        glUseProgram(0u);
        bound = false;
    }

    bool GLShaderProgram::SetParameter(std::string_view name, const UniformParameter &parameter)
    {
        if (name.empty() || programId == 0u)
            return false;

        auto [it, inserted] = uniformByName.try_emplace(std::string(name));
        UniformCacheEntry &entry = it->second;

        const GLint location = ResolveUniformLocation(name, entry);
        if (location < 0)
            return false;

        return SubmitParameter(location, entry, parameter);
    }

    bool GLShaderProgram::SetParameter(int location, const UniformParameter &parameter)
    {
        if (location < 0 || programId == 0u)
            return false;

        auto [it, inserted] = uniformByLocation.try_emplace(location);
        UniformCacheEntry &entry = it->second;
        entry.location = static_cast<GLint>(location);

        return SubmitParameter(entry.location, entry, parameter);
    }

    void GLShaderProgram::BindTexture(std::uint32_t unit, ITexture *texture)
    {
        const GLenum activeUnit = static_cast<GLenum>(GL_TEXTURE0 + unit);
        glActiveTexture(activeUnit);

        GLenum target = GL_TEXTURE_2D;
        GLuint textureId = 0u;

        if (texture != nullptr)
        {
            if (auto *glTexture = dynamic_cast<GLTexture *>(texture); glTexture != nullptr)
            {
                target = glTexture->GetTarget();
                textureId = glTexture->GetTextureId();
            }
            else
            {
                constexpr std::uintptr_t maxTextureId = static_cast<std::uintptr_t>(std::numeric_limits<GLuint>::max());
                const std::uintptr_t nativeHandle = texture->GetNativeHandle();

                if (nativeHandle <= maxTextureId)
                {
                    textureId = static_cast<GLuint>(nativeHandle);
                }
                else
                {
                    LOG_L(
                        L_WARNING,
                        "[GLShaderProgram::BindTexture] Native texture handle (%zu) exceeds GLuint max (%zu)",
                        static_cast<std::size_t>(nativeHandle),
                        static_cast<std::size_t>(maxTextureId));
                }
            }
        }

        glBindTexture(target, textureId);
        glActiveTexture(GL_TEXTURE0);

        textureUnitTargets[unit] = target;
    }

    void GLShaderProgram::UnbindTexture(std::uint32_t unit)
    {
        GLenum target = GL_TEXTURE_2D;

        const auto it = textureUnitTargets.find(unit);
        if (it != textureUnitTargets.end())
            target = it->second;

        const GLenum activeUnit = static_cast<GLenum>(GL_TEXTURE0 + unit);
        glActiveTexture(activeUnit);
        glBindTexture(target, 0u);
        glActiveTexture(GL_TEXTURE0);

        textureUnitTargets.erase(unit);
    }

    GLint GLShaderProgram::ResolveUniformLocation(std::string_view name, UniformCacheEntry &entry)
    {
        if (entry.location >= 0)
            return entry.location;

        const std::string uniformName(name);
        entry.location = glGetUniformLocation(programId, uniformName.c_str());
        return entry.location;
    }

    bool GLShaderProgram::BuildRawWords(
        const UniformParameter &parameter,
        std::array<std::uint32_t, 16> &rawWords,
        std::uint8_t &rawWordCount,
        bool &transpose) const
    {
        rawWords.fill(0u);
        rawWordCount = 0u;
        transpose = false;

        std::visit(
            [&](const auto &value)
            {
                using TValue = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<TValue, std::int32_t> || std::is_same_v<TValue, std::uint32_t> || std::is_same_v<TValue, float>)
                {
                    CopyScalarWord(value, rawWords, rawWordCount);
                }
                else if constexpr (
                    std::is_same_v<TValue, std::array<std::int32_t, 2>> ||
                    std::is_same_v<TValue, std::array<std::int32_t, 3>> ||
                    std::is_same_v<TValue, std::array<std::int32_t, 4>> ||
                    std::is_same_v<TValue, std::array<std::uint32_t, 2>> ||
                    std::is_same_v<TValue, std::array<std::uint32_t, 3>> ||
                    std::is_same_v<TValue, std::array<std::uint32_t, 4>> ||
                    std::is_same_v<TValue, std::array<float, 2>> ||
                    std::is_same_v<TValue, std::array<float, 3>> ||
                    std::is_same_v<TValue, std::array<float, 4>>)
                {
                    CopyArrayWords(value, rawWords, rawWordCount);
                }
                else if constexpr (std::is_same_v<TValue, UniformMat2>)
                {
                    CopyArrayWords(value.value, rawWords, rawWordCount);
                    transpose = value.transpose;
                }
                else if constexpr (std::is_same_v<TValue, UniformMat3>)
                {
                    CopyArrayWords(value.value, rawWords, rawWordCount);
                    transpose = value.transpose;
                }
                else if constexpr (std::is_same_v<TValue, UniformMat4>)
                {
                    CopyArrayWords(value.value, rawWords, rawWordCount);
                    transpose = value.transpose;
                }
            },
            parameter.value);

        return (rawWordCount > 0u);
    }

    bool GLShaderProgram::SubmitParameter(GLint location, UniformCacheEntry &entry, const UniformParameter &parameter)
    {
        if (location < 0 || !bound)
            return false;

        std::array<std::uint32_t, 16> rawWords = {};
        std::uint8_t rawWordCount = 0u;
        bool transpose = false;

        if (!BuildRawWords(parameter, rawWords, rawWordCount, transpose))
            return false;

        if (entry.initialized &&
            entry.rawWordCount == rawWordCount &&
            entry.transpose == transpose &&
            std::equal(rawWords.begin(), rawWords.begin() + rawWordCount, entry.rawWords.begin()))
        {
            return true;
        }

        std::visit(
            [&](const auto &value)
            {
                using TValue = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<TValue, std::int32_t>)
                {
                    glUniform1i(location, static_cast<GLint>(value));
                }
                else if constexpr (std::is_same_v<TValue, std::uint32_t>)
                {
                    glUniform1i(location, static_cast<GLint>(value));
                }
                else if constexpr (std::is_same_v<TValue, float>)
                {
                    glUniform1f(location, value);
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::int32_t, 2>>)
                {
                    glUniform2iv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::int32_t, 3>>)
                {
                    glUniform3iv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::int32_t, 4>>)
                {
                    glUniform4iv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::uint32_t, 2>>)
                {
                    const auto signedValue = ToSignedArray(value);
                    glUniform2iv(location, 1, signedValue.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::uint32_t, 3>>)
                {
                    const auto signedValue = ToSignedArray(value);
                    glUniform3iv(location, 1, signedValue.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<std::uint32_t, 4>>)
                {
                    const auto signedValue = ToSignedArray(value);
                    glUniform4iv(location, 1, signedValue.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<float, 2>>)
                {
                    glUniform2fv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<float, 3>>)
                {
                    glUniform3fv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, std::array<float, 4>>)
                {
                    glUniform4fv(location, 1, value.data());
                }
                else if constexpr (std::is_same_v<TValue, UniformMat2>)
                {
                    glUniformMatrix2fv(location, 1, static_cast<GLboolean>(value.transpose), value.value.data());
                }
                else if constexpr (std::is_same_v<TValue, UniformMat3>)
                {
                    glUniformMatrix3fv(location, 1, static_cast<GLboolean>(value.transpose), value.value.data());
                }
                else if constexpr (std::is_same_v<TValue, UniformMat4>)
                {
                    glUniformMatrix4fv(location, 1, static_cast<GLboolean>(value.transpose), value.value.data());
                }
            },
            parameter.value);

        entry.rawWords = rawWords;
        entry.rawWordCount = rawWordCount;
        entry.transpose = transpose;
        entry.initialized = true;

        return true;
    }

    void GLShaderProgram::ClearUniformCaches()
    {
        uniformByName.clear();
        uniformByLocation.clear();
    }

} // namespace gfx
