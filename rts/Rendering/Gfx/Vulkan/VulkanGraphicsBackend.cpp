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
        (void)target;
        ThrowNotImplemented("BindFramebuffer");
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
        (void)vertexArray;
        (void)topology;
        (void)draw;
        (void)indexType;
        ThrowNotImplemented("DrawIndexed");
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
        (void)vertexArray;
        (void)topology;
        (void)commands;
        (void)indexType;
        ThrowNotImplemented("MultiDrawIndexedIndirect");
    }

    void VulkanGraphicsBackend::BeginFrame()
    {
        ThrowNotImplemented("BeginFrame");
    }

    void VulkanGraphicsBackend::EndFrame()
    {
        ThrowNotImplemented("EndFrame");
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
        }
        catch (...)
        {
            Cleanup();
            throw;
        }
    }

    void VulkanGraphicsBackend::Cleanup() noexcept
    {
        if (commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE)
        {
            (void)vkDeviceWaitIdle(device);
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

} // namespace gfx
