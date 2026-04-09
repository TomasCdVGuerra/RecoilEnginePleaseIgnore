/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IGraphicsBackend.h"
#include "Rendering/Gfx/IVertexArray.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gfx
{

    class VulkanVertexBuffer;

    class VulkanVertexArray final : public IVertexArray
    {
    public:
        struct VertexBufferBindingState
        {
            std::uint32_t binding = 0;
            VulkanVertexBuffer *vertexBuffer = nullptr;
        };

    public:
        VulkanVertexArray(
            const VertexLayoutDesc &layout,
            std::span<const VertexArrayBufferBinding> vertexBuffers,
            IVertexBuffer *indexBuffer);

        ~VulkanVertexArray() override = default;

        VulkanVertexArray(const VulkanVertexArray &) = delete;
        VulkanVertexArray &operator=(const VulkanVertexArray &) = delete;

        void Bind() override;
        void Unbind() override;

        [[nodiscard]] const VertexLayoutDesc &GetLayout() const noexcept
        {
            return layout;
        }

        [[nodiscard]] std::span<const VertexBufferBindingState> GetVertexBufferStates() const noexcept
        {
            return {vertexBufferStates.data(), vertexBufferStates.size()};
        }

        [[nodiscard]] VulkanVertexBuffer *GetIndexBuffer() const noexcept
        {
            return indexBuffer;
        }

    private:
        VertexLayoutDesc layout;
        std::vector<VertexBufferBindingState> vertexBufferStates;
        VulkanVertexBuffer *indexBuffer = nullptr;
    };

} // namespace gfx
