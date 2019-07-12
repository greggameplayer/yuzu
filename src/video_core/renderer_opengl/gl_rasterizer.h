// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <boost/icl/interval_map.hpp>
#include <glad/glad.h>

#include "common/common_types.h"
#include "video_core/engines/const_buffer_info.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_opengl/gl_buffer_cache.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_framebuffer_cache.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_sampler_cache.h"
#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/renderer_opengl/gl_shader_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_cache.h"
#include "video_core/renderer_opengl/utils.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {
class MemoryManager;
}

namespace OpenGL {

struct ScreenInfo;
struct DrawParameters;

class RasterizerOpenGL : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerOpenGL(Core::System& system, Core::Frontend::EmuWindow& emu_window,
                              ScreenInfo& info);
    ~RasterizerOpenGL() override;

    void DrawArrays() override;
    void Clear() override;
    void FlushAll() override;
    void FlushRegion(CacheAddr addr, u64 size) override;
    void InvalidateRegion(CacheAddr addr, u64 size) override;
    void FlushAndInvalidateRegion(CacheAddr addr, u64 size) override;
    void TickFrame() override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Regs::Surface& src,
                               const Tegra::Engines::Fermi2D::Regs::Surface& dst,
                               const Tegra::Engines::Fermi2D::Config& copy_config) override;
    bool AccelerateDisplay(const Tegra::FramebufferConfig& config, VAddr framebuffer_addr,
                           u32 pixel_stride) override;
    bool AccelerateDrawBatch(bool is_indexed) override;
    void UpdatePagesCachedCount(VAddr addr, u64 size, int delta) override;
    void LoadDiskResources(const std::atomic_bool& stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;

private:
    struct FramebufferConfigState {
        bool using_color_fb{};
        bool using_depth_fb{};
        bool preserve_contents{};
        std::optional<std::size_t> single_color_target;

        bool operator==(const FramebufferConfigState& rhs) const {
            return std::tie(using_color_fb, using_depth_fb, preserve_contents,
                            single_color_target) == std::tie(rhs.using_color_fb, rhs.using_depth_fb,
                                                             rhs.preserve_contents,
                                                             rhs.single_color_target);
        }
        bool operator!=(const FramebufferConfigState& rhs) const {
            return !operator==(rhs);
        }
    };

    /**
     * Configures the color and depth framebuffer states.
     *
     * @param current_state       The current OpenGL state.
     * @param using_color_fb      If true, configure color framebuffers.
     * @param using_depth_fb      If true, configure the depth/stencil framebuffer.
     * @param preserve_contents   If true, tries to preserve data from a previously used
     *                            framebuffer.
     * @param single_color_target Specifies if a single color buffer target should be used.
     *
     * @returns If depth (first) or stencil (second) are being stored in the bound zeta texture
     *          (requires using_depth_fb to be true)
     */
    std::pair<bool, bool> ConfigureFramebuffers(
        OpenGLState& current_state, bool using_color_fb = true, bool using_depth_fb = true,
        bool preserve_contents = true, std::optional<std::size_t> single_color_target = {});

    /// Configures the current constbuffers to use for the draw command.
    void SetupDrawConstBuffers(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                               const Shader& shader);

    /// Configures a constant buffer.
    void SetupConstBuffer(const Tegra::Engines::ConstBufferInfo& buffer,
                          const GLShader::ConstBufferEntry& entry);

    /// Configures the current global memory entries to use for the draw command.
    void SetupGlobalRegions(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                            const Shader& shader);

    /// Configures the current textures to use for the draw command. Returns shaders texture buffer
    /// usage.
    TextureBufferUsage SetupTextures(Tegra::Engines::Maxwell3D::Regs::ShaderStage stage,
                                     const Shader& shader, BaseBindings base_bindings);

    /// Syncs the viewport and depth range to match the guest state
    void SyncViewport(OpenGLState& current_state);

    /// Syncs the clip enabled status to match the guest state
    void SyncClipEnabled(
        const std::array<bool, Tegra::Engines::Maxwell3D::Regs::NumClipDistances>& clip_mask);

    /// Syncs the clip coefficients to match the guest state
    void SyncClipCoef();

    /// Syncs the cull mode to match the guest state
    void SyncCullMode();

    /// Syncs the primitve restart to match the guest state
    void SyncPrimitiveRestart();

    /// Syncs the depth test state to match the guest state
    void SyncDepthTestState();

    /// Syncs the stencil test state to match the guest state
    void SyncStencilTestState();

    /// Syncs the blend state to match the guest state
    void SyncBlendState();

    /// Syncs the LogicOp state to match the guest state
    void SyncLogicOpState();

    /// Syncs the the color clamp state
    void SyncFragmentColorClampState();

    /// Syncs the alpha coverage and alpha to one
    void SyncMultiSampleState();

    /// Syncs the scissor test state to match the guest state
    void SyncScissorTest(OpenGLState& current_state);

    /// Syncs the transform feedback state to match the guest state
    void SyncTransformFeedback();

    /// Syncs the point state to match the guest state
    void SyncPointState();

    /// Syncs Color Mask
    void SyncColorMask();

    /// Syncs the polygon offsets
    void SyncPolygonOffset();

    /// Syncs the alpha test state to match the guest state
    void SyncAlphaTest();

    /// Check for extension that are not strictly required
    /// but are needed for correct emulation
    void CheckExtensions();

    const Device device;
    OpenGLState state;

    TextureCacheOpenGL texture_cache;
    ShaderCacheOpenGL shader_cache;
    SamplerCacheOpenGL sampler_cache;
    FramebufferCacheOpenGL framebuffer_cache;

    Core::System& system;
    ScreenInfo& screen_info;

    std::unique_ptr<GLShader::ProgramManager> shader_program_manager;
    std::map<std::array<Tegra::Engines::Maxwell3D::Regs::VertexAttribute,
                        Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes>,
             OGLVertexArray>
        vertex_array_cache;

    FramebufferConfigState current_framebuffer_config_state;
    std::pair<bool, bool> current_depth_stencil_usage{};

    static constexpr std::size_t STREAM_BUFFER_SIZE = 128 * 1024 * 1024;
    OGLBufferCache buffer_cache;

    VertexArrayPushBuffer vertex_array_pushbuffer;
    BindBuffersRangePushBuffer bind_ubo_pushbuffer{GL_UNIFORM_BUFFER};
    BindBuffersRangePushBuffer bind_ssbo_pushbuffer{GL_SHADER_STORAGE_BUFFER};

    std::size_t CalculateVertexArraysSize() const;

    std::size_t CalculateIndexBufferSize() const;

    /// Updates and returns a vertex array object representing current vertex format
    GLuint SetupVertexFormat();

    void SetupVertexBuffer(GLuint vao);

    GLintptr SetupIndexBuffer();

    DrawParameters SetupDraw(GLintptr index_buffer_offset);

    void SetupShaders(GLenum primitive_mode);

    enum class AccelDraw { Disabled, Arrays, Indexed };
    AccelDraw accelerate_draw = AccelDraw::Disabled;

    using CachedPageMap = boost::icl::interval_map<u64, int>;
    CachedPageMap cached_pages;
};

} // namespace OpenGL
