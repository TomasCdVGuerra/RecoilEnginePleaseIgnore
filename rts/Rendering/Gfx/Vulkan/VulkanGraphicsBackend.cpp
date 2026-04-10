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
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif

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

    void VulkanTrace(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        va_end(args);
        std::fflush(stderr);
    }

    struct VulkanVertex
    {
        float x;
        float y;
        float z;
        float nx;
        float ny;
        float nz;
        float u;
        float v;
    };

    static constexpr std::array<VulkanVertex, 24> SwapchainTriangleVertices = {{
        {-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
        {-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},

        {0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
        {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f},
        {-0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f},
        {0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f},

        {-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        {-0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {-0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {-0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f},

        {0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},

        {-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        {-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},

        {-0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        {0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f},
        {-0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f},
    }};

    static constexpr std::array<std::uint16_t, 36> SwapchainTriangleIndices = {{
        0,
        1,
        2,
        0,
        2,
        3,

        4,
        5,
        6,
        4,
        6,
        7,

        8,
        9,
        10,
        8,
        10,
        11,

        12,
        13,
        14,
        12,
        14,
        15,

        16,
        17,
        18,
        16,
        18,
        19,

        20,
        21,
        22,
        20,
        22,
        23,
    }};

    // clang-format off
    static constexpr std::uint32_t SwapchainTriangleVertSpv[] = {
        0x07230203, 0x00010000, 0x0008000b, 0x0000003a, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x000b000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000b, 0x0000000f,
        0x00000023, 0x0000002c, 0x00000030, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
        0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x5474756f, 0x6f437865, 0x0064726f, 0x00050005,
        0x0000000b, 0x65546e69, 0x6f6f4378, 0x00006472, 0x00050005, 0x0000000f, 0x4e74756f, 0x616d726f,
        0x0000006c, 0x00030005, 0x00000012, 0x004f4255, 0x00040006, 0x00000012, 0x00000000, 0x0070766d,
        0x00050006, 0x00000012, 0x00000001, 0x65646f6d, 0x0000006c, 0x00030005, 0x00000014, 0x006f6275,
        0x00050005, 0x00000023, 0x6f4e6e69, 0x6c616d72, 0x00000000, 0x00060005, 0x0000002a, 0x505f6c67,
        0x65567265, 0x78657472, 0x00000000, 0x00060006, 0x0000002a, 0x00000000, 0x505f6c67, 0x7469736f,
        0x006e6f69, 0x00070006, 0x0000002a, 0x00000001, 0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000,
        0x00070006, 0x0000002a, 0x00000002, 0x435f6c67, 0x4470696c, 0x61747369, 0x0065636e, 0x00070006,
        0x0000002a, 0x00000003, 0x435f6c67, 0x446c6c75, 0x61747369, 0x0065636e, 0x00030005, 0x0000002c,
        0x00000000, 0x00040005, 0x00000030, 0x6f506e69, 0x00000073, 0x00040047, 0x00000009, 0x0000001e,
        0x00000001, 0x00040047, 0x0000000b, 0x0000001e, 0x00000002, 0x00040047, 0x0000000f, 0x0000001e,
        0x00000000, 0x00030047, 0x00000012, 0x00000002, 0x00040048, 0x00000012, 0x00000000, 0x00000005,
        0x00050048, 0x00000012, 0x00000000, 0x00000007, 0x00000010, 0x00050048, 0x00000012, 0x00000000,
        0x00000023, 0x00000000, 0x00040048, 0x00000012, 0x00000001, 0x00000005, 0x00050048, 0x00000012,
        0x00000001, 0x00000007, 0x00000010, 0x00050048, 0x00000012, 0x00000001, 0x00000023, 0x00000040,
        0x00040047, 0x00000014, 0x00000021, 0x00000000, 0x00040047, 0x00000014, 0x00000022, 0x00000000,
        0x00040047, 0x00000023, 0x0000001e, 0x00000001, 0x00030047, 0x0000002a, 0x00000002, 0x00050048,
        0x0000002a, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000002a, 0x00000001, 0x0000000b,
        0x00000001, 0x00050048, 0x0000002a, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000002a,
        0x00000003, 0x0000000b, 0x00000004, 0x00040047, 0x00000030, 0x0000001e, 0x00000000, 0x00020013,
        0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
        0x00000007, 0x00000006, 0x00000002, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
        0x00000008, 0x00000009, 0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000007, 0x0004003b,
        0x0000000a, 0x0000000b, 0x00000001, 0x00040017, 0x0000000d, 0x00000006, 0x00000003, 0x00040020,
        0x0000000e, 0x00000003, 0x0000000d, 0x0004003b, 0x0000000e, 0x0000000f, 0x00000003, 0x00040017,
        0x00000010, 0x00000006, 0x00000004, 0x00040018, 0x00000011, 0x00000010, 0x00000004, 0x0004001e,
        0x00000012, 0x00000011, 0x00000011, 0x00040020, 0x00000013, 0x00000002, 0x00000012, 0x0004003b,
        0x00000013, 0x00000014, 0x00000002, 0x00040015, 0x00000015, 0x00000020, 0x00000001, 0x0004002b,
        0x00000015, 0x00000016, 0x00000001, 0x00040020, 0x00000017, 0x00000002, 0x00000011, 0x00040018,
        0x0000001a, 0x0000000d, 0x00000003, 0x00040020, 0x00000022, 0x00000001, 0x0000000d, 0x0004003b,
        0x00000022, 0x00000023, 0x00000001, 0x00040015, 0x00000027, 0x00000020, 0x00000000, 0x0004002b,
        0x00000027, 0x00000028, 0x00000001, 0x0004001c, 0x00000029, 0x00000006, 0x00000028, 0x0006001e,
        0x0000002a, 0x00000010, 0x00000006, 0x00000029, 0x00000029, 0x00040020, 0x0000002b, 0x00000003,
        0x0000002a, 0x0004003b, 0x0000002b, 0x0000002c, 0x00000003, 0x0004002b, 0x00000015, 0x0000002d,
        0x00000000, 0x0004003b, 0x00000022, 0x00000030, 0x00000001, 0x0004002b, 0x00000006, 0x00000032,
        0x3f800000, 0x00040020, 0x00000038, 0x00000003, 0x00000010, 0x00050036, 0x00000002, 0x00000004,
        0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x00000007, 0x0000000c, 0x0000000b,
        0x0003003e, 0x00000009, 0x0000000c, 0x00050041, 0x00000017, 0x00000018, 0x00000014, 0x00000016,
        0x0004003d, 0x00000011, 0x00000019, 0x00000018, 0x00050051, 0x00000010, 0x0000001b, 0x00000019,
        0x00000000, 0x0008004f, 0x0000000d, 0x0000001c, 0x0000001b, 0x0000001b, 0x00000000, 0x00000001,
        0x00000002, 0x00050051, 0x00000010, 0x0000001d, 0x00000019, 0x00000001, 0x0008004f, 0x0000000d,
        0x0000001e, 0x0000001d, 0x0000001d, 0x00000000, 0x00000001, 0x00000002, 0x00050051, 0x00000010,
        0x0000001f, 0x00000019, 0x00000002, 0x0008004f, 0x0000000d, 0x00000020, 0x0000001f, 0x0000001f,
        0x00000000, 0x00000001, 0x00000002, 0x00060050, 0x0000001a, 0x00000021, 0x0000001c, 0x0000001e,
        0x00000020, 0x0004003d, 0x0000000d, 0x00000024, 0x00000023, 0x00050091, 0x0000000d, 0x00000025,
        0x00000021, 0x00000024, 0x0006000c, 0x0000000d, 0x00000026, 0x00000001, 0x00000045, 0x00000025,
        0x0003003e, 0x0000000f, 0x00000026, 0x00050041, 0x00000017, 0x0000002e, 0x00000014, 0x0000002d,
        0x0004003d, 0x00000011, 0x0000002f, 0x0000002e, 0x0004003d, 0x0000000d, 0x00000031, 0x00000030,
        0x00050051, 0x00000006, 0x00000033, 0x00000031, 0x00000000, 0x00050051, 0x00000006, 0x00000034,
        0x00000031, 0x00000001, 0x00050051, 0x00000006, 0x00000035, 0x00000031, 0x00000002, 0x00070050,
        0x00000010, 0x00000036, 0x00000033, 0x00000034, 0x00000035, 0x00000032, 0x00050091, 0x00000010,
        0x00000037, 0x0000002f, 0x00000036, 0x00050041, 0x00000038, 0x00000039, 0x0000002c, 0x0000002d,
        0x0003003e, 0x00000039, 0x00000037, 0x000100fd, 0x00010038
    };

    static constexpr std::uint32_t SwapchainTriangleFragSpv[] = {
        0x07230203, 0x00010000, 0x0008000b, 0x00000031, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
        0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
        0x0008000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000f, 0x00000020, 0x00000024,
        0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
        0x6e69616d, 0x00000000, 0x00050005, 0x00000009, 0x6867696c, 0x72694474, 0x00000000, 0x00050005,
        0x0000000d, 0x65746e69, 0x7469736e, 0x00000079, 0x00050005, 0x0000000f, 0x6f4e6e69, 0x6c616d72,
        0x00000000, 0x00050005, 0x00000018, 0x43786574, 0x726f6c6f, 0x00000000, 0x00050005, 0x0000001c,
        0x53786574, 0x6c706d61, 0x00007265, 0x00050005, 0x00000020, 0x65546e69, 0x6f6f4378, 0x00006472,
        0x00050005, 0x00000024, 0x4374756f, 0x726f6c6f, 0x00000000, 0x00040047, 0x0000000f, 0x0000001e,
        0x00000000, 0x00040047, 0x0000001c, 0x00000021, 0x00000001, 0x00040047, 0x0000001c, 0x00000022,
        0x00000000, 0x00040047, 0x00000020, 0x0000001e, 0x00000001, 0x00040047, 0x00000024, 0x0000001e,
        0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
        0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000003, 0x00040020, 0x00000008, 0x00000007,
        0x00000007, 0x0004002b, 0x00000006, 0x0000000a, 0x3f13cd3a, 0x0006002c, 0x00000007, 0x0000000b,
        0x0000000a, 0x0000000a, 0x0000000a, 0x00040020, 0x0000000c, 0x00000007, 0x00000006, 0x00040020,
        0x0000000e, 0x00000001, 0x00000007, 0x0004003b, 0x0000000e, 0x0000000f, 0x00000001, 0x0004002b,
        0x00000006, 0x00000014, 0x3e4ccccd, 0x00040017, 0x00000016, 0x00000006, 0x00000004, 0x00040020,
        0x00000017, 0x00000007, 0x00000016, 0x00090019, 0x00000019, 0x00000006, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x0000001a, 0x00000019, 0x00040020,
        0x0000001b, 0x00000000, 0x0000001a, 0x0004003b, 0x0000001b, 0x0000001c, 0x00000000, 0x00040017,
        0x0000001e, 0x00000006, 0x00000002, 0x00040020, 0x0000001f, 0x00000001, 0x0000001e, 0x0004003b,
        0x0000001f, 0x00000020, 0x00000001, 0x00040020, 0x00000023, 0x00000003, 0x00000016, 0x0004003b,
        0x00000023, 0x00000024, 0x00000003, 0x00040015, 0x00000029, 0x00000020, 0x00000000, 0x0004002b,
        0x00000029, 0x0000002a, 0x00000003, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
        0x000200f8, 0x00000005, 0x0004003b, 0x00000008, 0x00000009, 0x00000007, 0x0004003b, 0x0000000c,
        0x0000000d, 0x00000007, 0x0004003b, 0x00000017, 0x00000018, 0x00000007, 0x0003003e, 0x00000009,
        0x0000000b, 0x0004003d, 0x00000007, 0x00000010, 0x0000000f, 0x0006000c, 0x00000007, 0x00000011,
        0x00000001, 0x00000045, 0x00000010, 0x0004003d, 0x00000007, 0x00000012, 0x00000009, 0x00050094,
        0x00000006, 0x00000013, 0x00000011, 0x00000012, 0x0007000c, 0x00000006, 0x00000015, 0x00000001,
        0x00000028, 0x00000013, 0x00000014, 0x0003003e, 0x0000000d, 0x00000015, 0x0004003d, 0x0000001a,
        0x0000001d, 0x0000001c, 0x0004003d, 0x0000001e, 0x00000021, 0x00000020, 0x00050057, 0x00000016,
        0x00000022, 0x0000001d, 0x00000021, 0x0003003e, 0x00000018, 0x00000022, 0x0004003d, 0x00000016,
        0x00000025, 0x00000018, 0x0008004f, 0x00000007, 0x00000026, 0x00000025, 0x00000025, 0x00000000,
        0x00000001, 0x00000002, 0x0004003d, 0x00000006, 0x00000027, 0x0000000d, 0x0005008e, 0x00000007,
        0x00000028, 0x00000026, 0x00000027, 0x00050041, 0x0000000c, 0x0000002b, 0x00000018, 0x0000002a,
        0x0004003d, 0x00000006, 0x0000002c, 0x0000002b, 0x00050051, 0x00000006, 0x0000002d, 0x00000028,
        0x00000000, 0x00050051, 0x00000006, 0x0000002e, 0x00000028, 0x00000001, 0x00050051, 0x00000006,
        0x0000002f, 0x00000028, 0x00000002, 0x00070050, 0x00000016, 0x00000030, 0x0000002d, 0x0000002e,
        0x0000002f, 0x0000002c, 0x0003003e, 0x00000024, 0x00000030, 0x000100fd, 0x00010038,
    };
    // clang-format on

    std::array<float, 16> BuildIdentityMatrix()
    {
        return {
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    std::array<float, 16> MultiplyMatrices(const std::array<float, 16> &lhs, const std::array<float, 16> &rhs)
    {
        std::array<float, 16> result{};

        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float value = 0.0f;
                for (int i = 0; i < 4; ++i)
                    value += lhs[(i * 4) + row] * rhs[(col * 4) + i];

                result[(col * 4) + row] = value;
            }
        }

        return result;
    }

    std::array<float, 16> BuildRotationXMatrix(float angleRadians)
    {
        std::array<float, 16> matrix = BuildIdentityMatrix();
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);

        matrix[5] = c;
        matrix[6] = s;
        matrix[9] = -s;
        matrix[10] = c;
        return matrix;
    }

    std::array<float, 16> BuildRotationYMatrix(float angleRadians)
    {
        std::array<float, 16> matrix = BuildIdentityMatrix();
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);

        matrix[0] = c;
        matrix[2] = -s;
        matrix[8] = s;
        matrix[10] = c;
        return matrix;
    }

    std::array<float, 16> BuildTranslationMatrix(float x, float y, float z)
    {
        std::array<float, 16> matrix = BuildIdentityMatrix();
        matrix[12] = x;
        matrix[13] = y;
        matrix[14] = z;
        return matrix;
    }

    std::array<float, 16> BuildPerspectiveProjectionMatrix(const VkExtent2D extent)
    {
        const float width = static_cast<float>((extent.width == 0u) ? 1u : extent.width);
        const float height = static_cast<float>((extent.height == 0u) ? 1u : extent.height);
        const float aspect = width / height;
        const float fovYRadians = 45.0f * 0.01745329251994329577f;
        const float nearPlane = 0.1f;
        const float farPlane = 100.0f;
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);

        std::array<float, 16> matrix{};
        matrix[0] = f / aspect;
        matrix[5] = -f;
        matrix[10] = farPlane / (nearPlane - farPlane);
        matrix[11] = -1.0f;
        matrix[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
        return matrix;
    }

    std::array<float, 16> BuildSwapchainTriangleModelMatrix(float elapsedSeconds)
    {
        const std::array<float, 16> rotationY = BuildRotationYMatrix(elapsedSeconds * 0.9f);
        const std::array<float, 16> rotationX = BuildRotationXMatrix(elapsedSeconds * 0.6f);
        const std::array<float, 16> translation = BuildTranslationMatrix(0.0f, 0.0f, -2.5f);
        return MultiplyMatrices(translation, MultiplyMatrices(rotationY, rotationX));
    }

    std::array<float, 16> BuildSwapchainTriangleMvpMatrix(const VkExtent2D extent, float elapsedSeconds)
    {
        const std::array<float, 16> projection = BuildPerspectiveProjectionMatrix(extent);
        const std::array<float, 16> model = BuildSwapchainTriangleModelMatrix(elapsedSeconds);
        return MultiplyMatrices(projection, model);
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

        if ((currentSwapchainImageIndex < swapchainFramebuffers.size()) && (swapchainFramebuffers[currentSwapchainImageIndex] != VK_NULL_HANDLE) && (swapchainRenderPass != VK_NULL_HANDLE))
        {
            UpdateSwapchainTriangleProjection();

            BeginFrame();

            if (frameRecording && (primaryCommandBuffer != VK_NULL_HANDLE))
            {
                std::array<VkClearValue, 2> clearValues{};
                clearValues[0].color = {{1.0f, 0.5f, 0.0f, 1.0f}};
                clearValues[1].depthStencil = {1.0f, 0u};

                VkRenderPassBeginInfo renderPassBeginInfo{};
                renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassBeginInfo.renderPass = swapchainRenderPass;
                renderPassBeginInfo.framebuffer = swapchainFramebuffers[currentSwapchainImageIndex];
                renderPassBeginInfo.renderArea.offset = {0, 0};
                renderPassBeginInfo.renderArea.extent.width = (swapchainExtent.width == 0u) ? 1u : swapchainExtent.width;
                renderPassBeginInfo.renderArea.extent.height = (swapchainExtent.height == 0u) ? 1u : swapchainExtent.height;
                renderPassBeginInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
                renderPassBeginInfo.pClearValues = clearValues.data();

                vkCmdBeginRenderPass(primaryCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                if (
                    (swapchainTrianglePipeline != VK_NULL_HANDLE) &&
                    (swapchainTriangleVertexBuffer != VK_NULL_HANDLE) &&
                    (swapchainTriangleIndexBuffer != VK_NULL_HANDLE))
                {
                    const float swapchainDrawWidth = static_cast<float>((swapchainExtent.width == 0u) ? 1u : swapchainExtent.width);
                    const float swapchainDrawHeight = static_cast<float>((swapchainExtent.height == 0u) ? 1u : swapchainExtent.height);

                    VkViewport viewport{};
                    viewport.x = 0.0f;
                    viewport.y = 0.0f;
                    viewport.width = swapchainDrawWidth;
                    viewport.height = swapchainDrawHeight;
                    viewport.minDepth = 0.0f;
                    viewport.maxDepth = 1.0f;

                    VkRect2D scissor{};
                    scissor.offset = {0, 0};
                    scissor.extent.width = (swapchainExtent.width == 0u) ? 1u : swapchainExtent.width;
                    scissor.extent.height = (swapchainExtent.height == 0u) ? 1u : swapchainExtent.height;

                    vkCmdBindPipeline(primaryCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchainTrianglePipeline);

                    if ((swapchainTrianglePipelineLayout != VK_NULL_HANDLE) && (swapchainTriangleDescriptorSet != VK_NULL_HANDLE))
                    {
                        UpdateSwapchainTriangleTextureDescriptor(swapchainTriangleTexture);

                        vkCmdBindDescriptorSets(
                            primaryCommandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            swapchainTrianglePipelineLayout,
                            0u,
                            1u,
                            &swapchainTriangleDescriptorSet,
                            0u,
                            nullptr);
                    }

                    const VkDeviceSize vertexBufferOffset = 0;
                    vkCmdBindVertexBuffers(primaryCommandBuffer, 0u, 1u, &swapchainTriangleVertexBuffer, &vertexBufferOffset);
                    vkCmdBindIndexBuffer(primaryCommandBuffer, swapchainTriangleIndexBuffer, 0u, VK_INDEX_TYPE_UINT16);
                    vkCmdSetViewport(primaryCommandBuffer, 0u, 1u, &viewport);
                    vkCmdSetScissor(primaryCommandBuffer, 0u, 1u, &scissor);
                    vkCmdDrawIndexed(primaryCommandBuffer, static_cast<std::uint32_t>(SwapchainTriangleIndices.size()), 1u, 0u, 0, 0u);
                }

                vkCmdEndRenderPass(primaryCommandBuffer);
            }

            EndFrame();
        }

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

    void VulkanGraphicsBackend::UpdateSwapchainTriangleTextureDescriptor(ITexture *texture)
    {
        if ((device == VK_NULL_HANDLE) || (swapchainTriangleDescriptorSet == VK_NULL_HANDLE))
            return;

        ITexture *textureToBind = (texture != nullptr) ? texture : swapchainTriangleFallbackTexture.get();
        if (textureToBind == nullptr)
            return;

        auto *vulkanTexture = dynamic_cast<VulkanTexture *>(textureToBind);
        if ((vulkanTexture == nullptr) || (vulkanTexture->GetImageView() == VK_NULL_HANDLE) || (vulkanTexture->GetSampler() == VK_NULL_HANDLE))
        {
            vulkanTexture = dynamic_cast<VulkanTexture *>(swapchainTriangleFallbackTexture.get());
            if ((vulkanTexture == nullptr) || (vulkanTexture->GetImageView() == VK_NULL_HANDLE) || (vulkanTexture->GetSampler() == VK_NULL_HANDLE))
                return;
        }

        VkDescriptorImageInfo textureImageInfo{};
        textureImageInfo.sampler = vulkanTexture->GetSampler();
        textureImageInfo.imageView = vulkanTexture->GetImageView();
        textureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = swapchainTriangleDescriptorSet;
        descriptorWrite.dstBinding = 1;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.pBufferInfo = nullptr;
        descriptorWrite.pImageInfo = &textureImageInfo;
        descriptorWrite.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(device, 1u, &descriptorWrite, 0u, nullptr);
    }

    void VulkanGraphicsBackend::SetSwapchainTriangleTexture(ITexture *texture)
    {
        swapchainTriangleTexture = texture;
        UpdateSwapchainTriangleTextureDescriptor(swapchainTriangleTexture);
    }

    VkFormat VulkanGraphicsBackend::FindSupportedDepthFormat() const
    {
        if (physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("VulkanGraphicsBackend::FindSupportedDepthFormat requires a valid physical device");

        static constexpr std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT,
        };

        for (const VkFormat format : candidates)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);

            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0u)
                return format;
        }

        throw std::runtime_error("VulkanGraphicsBackend::FindSupportedDepthFormat failed to find supported depth format");
    }

    void VulkanGraphicsBackend::CreateSwapchainDepthResources()
    {
        if ((device == VK_NULL_HANDLE) || (physicalDevice == VK_NULL_HANDLE))
            return;

        if (swapchainDepthImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, swapchainDepthImageView, nullptr);
            swapchainDepthImageView = VK_NULL_HANDLE;
        }

        if (swapchainDepthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, swapchainDepthImage, nullptr);
            swapchainDepthImage = VK_NULL_HANDLE;
        }

        if (swapchainDepthImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, swapchainDepthImageMemory, nullptr);
            swapchainDepthImageMemory = VK_NULL_HANDLE;
        }

        swapchainDepthFormat = FindSupportedDepthFormat();

        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if ((swapchainDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) || (swapchainDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT))
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        CreateImage(
            (swapchainExtent.width == 0u) ? 1u : swapchainExtent.width,
            (swapchainExtent.height == 0u) ? 1u : swapchainExtent.height,
            swapchainDepthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            aspectMask,
            swapchainDepthImage,
            swapchainDepthImageMemory,
            swapchainDepthImageView);
    }

    std::uint32_t VulkanGraphicsBackend::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            const bool typeMatches = ((typeFilter & (1u << i)) != 0u);
            const bool propertiesMatch = ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties);

            if (typeMatches && propertiesMatch)
                return i;
        }

        throw std::runtime_error("VulkanGraphicsBackend: failed to find compatible Vulkan memory type");
    }

    void VulkanGraphicsBackend::CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer &buffer,
        VkDeviceMemory &memory)
    {
        if ((device == VK_NULL_HANDLE) || (physicalDevice == VK_NULL_HANDLE))
            throw std::runtime_error("VulkanGraphicsBackend::CreateBuffer requires a valid device and physical device");

        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }

        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        const VkDeviceSize bufferSize = (size == 0u) ? static_cast<VkDeviceSize>(1u) : size;

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = bufferSize;
        bufferCreateInfo.usage = usage;
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CheckVkResult(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer), "vkCreateBuffer(swapchainTriangle)");

        try
        {
            VkMemoryRequirements memoryRequirements{};
            vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, properties);

            CheckVkResult(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory(swapchainTriangle)");
            CheckVkResult(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory(swapchainTriangle)");
        }
        catch (...)
        {
            if (buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device, buffer, nullptr);
                buffer = VK_NULL_HANDLE;
            }

            if (memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, memory, nullptr);
                memory = VK_NULL_HANDLE;
            }

            throw;
        }
    }

    void VulkanGraphicsBackend::CreateImage(
        std::uint32_t width,
        std::uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectFlags,
        VkImage &image,
        VkDeviceMemory &memory,
        VkImageView &imageView)
    {
        if ((device == VK_NULL_HANDLE) || (physicalDevice == VK_NULL_HANDLE))
            throw std::runtime_error("VulkanGraphicsBackend::CreateImage requires a valid device and physical device");

        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }

        if (image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
        }

        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }

        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.extent.width = (width == 0u) ? 1u : width;
        imageCreateInfo.extent.height = (height == 0u) ? 1u : height;
        imageCreateInfo.extent.depth = 1;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.format = format;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.usage = usage;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CheckVkResult(vkCreateImage(device, &imageCreateInfo, nullptr, &image), "vkCreateImage(swapchainTriangle)");

        try
        {
            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(device, image, &memoryRequirements);

            VkMemoryAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            CheckVkResult(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory(swapchainTriangleImage)");
            CheckVkResult(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory(swapchainTriangleImage)");

            VkImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = image;
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = format;
            imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.subresourceRange.aspectMask = aspectFlags;
            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = 1;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            imageViewCreateInfo.subresourceRange.layerCount = 1;
            CheckVkResult(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &imageView), "vkCreateImageView(swapchainTriangle)");
        }
        catch (...)
        {
            if (imageView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, imageView, nullptr);
                imageView = VK_NULL_HANDLE;
            }

            if (image != VK_NULL_HANDLE)
            {
                vkDestroyImage(device, image, nullptr);
                image = VK_NULL_HANDLE;
            }

            if (memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, memory, nullptr);
                memory = VK_NULL_HANDLE;
            }

            throw;
        }
    }

    void VulkanGraphicsBackend::CreateSwapchainTriangleResources()
    {
        if (device == VK_NULL_HANDLE)
            return;

        DestroySwapchainTriangleResources();

        try
        {
            const VkDeviceSize vertexBufferSize = static_cast<VkDeviceSize>(sizeof(SwapchainTriangleVertices));
            const VkMemoryPropertyFlags hostVisibleAndCoherentMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            CreateBuffer(
                vertexBufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                hostVisibleAndCoherentMemory,
                swapchainTriangleVertexBuffer,
                swapchainTriangleVertexBufferMemory);

            void *vertexData = nullptr;
            CheckVkResult(vkMapMemory(device, swapchainTriangleVertexBufferMemory, 0, vertexBufferSize, 0, &vertexData), "vkMapMemory(swapchainTriangleVertexBuffer)");
            std::memcpy(vertexData, SwapchainTriangleVertices.data(), sizeof(SwapchainTriangleVertices));
            vkUnmapMemory(device, swapchainTriangleVertexBufferMemory);

            const VkDeviceSize indexBufferSize = static_cast<VkDeviceSize>(sizeof(SwapchainTriangleIndices));
            CreateBuffer(
                indexBufferSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                hostVisibleAndCoherentMemory,
                swapchainTriangleIndexBuffer,
                swapchainTriangleIndexBufferMemory);

            void *indexData = nullptr;
            CheckVkResult(vkMapMemory(device, swapchainTriangleIndexBufferMemory, 0, indexBufferSize, 0, &indexData), "vkMapMemory(swapchainTriangleIndexBuffer)");
            std::memcpy(indexData, SwapchainTriangleIndices.data(), sizeof(SwapchainTriangleIndices));
            vkUnmapMemory(device, swapchainTriangleIndexBufferMemory);

            const VkDeviceSize uniformBufferSize = static_cast<VkDeviceSize>(sizeof(float) * 32u);
            CreateBuffer(
                uniformBufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                hostVisibleAndCoherentMemory,
                swapchainTriangleUniformBuffer,
                swapchainTriangleUniformBufferMemory);

            VkDescriptorSetLayoutBinding uniformBufferBinding{};
            uniformBufferBinding.binding = 0;
            uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformBufferBinding.descriptorCount = 1;
            uniformBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            uniformBufferBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutBinding combinedSamplerBinding{};
            combinedSamplerBinding.binding = 1;
            combinedSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            combinedSamplerBinding.descriptorCount = 1;
            combinedSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            combinedSamplerBinding.pImmutableSamplers = nullptr;

            const std::array<VkDescriptorSetLayoutBinding, 2> descriptorSetLayoutBindings = {
                uniformBufferBinding,
                combinedSamplerBinding,
            };

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
            descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutCreateInfo.bindingCount = static_cast<std::uint32_t>(descriptorSetLayoutBindings.size());
            descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings.data();
            CheckVkResult(
                vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &swapchainTriangleDescriptorSetLayout),
                "vkCreateDescriptorSetLayout(swapchainTriangle)");

            std::array<VkDescriptorPoolSize, 2> descriptorPoolSizes{};
            descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorPoolSizes[0].descriptorCount = 1;
            descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorPoolSizes[1].descriptorCount = 1;

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
            descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreateInfo.flags = 0;
            descriptorPoolCreateInfo.maxSets = 1;
            descriptorPoolCreateInfo.poolSizeCount = static_cast<std::uint32_t>(descriptorPoolSizes.size());
            descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
            CheckVkResult(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &swapchainTriangleDescriptorPool), "vkCreateDescriptorPool(swapchainTriangle)");

            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
            descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool = swapchainTriangleDescriptorPool;
            descriptorSetAllocateInfo.descriptorSetCount = 1;
            descriptorSetAllocateInfo.pSetLayouts = &swapchainTriangleDescriptorSetLayout;
            CheckVkResult(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &swapchainTriangleDescriptorSet), "vkAllocateDescriptorSets(swapchainTriangle)");

            VkDescriptorBufferInfo uniformBufferInfo{};
            uniformBufferInfo.buffer = swapchainTriangleUniformBuffer;
            uniformBufferInfo.offset = 0;
            uniformBufferInfo.range = uniformBufferSize;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = swapchainTriangleDescriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.pBufferInfo = &uniformBufferInfo;
            descriptorWrite.pImageInfo = nullptr;
            descriptorWrite.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(device, 1u, &descriptorWrite, 0u, nullptr);

            TextureCreateInfo fallbackTextureCI{};
            fallbackTextureCI.dimension = TextureDimension::Tex2D;
            fallbackTextureCI.format = PixelFormat::RGBA8_UNorm;
            fallbackTextureCI.extent.width = 1u;
            fallbackTextureCI.extent.height = 1u;
            fallbackTextureCI.extent.depth = 1u;
            fallbackTextureCI.mipLevels = 1u;
            fallbackTextureCI.arrayLayers = 1u;
            fallbackTextureCI.usage = TextureUsage::Sampled | TextureUsage::TransferDst;
            fallbackTextureCI.debugName = "SwapchainTriangleFallbackTexture";

            swapchainTriangleFallbackTexture = CreateTexture(fallbackTextureCI);
            if (swapchainTriangleFallbackTexture != nullptr)
            {
                static constexpr std::array<std::byte, 4> whitePixel = {
                    std::byte{0xFF},
                    std::byte{0xFF},
                    std::byte{0xFF},
                    std::byte{0xFF},
                };

                swapchainTriangleFallbackTexture->Upload(0u, 0u, std::span<const std::byte>(whitePixel.data(), whitePixel.size()));
            }

            UpdateSwapchainTriangleTextureDescriptor(swapchainTriangleTexture);

            UpdateSwapchainTriangleProjection();
        }
        catch (...)
        {
            DestroySwapchainTriangleResources();
            throw;
        }
    }

    void VulkanGraphicsBackend::UpdateSwapchainTriangleProjection()
    {
        if ((device == VK_NULL_HANDLE) || (swapchainTriangleUniformBufferMemory == VK_NULL_HANDLE))
            return;

        float elapsedSeconds = 0.0f;
#if !defined(HEADLESS)
        elapsedSeconds = static_cast<float>(SDL_GetTicks()) * 0.001f;
#endif

        const std::array<float, 16> modelMatrix = BuildSwapchainTriangleModelMatrix(elapsedSeconds);
        const std::array<float, 16> mvpMatrix = BuildSwapchainTriangleMvpMatrix(swapchainExtent, elapsedSeconds);

        struct SwapchainTriangleUniformData
        {
            std::array<float, 16> mvp;
            std::array<float, 16> model;
        };

        const SwapchainTriangleUniformData uniformData = {
            mvpMatrix,
            modelMatrix,
        };

        void *mappedData = nullptr;
        CheckVkResult(
            vkMapMemory(device, swapchainTriangleUniformBufferMemory, 0, static_cast<VkDeviceSize>(sizeof(uniformData)), 0, &mappedData),
            "vkMapMemory(swapchainTriangleUniformBuffer)");

        std::memcpy(mappedData, &uniformData, sizeof(uniformData));
        vkUnmapMemory(device, swapchainTriangleUniformBufferMemory);
    }

    void VulkanGraphicsBackend::DestroySwapchainTriangleResources() noexcept
    {
        if (device == VK_NULL_HANDLE)
            return;

        if (swapchainTrianglePipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, swapchainTrianglePipeline, nullptr);
            swapchainTrianglePipeline = VK_NULL_HANDLE;
        }

        if (swapchainTrianglePipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, swapchainTrianglePipelineLayout, nullptr);
            swapchainTrianglePipelineLayout = VK_NULL_HANDLE;
        }

        if (swapchainTriangleDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, swapchainTriangleDescriptorPool, nullptr);
            swapchainTriangleDescriptorPool = VK_NULL_HANDLE;
        }
        swapchainTriangleDescriptorSet = VK_NULL_HANDLE;

        if (swapchainTriangleDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, swapchainTriangleDescriptorSetLayout, nullptr);
            swapchainTriangleDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (swapchainTriangleVertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, swapchainTriangleVertexBuffer, nullptr);
            swapchainTriangleVertexBuffer = VK_NULL_HANDLE;
        }

        if (swapchainTriangleVertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, swapchainTriangleVertexBufferMemory, nullptr);
            swapchainTriangleVertexBufferMemory = VK_NULL_HANDLE;
        }

        if (swapchainTriangleIndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, swapchainTriangleIndexBuffer, nullptr);
            swapchainTriangleIndexBuffer = VK_NULL_HANDLE;
        }

        if (swapchainTriangleIndexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, swapchainTriangleIndexBufferMemory, nullptr);
            swapchainTriangleIndexBufferMemory = VK_NULL_HANDLE;
        }

        if (swapchainTriangleUniformBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, swapchainTriangleUniformBuffer, nullptr);
            swapchainTriangleUniformBuffer = VK_NULL_HANDLE;
        }

        if (swapchainTriangleUniformBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, swapchainTriangleUniformBufferMemory, nullptr);
            swapchainTriangleUniformBufferMemory = VK_NULL_HANDLE;
        }

        swapchainTriangleTexture = nullptr;
        swapchainTriangleFallbackTexture.reset();
    }

    void VulkanGraphicsBackend::Init()
    {
        VulkanTrace("[Vulkan][Init] begin\n");

        try
        {
            VulkanTrace("[Vulkan][Init] CreateInstance begin\n");
            CreateInstance();

            VulkanTrace("[Vulkan][Init] CreateSurface begin\n");
            CreateSurface();

            VulkanTrace("[Vulkan][Init] SelectPhysicalDevice begin\n");
            SelectPhysicalDevice();

            VulkanTrace("[Vulkan][Init] SelectGraphicsQueueFamily begin\n");
            SelectGraphicsQueueFamily();

            VulkanTrace("[Vulkan][Init] SelectPresentQueueFamily begin\n");
            SelectPresentQueueFamily();

            VulkanTrace("[Vulkan][Init] CreateLogicalDevice begin\n");
            CreateLogicalDevice();

            VulkanTrace("[Vulkan][Init] CreateCommandPool begin\n");
            CreateCommandPool();

            VulkanTrace("[Vulkan][Init] CreatePrimaryCommandBuffer begin\n");
            CreatePrimaryCommandBuffer();

            VulkanTrace("[Vulkan][Init] CreateSwapchain begin\n");
            CreateSwapchain();

            VulkanTrace("[Vulkan][Init] complete\n");
        }
        catch (const std::exception &ex)
        {
            VulkanTrace("[Vulkan][Init] exception: %s\n", ex.what());
            Cleanup();
            throw;
        }
        catch (...)
        {
            VulkanTrace("[Vulkan][Init] unknown exception\n");
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

        std::uint32_t availableExtensionCount = 0;
        CheckVkResult(
            vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr),
            "vkEnumerateInstanceExtensionProperties(count)");

        std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
        if (availableExtensionCount > 0u)
        {
            CheckVkResult(
                vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensions.data()),
                "vkEnumerateInstanceExtensionProperties(list)");
        }

        std::fprintf(stderr, "[Vulkan] Available instance extensions (%u):\n", static_cast<unsigned int>(availableExtensionCount));
        for (const VkExtensionProperties &extension : availableExtensions)
            std::fprintf(stderr, "[Vulkan]   %s (specVersion=%u)\n", extension.extensionName, static_cast<unsigned int>(extension.specVersion));

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
        AddInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.flags = instanceFlags;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
        createInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

        std::fprintf(stderr, "[Vulkan] vkCreateInstance enabled extensions (%u):\n", static_cast<unsigned int>(createInfo.enabledExtensionCount));
        for (std::uint32_t i = 0; i < createInfo.enabledExtensionCount; ++i)
        {
            const char *extName = createInfo.ppEnabledExtensionNames[i];
            std::fprintf(stderr, "[Vulkan]   %s\n", (extName != nullptr) ? extName : "<null>");
        }
        std::fprintf(stderr,
                     "[Vulkan] vkCreateInstance flags=0x%08x (%u)\n",
                     static_cast<unsigned int>(createInfo.flags),
                     static_cast<unsigned int>(createInfo.flags));

        CheckVkResult(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
    }

    void VulkanGraphicsBackend::CreateSurface()
    {
#if !defined(HEADLESS)
        VulkanTrace("[Vulkan] CreateSurface window=%s instance=%s\n", (window != nullptr) ? "set" : "null", (instance != VK_NULL_HANDLE) ? "set" : "null");

        if ((window == nullptr) || (instance == VK_NULL_HANDLE))
            return;

#if SDL_MAJOR_VERSION >= 3
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
#else
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
#endif

        VulkanTrace("[Vulkan] CreateSurface done\n");
#else
        surface = VK_NULL_HANDLE;
        VulkanTrace("[Vulkan] CreateSurface skipped in headless mode\n");
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
        VkPhysicalDeviceProperties selectedProperties{};

        for (const VkPhysicalDevice candidate : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                selectedDevice = candidate;
                selectedProperties = properties;
                break;
            }

            if (selectedDevice == VK_NULL_HANDLE)
            {
                selectedDevice = candidate;
                selectedProperties = properties;
            }
        }

        if (selectedDevice == VK_NULL_HANDLE)
            throw std::runtime_error("failed to select a VkPhysicalDevice");

        physicalDevice = selectedDevice;

        VulkanTrace(
            "[Vulkan] Selected physical device: %s type=%u api=%u.%u.%u\n",
            selectedProperties.deviceName,
            static_cast<unsigned int>(selectedProperties.deviceType),
            static_cast<unsigned int>(VK_API_VERSION_MAJOR(selectedProperties.apiVersion)),
            static_cast<unsigned int>(VK_API_VERSION_MINOR(selectedProperties.apiVersion)),
            static_cast<unsigned int>(VK_API_VERSION_PATCH(selectedProperties.apiVersion)));
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

        VulkanTrace(
            "[Vulkan] Graphics queue family selected: %u (queueFamilyCount=%u)\n",
            static_cast<unsigned int>(graphicsQueueFamilyIndex),
            static_cast<unsigned int>(queueFamilyCount));
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

        VulkanTrace(
            "[Vulkan] Present queue family selected: %u\n",
            static_cast<unsigned int>(presentQueueFamilyIndex));
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

        VulkanTrace(
            "[Vulkan] vkCreateDevice queueCreateInfoCount=%u graphicsQ=%u presentQ=%u\n",
            static_cast<unsigned int>(createInfo.queueCreateInfoCount),
            static_cast<unsigned int>(graphicsQueueFamilyIndex),
            static_cast<unsigned int>(presentQueueFamilyIndex));

        VulkanTrace("[Vulkan] vkCreateDevice enabled extensions (%u):\n", static_cast<unsigned int>(createInfo.enabledExtensionCount));
        for (std::uint32_t i = 0; i < createInfo.enabledExtensionCount; ++i)
        {
            const char *extName = createInfo.ppEnabledExtensionNames[i];
            VulkanTrace("[Vulkan]   %s\n", (extName != nullptr) ? extName : "<null>");
        }

        CheckVkResult(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
        VulkanTrace("[Vulkan] vkCreateDevice succeeded\n");

        vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0u, &graphicsQueue);
        vkGetDeviceQueue(device, presentQueueFamilyIndex, 0u, &presentQueue);
        VulkanTrace("[Vulkan] vkGetDeviceQueue done\n");
    }

    void VulkanGraphicsBackend::CreateCommandPool()
    {
        VulkanTrace("[Vulkan] CreateCommandPool begin\n");

        VkCommandPoolCreateInfo commandPoolCreateInfo{};
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

        CheckVkResult(vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool), "vkCreateCommandPool");
        VulkanTrace("[Vulkan] CreateCommandPool done\n");
    }

    void VulkanGraphicsBackend::CreatePrimaryCommandBuffer()
    {
        VulkanTrace("[Vulkan] CreatePrimaryCommandBuffer begin\n");

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        CheckVkResult(vkAllocateCommandBuffers(device, &allocateInfo, &primaryCommandBuffer), "vkAllocateCommandBuffers");
        VulkanTrace("[Vulkan] CreatePrimaryCommandBuffer done\n");
    }

    void VulkanGraphicsBackend::CreateSwapchain()
    {
        VulkanTrace(
            "[Vulkan] CreateSwapchain begin (surface=%s device=%s physicalDevice=%s)\n",
            (surface != VK_NULL_HANDLE) ? "set" : "null",
            (device != VK_NULL_HANDLE) ? "set" : "null",
            (physicalDevice != VK_NULL_HANDLE) ? "set" : "null");

        if ((surface == VK_NULL_HANDLE) || (device == VK_NULL_HANDLE) || (physicalDevice == VK_NULL_HANDLE))
            return;

        DestroySwapchain();

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        CheckVkResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t surfaceFormatCount = 0;
        CheckVkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");

        VulkanTrace("[Vulkan] Surface format count=%u\n", static_cast<unsigned int>(surfaceFormatCount));

        if (surfaceFormatCount == 0u)
            throw std::runtime_error("vkGetPhysicalDeviceSurfaceFormatsKHR returned zero formats");

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        CheckVkResult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");

        std::uint32_t presentModeCount = 0;
        CheckVkResult(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");

        VulkanTrace("[Vulkan] Present mode count=%u\n", static_cast<unsigned int>(presentModeCount));

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

        VulkanTrace(
            "[Vulkan] Swapchain selection format=%u colorSpace=%u presentMode=%u extent=%ux%u\n",
            static_cast<unsigned int>(selectedSurfaceFormat.format),
            static_cast<unsigned int>(selectedSurfaceFormat.colorSpace),
            static_cast<unsigned int>(selectedPresentMode),
            static_cast<unsigned int>(desiredExtent.width),
            static_cast<unsigned int>(desiredExtent.height));

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
        VulkanTrace("[Vulkan] vkCreateSwapchainKHR succeeded\n");

        swapchainImageFormat = selectedSurfaceFormat.format;
        swapchainExtent = desiredExtent;

        std::uint32_t swapchainImageCount = 0;
        CheckVkResult(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr), "vkGetSwapchainImagesKHR(count)");

        VulkanTrace("[Vulkan] Swapchain image count=%u\n", static_cast<unsigned int>(swapchainImageCount));

        if (swapchainImageCount == 0u)
            throw std::runtime_error("vkGetSwapchainImagesKHR returned zero images");

        swapchainImages.assign(swapchainImageCount, VK_NULL_HANDLE);
        CheckVkResult(vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data()), "vkGetSwapchainImagesKHR(list)");

        CreateSwapchainImageViews();
        CreateSwapchainDepthResources();
        CreateSwapchainRenderPass();
        CreateSwapchainFramebuffers();
        CreateSwapchainTriangleResources();
        CreateSwapchainTrianglePipeline();

        VkFenceCreateInfo fenceCreateInfo{};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        CheckVkResult(vkCreateFence(device, &fenceCreateInfo, nullptr, &swapchainAcquireFence), "vkCreateFence(swapchainAcquireFence)");

        VulkanTrace("[Vulkan] CreateSwapchain done\n");
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

    void VulkanGraphicsBackend::CreateSwapchainRenderPass()
    {
        if ((device == VK_NULL_HANDLE) || (swapchainImageFormat == VK_FORMAT_UNDEFINED) || (swapchainDepthFormat == VK_FORMAT_UNDEFINED))
            return;

        if (swapchainRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, swapchainRenderPass, nullptr);
            swapchainRenderPass = VK_NULL_HANDLE;
        }

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = swapchainDepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::array<VkAttachmentDescription, 2> attachments = {
            colorAttachment,
            depthAttachment,
        };

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        renderPassCreateInfo.pAttachments = attachments.data();
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &dependency;

        CheckVkResult(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &swapchainRenderPass), "vkCreateRenderPass(swapchain)");
    }

    void VulkanGraphicsBackend::CreateSwapchainFramebuffers()
    {
        if ((device == VK_NULL_HANDLE) || (swapchainRenderPass == VK_NULL_HANDLE) || (swapchainDepthImageView == VK_NULL_HANDLE))
            return;

        for (const VkFramebuffer framebuffer : swapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        swapchainFramebuffers.clear();

        swapchainFramebuffers.reserve(swapchainImageViews.size());

        for (const VkImageView imageView : swapchainImageViews)
        {
            if ((imageView == VK_NULL_HANDLE) || (swapchainDepthImageView == VK_NULL_HANDLE))
            {
                swapchainFramebuffers.push_back(VK_NULL_HANDLE);
                continue;
            }

            const std::array<VkImageView, 2> framebufferAttachments = {
                imageView,
                swapchainDepthImageView,
            };

            VkFramebufferCreateInfo framebufferCreateInfo{};
            framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCreateInfo.renderPass = swapchainRenderPass;
            framebufferCreateInfo.attachmentCount = static_cast<std::uint32_t>(framebufferAttachments.size());
            framebufferCreateInfo.pAttachments = framebufferAttachments.data();
            framebufferCreateInfo.width = (swapchainExtent.width == 0u) ? 1u : swapchainExtent.width;
            framebufferCreateInfo.height = (swapchainExtent.height == 0u) ? 1u : swapchainExtent.height;
            framebufferCreateInfo.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            CheckVkResult(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer), "vkCreateFramebuffer(swapchain)");
            swapchainFramebuffers.push_back(framebuffer);
        }
    }

    void VulkanGraphicsBackend::CreateSwapchainTrianglePipeline()
    {
        if ((device == VK_NULL_HANDLE) || (swapchainRenderPass == VK_NULL_HANDLE))
            return;

        if (swapchainTrianglePipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, swapchainTrianglePipeline, nullptr);
            swapchainTrianglePipeline = VK_NULL_HANDLE;
        }

        if (swapchainTrianglePipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, swapchainTrianglePipelineLayout, nullptr);
            swapchainTrianglePipelineLayout = VK_NULL_HANDLE;
        }

        VkShaderModule vertexShaderModule = VK_NULL_HANDLE;
        VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;

        const auto CleanupShaderModules = [&]()
        {
            if (vertexShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(device, vertexShaderModule, nullptr);
                vertexShaderModule = VK_NULL_HANDLE;
            }

            if (fragmentShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
                fragmentShaderModule = VK_NULL_HANDLE;
            }
        };

        try
        {
            VkShaderModuleCreateInfo vertexShaderCreateInfo{};
            vertexShaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vertexShaderCreateInfo.codeSize = sizeof(SwapchainTriangleVertSpv);
            vertexShaderCreateInfo.pCode = SwapchainTriangleVertSpv;
            CheckVkResult(vkCreateShaderModule(device, &vertexShaderCreateInfo, nullptr, &vertexShaderModule), "vkCreateShaderModule(swapchainTriangleVert)");

            VkShaderModuleCreateInfo fragmentShaderCreateInfo{};
            fragmentShaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fragmentShaderCreateInfo.codeSize = sizeof(SwapchainTriangleFragSpv);
            fragmentShaderCreateInfo.pCode = SwapchainTriangleFragSpv;
            CheckVkResult(vkCreateShaderModule(device, &fragmentShaderCreateInfo, nullptr, &fragmentShaderModule), "vkCreateShaderModule(swapchainTriangleFrag)");

            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.setLayoutCount = (swapchainTriangleDescriptorSetLayout != VK_NULL_HANDLE) ? 1u : 0u;
            pipelineLayoutCreateInfo.pSetLayouts = (swapchainTriangleDescriptorSetLayout != VK_NULL_HANDLE) ? &swapchainTriangleDescriptorSetLayout : nullptr;
            pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
            pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
            CheckVkResult(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &swapchainTrianglePipelineLayout), "vkCreatePipelineLayout(swapchainTriangle)");

            VkPipelineShaderStageCreateInfo shaderStageCreateInfos[2]{};
            shaderStageCreateInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageCreateInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStageCreateInfos[0].module = vertexShaderModule;
            shaderStageCreateInfos[0].pName = "main";
            shaderStageCreateInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageCreateInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStageCreateInfos[1].module = fragmentShaderModule;
            shaderStageCreateInfos[1].pName = "main";

            VkVertexInputBindingDescription vertexBindingDescription{};
            vertexBindingDescription.binding = 0;
            vertexBindingDescription.stride = static_cast<std::uint32_t>(sizeof(VulkanVertex));
            vertexBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 3> vertexAttributeDescriptions{};
            vertexAttributeDescriptions[0].location = 0;
            vertexAttributeDescriptions[0].binding = 0;
            vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            vertexAttributeDescriptions[0].offset = static_cast<std::uint32_t>(offsetof(VulkanVertex, x));

            vertexAttributeDescriptions[1].location = 1;
            vertexAttributeDescriptions[1].binding = 0;
            vertexAttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            vertexAttributeDescriptions[1].offset = static_cast<std::uint32_t>(offsetof(VulkanVertex, nx));

            vertexAttributeDescriptions[2].location = 2;
            vertexAttributeDescriptions[2].binding = 0;
            vertexAttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            vertexAttributeDescriptions[2].offset = static_cast<std::uint32_t>(offsetof(VulkanVertex, u));

            VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{};
            vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
            vertexInputStateCreateInfo.pVertexBindingDescriptions = &vertexBindingDescription;
            vertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexAttributeDescriptions.size());
            vertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo{};
            inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewportStateCreateInfo{};
            viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportStateCreateInfo.viewportCount = 1;
            viewportStateCreateInfo.pViewports = nullptr;
            viewportStateCreateInfo.scissorCount = 1;
            viewportStateCreateInfo.pScissors = nullptr;

            const VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };

            VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{};
            dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicStateCreateInfo.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
            dynamicStateCreateInfo.pDynamicStates = dynamicStates;

            VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo{};
            rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
            rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
            rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
            rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
            rasterizationStateCreateInfo.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo{};
            multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
            multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo{};
            depthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilStateCreateInfo.depthTestEnable = VK_TRUE;
            depthStencilStateCreateInfo.depthWriteEnable = VK_TRUE;
            depthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
            depthStencilStateCreateInfo.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};
            colorBlendAttachmentState.blendEnable = VK_TRUE;
            colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachmentState.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo{};
            colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
            colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
            colorBlendStateCreateInfo.attachmentCount = 1;
            colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;

            VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
            graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            graphicsPipelineCreateInfo.stageCount = 2;
            graphicsPipelineCreateInfo.pStages = shaderStageCreateInfos;
            graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
            graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
            graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
            graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
            graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
            graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
            graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
            graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
            graphicsPipelineCreateInfo.layout = swapchainTrianglePipelineLayout;
            graphicsPipelineCreateInfo.renderPass = swapchainRenderPass;
            graphicsPipelineCreateInfo.subpass = 0;
            graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
            graphicsPipelineCreateInfo.basePipelineIndex = -1;

            CheckVkResult(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphicsPipelineCreateInfo, nullptr, &swapchainTrianglePipeline), "vkCreateGraphicsPipelines(swapchainTriangle)");

            CleanupShaderModules();
        }
        catch (...)
        {
            CleanupShaderModules();

            if (swapchainTrianglePipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, swapchainTrianglePipeline, nullptr);
                swapchainTrianglePipeline = VK_NULL_HANDLE;
            }

            if (swapchainTrianglePipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, swapchainTrianglePipelineLayout, nullptr);
                swapchainTrianglePipelineLayout = VK_NULL_HANDLE;
            }

            throw;
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

        for (const VkFramebuffer framebuffer : swapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        swapchainFramebuffers.clear();

        DestroySwapchainTriangleResources();

        if (swapchainDepthImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, swapchainDepthImageView, nullptr);
            swapchainDepthImageView = VK_NULL_HANDLE;
        }

        if (swapchainDepthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, swapchainDepthImage, nullptr);
            swapchainDepthImage = VK_NULL_HANDLE;
        }

        if (swapchainDepthImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, swapchainDepthImageMemory, nullptr);
            swapchainDepthImageMemory = VK_NULL_HANDLE;
        }

        if (swapchainRenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, swapchainRenderPass, nullptr);
            swapchainRenderPass = VK_NULL_HANDLE;
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
        swapchainDepthFormat = VK_FORMAT_UNDEFINED;
        swapchainExtent = {0u, 0u};
        currentSwapchainImageIndex = 0;
    }

} // namespace gfx
