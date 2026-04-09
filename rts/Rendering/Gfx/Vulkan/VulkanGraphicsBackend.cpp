/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanGraphicsBackend.h"

#include "VulkanFramebuffer.h"
#include "VulkanShader.h"
#include "VulkanShaderProgram.h"
#include "VulkanTexture.h"
#include "VulkanVertexArray.h"
#include "VulkanVertexBuffer.h"

#include <cstring>
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

} // namespace

namespace gfx
{

    VulkanGraphicsBackend::VulkanGraphicsBackend()
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
            SelectPhysicalDevice();
            SelectGraphicsQueueFamily();
            CreateLogicalDevice();
            CreateCommandPool();
            CreatePrimaryCommandBuffer();
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

        graphicsQueue = VK_NULL_HANDLE;
        graphicsQueueFamilyIndex = InvalidQueueFamilyIndex;
        physicalDevice = VK_NULL_HANDLE;

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
        VkInstanceCreateFlags instanceFlags = 0;

#if defined(__APPLE__)
        instanceExtensions.push_back("VK_KHR_portability_enumeration");

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

    void VulkanGraphicsBackend::CreateLogicalDevice()
    {
        const float queuePriority = 1.0f;

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        std::vector<const char *> deviceExtensions;

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
                    deviceExtensions.push_back("VK_KHR_portability_subset");
                    break;
                }
            }
        }
#endif

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

        CheckVkResult(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");

        vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0u, &graphicsQueue);
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

} // namespace gfx
