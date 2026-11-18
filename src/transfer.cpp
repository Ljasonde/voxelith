#include "transfer.hpp"
#include <cstring>
UploadBatch::UploadBatch(Gpu &g) : gpu(g) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = g.pool;
    ai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(g.device, &ai, &cmd));
    pending.add([this] { vkFreeCommandBuffers(gpu.device, gpu.pool, 1, &cmd); });
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &bi));
    VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(g.device, &fc, nullptr, &fence));
    pending.add([this] { vkDestroyFence(gpu.device, fence, nullptr); });
}
Buffer UploadBatch::upload(Lifetime &owner, const void *data, size_t bytes, VkBufferUsageFlags usage) {
    auto staging = gpu.buffer(pending, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    memcpy(staging.mapped, data, bytes);
    check(vmaFlushAllocation(gpu.allocator, staging.allocation, 0, VK_WHOLE_SIZE));
    auto target = gpu.buffer(owner, bytes, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(cmd, staging.handle, target.handle, 1, &copy);
    return target;
}
VkImageView UploadBatch::atlas(Lifetime &owner) {
    constexpr int layers = 22;
    std::array<uint8_t, 16 * 16 * layers> pixels{};
    for (uint32_t i = 0; i < pixels.size(); i++) {
        uint32_t n = (i % 16) * 1597334677u ^ ((i / 16) % 16) * 3812015801u ^ (i / 256) * 2798796415u;
        n = (n ^ (n >> 16)) * 2246822519u;
        pixels[i] = uint8_t((n ^ (n >> 13)) >> 24);
    }
    auto staging = gpu.buffer(pending, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    memcpy(staging.mapped, pixels.data(), pixels.size());
    check(vmaFlushAllocation(gpu.allocator, staging.allocation, 0, VK_WHOLE_SIZE));
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8_UNORM;
    ci.extent = {16, 16, 1};
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.arrayLayers = layers;
    ci.mipLevels = 1;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage img;
    VmaAllocation alloc;
    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateImage(gpu.allocator, &ci, &ac, &img, &alloc, nullptr));
    auto allocator = gpu.allocator;
    owner.add([=] { vmaDestroyImage(allocator, img, alloc); });
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.image = img;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange = {1, 0, 1, 0, layers};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
    VkBufferImageCopy region{};
    region.imageSubresource = {1, 0, 0, layers};
    region.imageExtent = {16, 16, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.srcStageMask = b.dstStageMask;
    b.srcAccessMask = b.dstAccessMask;
    b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    b.oldLayout = b.newLayout;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cmd, &di);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = ci.format;
    vi.image = img;
    vi.subresourceRange = b.subresourceRange;
    VkImageView view;
    check(vkCreateImageView(gpu.device, &vi, nullptr, &view));
    auto device = gpu.device;
    owner.add([=] { vkDestroyImageView(device, view, nullptr); });
    return view;
}
void UploadBatch::submit() {
    VkMemoryBarrier2 m{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    m.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    m.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    m.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    m.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &m;
    vkCmdPipelineBarrier2(cmd, &di);
    check(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    check(vkQueueSubmit(gpu.queue, 1, &si, fence));
    submitted = true;
}
bool UploadBatch::complete() const { return submitted && vkGetFenceStatus(gpu.device, fence) == VK_SUCCESS; }
UploadBatch::~UploadBatch() {
    if (submitted)
        vkWaitForFences(gpu.device, 1, &fence, VK_TRUE, UINT64_MAX);
    pending.clear();
}
