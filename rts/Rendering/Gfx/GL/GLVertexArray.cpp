/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLVertexArray.h"

#include <utility>

namespace gfx
{

    GLVertexArray::GLVertexArray(GLuint vaoId_) noexcept
        : vaoId(vaoId_)
    {
    }

    GLVertexArray::~GLVertexArray()
    {
        if (vaoId != 0u)
        {
            glDeleteVertexArrays(1, &vaoId);
            vaoId = 0u;
        }
    }

    GLVertexArray::GLVertexArray(GLVertexArray &&other) noexcept
    {
        MoveFrom(std::move(other));
    }

    GLVertexArray &GLVertexArray::operator=(GLVertexArray &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (vaoId != 0u)
            glDeleteVertexArrays(1, &vaoId);

        MoveFrom(std::move(other));
        return *this;
    }

    void GLVertexArray::Bind()
    {
        glBindVertexArray(vaoId);
    }

    void GLVertexArray::Unbind()
    {
        glBindVertexArray(0);
    }

    void GLVertexArray::MoveFrom(GLVertexArray &&other) noexcept
    {
        vaoId = other.vaoId;
        other.vaoId = 0;
    }

} // namespace gfx
