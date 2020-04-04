// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <utility>
#include <vector>
#include "common/common_types.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"

namespace Vulkan {

class VKDevice;
class VKFence;
class VKScheduler;
class VKStagingBufferPool;
class VKUpdateDescriptorQueue;

class VKComputePass {
public:
    explicit VKComputePass(const VKDevice& device, VKDescriptorPool& descriptor_pool,
                           const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
                           const std::vector<vk::DescriptorUpdateTemplateEntry>& templates,
                           const std::vector<vk::PushConstantRange> push_constants,
                           std::size_t code_size, const u8* code);
    ~VKComputePass();

protected:
    vk::DescriptorSet CommitDescriptorSet(VKUpdateDescriptorQueue& update_descriptor_queue,
                                          VKFence& fence);

    UniqueDescriptorUpdateTemplate descriptor_template;
    UniquePipelineLayout layout;
    UniquePipeline pipeline;

private:
    UniqueDescriptorSetLayout descriptor_set_layout;
    std::optional<DescriptorAllocator> descriptor_allocator;
    UniqueShaderModule module;
};

class QuadArrayPass final : public VKComputePass {
public:
    explicit QuadArrayPass(const VKDevice& device, VKScheduler& scheduler,
                           VKDescriptorPool& descriptor_pool,
                           VKStagingBufferPool& staging_buffer_pool,
                           VKUpdateDescriptorQueue& update_descriptor_queue);
    ~QuadArrayPass();

    std::pair<vk::Buffer, vk::DeviceSize> Assemble(u32 num_vertices, u32 first);

private:
    VKScheduler& scheduler;
    VKStagingBufferPool& staging_buffer_pool;
    VKUpdateDescriptorQueue& update_descriptor_queue;
};

class Uint8Pass final : public VKComputePass {
public:
    explicit Uint8Pass(const VKDevice& device, VKScheduler& scheduler,
                       VKDescriptorPool& descriptor_pool, VKStagingBufferPool& staging_buffer_pool,
                       VKUpdateDescriptorQueue& update_descriptor_queue);
    ~Uint8Pass();

    std::pair<vk::Buffer, u64> Assemble(u32 num_vertices, vk::Buffer src_buffer, u64 src_offset);

private:
    VKScheduler& scheduler;
    VKStagingBufferPool& staging_buffer_pool;
    VKUpdateDescriptorQueue& update_descriptor_queue;
};

} // namespace Vulkan
