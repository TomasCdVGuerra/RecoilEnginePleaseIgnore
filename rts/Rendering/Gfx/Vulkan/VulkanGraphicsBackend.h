/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include "Rendering/Gfx/IGraphicsBackend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace gfx
{

    class VulkanFramebuffer;

    class VulkanGraphicsBackend final : public IGraphicsBackend
    {
    public:
        explicit VulkanGraphicsBackend(SDL_Window *window = nullptr);
        ~VulkanGraphicsBackend() override;

        [[nodiscard]] BackendType Type() const noexcept override;
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] bool SupportsPersistentMapping() const noexcept override;

        [[nodiscard]] std::unique_ptr<IVertexBuffer> CreateVertexBuffer(const BufferCreateInfo &ci) override;
        [[nodiscard]] std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &ci) override;
        [[nodiscard]] std::unique_ptr<IFramebuffer> CreateFramebuffer(const RenderTargetDesc &desc) override;
        [[nodiscard]] std::unique_ptr<IShader> CreateShader(const ShaderCreateInfo &ci) override;
        [[nodiscard]] std::unique_ptr<IShaderProgram> CreateShaderProgram(const ShaderProgramCreateInfo &ci) override;
        [[nodiscard]] std::unique_ptr<IVertexArray> CreateVertexArray(
            const VertexLayoutDesc &layout,
            std::span<const VertexArrayBufferBinding> vertexBuffers,
            IVertexBuffer *indexBuffer = nullptr) override;

        void BindFramebuffer(IRenderTarget *target) override;

        void DrawLineBatches(
            IVertexBuffer &vertexBuffer,
            std::span<const LineBatchDesc> batches,
            const LineStippleState &stippleState) override;

        void DrawTexturedIndexedBatches(
            IVertexBuffer &vertexBuffer,
            IVertexBuffer &indexBuffer,
            ITexture &texture,
            std::span<const TexturedIndexedBatchDesc> batches,
            const TexturedVertexLayout &vertexLayout,
            IndexElementType indexType,
            const TexturedBatchState &state) override;

        void DrawIndexed(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedDrawDesc &draw,
            IndexElementType indexType) override;

        void DrawIndexedInstanced(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            const IndexedInstancedDrawDesc &draw,
            IndexElementType indexType) override;

        void MultiDrawIndexedIndirect(
            IVertexArray &vertexArray,
            PrimitiveTopology topology,
            std::span<const IndexedIndirectDrawCommand> commands,
            IndexElementType indexType) override;

        void BeginFrame() override;
        void EndFrame() override;
        void SwapBuffers() override;
        void DeviceWaitIdle() override;

        // Binds the sampled texture used by the swapchain demo draw.
        void SetSwapchainTriangleTexture(ITexture *texture);

    private:
        void Init();
        void Cleanup() noexcept;

        void CreateInstance();
        void CreateSurface();
        void SelectPhysicalDevice();
        void SelectGraphicsQueueFamily();
        void SelectPresentQueueFamily();
        void CreateLogicalDevice();
        void CreateCommandPool();
        void CreatePrimaryCommandBuffer();
        void CreateSwapchain();
        void CreateSwapchainImageViews();
        void CreateSwapchainDepthResources();
        void CreateSwapchainRenderPass();
        void CreateSwapchainFramebuffers();
        void CreateSwapchainTriangleResources();
        void CreateSwapchainTrianglePipeline();
        void UpdateSwapchainTriangleProjection();
        void UpdateSwapchainTriangleTextureDescriptor(ITexture *texture);
        void DestroySwapchainTriangleResources() noexcept;
        void CreateBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer &buffer,
            VkDeviceMemory &memory);
        void CreateImage(
            std::uint32_t width,
            std::uint32_t height,
            VkFormat format,
            VkImageUsageFlags usage,
            VkImageAspectFlags aspectFlags,
            VkImage &image,
            VkDeviceMemory &memory,
            VkImageView &imageView);
        [[nodiscard]] VkFormat FindSupportedDepthFormat() const;
        [[nodiscard]] std::uint32_t FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        void DestroySwapchain() noexcept;

        static constexpr std::uint32_t InvalidQueueFamilyIndex = std::numeric_limits<std::uint32_t>::max();

        SDL_Window *window = nullptr;

        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamilyIndex = InvalidQueueFamilyIndex;
        std::uint32_t presentQueueFamilyIndex = InvalidQueueFamilyIndex;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer primaryCommandBuffer = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D swapchainExtent = {0u, 0u};
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        VkRenderPass swapchainRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchainFramebuffers;
        VkDescriptorSetLayout swapchainTriangleDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool swapchainTriangleDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet swapchainTriangleDescriptorSet = VK_NULL_HANDLE;
        VkBuffer swapchainTriangleVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory swapchainTriangleVertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer swapchainTriangleUniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory swapchainTriangleUniformBufferMemory = VK_NULL_HANDLE;
        VkBuffer swapchainTriangleIndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory swapchainTriangleIndexBufferMemory = VK_NULL_HANDLE;
        VkPipelineLayout swapchainTrianglePipelineLayout = VK_NULL_HANDLE;
        VkPipeline swapchainTrianglePipeline = VK_NULL_HANDLE;
        ITexture *swapchainTriangleTexture = nullptr;
        std::unique_ptr<ITexture> swapchainTriangleFallbackTexture;
        VkFormat swapchainDepthFormat = VK_FORMAT_UNDEFINED;
        VkImage swapchainDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory swapchainDepthImageMemory = VK_NULL_HANDLE;
        VkImageView swapchainDepthImageView = VK_NULL_HANDLE;
        VkFence swapchainAcquireFence = VK_NULL_HANDLE;
        std::uint32_t currentSwapchainImageIndex = 0;
        bool frameRecording = false;
        bool renderPassActive = false;
        VulkanFramebuffer *boundFramebuffer = nullptr;
    };

} // namespace gfx
