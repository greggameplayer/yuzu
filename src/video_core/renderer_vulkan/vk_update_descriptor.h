// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <type_traits>
#include <variant>
#include <boost/container/static_vector.hpp>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/declarations.h"

namespace Vulkan {

class VKDevice;
class VKScheduler;

class DescriptorUpdateEntry {
public:
    explicit DescriptorUpdateEntry() : image{} {}

    DescriptorUpdateEntry(vk::DescriptorImageInfo image) : image{image} {}

    DescriptorUpdateEntry(vk::DescriptorBufferInfo buffer) : buffer{buffer} {}

    DescriptorUpdateEntry(vk::BufferView texel_buffer) : texel_buffer{texel_buffer} {}

private:
    union {
        vk::DescriptorImageInfo image;
        vk::DescriptorBufferInfo buffer;
        vk::BufferView texel_buffer;
    };
};

class VKUpdateDescriptorQueue final {
public:
    explicit VKUpdateDescriptorQueue(const VKDevice& device, VKScheduler& scheduler);
    ~VKUpdateDescriptorQueue();

    void TickFrame();

    void Acquire();

    void Send(vk::DescriptorUpdateTemplate update_template, vk::DescriptorSet set);

    void AddSampledImage(vk::Sampler sampler, vk::ImageView image_view) {
        entries.emplace_back(vk::DescriptorImageInfo{sampler, image_view, {}});
    }

    void AddImage(vk::ImageView image_view) {
        entries.emplace_back(vk::DescriptorImageInfo{{}, image_view, {}});
    }

    void AddBuffer(vk::Buffer buffer, u64 offset, std::size_t size) {
        entries.push_back(vk::DescriptorBufferInfo{buffer, offset, size});
    }

    void AddTexelBuffer(vk::BufferView texel_buffer) {
        entries.emplace_back(texel_buffer);
    }

    vk::ImageLayout* GetLastImageLayout() {
        return &std::get<vk::DescriptorImageInfo>(entries.back()).imageLayout;
    }

private:
    using Variant = std::variant<vk::DescriptorImageInfo, vk::DescriptorBufferInfo, vk::BufferView>;

    const VKDevice& device;
    VKScheduler& scheduler;

    boost::container::static_vector<Variant, 0x400> entries;
    boost::container::static_vector<DescriptorUpdateEntry, 0x10000> payload;
};

} // namespace Vulkan
