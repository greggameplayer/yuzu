// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "common/common_types.h"
#include "core/hle/service/nvflinger/buffer_queue.h"
#include "video_core/dma_pusher.h"
#include "video_core/memory_manager.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace Tegra {

enum class RenderTargetFormat : u32 {
    NONE = 0x0,
    RGBA32_FLOAT = 0xC0,
    RGBA32_UINT = 0xC2,
    RGBA16_UNORM = 0xC6,
    RGBA16_UINT = 0xC9,
    RGBA16_FLOAT = 0xCA,
    RG32_FLOAT = 0xCB,
    RG32_UINT = 0xCD,
    BGRA8_UNORM = 0xCF,
    BGRA8_SRGB = 0xD0,
    RGB10_A2_UNORM = 0xD1,
    RGBA8_UNORM = 0xD5,
    RGBA8_SRGB = 0xD6,
    RGBA8_SNORM = 0xD7,
    RGBA8_UINT = 0xD9,
    RG16_UNORM = 0xDA,
    RG16_SNORM = 0xDB,
    RG16_SINT = 0xDC,
    RG16_UINT = 0xDD,
    RG16_FLOAT = 0xDE,
    R11G11B10_FLOAT = 0xE0,
    R32_UINT = 0xE4,
    R32_FLOAT = 0xE5,
    B5G6R5_UNORM = 0xE8,
    BGR5A1_UNORM = 0xE9,
    RG8_UNORM = 0xEA,
    RG8_SNORM = 0xEB,
    R16_UNORM = 0xEE,
    R16_SNORM = 0xEF,
    R16_SINT = 0xF0,
    R16_UINT = 0xF1,
    R16_FLOAT = 0xF2,
    R8_UNORM = 0xF3,
    R8_UINT = 0xF6,
};

enum class DepthFormat : u32 {
    Z32_FLOAT = 0xA,
    Z16_UNORM = 0x13,
    S8_Z24_UNORM = 0x14,
    Z24_X8_UNORM = 0x15,
    Z24_S8_UNORM = 0x16,
    Z24_C8_UNORM = 0x18,
    Z32_S8_X24_FLOAT = 0x19,
};

/// Returns the number of bytes per pixel of each rendertarget format.
u32 RenderTargetBytesPerPixel(RenderTargetFormat format);

/// Returns the number of bytes per pixel of each depth format.
u32 DepthFormatBytesPerPixel(DepthFormat format);

struct CommandListHeader;
class DebugContext;

/**
 * Struct describing framebuffer configuration
 */
struct FramebufferConfig {
    enum class PixelFormat : u32 {
        ABGR8 = 1,
        BGRA8 = 5,
    };

    /**
     * Returns the number of bytes per pixel.
     */
    static u32 BytesPerPixel(PixelFormat format);

    VAddr address;
    u32 offset;
    u32 width;
    u32 height;
    u32 stride;
    PixelFormat pixel_format;

    using TransformFlags = Service::NVFlinger::BufferQueue::BufferTransformFlags;
    TransformFlags transform_flags;
    MathUtil::Rectangle<int> crop_rect;
};

namespace Engines {
class Fermi2D;
class Maxwell3D;
class MaxwellCompute;
class MaxwellDMA;
class KeplerMemory;
} // namespace Engines

enum class EngineID {
    FERMI_TWOD_A = 0x902D, // 2D Engine
    MAXWELL_B = 0xB197,    // 3D Engine
    MAXWELL_COMPUTE_B = 0xB1C0,
    KEPLER_INLINE_TO_MEMORY_B = 0xA140,
    MAXWELL_DMA_COPY_A = 0xB0B5,
};

class GPU final {
public:
    explicit GPU(VideoCore::RasterizerInterface& rasterizer);
    ~GPU();

    struct MethodCall {
        u32 method{};
        u32 argument{};
        u32 subchannel{};
        u32 method_count{};

        bool IsLastCall() const {
            return method_count <= 1;
        }

        MethodCall(u32 method, u32 argument, u32 subchannel = 0, u32 method_count = 0)
            : method(method), argument(argument), subchannel(subchannel),
              method_count(method_count) {}
    };

    /// Calls a GPU method.
    void CallMethod(const MethodCall& method_call);

    /// Returns a reference to the Maxwell3D GPU engine.
    Engines::Maxwell3D& Maxwell3D();

    /// Returns a const reference to the Maxwell3D GPU engine.
    const Engines::Maxwell3D& Maxwell3D() const;

    /// Returns a reference to the GPU memory manager.
    Tegra::MemoryManager& MemoryManager();

    /// Returns a const reference to the GPU memory manager.
    const Tegra::MemoryManager& MemoryManager() const;

    /// Returns a reference to the GPU DMA pusher.
    Tegra::DmaPusher& DmaPusher();

    /// Returns a const reference to the GPU DMA pusher.
    const Tegra::DmaPusher& DmaPusher() const;

private:
    std::unique_ptr<Tegra::DmaPusher> dma_pusher;
    std::unique_ptr<Tegra::MemoryManager> memory_manager;

    /// Mapping of command subchannels to their bound engine ids.
    std::array<EngineID, 8> bound_engines = {};

    /// 3D engine
    std::unique_ptr<Engines::Maxwell3D> maxwell_3d;
    /// 2D engine
    std::unique_ptr<Engines::Fermi2D> fermi_2d;
    /// Compute engine
    std::unique_ptr<Engines::MaxwellCompute> maxwell_compute;
    /// DMA engine
    std::unique_ptr<Engines::MaxwellDMA> maxwell_dma;
    /// Inline memory engine
    std::unique_ptr<Engines::KeplerMemory> kepler_memory;
};

} // namespace Tegra
