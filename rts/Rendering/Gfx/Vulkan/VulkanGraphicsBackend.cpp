/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanGraphicsBackend.h"

#include "VulkanFramebuffer.h"
#include "VulkanShader.h"
#include "VulkanShaderProgram.h"
#include "VulkanTexture.h"
#include "VulkanVertexArray.h"
#include "VulkanVertexBuffer.h"

#if !defined(HEADLESS)
#include <SDL.h>

#if SDL_MAJOR_VERSION >= 3
#include <SDL3/SDL_vulkan.h>
#else
#include <SDL2/SDL_vulkan.h>
#endif

#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    [[noreturn]] void ThrowNotImplemented(const char *method)
    {
        throw std::runtime_error(std::string("VulkanGraphicsBackend::") + method + " is not implemented yet");
    }

    [[noreturn]] void ThrowVkError(const char *what, VkResult result)
    {
        throw std::runtime_error(std::string(what) + " failed with VkResult=" + std::to_string(static_cast<int>(result)));
    }

    void CheckVkResult(VkResult result, const char *what)
    {
        if (result != VK_SUCCESS)
            ThrowVkError(what, result);
    }

    VkIndexType TranslateIndexElementType(gfx::IndexElementType indexType) noexcept
    {
        switch (indexType)
        {
        case gfx::IndexElementType::UInt16:
            return VK_INDEX_TYPE_UINT16;
        case gfx::IndexElementType::UInt32:
            return VK_INDEX_TYPE_UINT32;
        }

        return VK_INDEX_TYPE_UINT16;
    }

    VkPrimitiveTopology TranslatePrimitiveTopology(gfx::PrimitiveTopology topology) noexcept
    {
        switch (topology)
        {
        case gfx::PrimitiveTopology::Triangles:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case gfx::PrimitiveTopology::Lines:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case gfx::PrimitiveTopology::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        }

        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    VkDeviceSize ResolveVertexBufferOffset(const gfx::VertexLayoutDesc &layout, std::uint32_t binding) noexcept
    {
        for (const gfx::VertexBufferBindingDesc &bindingDesc : layout.bindings)
        {
            if (bindingDesc.binding == binding)
                return static_cast<VkDeviceSize>(bindingDesc.offsetBytes);
        }

        return 0;
    }

    VkPipeline ResolveCurrentGraphicsPipeline() noexcept
    {
        const gfx::VulkanShaderProgram *program = gfx::VulkanShaderProgram::GetBoundProgram();
        if (program == nullptr)
            return VK_NULL_HANDLE;

        return program->GetPipeline();
    }

    VkSurfaceFormatKHR ChooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats)
    {
        for (const VkSurfaceFormatKHR &format : formats)
        {
            if ((format.format == VK_FORMAT_B8G8R8A8_UNORM) && (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
                return format;
        }

        if (!formats.empty())
            return formats.front();

        VkSurfaceFormatKHR fallback{};
        fallback.format = VK_FORMAT_B8G8R8A8_UNORM;
        fallback.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        return fallback;
    }

    VkPresentModeKHR ChoosePresentMode(std::span<const VkPresentModeKHR> presentModes)
    {
        for (const VkPresentModeKHR presentMode : presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
                return presentMode;
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D ClampSwapchainExtent(VkExtent2D requested, const VkSurfaceCapabilitiesKHR &surfaceCapabilities)
    {
        VkExtent2D clamped = requested;
        clamped.width = std::clamp(clamped.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        clamped.height = std::clamp(clamped.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        return clamped;
    }

    VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(const VkSurfaceCapabilitiesKHR &surfaceCapabilities)
    {
        static constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preferredCompositeAlpha = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };

        for (const VkCompositeAlphaFlagBitsKHR candidate : preferredCompositeAlpha)
        {
            if ((surfaceCapabilities.supportedCompositeAlpha & candidate) != 0u)
                return candidate;
        }

        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

} // namespace

namespace gfx
{

    VulkanGraphicsBackend::VulkanGraphicsBackend(SDL_Window *window_)
        : window(window_)
    {
        Init();
    }

    VulkanGraphicsBackend::~VulkanGraphicsBackend()
    {
        Cleanup();
    }

    BackendType VulkanGraphicsBackend::Type() const noexcept
    {
        return BackendType::Vulkan;
    }

    std::string_view VulkanGraphicsBackend::Name() const noexcept
    {
        return "Vulkan";
    }

    bool VulkanGraphicsBackend::SupportsPersistentMapping() const noexcept
    {
        return false;
    }

    std::unique_ptr<IVertexBuffer> VulkanGraphicsBackend::CreateVertexBuffer(const BufferCreateInfo &ci)
    {
        return std::make_unique<VulkanVertexBuffer>(device, physicalDevice, ci);
    }

    std::unique_ptr<ITexture> VulkanGraphicsBackend::CreateTexture(const TextureCreateInfo &ci)
    {
        return std::make_unique<VulkanTexture>(device, physicalDevice, commandPool, graphicsQueue, ci);
    }

    std::unique_ptr<IFramebuffer> VulkanGraphicsBackend::CreateFramebuffer(const RenderTargetDesc &desc)
    {
        return std::make_unique<VulkanFramebuffer>(device, desc);
    }

    std::unique_ptr<IShader> VulkanGraphicsBackend::CreateShader(const ShaderCreateInfo &ci)
    {
        return std::make_unique<VulkanShader>(device, ci);
    }

    std::unique_ptr<IShaderProgram> VulkanGraphicsBackend::CreateShaderProgram(const ShaderProgramCreateInfo &ci)
    {
        return std::make_unique<VulkanShaderProgram>(device, ci);
    }

    std::unique_ptr<IVertexArray> VulkanGraphicsBackend::CreateVertexArray(
        const VertexLayoutDesc &layout,
        std::span<const VertexArrayBufferBinding> vertexBuffers,
        IVertexBuffer *indexBuffer)
    {
        return std::make_unique<VulkanVertexArray>(layout, vertexBuffers, indexBuffer);
    }

    void VulkanGraphicsBackend::BindFramebuffer(IRenderTarget *target)
    {
        if (!frameRecording)
            BeginFrame();

        if (!frameRecording || (primaryCommandBuffer == VK_NULL_HANDLE))
            return;

        if (renderPassActive)
        {
            vkCmdEndRenderPass(primaryCommandBuffer);
            renderPassActive = false;
            boundFramebuffer = nullptr;
        }

        auto *vulkanFramebuffer = dynamic_cast<VulkanFramebuffer *>(target);
        if (vulkanFramebuffer == nullptr)
            return;

        if (vulkanFramebuffer->Validate() != FramebufferStatus::Complete)
            return;

        const VkRenderPass renderPass = vulkanFramebuffer->GetRenderPass();
        const VkFramebuffer framebuffer = vulkanFramebuffer->GetFramebuffer();

        if ((renderPass == VK_NULL_HANDLE) || (framebuffer == VK_NULL_HANDLE))
            return;

        const Extent3D extent = vulkanFramebuffer->GetExtent();

        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = renderPass;
        beginInfo.framebuffer = framebuffer;
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderArea.extent.width = (extent.width == 0u) ? 1u : extent.width;
        beginInfo.renderArea.extent.height = (extent.height == 0u) ? 1u : extent.height;
        beginInfo.clearValueCount = 0;
        beginInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(primaryCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        renderPassActive = true;
        boundFramebuffer = vulkanFramebuffer;
    }

    void VulkanGraphicsBackend::DrawLineBatches(
        IVertexBuffer &vertexBuffer,
        std::span<const LineBatchDesc> batches,
        const LineStippleState &stippleState)
    {
        (void)vertexBuffer;
        (void)batches;
        (void)stippleState;
        ThrowNotImplemented("DrawLineBatches");
    }

    void VulkanGraphicsBackend::DrawTexturedIndexedBatches(
        IVertexBuffer &vertexBuffer,
        IVertexBuffer &indexBuffer,
        ITexture &texture,
        std::span<const TexturedIndexedBatchDesc> batches,
        const TexturedVertexLayout &vertexLayout,
        IndexElementType indexType,
        const TexturedBatchState &state)
    {
        (void)vertexBuffer;
        (void)indexBuffer;
        (void)texture;
        (void)batches;
        (void)vertexLayout;
        (void)indexType;
        (void)state;
        ThrowNotImplemented("DrawTexturedIndexedBatches");
    }

    void VulkanGraphicsBackend::DrawIndexed(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        const IndexedDrawDesc &draw,
        IndexElementType indexType)
    {
        if ((draw.indexCount == 0u) || !frameRecording || !renderPassActive || (primaryCommandBuffer == VK_NULL_HANDLE))
            return;

        auto *vulkanVertexArray = dynamic_cast<VulkanVertexArray *>(&vertexArray);
        if (vulkanVertexArray == nullptr)
            return;

        VulkanVertexBuffer *indexBuffer = vulkanVertexArray->GetIndexBuffer();
        if ((indexBuffer == nullptr) || (indexBuffer->GetBuffer() == VK_NULL_HANDLE))
            return;

        const VkPipeline pipeline = ResolveCurrentGraphicsPipeline();
        if (pipeline == VK_NULL_HANDLE)
            return;

        const VkPrimitiveTopology vkTopology = TranslatePrimitiveTopology(topology);
        (void)vkTopology;

        vkCmdBindPipeline(primaryCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VertexLayoutDesc &layout = vulkanVertexArray->GetLayout();
        for (const VulkanVertexArray::VertexBufferBindingState &bindingState : vulkanVertexArray->GetVertexBufferStates())
        {
            if ((bindingState.vertexBuffer == nullptr) || (bindingState.vertexBuffer->GetBuffer() == VK_NULL_HANDLE))
                continue;

            const VkBuffer vertexBuffer = bindingState.vertexBuffer->GetBuffer();
            const VkDeviceSize vertexBufferOffset = ResolveVertexBufferOffset(layout, bindingState.binding);
            vkCmdBindVertexBuffers(primaryCommandBuffer, bindingState.binding, 1u, &vertexBuffer, &vertexBufferOffset);
        }

        vkCmdBindIndexBuffer(primaryCommandBuffer, indexBuffer->GetBuffer(), 0u, TranslateIndexElementType(indexType));
        vkCmdDrawIndexed(primaryCommandBuffer, draw.indexCount, 1u, draw.firstIndex, draw.baseVertex, 0u);
    }

    void VulkanGraphicsBackend::DrawIndexedInstanced(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        const IndexedInstancedDrawDesc &draw,
        IndexElementType indexType)
    {
        (void)vertexArray;
        (void)topology;
        (void)draw;
        (void)indexType;
        ThrowNotImplemented("DrawIndexedInstanced");
    }

    void VulkanGraphicsBackend::MultiDrawIndexedIndirect(
        IVertexArray &vertexArray,
        PrimitiveTopology topology,
        std::span<const IndexedIndirectDrawCommand> commands,
        IndexElementType indexType)
    {
        if (commands.empty() || !frameRecording || !renderPassActive || (primaryCommandBuffer == VK_NULL_HANDLE))
            return;

        auto *vulkanVertexArray = dynamic_cast<VulkanVertexArray *>(&vertexArray);
        if (vulkanVertexArray == nullptr)
            return;

        VulkanVertexBuffer *indexBuffer = vulkanVertexArray->GetIndexBuffer();
        if ((indexBuffer == nullptr) || (indexBuffer->GetBuffer() == VK_NULL_HANDLE))
            return;

        const VkPipeline pipeline = ResolveCurrentGraphicsPipeline();
        if (pipeline == VK_NULL_HANDLE)
            return;

        const VkPrimitiveTopology vkTopology = TranslatePrimitiveTopology(topology);
        (void)vkTopology;

        vkCmdBindPipeline(primaryCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VertexLayoutDesc &layout = vulkanVertexArray->GetLayout();
        for (const VulkanVertexArray::VertexBufferBindingState &bindingState : vulkanVertexArray->GetVertexBufferStates())
        {
            if ((bindingState.vertexBuffer == nullptr) || (bindingState.vertexBuffer->GetBuffer() == VK_NULL_HANDLE))
                continue;

            const VkBuffer vertexBuffer = bindingState.vertexBuffer->GetBuffer();
            const VkDeviceSize vertexBufferOffset = ResolveVertexBufferOffset(layout, bindingState.binding);
            vkCmdBindVertexBuffers(primaryCommandBuffer, bindingState.binding, 1u, &vertexBuffer, &vertexBufferOffset);
        }

        vkCmdBindIndexBuffer(primaryCommandBuffer, indexBuffer->GetBuffer(), 0u, TranslateIndexElementType(indexType));

        // Temporary CPU-side fan-out for scaffolded draw-command support.
        for (const IndexedIndirectDrawCommand &command : commands)
        {
            if ((command.indexCount == 0u) || (command.instanceCount == 0u))
                continue;

            vkCmdDrawIndexed(
                primaryCommandBuffer,
                command.indexCount,
                command.instanceCount,
                command.firstIndex,
                command.baseVertex,
                command.firstInstance);
        }
    }

    void VulkanGraphicsBackend::BeginFrame()
    {
        if (frameRecording || (primaryCommandBuffer == VK_NULL_HANDLE))
            return;

        CheckVkResult(vkResetCommandBuffer(primaryCommandBuffer, 0), "vkResetCommandBuffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        CheckVkResult(vkBeginCommandBuffer(primaryCommandBuffer, &beginInfo), "vkBeginCommandBuffer");

        frameRecording = true;
        renderPassActive = false;
        boundFramebuffer = nullptr;
    }

    void VulkanGraphicsBackend::EndFrame()
    {
        if (!frameRecording || (primaryCommandBuffer == VK_NULL_HANDLE) || (graphicsQueue == VK_NULL_HANDLE))
            return;

        if (renderPassActive)
        {
            vkCmdEndRenderPass(primaryCommandBuffer);
            renderPassActive = false;
            boundFramebuffer = nullptr;
        }

        CheckVkResult(vkEndCommandBuffer(primaryCommandBuffer), "vkEndCommandBuffer");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &primaryCommandBuffer;

        CheckVkResult(vkQueueSubmit(graphicsQueue, 1u, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
        CheckVkResult(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle");

        frameRecording = false;
    }

    void VulkanGraphicsBackend::SwapBuffers()
    {
        if ((device == VK_NULL_HANDLE) || (swapchain == VK_NULL_HANDLE) || (presentQueue == VK_NULL_HANDLE) || (swapchainAcquireFence == VK_NULL_HANDLE))
            return;

        if (frameRecording)
            EndFrame();

        CheckVkResult(vkResetFences(device, 1u, &swapchainAcquireFence), "vkResetFences");

        VkResult acquireResult = vkAcquireNextImageKHR(
            device,
            swapchain,
            std::numeric_limits<std::uint64_t>::max(),
            VK_NULL_HANDLE,
            swapchainAcquireFence,
            &currentSwapchainImageIndex);

        if ((acquireResult == VK_ERROR_OUT_OF_DATE_KHR) || (acquireResult == VK_SUBOPTIMAL_KHR))
        {
            DestroySwapchain();
            CreateSwapchain();
            return;
        }

        CheckVkResult(acquireResult, "vkAcquireNextImageKHR");
        CheckVkResult(vkWaitForFences(device, 1u, &swapchainAcquireFence, VK_TRUE, std::numeric_limits<std::uint64_t>::max()), "vkWaitForFences");

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 0;
        presentInfo.pWaitSemaphores = nullptr;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &currentSwapchainImageIndex;
        presentInfo.pResults = nullptr;

        const VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
        if ((presentResult == VK_ERROR_OUT_OF_DATE_KHR) || (presentResult == VK_SUBOPTIMAL_KHR))
        {
            DestroySwapchain();
            CreateSwapchain();
            return;
        }

        CheckVkResult(presentResult, "vkQueuePresentKHR");
        CheckVkResult(vkQueueWaitIdle(presentQueue), "vkQueueWaitIdle(present)");
    }

    void VulkanGraphicsBackend::DeviceWaitIdle()
    {
        if (device != VK_NULL_HANDLE)
            (void)vkDeviceWaitIdle(device);
    }

    void VulkanGraphicsBackend::Init()
    {
        try
        {
            CreateInstance();
            CreateSurface();
            SelectPhysicalDevice();
            SelectGraphicsQueueFamily();
            SelectPresentQueueFamily();
            CreateLogicalDevice();
            CreateCommandPool();
            CreatePrimaryCommandBuffer();
            CreateSwapchain();
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    void VulkanGraphicsBackend::Cleanup() noexcept
    {
        if (device != VK_NULL_HANDLE)
            (void)vkDeviceWaitIdle(device);

        frameRecording = false;
        renderPassActive = false;
        boundFramebuffer = nullptr;

        DestroySwapchain();

        if ((device != VK_NULL_HANDLE) && (commandPool != VK_NULL_HANDLE) && (primaryCommandBuffer != VK_NULL_HANDLE))
        {
            vkFreeCommandBuffers(device, commandPool, 1u, &primaryCommandBuffer);
            primaryCommandBuffer = VK_NULL_HANDLE;
        }

        if (commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        presentQueue = VK_NULL_HANDLE;
        graphicsQueue = VK_NULL_HANDLE;
        presentQueueFamilyIndex = InvalidQueueFamilyIndex;
        graphicsQueueFamilyIndex = InvalidQueueFamilyIndex;
        physicalDevice = VK_NULL_HANDLE;

        if (surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }

    void VulkanGraphicsBackend::CreateInstance()
    {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RecoilEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
        appInfo.pEngineName = "RecoilEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        std::vector<const char *> instanceExtensions;

        const auto AddInstanceExtension = [&instanceExtensions](const char *extensionName)
        {
            if ((extensionName == nullptr) || (extensionName[0] == '\0'))
                return;

            const auto existingIt = std::find_if(
                instanceExtensions.begin(),
                instanceExtensions.end(),
                [extensionName](const char *existingExtension)
                {
                    return (existingExtension != nullptr) && (std::strcmp(existingExtension, extensionName) == 0);
                });

            if (existingIt == instanceExtensions.end())
                instanceExtensions.push_back(extensionName);
        };

#if !defined(HEADLESS)
        if (window != nullptr)
        {
#if SDL_MAJOR_VERSION >= 3
            unsigned int sdlExtensionCount = 0;
            const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
            if (sdlExtensions == nullptr)
                throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed for SDL3");

            for (unsigned int i = 0; i < sdlExtensionCount; ++i)
                AddInstanceExtension(sdlExtensions[i]);
#else
            unsigned int sdlExtensionCount = 0;
            if (!SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, nullptr))
                throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions(count) failed: ") + SDL_GetError());

            std::vector<const char *> sdlExtensions(sdlExtensionCount, nullptr);
            if ((sdlExtensionCount > 0u) && !SDL_Vulkan_GetInstanceExtensions(window, &sdlExtensionCount, sdlExtensions.data()))
                throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions(list) failed: ") + SDL_GetError());

            for (const char *extensionName : sdlExtensions)
                AddInstanceExtension(extensionName);
#endif
        }
#endif

        VkInstanceCreateFlags instanceFlags = 0;

#if defined(__APPLE__)
        AddInstanceExtension("VK_KHR_portability_enumeration");

#if defined(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR)
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
#endif

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.flags = instanceFlags;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

        CheckVkResult(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
    }

    void VulkanGraphicsBackend::CreateSurface()
    {
#if !defined(HEADLESS)
        if ((window == nullptr) || (instance == VK_NULL_HANDLE))
            return;

#if SDL_MAJOR_VERSION >= 3
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
#else
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
#endif
#else
        surface = VK_NULL_HANDLE;
#endif
    }

    void VulkanGraphicsBackend::SelectPhysicalDevice()
    {
        std::uint32_t deviceCount = 0;
        CheckVkResult(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");

        if (deviceCount == 0)
            throw std::runtime_error("vkEnumeratePhysicalDevices returned zero devices");

        std::vector<VkPhysicalDevice> devices(deviceCount, VK_NULL_HANDLE);
        CheckVkResult(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices(list)");

        VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;

        for (const VkPhysicalDevice candidate : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                selectedDevice = candidate;
                break;
            }

            if (selectedDevice == VK_NULL_HANDLE)
                selectedDevice = candidate;
        }

        if (selectedDevice == VK_NULL_HANDLE)
            throw std::runtime_error("failed to select a VkPhysicalDevice");

        physicalDevice = selectedDevice;
    }

    void VulkanGraphicsBackend::SelectGraphicsQueueFamily()
    {
        std::uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

        if (queueFamilyCount == 0)
            throw std::runtime_error("vkGetPhysicalDeviceQueueFamilyProperties returned zero queue families");

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        graphicsQueueFamilyIndex = InvalidQueueFamilyIndex;

        for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            const VkQueueFamilyProperties &queueFamily = queueFamilies[i];

            if ((queueFamily.queueCount > 0u) && ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u))
            {
                graphicsQueueFamilyIndex = i;
                break;
            }
        }

        if (graphicsQueueFamilyIndex == InvalidQueueFamilyIndex)
            throw std::runtime_error("failed to find a Vulkan graphics queue family");
    }

    void VulkanGraphicsBackend::SelectPresentQueueFamily()
    {
        presentQueueFamilyIndex = graphicsQueueFamilyIndex;

        if (surface == VK_NULL_HANDLE)
            return;

        std::uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

        if (queueFamilyCount == 0u)
            throw std::runtime_error("vkGetPhysicalDeviceQueueFamilyProperties returned zero queue families for presentation");

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        presentQueueFamilyIndex = InvalidQueueFamilyIndex;

        for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            VkBool32 supportsPresent = VK_FALSE;
            CheckVkResult(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent), "vkGetPhysicalDeviceSurfaceSupportKHR");

            if ((supportsPresent == VK_TRUE) && ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u))
            {
                presentQueueFamilyIndex = i;
                break;
            }
        }

        if (presentQueueFamilyIndex == InvalidQueueFamilyIndex)
        {
            for (std::uint32_t i = 0; i < queueFamilyCount; ++i)
            {
                VkBool32 supportsPresent = VK_FALSE;
                CheckVkResult(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent), "vkGetPhysicalDeviceSurfaceSupportKHR");

                if (supportsPresent == VK_TRUE)
                {
                    presentQueueFamilyIndex = i;
                    break;
                }
            }
        }

        if (presentQueueFamilyIndex == InvalidQueueFamilyIndex)
            throw std::runtime_error("failed to find a Vulkan present queue family");
    }

    void VulkanGraphicsBackend::CreateLogicalDevice()
    {
        const float queuePriority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(2);

        const auto PushQueueCreateInfo = [&queueCreateInfos, &queuePriority](std::uint32_t familyIndex)
        {
            if (familyIndex == InvalidQueueFamilyIndex)
                return;

            const auto existingIt = std::find_if(
                queueCreateInfos.begin(),
                queueCreateInfos.end(),
                [familyIndex](const VkDeviceQueueCreateInfo &queueCreateInfo)
                {
                    return queueCreateInfo.queueFamilyIndex == familyIndex;
                });

            if (existingIt != queueCreateInfos.end())
                return;

            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = familyIndex;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        };

        PushQueueCreateInfo(graphicsQueueFamilyIndex);
        PushQueueCreateInfo(presentQueueFamilyIndex);

        std::vector<const char *> deviceExtensions;

        const auto AddDeviceExtension = [&deviceExtensions](const char *extensionName)
        {
            if ((extensionName == nullptr) || (extensionName[0] == '\0'))
                return;

            const auto existingIt = std::find_if(
                deviceExtensions.begin(),
                deviceExtensions.end(),
                [extensionName](const char *existingExtension)
                {
                    return (existingExtension != nullptr) && (std::strcmp(existingExtension, extensionName) == 0);
                });

            if (existingIt == deviceExtensions.end())
                deviceExtensions.push_back(extensionName);
        };

        if (surface != VK_NULL_HANDLE)
            AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#if defined(__APPLE__)
        std::uint32_t extensionCount = 0;
        CheckVkResult(
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties(count)");

        if (extensionCount > 0)
        {
            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            CheckVkResult(
                vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data()),
                "vkEnumerateDeviceExtensionProperties(list)");

            for (const VkExtensionProperties &extension : availableExtensions)
            {
                if (std::strcmp(extension.extensionName, "VK_KHR_portability_subset") == 0)
                {
                    AddDeviceExtension("VK_KHR_portability_subset");
                    break;
                }
            }
        }
#endif

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

        CheckVkResult(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");

        vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0u, &graphicsQueue);
        vkGetDeviceQueue(device, presentQueueFamilyIndex, 0u, &presentQueue);
    }

    void VulkanGraphicsBackend::CreateCommandPool()
    {
        VkCommandPoolCreateInfo commandPoolCreateInfo{};
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

        CheckVkResult(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool), "vkCreateCommandPool");
    }

    void VulkanGraphicsBackend::CreatePrimaryCommandBuffer()
    {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        CheckVkResult(vkAllocateCommandBuffers(device, &allocateInfo, &primaryCommandBuffer), "vkAllocateCommandBuffers");
    }

    void VulkanGraphicsBackend::CreateSwapchain()
    {
        if ((surface == VK_NULL_HANDLE) || (device == VK_NULL_HANDLE) || (physicalDevice == VK_NULL_HANDLE))
            return;

        DestroySwapchain();

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        CheckVkResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t surfaceFormatCount = 0;
        CheckVkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");

        if (surfaceFormatCount == 0u)
            throw std::runtime_error("vkGetPhysicalDeviceSurfaceFormatsKHR returned zero formats");

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        CheckVkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");

        std::uint32_t presentModeCount = 0;
        CheckVkResult(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");

        std::vector<VkPresentModeKHR> presentModes;
        if (presentModeCount > 0u)
        {
            presentModes.resize(presentModeCount);
            CheckVkResult(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR(list)");
        }

        const VkSurfaceFormatKHR selectedSurfaceFormat = ChooseSurfaceFormat(surfaceFormats);
        const VkPresentModeKHR selectedPresentMode = ChoosePresentMode(presentModes);

        VkExtent2D desiredExtent = surfaceCapabilities.currentExtent;

        if (surfaceCapabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max())
        {
            std::uint32_t desiredWidth = surfaceCapabilities.minImageExtent.width;
            std::uint32_t desiredHeight = surfaceCapabilities.minImageExtent.height;

#if !defined(HEADLESS)
            if (window != nullptr)
            {
                int drawableWidth = 0;
                int drawableHeight = 0;
                SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
                desiredWidth = static_cast<std::uint32_t>(std::max(1, drawableWidth));
                desiredHeight = static_cast<std::uint32_t>(std::max(1, drawableHeight));
            }
#endif

            desiredExtent = {desiredWidth, desiredHeight};
        }

        desiredExtent = ClampSwapchainExtent(desiredExtent, surfaceCapabilities);

        std::uint32_t imageCount = surfaceCapabilities.minImageCount + 1u;
        if ((surfaceCapabilities.maxImageCount > 0u) && (imageCount > surfaceCapabilities.maxImageCount))
            imageCount = surfaceCapabilities.maxImageCount;

        const std::array<std::uint32_t, 2> queueFamilyIndices = {graphicsQueueFamilyIndex, presentQueueFamilyIndex};

        VkSwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.surface = surface;
        swapchainCreateInfo.minImageCount = imageCount;
        swapchainCreateInfo.imageFormat = selectedSurfaceFormat.format;
        swapchainCreateInfo.imageColorSpace = selectedSurfaceFormat.colorSpace;
        swapchainCreateInfo.imageExtent = desiredExtent;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (graphicsQueueFamilyIndex != presentQueueFamilyIndex)
        {
            swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainCreateInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size());
            swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        }
        else
        {
            swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainCreateInfo.queueFamilyIndexCount = 0;
            swapchainCreateInfo.pQueueFamilyIndices = nullptr;
        }

        swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = ChooseCompositeAlpha(surfaceCapabilities);
        swapchainCreateInfo.presentMode = selectedPresentMode;
        swapchainCreateInfo.clipped = VK_TRUE;
        swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

        CheckVkResult(vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain), "vkCreateSwapchainKHR");

        swapchainImageFormat = selectedSurfaceFormat.format;
        swapchainExtent = desiredExtent;

        std::uint32_t swapchainImageCount = 0;
        CheckVkResult(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr), "vkGetSwapchainImagesKHR(count)");

        if (swapchainImageCount == 0u)
            throw std::runtime_error("vkGetSwapchainImagesKHR returned zero images");

        swapchainImages.assign(swapchainImageCount, VK_NULL_HANDLE);
        CheckVkResult(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data()), "vkGetSwapchainImagesKHR(list)");

        CreateSwapchainImageViews();

        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        CheckVkResult(vkCreateFence(device, &fenceCreateInfo, nullptr, &swapchainAcquireFence), "vkCreateFence(swapchainAcquireFence)");
    }

    void VulkanGraphicsBackend::CreateSwapchainImageViews()
    {
        swapchainImageViews.clear();
        swapchainImageViews.reserve(swapchainImages.size());

        for (const VkImage image : swapchainImages)
        {
            VkImageViewCreateInfo viewCreateInfo{};
            viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewCreateInfo.image = image;
            viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCreateInfo.format = swapchainImageFormat;
            viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCreateInfo.subresourceRange.baseMipLevel = 0;
            viewCreateInfo.subresourceRange.levelCount = 1;
            viewCreateInfo.subresourceRange.baseArrayLayer = 0;
            viewCreateInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            CheckVkResult(vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView), "vkCreateImageView(swapchain)");
            swapchainImageViews.push_back(imageView);
        }
    }

    void VulkanGraphicsBackend::DestroySwapchain() noexcept
    {
        if (device == VK_NULL_HANDLE)
            return;

        if (swapchainAcquireFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, swapchainAcquireFence, nullptr);
            swapchainAcquireFence = VK_NULL_HANDLE;
        }

        for (VkImageView imageView : swapchainImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
                vkDestroyImageView(device, imageView, nullptr);
        }

        swapchainImageViews.clear();
        swapchainImages.clear();

        if (swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        swapchainImageFormat = VK_FORMAT_UNDEFINED;
        swapchainExtent = {0u, 0u};
        currentSwapchainImageIndex = 0;
    }

} // namespace gfx
