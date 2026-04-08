/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "GLGraphicsBackend.h"

#include "GLTexture.h"
#include "GLVertexBuffer.h"

#include "Rendering/GL/myGL.h"

#include <memory>

namespace gfx
{

    BackendType GLGraphicsBackend::Type() const noexcept
    {
        return BackendType::OpenGL;
    }

    std::string_view GLGraphicsBackend::Name() const noexcept
    {
        return "OpenGL";
    }

    bool GLGraphicsBackend::SupportsPersistentMapping() const noexcept
    {
        return (GLAD_GL_ARB_buffer_storage != 0);
    }

    std::unique_ptr<IVertexBuffer> GLGraphicsBackend::CreateVertexBuffer(const BufferCreateInfo &ci)
    {
        return std::make_unique<GLVertexBuffer>(ci);
    }

    std::unique_ptr<ITexture> GLGraphicsBackend::CreateTexture(const TextureCreateInfo &ci)
    {
        return std::make_unique<GLTexture>(ci);
    }

    void GLGraphicsBackend::BeginFrame()
    {
    }

    void GLGraphicsBackend::EndFrame()
    {
    }

    void GLGraphicsBackend::DeviceWaitIdle()
    {
        glFinish();
    }

} // namespace gfx
