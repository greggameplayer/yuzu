// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <cstring>
#include "common/assert.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/texture.h"

namespace Tegra::Engines {

/// First register id that is actually a Macro call.
constexpr u32 MacroRegistersStart = 0xE00;

Maxwell3D::Maxwell3D(Core::System& system, VideoCore::RasterizerInterface& rasterizer,
                     MemoryManager& memory_manager)
    : system{system}, rasterizer{rasterizer}, memory_manager{memory_manager},
      macro_interpreter{*this}, upload_state{memory_manager, regs.upload} {
    InitializeRegisterDefaults();
}

void Maxwell3D::InitializeRegisterDefaults() {
    // Initializes registers to their default values - what games expect them to be at boot. This is
    // for certain registers that may not be explicitly set by games.

    // Reset all registers to zero
    std::memset(&regs, 0, sizeof(regs));

    // Depth range near/far is not always set, but is expected to be the default 0.0f, 1.0f. This is
    // needed for ARMS.
    for (auto& viewport : regs.viewports) {
        viewport.depth_range_near = 0.0f;
        viewport.depth_range_far = 1.0f;
    }

    // Doom and Bomberman seems to use the uninitialized registers and just enable blend
    // so initialize blend registers with sane values
    regs.blend.equation_rgb = Regs::Blend::Equation::Add;
    regs.blend.factor_source_rgb = Regs::Blend::Factor::One;
    regs.blend.factor_dest_rgb = Regs::Blend::Factor::Zero;
    regs.blend.equation_a = Regs::Blend::Equation::Add;
    regs.blend.factor_source_a = Regs::Blend::Factor::One;
    regs.blend.factor_dest_a = Regs::Blend::Factor::Zero;
    for (auto& blend : regs.independent_blend) {
        blend.equation_rgb = Regs::Blend::Equation::Add;
        blend.factor_source_rgb = Regs::Blend::Factor::One;
        blend.factor_dest_rgb = Regs::Blend::Factor::Zero;
        blend.equation_a = Regs::Blend::Equation::Add;
        blend.factor_source_a = Regs::Blend::Factor::One;
        blend.factor_dest_a = Regs::Blend::Factor::Zero;
    }
    regs.stencil_front_op_fail = Regs::StencilOp::Keep;
    regs.stencil_front_op_zfail = Regs::StencilOp::Keep;
    regs.stencil_front_op_zpass = Regs::StencilOp::Keep;
    regs.stencil_front_func_func = Regs::ComparisonOp::Always;
    regs.stencil_front_func_mask = 0xFFFFFFFF;
    regs.stencil_front_mask = 0xFFFFFFFF;
    regs.stencil_two_side_enable = 1;
    regs.stencil_back_op_fail = Regs::StencilOp::Keep;
    regs.stencil_back_op_zfail = Regs::StencilOp::Keep;
    regs.stencil_back_op_zpass = Regs::StencilOp::Keep;
    regs.stencil_back_func_func = Regs::ComparisonOp::Always;
    regs.stencil_back_func_mask = 0xFFFFFFFF;
    regs.stencil_back_mask = 0xFFFFFFFF;

    // TODO(Rodrigo): Most games do not set a point size. I think this is a case of a
    // register carrying a default value. Assume it's OpenGL's default (1).
    regs.point_size = 1.0f;

    // TODO(bunnei): Some games do not initialize the color masks (e.g. Sonic Mania). Assuming a
    // default of enabled fixes rendering here.
    for (auto& color_mask : regs.color_mask) {
        color_mask.R.Assign(1);
        color_mask.G.Assign(1);
        color_mask.B.Assign(1);
        color_mask.A.Assign(1);
    }

    // Commercial games seem to assume this value is enabled and nouveau sets this value manually.
    regs.rt_separate_frag_data = 1;
}

void Maxwell3D::CallMacroMethod(u32 method, std::vector<u32> parameters) {
    // Reset the current macro.
    executing_macro = 0;

    // Lookup the macro offset
    const u32 entry{(method - MacroRegistersStart) >> 1};
    const auto& search{macro_offsets.find(entry)};
    if (search == macro_offsets.end()) {
        LOG_CRITICAL(HW_GPU, "macro not found for method 0x{:X}!", method);
        UNREACHABLE();
        return;
    }

    // Execute the current macro.
    macro_interpreter.Execute(search->second, std::move(parameters));
}

void Maxwell3D::CallMethod(const GPU::MethodCall& method_call) {
    auto debug_context = system.GetGPUDebugContext();

    const u32 method = method_call.method;

    // It is an error to write to a register other than the current macro's ARG register before it
    // has finished execution.
    if (executing_macro != 0) {
        ASSERT(method == executing_macro + 1);
    }

    // Methods after 0xE00 are special, they're actually triggers for some microcode that was
    // uploaded to the GPU during initialization.
    if (method >= MacroRegistersStart) {
        // We're trying to execute a macro
        if (executing_macro == 0) {
            // A macro call must begin by writing the macro method's register, not its argument.
            ASSERT_MSG((method % 2) == 0,
                       "Can't start macro execution by writing to the ARGS register");
            executing_macro = method;
        }

        macro_params.push_back(method_call.argument);

        // Call the macro when there are no more parameters in the command buffer
        if (method_call.IsLastCall()) {
            CallMacroMethod(executing_macro, std::move(macro_params));
        }
        return;
    }

    ASSERT_MSG(method < Regs::NUM_REGS,
               "Invalid Maxwell3D register, increase the size of the Regs structure");

    if (debug_context) {
        debug_context->OnEvent(Tegra::DebugContext::Event::MaxwellCommandLoaded, nullptr);
    }

    if (regs.reg_array[method] != method_call.argument) {
        regs.reg_array[method] = method_call.argument;
        // Color buffers
        constexpr u32 first_rt_reg = MAXWELL3D_REG_INDEX(rt);
        constexpr u32 registers_per_rt = sizeof(regs.rt[0]) / sizeof(u32);
        if (method >= first_rt_reg &&
            method < first_rt_reg + registers_per_rt * Regs::NumRenderTargets) {
            const std::size_t rt_index = (method - first_rt_reg) / registers_per_rt;
            dirty_flags.color_buffer.set(rt_index);
        }

        // Zeta buffer
        constexpr u32 registers_in_zeta = sizeof(regs.zeta) / sizeof(u32);
        if (method == MAXWELL3D_REG_INDEX(zeta_enable) ||
            method == MAXWELL3D_REG_INDEX(zeta_width) ||
            method == MAXWELL3D_REG_INDEX(zeta_height) ||
            (method >= MAXWELL3D_REG_INDEX(zeta) &&
             method < MAXWELL3D_REG_INDEX(zeta) + registers_in_zeta)) {
            dirty_flags.zeta_buffer = true;
        }

        // Shader
        constexpr u32 shader_registers_count =
            sizeof(regs.shader_config[0]) * Regs::MaxShaderProgram / sizeof(u32);
        if (method >= MAXWELL3D_REG_INDEX(shader_config[0]) &&
            method < MAXWELL3D_REG_INDEX(shader_config[0]) + shader_registers_count) {
            dirty_flags.shaders = true;
        }

        // Vertex format
        if (method >= MAXWELL3D_REG_INDEX(vertex_attrib_format) &&
            method < MAXWELL3D_REG_INDEX(vertex_attrib_format) + regs.vertex_attrib_format.size()) {
            dirty_flags.vertex_attrib_format = true;
        }

        // Vertex buffer
        if (method >= MAXWELL3D_REG_INDEX(vertex_array) &&
            method < MAXWELL3D_REG_INDEX(vertex_array) + 4 * Regs::NumVertexArrays) {
            dirty_flags.vertex_array.set((method - MAXWELL3D_REG_INDEX(vertex_array)) >> 2);
        } else if (method >= MAXWELL3D_REG_INDEX(vertex_array_limit) &&
                   method < MAXWELL3D_REG_INDEX(vertex_array_limit) + 2 * Regs::NumVertexArrays) {
            dirty_flags.vertex_array.set((method - MAXWELL3D_REG_INDEX(vertex_array_limit)) >> 1);
        } else if (method >= MAXWELL3D_REG_INDEX(instanced_arrays) &&
                   method < MAXWELL3D_REG_INDEX(instanced_arrays) + Regs::NumVertexArrays) {
            dirty_flags.vertex_array.set(method - MAXWELL3D_REG_INDEX(instanced_arrays));
        }
    }

    switch (method) {
    case MAXWELL3D_REG_INDEX(macros.data): {
        ProcessMacroUpload(method_call.argument);
        break;
    }
    case MAXWELL3D_REG_INDEX(macros.bind): {
        ProcessMacroBind(method_call.argument);
        break;
    }
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[0]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[1]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[2]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[3]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[4]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[5]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[6]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[7]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[8]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[9]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[10]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[11]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[12]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[13]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[14]):
    case MAXWELL3D_REG_INDEX(const_buffer.cb_data[15]): {
        ProcessCBData(method_call.argument);
        break;
    }
    case MAXWELL3D_REG_INDEX(cb_bind[0].raw_config): {
        ProcessCBBind(Regs::ShaderStage::Vertex);
        break;
    }
    case MAXWELL3D_REG_INDEX(cb_bind[1].raw_config): {
        ProcessCBBind(Regs::ShaderStage::TesselationControl);
        break;
    }
    case MAXWELL3D_REG_INDEX(cb_bind[2].raw_config): {
        ProcessCBBind(Regs::ShaderStage::TesselationEval);
        break;
    }
    case MAXWELL3D_REG_INDEX(cb_bind[3].raw_config): {
        ProcessCBBind(Regs::ShaderStage::Geometry);
        break;
    }
    case MAXWELL3D_REG_INDEX(cb_bind[4].raw_config): {
        ProcessCBBind(Regs::ShaderStage::Fragment);
        break;
    }
    case MAXWELL3D_REG_INDEX(draw.vertex_end_gl): {
        DrawArrays();
        break;
    }
    case MAXWELL3D_REG_INDEX(clear_buffers): {
        ProcessClearBuffers();
        break;
    }
    case MAXWELL3D_REG_INDEX(query.query_get): {
        ProcessQueryGet();
        break;
    }
    case MAXWELL3D_REG_INDEX(sync_info): {
        ProcessSyncPoint();
        break;
    }
    case MAXWELL3D_REG_INDEX(exec_upload): {
        upload_state.ProcessExec(regs.exec_upload.linear != 0);
        break;
    }
    case MAXWELL3D_REG_INDEX(data_upload): {
        const bool is_last_call = method_call.IsLastCall();
        upload_state.ProcessData(method_call.argument, is_last_call);
        if (is_last_call) {
            dirty_flags.OnMemoryWrite();
        }
        break;
    }
    default:
        break;
    }

    if (debug_context) {
        debug_context->OnEvent(Tegra::DebugContext::Event::MaxwellCommandProcessed, nullptr);
    }
}

void Maxwell3D::ProcessMacroUpload(u32 data) {
    ASSERT_MSG(regs.macros.upload_address < macro_memory.size(),
               "upload_address exceeded macro_memory size!");
    macro_memory[regs.macros.upload_address++] = data;
}

void Maxwell3D::ProcessMacroBind(u32 data) {
    macro_offsets[regs.macros.entry] = data;
}

void Maxwell3D::ProcessQueryGet() {
    const GPUVAddr sequence_address{regs.query.QueryAddress()};
    // Since the sequence address is given as a GPU VAddr, we have to convert it to an application
    // VAddr before writing.

    // TODO(Subv): Support the other query units.
    ASSERT_MSG(regs.query.query_get.unit == Regs::QueryUnit::Crop,
               "Units other than CROP are unimplemented");

    u64 result = 0;

    // TODO(Subv): Support the other query variables
    switch (regs.query.query_get.select) {
    case Regs::QuerySelect::Zero:
        // This seems to actually write the query sequence to the query address.
        result = regs.query.query_sequence;
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented query select type {}",
                          static_cast<u32>(regs.query.query_get.select.Value()));
    }

    // TODO(Subv): Research and implement how query sync conditions work.

    struct LongQueryResult {
        u64_le value;
        u64_le timestamp;
    };
    static_assert(sizeof(LongQueryResult) == 16, "LongQueryResult has wrong size");

    switch (regs.query.query_get.mode) {
    case Regs::QueryMode::Write:
    case Regs::QueryMode::Write2: {
        u32 sequence = regs.query.query_sequence;
        if (regs.query.query_get.short_query) {
            // Write the current query sequence to the sequence address.
            // TODO(Subv): Find out what happens if you use a long query type but mark it as a short
            // query.
            memory_manager.Write<u32>(sequence_address, sequence);
        } else {
            // Write the 128-bit result structure in long mode. Note: We emulate an infinitely fast
            // GPU, this command may actually take a while to complete in real hardware due to GPU
            // wait queues.
            LongQueryResult query_result{};
            query_result.value = result;
            // TODO(Subv): Generate a real GPU timestamp and write it here instead of CoreTiming
            query_result.timestamp = system.CoreTiming().GetTicks();
            memory_manager.WriteBlock(sequence_address, &query_result, sizeof(query_result));
        }
        dirty_flags.OnMemoryWrite();
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Query mode {} not implemented",
                          static_cast<u32>(regs.query.query_get.mode.Value()));
    }
}

void Maxwell3D::ProcessSyncPoint() {
    const u32 sync_point = regs.sync_info.sync_point.Value();
    const u32 increment = regs.sync_info.increment.Value();
    const u32 cache_flush = regs.sync_info.unknown.Value();
    if (increment) {
        system.GPU().IncrementSyncPoint(sync_point);
    }
}

void Maxwell3D::DrawArrays() {
    LOG_DEBUG(HW_GPU, "called, topology={}, count={}", static_cast<u32>(regs.draw.topology.Value()),
              regs.vertex_buffer.count);
    ASSERT_MSG(!(regs.index_array.count && regs.vertex_buffer.count), "Both indexed and direct?");

    auto debug_context = system.GetGPUDebugContext();

    if (debug_context) {
        debug_context->OnEvent(Tegra::DebugContext::Event::IncomingPrimitiveBatch, nullptr);
    }

    // Both instance configuration registers can not be set at the same time.
    ASSERT_MSG(!regs.draw.instance_next || !regs.draw.instance_cont,
               "Illegal combination of instancing parameters");

    if (regs.draw.instance_next) {
        // Increment the current instance *before* drawing.
        state.current_instance += 1;
    } else if (!regs.draw.instance_cont) {
        // Reset the current instance to 0.
        state.current_instance = 0;
    }

    const bool is_indexed{regs.index_array.count && !regs.vertex_buffer.count};
    rasterizer.AccelerateDrawBatch(is_indexed);

    if (debug_context) {
        debug_context->OnEvent(Tegra::DebugContext::Event::FinishedPrimitiveBatch, nullptr);
    }

    // TODO(bunnei): Below, we reset vertex count so that we can use these registers to determine if
    // the game is trying to draw indexed or direct mode. This needs to be verified on HW still -
    // it's possible that it is incorrect and that there is some other register used to specify the
    // drawing mode.
    if (is_indexed) {
        regs.index_array.count = 0;
    } else {
        regs.vertex_buffer.count = 0;
    }
}

void Maxwell3D::ProcessCBBind(Regs::ShaderStage stage) {
    // Bind the buffer currently in CB_ADDRESS to the specified index in the desired shader stage.
    auto& shader = state.shader_stages[static_cast<std::size_t>(stage)];
    auto& bind_data = regs.cb_bind[static_cast<std::size_t>(stage)];

    ASSERT(bind_data.index < Regs::MaxConstBuffers);
    auto& buffer = shader.const_buffers[bind_data.index];

    buffer.enabled = bind_data.valid.Value() != 0;
    buffer.address = regs.const_buffer.BufferAddress();
    buffer.size = regs.const_buffer.cb_size;
}

void Maxwell3D::ProcessCBData(u32 value) {
    // Write the input value to the current const buffer at the current position.
    const GPUVAddr buffer_address = regs.const_buffer.BufferAddress();
    ASSERT(buffer_address != 0);

    // Don't allow writing past the end of the buffer.
    ASSERT(regs.const_buffer.cb_pos + sizeof(u32) <= regs.const_buffer.cb_size);

    const GPUVAddr address{buffer_address + regs.const_buffer.cb_pos};

    u8* ptr{memory_manager.GetPointer(address)};
    rasterizer.InvalidateRegion(ToCacheAddr(ptr), sizeof(u32));
    memory_manager.Write<u32>(address, value);

    dirty_flags.OnMemoryWrite();

    // Increment the current buffer position.
    regs.const_buffer.cb_pos = regs.const_buffer.cb_pos + 4;
}

Texture::TICEntry Maxwell3D::GetTICEntry(u32 tic_index) const {
    const GPUVAddr tic_address_gpu{regs.tic.TICAddress() + tic_index * sizeof(Texture::TICEntry)};

    Texture::TICEntry tic_entry;
    memory_manager.ReadBlockUnsafe(tic_address_gpu, &tic_entry, sizeof(Texture::TICEntry));

    const auto r_type{tic_entry.r_type.Value()};
    const auto g_type{tic_entry.g_type.Value()};
    const auto b_type{tic_entry.b_type.Value()};
    const auto a_type{tic_entry.a_type.Value()};

    // TODO(Subv): Different data types for separate components are not supported
    DEBUG_ASSERT(r_type == g_type && r_type == b_type && r_type == a_type);

    return tic_entry;
}

Texture::TSCEntry Maxwell3D::GetTSCEntry(u32 tsc_index) const {
    const GPUVAddr tsc_address_gpu{regs.tsc.TSCAddress() + tsc_index * sizeof(Texture::TSCEntry)};

    Texture::TSCEntry tsc_entry;
    memory_manager.ReadBlockUnsafe(tsc_address_gpu, &tsc_entry, sizeof(Texture::TSCEntry));
    return tsc_entry;
}

std::vector<Texture::FullTextureInfo> Maxwell3D::GetStageTextures(Regs::ShaderStage stage) const {
    std::vector<Texture::FullTextureInfo> textures;

    auto& fragment_shader = state.shader_stages[static_cast<std::size_t>(stage)];
    auto& tex_info_buffer = fragment_shader.const_buffers[regs.tex_cb_index];
    ASSERT(tex_info_buffer.enabled && tex_info_buffer.address != 0);

    GPUVAddr tex_info_buffer_end = tex_info_buffer.address + tex_info_buffer.size;

    // Offset into the texture constbuffer where the texture info begins.
    static constexpr std::size_t TextureInfoOffset = 0x20;

    for (GPUVAddr current_texture = tex_info_buffer.address + TextureInfoOffset;
         current_texture < tex_info_buffer_end; current_texture += sizeof(Texture::TextureHandle)) {

        const Texture::TextureHandle tex_handle{memory_manager.Read<u32>(current_texture)};

        Texture::FullTextureInfo tex_info{};
        // TODO(Subv): Use the shader to determine which textures are actually accessed.
        tex_info.index =
            static_cast<u32>(current_texture - tex_info_buffer.address - TextureInfoOffset) /
            sizeof(Texture::TextureHandle);

        // Load the TIC data.
        auto tic_entry = GetTICEntry(tex_handle.tic_id);
        // TODO(Subv): Workaround for BitField's move constructor being deleted.
        std::memcpy(&tex_info.tic, &tic_entry, sizeof(tic_entry));

        // Load the TSC data
        auto tsc_entry = GetTSCEntry(tex_handle.tsc_id);
        // TODO(Subv): Workaround for BitField's move constructor being deleted.
        std::memcpy(&tex_info.tsc, &tsc_entry, sizeof(tsc_entry));

        textures.push_back(tex_info);
    }

    return textures;
}

Texture::FullTextureInfo Maxwell3D::GetTextureInfo(const Texture::TextureHandle tex_handle,
                                                   std::size_t offset) const {
    Texture::FullTextureInfo tex_info{};
    tex_info.index = static_cast<u32>(offset);

    // Load the TIC data.
    auto tic_entry = GetTICEntry(tex_handle.tic_id);
    // TODO(Subv): Workaround for BitField's move constructor being deleted.
    std::memcpy(&tex_info.tic, &tic_entry, sizeof(tic_entry));

    // Load the TSC data
    auto tsc_entry = GetTSCEntry(tex_handle.tsc_id);
    // TODO(Subv): Workaround for BitField's move constructor being deleted.
    std::memcpy(&tex_info.tsc, &tsc_entry, sizeof(tsc_entry));

    return tex_info;
}

Texture::FullTextureInfo Maxwell3D::GetStageTexture(Regs::ShaderStage stage,
                                                    std::size_t offset) const {
    const auto& shader = state.shader_stages[static_cast<std::size_t>(stage)];
    const auto& tex_info_buffer = shader.const_buffers[regs.tex_cb_index];
    ASSERT(tex_info_buffer.enabled && tex_info_buffer.address != 0);

    const GPUVAddr tex_info_address =
        tex_info_buffer.address + offset * sizeof(Texture::TextureHandle);

    ASSERT(tex_info_address < tex_info_buffer.address + tex_info_buffer.size);

    const Texture::TextureHandle tex_handle{memory_manager.Read<u32>(tex_info_address)};

    return GetTextureInfo(tex_handle, offset);
}

u32 Maxwell3D::GetRegisterValue(u32 method) const {
    ASSERT_MSG(method < Regs::NUM_REGS, "Invalid Maxwell3D register");
    return regs.reg_array[method];
}

void Maxwell3D::ProcessClearBuffers() {
    ASSERT(regs.clear_buffers.R == regs.clear_buffers.G &&
           regs.clear_buffers.R == regs.clear_buffers.B &&
           regs.clear_buffers.R == regs.clear_buffers.A);

    rasterizer.Clear();
}

u32 Maxwell3D::AccessConstBuffer32(Regs::ShaderStage stage, u64 const_buffer, u64 offset) const {
    const auto& shader_stage = state.shader_stages[static_cast<std::size_t>(stage)];
    const auto& buffer = shader_stage.const_buffers[const_buffer];
    u32 result;
    std::memcpy(&result, memory_manager.GetPointer(buffer.address + offset), sizeof(u32));
    return result;
}

} // namespace Tegra::Engines
