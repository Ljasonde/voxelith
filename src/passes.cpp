#include "scene.hpp"
#include <cstring>
#include <algorithm>
namespace {
constexpr auto fragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               colStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr auto zStage =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
void attach(VkCommandBuffer cmd, Image img, bool depth) {
    transition(cmd, img.handle, depth ? 2 : 1, VK_IMAGE_LAYOUT_UNDEFINED,
               depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
               VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, depth ? zStage : colStage,
               depth ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                     : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}
void sample(VkCommandBuffer cmd, Image img, bool depth) {
    transition(cmd, img.handle, depth ? 2 : 1,
               depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, depth ? zStage : colStage,
               depth ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                     : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
VkRenderingAttachmentInfo attachment(Image img, bool z) {
    VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    a.imageView = img.view;
    a.imageLayout = z ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (z)
        a.clearValue.depthStencil = {1, 0};
    return a;
}
void begin(VkCommandBuffer cmd, VkExtent2D sz, const VkRenderingAttachmentInfo *color, uint32_t count,
           const VkRenderingAttachmentInfo *depth = nullptr) {
    VkRect2D rect{{0, 0}, sz};
    VkViewport vp{0, 0, float(sz.width), float(sz.height), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &rect);
    VkRenderingInfo r{VK_STRUCTURE_TYPE_RENDERING_INFO};
    r.renderArea = rect;
    r.layerCount = 1;
    r.colorAttachmentCount = count;
    r.pColorAttachments = color;
    r.pDepthAttachment = depth;
    vkCmdBeginRendering(cmd, &r);
}
VkExtent2D smaller(VkExtent2D e) { return {std::max(1u, e.width / 2), std::max(1u, e.height / 2)}; }
void timestamp(Frame &f, int i) {
    vkCmdWriteTimestamp2(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, i);
}
} // namespace
void Passes::init() {
    auto sampler = [&](VkFilter filter) {
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        ci.minFilter = ci.magFilter = filter;
        ci.addressModeU = ci.addressModeV = ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler s;
        check(vkCreateSampler(g.device, &ci, nullptr, &s));
        fixed.add([this, s] { vkDestroySampler(g.device, s, nullptr); });
        return s;
    };
    point = sampler(VK_FILTER_NEAREST);
    linear = sampler(VK_FILTER_LINEAR);
    auto createLayout = [&](bool rt, VkDescriptorSetLayout &set, VkPipelineLayout &pipe) {
        std::vector<VkDescriptorSetLayoutBinding> b;
        for (uint32_t i = 0; i < 15; i++)
            b.push_back(
                {i, i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        if (rt)
            b.push_back({15, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                         nullptr});
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = uint32_t(b.size());
        ci.pBindings = b.data();
        check(vkCreateDescriptorSetLayout(g.device, &ci, nullptr, &set));
        fixed.add([this, set] { vkDestroyDescriptorSetLayout(g.device, set, nullptr); });
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lc.setLayoutCount = 1;
        lc.pSetLayouts = &set;
        check(vkCreatePipelineLayout(g.device, &lc, nullptr, &pipe));
        fixed.add([this, pipe] { vkDestroyPipelineLayout(g.device, pipe, nullptr); });
    };
    createLayout(false, setLayout, layout);
    VkPipelineVertexInputStateCreateInfo empty{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    light = g.graphics(fixed, layout, "sky.vert", "lighting.frag", empty, false, 3);
    ssao = g.graphics(fixed, layout, "sky.vert", "ao.frag", empty, false);
    post = g.graphics(fixed, layout, "sky.vert", "post.frag", empty, false);
    if (g.rayCapable) {
        createLayout(true, rtSetLayout, rtLayout);
        traced = g.graphics(fixed, rtLayout, "sky.vert", "traced.frag", empty, false, 3);
    }
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(g.device, &ci, nullptr, &shadowLayout));
    fixed.add([this] { vkDestroyPipelineLayout(g.device, shadowLayout, nullptr); });
    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription a{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    empty.vertexBindingDescriptionCount = empty.vertexAttributeDescriptionCount = 1;
    empty.pVertexBindingDescriptions = &bind;
    empty.pVertexAttributeDescriptions = &a;
    shadow = g.graphics(fixed, shadowLayout, "shadow.vert", "shadow.frag", empty, true, 2);
}
void Passes::allocate(Scene &s) {
    VkExtent2D desired{uint32_t(g.size.width * s.scale), uint32_t(g.size.height * s.scale)};
    if (size.width == desired.width && size.height == desired.height && output.width == g.size.width &&
        output.height == g.size.height)
        return;
    check(vkDeviceWaitIdle(g.device));
    targets.clear();
    size = desired;
    output = g.size;
    std::vector<VkDescriptorPoolSize> types = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 56},
                                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}};
    if (g.rayCapable)
        types.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2});
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = 4;
    pc.poolSizeCount = uint32_t(types.size());
    pc.pPoolSizes = types.data();
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(g.device, &pc, nullptr, &pool));
    targets.add([this, pool] { vkDestroyDescriptorPool(g.device, pool, nullptr); });
    auto make = [&](VkFormat fmt, VkExtent2D e,
                    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        bool z = fmt == VK_FORMAT_D32_SFLOAT;
        return g.image(targets, fmt, usage | VK_IMAGE_USAGE_SAMPLED_BIT, z ? 2 : 1, e);
    };
    for (int slot = 0; slot < 2; slot++) {
        auto &t = frame[slot];
        t.cached.fill(false);
        t.albedo = make(g.format, size);
        t.normal = make(g.format, size);
        t.depth = make(VK_FORMAT_D32_SFLOAT, size, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        t.ao = make(g.format, smaller(size));
        t.hdr = make(VK_FORMAT_R16G16B16A16_SFLOAT, size,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto dim = size;
        for (auto &b : t.bloom) {
            dim = smaller(dim);
            b = make(VK_FORMAT_R16G16B16A16_SFLOAT, dim,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        }
        for (auto &z : t.shadow)
            z = make(VK_FORMAT_D32_SFLOAT, {2048, 2048}, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        t.uniform = g.buffer(targets, sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        for (int variant = 0; variant < (g.rayCapable ? 2 : 1); variant++) {
            auto &set = variant ? t.rtSet : t.set;
            auto sl = variant ? rtSetLayout : setLayout;
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &sl;
            check(vkAllocateDescriptorSets(g.device, &ai, &set));
            VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            d.dstSet = set;
            d.descriptorCount = 1;
            d.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            VkDescriptorBufferInfo bi{t.uniform.handle, 0, sizeof(Uniforms)};
            d.pBufferInfo = &bi;
            vkUpdateDescriptorSets(g.device, 1, &d, 0, nullptr);
            Image imgs[] = {t.albedo,    t.normal,   t.depth,    t.shadow[0], t.shadow[1],
                            t.shadow[2], t.ao,       t.hdr,      t.bloom[0],  t.bloom[1],
                            t.bloom[2],  t.bloom[3], t.bloom[4], t.bloom[5]};
            for (uint32_t j = 0; j < 14; j++) {
                VkDescriptorImageInfo ii{j < 6 ? point : linear, imgs[j].view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                d.dstBinding = j + 1;
                d.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                d.pImageInfo = &ii;
                vkUpdateDescriptorSets(g.device, 1, &d, 0, nullptr);
            }
            if (variant) {
                VkWriteDescriptorSetAccelerationStructureKHR as{
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
                as.accelerationStructureCount = 1;
                as.pAccelerationStructures = &s.rays.top[slot];
                d.pNext = &as;
                d.dstBinding = 15;
                d.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                vkUpdateDescriptorSets(g.device, 1, &d, 0, nullptr);
            }
        }
    }
    s.history.resize(size, {frame[0].depth, frame[1].depth});
    for (int i = 0; i < 2; i++) {
        VkDescriptorImageInfo image{s.history.sampler, s.history.pyramids[1 - i].all,
                                    VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = s.sets[i];
        w.dstBinding = 3;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.descriptorCount = 1;
        w.pImageInfo = &image;
        vkUpdateDescriptorSets(g.device, 1, &w, 0, nullptr);
    }
}
void Passes::start(Scene &s, Frame &f, int slot, const View &v, bool rt) {
    auto &t = frame[slot];
    auto cmd = f.cmd;
    Uniforms u{};
    u.inverse = glm::inverse(v.vp);
    u.eyeTime = v.eyeTime;
    u.dir = v.direction;
    u.up = v.up;
    u.right = v.right;
    u.settings = {s.shadows ? 1.f : 0.f, 0, 0, 0};
    for (int i = 0; i < 3; i++) {
        float r = std::array<float, 3>{32, 96, 256}[i];
        auto focus = glm::vec3(v.eyeTime) + glm::vec3(v.direction) * r * .38f;
        if (!rt && s.shadows && t.cached[i] && glm::distance(focus, t.centers[i]) < r * .08f) {
            u.light[i] = t.matrices[i];
            continue;
        }
        auto lightDir = glm::normalize(glm::vec3(-.584, .648, -.488));
        auto proj = glm::ortho(-r, r, -r, r, 1.f, 650.f);
        proj[1][1] *= -1;
        u.light[i] = proj * glm::lookAt(focus + lightDir * 320.f, focus, glm::vec3(0, 1, 0));
        if (!rt) {
            t.cached[i] = s.shadows;
            t.centers[i] = focus;
            t.matrices[i] = u.light[i];
            attach(cmd, t.shadow[i], true);
            auto z = attachment(t.shadow[i], true);
            begin(cmd, {2048, 2048}, nullptr, 0, &z);
            if (s.shadows) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow);
                vkCmdPushConstants(cmd, shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &u.light[i]);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &s.vertices.handle, &off);
                vkCmdBindIndexBuffer(cmd, s.indices.handle, 0, VK_INDEX_TYPE_UINT32);
                if (g.indirect)
                    vkCmdDrawIndexedIndirect(cmd, s.allDraws.handle, 0, uint32_t(s.chunks.size()), 20);
                else
                    for (auto &c : s.chunks)
                        vkCmdDrawIndexed(cmd, c.draw.x, 1, c.draw.y, c.draw.z, 0);
            }
            vkCmdEndRendering(cmd);
            sample(cmd, t.shadow[i], true);
        }
    }
    memcpy(t.uniform.mapped, &u, sizeof(u));
    check(vmaFlushAllocation(g.allocator, t.uniform.allocation, 0, VK_WHOLE_SIZE));
    timestamp(f, 3);
    attach(cmd, t.albedo, false);
    attach(cmd, t.normal, false);
    attach(cmd, t.depth, true);
    VkRenderingAttachmentInfo colors[] = {attachment(t.albedo, false), attachment(t.normal, false)};
    auto depth = attachment(t.depth, true);
    begin(cmd, size, colors, 2, &depth);
}
void Passes::finish(Scene &s, Frame &f, int slot, bool rt) {
    auto &t = frame[slot];
    auto cmd = f.cmd;
    sample(cmd, t.albedo, false);
    sample(cmd, t.normal, false);
    sample(cmd, t.depth, true);
    if (s.hiz && s.culling && s.history.pyramids[1 - slot].vp == s.currentVP)
        s.history.record(f, slot, s.currentVP);
    else {
        s.history.pyramids[slot].built = false;
        s.history.pyramids[slot].vp = s.currentVP;
    }
    timestamp(f, 4);
    auto full = [&](Image target, VkExtent2D dims, VkPipeline pipe, VkPipelineLayout pl,
                    VkDescriptorSet set) {
        attach(cmd, target, false);
        auto c = attachment(target, false);
        begin(cmd, dims, &c, 1);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &set, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    };
    if (rt)
        s.rays.record(f, slot);
    else {
        full(t.ao, smaller(size), ssao, layout, t.set);
        sample(cmd, t.ao, false);
    }
    timestamp(f, 5);
    full(t.hdr, size, rt ? traced : light, rt ? rtLayout : layout, rt ? t.rtSet : t.set);
    timestamp(f, 6);
    transition(cmd, t.hdr.handle, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colStage, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    auto previous = t.hdr;
    auto dim = size;
    for (auto b : t.bloom) {
        auto half = smaller(dim);
        transition(cmd, b.handle, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                   VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {1, 0, 0, 1};
        blit.srcOffsets[1] = {int(dim.width), int(dim.height), 1};
        blit.dstOffsets[1] = {int(half.width), int(half.height), 1};
        vkCmdBlitImage(cmd, previous.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        transition(cmd, previous.handle, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT, fragment, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        transition(cmd, b.handle, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT);
        previous = b;
        dim = half;
    }
    transition(cmd, previous.handle, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT, fragment,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    g.render(f, false);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, post);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &t.set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    timestamp(f, 7);
}
