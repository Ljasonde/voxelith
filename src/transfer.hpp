#pragma once
#include "gpu.hpp"
struct UploadBatch {
    Gpu &gpu;
    Lifetime pending;
    VkCommandBuffer cmd{};
    VkFence fence{};
    bool submitted = false;
    explicit UploadBatch(Gpu &g);
    Buffer upload(Lifetime &owner, const void *data, size_t bytes, VkBufferUsageFlags usage);
    VkImageView atlas(Lifetime &owner);
    void submit();
    bool complete() const;
    ~UploadBatch();
};
