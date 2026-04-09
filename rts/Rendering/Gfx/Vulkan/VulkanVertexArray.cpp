/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanVertexArray.h"

#include "VulkanVertexBuffer.h"

namespace gfx
{

    VulkanVertexArray::VulkanVertexArray(
        const VertexLayoutDesc &layout_,
        std::span<const VertexArrayBufferBinding> vertexBuffers,
        IVertexBuffer *indexBuffer_)
        : layout(layout_)
    {
        vertexBufferStates.reserve(vertexBuffers.size());

        for (const VertexArrayBufferBinding &binding : vertexBuffers)
        {
            VertexBufferBindingState state{};
            state.binding = binding.binding;
            state.vertexBuffer = dynamic_cast<VulkanVertexBuffer *>(binding.vertexBuffer);
            vertexBufferStates.push_back(state);
        }

        indexBuffer = dynamic_cast<VulkanVertexBuffer *>(indexBuffer_);
    }

    void VulkanVertexArray::Bind()
    {
        // Vulkan has no mutable global VAO state; this object only caches binding state.
    }

    void VulkanVertexArray::Unbind()
    {
        // No-op for Vulkan state-tracker object.
    }

} // namespace gfx
