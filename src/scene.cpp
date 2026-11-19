#include "scene.hpp"
#include <glm/gtc/constants.hpp>
#include <cstring>
#include <cmath>
#include <iostream>

Scene::Scene(Gpu &gpu, World &w) : g(gpu), world(w), passes(gpu), history(gpu) {
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(View)};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.pushConstantRangeCount = 1;
    lc.pPushConstantRanges = &push;
    VkDescriptorSetLayoutBinding tex{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo atlasInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    atlasInfo.bindingCount = 1;
    atlasInfo.pBindings = &tex;
    VkDescriptorSetLayout atlasLayout;
    check(vkCreateDescriptorSetLayout(g.device, &atlasInfo, nullptr, &atlasLayout));
    fixed.add([this, atlasLayout] { vkDestroyDescriptorSetLayout(g.device, atlasLayout, nullptr); });
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &atlasLayout;
    check(vkCreatePipelineLayout(g.device, &lc, nullptr, &layout));
    fixed.add([this] { vkDestroyPipelineLayout(g.device, layout, nullptr); });
    VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                 {1, 0, VK_FORMAT_R32_UINT, 12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    sky = g.graphics(fixed, layout, "sky.vert", "sky.frag", vi, false);
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;
    terrain = g.graphics(fixed, layout, "world.vert", "world.frag", vi, true, 1);
    passes.init();
    history.init();
    VkDescriptorPoolSize one{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &one;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(g.device, &poolInfo, nullptr, &pool));
    fixed.add([this, pool] { vkDestroyDescriptorPool(g.device, pool, nullptr); });
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &atlasLayout;
    check(vkAllocateDescriptorSets(g.device, &allocate, &atlasSet));
    UploadBatch atlas(g);
    VkDescriptorImageInfo sampled{passes.point, atlas.atlas(fixed), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    atlas.submit();
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = atlasSet;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &sampled;
    vkUpdateDescriptorSets(g.device, 1, &write, 0, nullptr);
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t i = 0; i < 3; i++)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    sci.bindingCount = 4;
    sci.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(g.device, &sci, nullptr, &cullSetLayout));
    fixed.add([this] { vkDestroyDescriptorSetLayout(g.device, cullSetLayout, nullptr); });
    VkPushConstantRange cp{VK_SHADER_STAGE_COMPUTE_BIT, 0, 96};
    lc.pPushConstantRanges = &cp;
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &cullSetLayout;
    check(vkCreatePipelineLayout(g.device, &lc, nullptr, &computeLayout));
    fixed.add([this] { vkDestroyPipelineLayout(g.device, computeLayout, nullptr); });
    Lifetime temp;
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.layout = computeLayout;
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = g.module(temp, "cull.comp");
    ci.stage.pName = "main";
    check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &ci, nullptr, &cull));
    fixed.add([this] { vkDestroyPipeline(g.device, cull, nullptr); });
    resident.fill(true);
    meshes.resize(Chunks);
    parallelFor(Chunks, [&](int i) { meshes[i] = world.mesh(i); });
    upload();
}
void Scene::upload() {
    check(vkDeviceWaitIdle(g.device));
    geometry.clear();
    chunks.clear();
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds, water;
    for (int i = 0; i < Chunks; i++) {
        auto &m = meshes[i];
        if (m.indices.empty())
            continue;
        glm::vec3 lo = glm::vec3(1e5f), hi = -lo;
        for (auto &v : m.vertices) {
            lo = glm::min(lo, v.pos);
            hi = glm::max(hi, v.pos);
        }
        chunks.push_back({glm::vec4(lo, 0),
                          glm::vec4(hi, 0),
                          {uint32_t(m.indices.size()), uint32_t(inds.size()), uint32_t(verts.size()), 0}});
        verts.insert(verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto index : m.indices) {
            if ((m.vertices[index].face & 255) == Water)
                water.push_back(index + chunks.back().draw.z);
            else
                inds.push_back(index);
        }
        chunks.back().draw.x = uint32_t(inds.size()) - chunks.back().draw.y;
    }
    uploads = std::make_unique<UploadBatch>(g);
    auto make = [&](auto &data, VkBufferUsageFlags flags) {
        return uploads->upload(geometry, data.data(), data.size() * sizeof(data[0]), flags);
    };
    auto extra = g.rayCapable ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                              : 0;
    vertices = make(verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | extra);
    std::vector<VkDrawIndexedIndirectCommand> all;
    for (auto &c : chunks)
        all.push_back({c.draw.x, 1, c.draw.y, int(c.draw.z), 0});
    allDraws = make(all, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    indices = make(inds, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    waterCount = uint32_t(water.size());
    if (waterCount)
        waterIndices = make(water, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    info = make(chunks, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    uploads->submit();
    triangles = uint32_t((inds.size() + water.size()) / 3);
    if (g.rayCapable)
        rays.build(g, meshes, vertices);
    passes.size = {};
    VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
                                 {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = 2;
    pc.poolSizeCount = 2;
    pc.pPoolSizes = ps;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(g.device, &pc, nullptr, &pool));
    geometry.add([this, pool] { vkDestroyDescriptorPool(g.device, pool, nullptr); });
    for (int i = 0; i < 2; i++) {
        auto flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        commands[i] = g.buffer(geometry, chunks.size() * sizeof(VkDrawIndexedIndirectCommand), flags, false);
        counts[i] = g.buffer(geometry, 8, flags | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        memset(counts[i].mapped, 0, 8);
        check(vmaFlushAllocation(g.allocator, counts[i].allocation, 0, 8));
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &cullSetLayout;
        check(vkAllocateDescriptorSets(g.device, &ai, &sets[i]));
        VkDescriptorBufferInfo data[] = {{info.handle, 0, VK_WHOLE_SIZE},
                                         {commands[i].handle, 0, VK_WHOLE_SIZE},
                                         {counts[i].handle, 0, 8}};
        for (uint32_t j = 0; j < 3; j++) {
            VkWriteDescriptorSet d{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            d.dstSet = sets[i];
            d.dstBinding = j;
            d.descriptorCount = 1;
            d.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            d.pBufferInfo = &data[j];
            vkUpdateDescriptorSets(g.device, 1, &d, 0, nullptr);
        }
    }
}
void Scene::invalidateStream() {
    if (pending.valid())
        pending.get();
    streamRadius = 0;
}
void Scene::stream(glm::vec3 eye) {
    if (pending.valid()) {
        if (pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        auto updated = pending.get();
        for (auto &[i, m] : updated)
            meshes[i] = std::move(m);
        upload();
    }
    glm::ivec2 center =
        glm::clamp(glm::ivec2(eye.x / Chunk, eye.z / Chunk), glm::ivec2(0), glm::ivec2(Across - 1));
    if (streamCenter == center && streamRadius == distance)
        return;
    streamCenter = center;
    streamRadius = distance;
    std::vector<int> load, drop;
    for (int i = 0; i < Chunks; i++) {
        if (!world.occupied[i])
            continue;
        auto p = World::origin(i);
        auto delta = glm::vec2(p.x / Chunk - center.x, p.z / Chunk - center.y);
        bool wanted = glm::dot(delta, delta) <= float((distance + 1) * (distance + 1));
        if (wanted != resident[i]) {
            (wanted ? load : drop).push_back(i);
            resident[i] = wanted;
        }
    }
    if (load.empty() && drop.empty())
        return;
    // A snapshot lets edits proceed while the worker pool rebuilds the next ring.
    pending =
        std::async(std::launch::async, [snapshot = world, load = std::move(load), drop = std::move(drop)] {
            std::vector<MeshUpdate> changes(load.size());
            parallelFor(int(load.size()), [&](int k) {
                int i = load[k];
                changes[k] = {i, snapshot.mesh(i)};
            });
            for (int i : drop)
                changes.emplace_back(i, Mesh{});
            return changes;
        });
}
void Scene::changed(glm::ivec3 p) {
    if (pending.valid()) {
        auto updated = pending.get();
        for (auto &[i, m] : updated)
            meshes[i] = std::move(m);
    }
    streamRadius = 0;
    resident[World::chunkId(p)] = true;
    world.occupied[World::chunkId(p)] = true;
    glm::ivec3 lo = glm::max(p - 1, glm::ivec3(0)),
               hi = glm::min(p + 1, glm::ivec3(Side - 1, Height - 1, Side - 1));
    std::vector<int> dirty;
    for (int y = lo.y / 32; y <= hi.y / 32; y++)
        for (int z = lo.z / 32; z <= hi.z / 32; z++)
            for (int x = lo.x / 32; x <= hi.x / 32; x++) {
                int id = (y * Across + z) * Across + x;
                dirty.push_back(id);
            }
    parallelFor(int(dirty.size()), [&](int i) { meshes[dirty[i]] = world.mesh(dirty[i]); });
    upload();
}
View Scene::view(Camera cam, float time) const {
    float a = float(g.size.width) / g.size.height;
    auto dir = glm::normalize(cam.dir);
    auto right = glm::normalize(glm::cross(dir, {0, 1, 0}));
    auto up = glm::cross(right, dir);
    float tanHalf = std::tan(glm::radians(cam.fov) * .5f);
    auto proj = glm::perspective(glm::radians(cam.fov), a, .1f, 760.f);
    proj[1][1] *= -1;
    return {proj * glm::lookAt(cam.eye, cam.eye + dir, up),
            {cam.eye, time},
            {dir, 0},
            {right, tanHalf * a},
            {up, tanHalf}};
}
static void memory(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkAccessFlags2 writes,
                   VkPipelineStageFlags2 dst, VkAccessFlags2 reads) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = src;
    b.srcAccessMask = writes;
    b.dstStageMask = dst;
    b.dstAccessMask = reads;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}
void Scene::draw(Frame &f, Camera cam, float time) {
    if (uploads && uploads->complete())
        uploads.reset();
    auto v = view(cam, time);
    uint32_t idx = g.sequence % 2;
    bool gpuCull = culling && g.indirect;
    passes.allocate(*this);
    history.prepare(f.cmd);
    currentVP = v.vp;
    if (gpuCull) {
        if (g.sequence >= 2) {
            check(vmaInvalidateAllocation(g.allocator, counts[idx].allocation, 0, VK_WHOLE_SIZE));
            auto n = static_cast<uint32_t *>(counts[idx].mapped);
            visible = n[0];
            visibleTriangles = n[1];
        }
        vkCmdFillBuffer(f.cmd, counts[idx].handle, 0, 8, 0);
        memory(f.cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
               VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
        struct Params {
            glm::mat4 vp;
            glm::vec4 eye;
            glm::uvec4 num;
        } p{v.vp,
            {cam.eye, float(distance * 32)},
            {uint32_t(chunks.size()),
             uint32_t(hiz && history.pyramids[1 - idx].built && history.pyramids[1 - idx].vp == v.vp), 0, 0}};
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeLayout, 0, 1, &sets[idx], 0,
                                nullptr);
        vkCmdPushConstants(f.cmd, computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(p), &p);
        vkCmdDispatch(f.cmd, (uint32_t(chunks.size()) + 63) / 64, 1, 1);
        memory(f.cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
               VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_HOST_READ_BIT);
    }
    vkCmdWriteTimestamp2(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, 2);
    passes.start(*this, f, idx, v, rt && g.rayCapable);
    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain);
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &atlasSet, 0, nullptr);
    vkCmdPushConstants(f.cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(v),
                       &v);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(f.cmd, 0, 1, &vertices.handle, &offset);
    vkCmdBindIndexBuffer(f.cmd, indices.handle, 0, VK_INDEX_TYPE_UINT32);
    if (gpuCull)
        vkCmdDrawIndexedIndirectCount(f.cmd, commands[idx].handle, 0, counts[idx].handle, 0,
                                      uint32_t(chunks.size()), sizeof(VkDrawIndexedIndirectCommand));
    else {
        visible = 0;
        visibleTriangles = 0;
        for (auto &c : chunks) {
            if (glm::distance(cam.eye, glm::clamp(cam.eye, glm::vec3(c.lo), glm::vec3(c.hi))) > distance * 32)
                continue;
            visible++;
            visibleTriangles += c.draw.x / 3;
            vkCmdDrawIndexed(f.cmd, c.draw.x, 1, c.draw.y, c.draw.z, 0);
        }
    }
    vkCmdEndRendering(f.cmd);
    passes.finish(*this, f, idx, rt && g.rayCapable);
}
Camera tour(float t, int shot) {
    const glm::vec3 focus[] = {
        {105, 76, 116}, {307, 62, 115}, {315, 70, 310}, {208, 63, 216}, {106, 67, 323}};
    int k = shot % 5;
    float angle = -2.4f + .52f * std::sin(t * .12f) + k * .85f;
    glm::vec3 at = focus[k], eye = at + glm::vec3(std::sin(angle) * 106, 20 + 11 * std::sin(t * .15f),
                                                  std::cos(angle) * 106);
    return {eye, glm::normalize(at - eye), 59};
}
