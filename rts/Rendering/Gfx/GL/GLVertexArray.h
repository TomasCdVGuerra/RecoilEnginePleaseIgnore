/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IVertexArray.h"
#include "Rendering/GL/myGL.h"

namespace gfx
{

    class GLVertexArray final : public IVertexArray
    {
    public:
        explicit GLVertexArray(GLuint vaoId) noexcept;
        ~GLVertexArray() override;

        GLVertexArray(const GLVertexArray &) = delete;
        GLVertexArray &operator=(const GLVertexArray &) = delete;

        GLVertexArray(GLVertexArray &&other) noexcept;
        GLVertexArray &operator=(GLVertexArray &&other) noexcept;

        void Bind() override;
        void Unbind() override;

        [[nodiscard]] GLuint GetVaoId() const noexcept
        {
            return vaoId;
        }

    private:
        void MoveFrom(GLVertexArray &&other) noexcept;

    private:
        GLuint vaoId = 0;
    };

} // namespace gfx
