#include "hiz.hpp"
#include <bit>
#include <algorithm>
void DepthHistory::init() {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 16;
    check(vkCreateSampler(g.device, &si, nullptr, &sampler));
    fixed.add([this] { vkDestroySampler(g.device, sampler, nullptr); });
    VkDescriptorSetLayoutBinding b[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = 2;
    ci.pBindings = b;
    check(vkCreateDescriptorSetLayout(g.device, &ci, nullptr, &setLayout));
    fixed.add([this] { vkDestroyDescriptorSetLayout(g.device, setLayout, nullptr); });
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &setLayout;
    check(vkCreatePipelineLayout(g.device, &lc, nullptr, &layout));
    fixed.add([this] { vkDestroyPipelineLayout(g.device, layout, nullptr); });
    Lifetime temp;
    VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pc.layout = layout;
    pc.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.stage.module = g.module(temp, "depth.comp");
    pc.stage.pName = "main";
    check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &pc, nullptr, &pipeline));
    fixed.add([this] { vkDestroyPipeline(g.device, pipeline, nullptr); });
}
void DepthHistory::resize(VkExtent2D source, std::array<Image, 2> depths) {
    resources.clear();
    extent = {std::bit_floor(source.width), std::bit_floor(source.height)};
    levels = std::bit_width(std::max(extent.width, extent.height));
    fresh = true;
    uint32_t sets = levels * 2;
    VkDescriptorPoolSize types[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets},
                                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets}};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = sets;
    pc.poolSizeCount = 2;
    pc.pPoolSizes = types;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(g.device, &pc, nullptr, &pool));
    resources.add([this, pool] { vkDestroyDescriptorPool(g.device, pool, nullptr); });
    for (int slot = 0; slot < 2; slot++) {
        auto &p = pyramids[slot];
        p.built = false;
        p.mips.resize(levels);
        p.reduce.resize(levels);
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R32_SFLOAT;
        ci.extent = {extent.width, extent.height, 1};
        ci.mipLevels = levels;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocation allocation;
        VmaAllocationCreateInfo ac{};
        ac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        check(vmaCreateImage(g.allocator, &ci, &ac, &p.image, &allocation, nullptr));
        auto img = p.image;
        resources.add([this, img, allocation] { vmaDestroyImage(g.allocator, img, allocation); });
        auto view = [&](uint32_t start, uint32_t n) {
            VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            v.image = img;
            v.viewType = VK_IMAGE_VIEW_TYPE_2D;
            v.format = ci.format;
            v.subresourceRange = {1, start, n, 0, 1};
            VkImageView result;
            check(vkCreateImageView(g.device, &v, nullptr, &result));
            resources.add([this, result] { vkDestroyImageView(g.device, result, nullptr); });
            return result;
        };
        p.all = view(0, levels);
        for (int i = 0; i < levels; i++) {
            p.mips[i] = view(i, 1);
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &setLayout;
            check(vkAllocateDescriptorSets(g.device, &ai, &p.reduce[i]));
            VkDescriptorImageInfo imgs[] = {
                {sampler, i ? p.mips[i - 1] : depths[slot].view,
                 i ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {VK_NULL_HANDLE, p.mips[i], VK_IMAGE_LAYOUT_GENERAL}};
            for (int j = 0; j < 2; j++) {
                VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                d.dstSet = p.reduce[i];
                d.dstBinding = j;
                d.descriptorCount = 1;
                d.descriptorType =
                    j ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                d.pImageInfo = &imgs[j];
                vkUpdateDescriptorSets(g.device, 1, &d, 0, nullptr);
            }
        }
    }
}
void DepthHistory::prepare(VkCommandBuffer cmd) {
    if (!fresh)
        return;
    for (auto &p : pyramids) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.image = p.image;
        b.subresourceRange = {1, 0, uint32_t(levels), 0, 1};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &di);
    }
    fresh = false;
}
void DepthHistory::record(Frame &f, int slot, glm::mat4 vp) {
    auto &p = pyramids[slot];
    auto cmd = f.cmd;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &b;
    for (int i = 0; i < levels; i++) {
        vkCmdPipelineBarrier2(cmd, &di);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &p.reduce[i], 0, nullptr);
        vkCmdDispatch(cmd, (std::max(1u, extent.width >> i) + 7) / 8,
                      (std::max(1u, extent.height >> i) + 7) / 8, 1);
    }
    vkCmdPipelineBarrier2(cmd, &di);
    p.built = true;
    p.vp = vp;
}
