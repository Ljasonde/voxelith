#include "rays.hpp"
#include <cstring>
#include <algorithm>
namespace {
VkDeviceAddress address(Gpu &g, Buffer b) {
    VkBufferDeviceAddressInfo ci{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    ci.buffer = b.handle;
    return vkGetBufferDeviceAddress(g.device, &ci);
}
void dependency(VkCommandBuffer cmd, VkPipelineStageFlags2 dst, VkAccessFlags2 access) {
    VkMemoryBarrier2 m{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    m.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    m.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    m.dstStageMask = dst;
    m.dstAccessMask = access;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &m;
    vkCmdPipelineBarrier2(cmd, &di);
}
VkAccelerationStructureGeometryKHR instanceGeometry(VkDeviceAddress addr) {
    VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    geom.geometry.instances.data.deviceAddress = addr;
    return geom;
}
VkAccelerationStructureBuildGeometryInfoKHR buildInfo(VkAccelerationStructureTypeKHR type,
                                                      const VkAccelerationStructureGeometryKHR &geometry) {
    VkAccelerationStructureBuildGeometryInfoKHR b{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    b.type = type;
    b.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    b.geometryCount = 1;
    b.pGeometries = &geometry;
    return b;
}
} // namespace
void Rays::build(Gpu &g, const std::vector<Mesh> &meshes, Buffer vertices) {
    resources.clear();
    count = 0;
    Lifetime temporary;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR prop{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &prop};
    vkGetPhysicalDeviceProperties2(g.physical, &p);
    auto makeAS = [&](VkAccelerationStructureTypeKHR type, VkDeviceSize bytes) {
        auto buf = g.buffer(resources, bytes,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            false);
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        ci.buffer = buf.handle;
        ci.size = bytes;
        ci.type = type;
        VkAccelerationStructureKHR obj;
        check(vkCreateAccelerationStructureKHR(g.device, &ci, nullptr, &obj));
        resources.add([&g, obj] { vkDestroyAccelerationStructureKHR(g.device, obj, nullptr); });
        return obj;
    };
    auto makeScratch = [&](Lifetime &owner, VkDeviceSize bytes) {
        auto alignment = prop.minAccelerationStructureScratchOffsetAlignment;
        auto b =
            g.buffer(owner, bytes + alignment,
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        return (address(g, b) + alignment - 1) & ~VkDeviceAddress(alignment - 1);
    };
    struct Part {
        uint32_t index, prim, vertex, last;
    };
    std::vector<Part> parts;
    std::vector<uint32_t> opaque;
    uint32_t vertex = 0;
    for (auto &m : meshes) {
        uint32_t start = uint32_t(opaque.size());
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            auto b = m.vertices[m.indices[i]].face & 255;
            if (b != Water && b != Portal)
                opaque.insert(opaque.end(), m.indices.begin() + i, m.indices.begin() + i + 3);
        }
        uint32_t n = (uint32_t(opaque.size()) - start) / 3;
        if (n)
            parts.push_back({start, n, vertex, uint32_t(m.vertices.size() - 1)});
        vertex += uint32_t(m.vertices.size());
    }
    auto usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    auto idx = g.buffer(temporary, opaque.size() * 4, usage, true);
    memcpy(idx.mapped, opaque.data(), opaque.size() * 4);
    check(vmaFlushAllocation(g.allocator, idx.allocation, 0, VK_WHOLE_SIZE));
    auto ia = address(g, idx), va = address(g, vertices);
    std::vector<VkAccelerationStructureGeometryKHR> geometries(parts.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds;
    std::vector<VkAccelerationStructureInstanceKHR> inst;
    VkDeviceSize maxScratch = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        auto part = parts[i];
        auto &geo = geometries[i];
        geo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto &tr = geo.geometry.triangles;
        tr = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tr.vertexData.deviceAddress = va + part.vertex * sizeof(Vertex);
        tr.vertexStride = sizeof(Vertex);
        tr.maxVertex = part.last;
        tr.indexType = VK_INDEX_TYPE_UINT32;
        tr.indexData.deviceAddress = ia + part.index * 4;
        auto b = buildInfo(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geo);
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &b,
                                                &part.prim, &sizes);
        b.dstAccelerationStructure = makeAS(b.type, sizes.accelerationStructureSize);
        maxScratch = std::max(maxScratch, sizes.buildScratchSize);
        builds.push_back(b);
        VkAccelerationStructureDeviceAddressInfoKHR ai{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        ai.accelerationStructure = b.dstAccelerationStructure;
        VkAccelerationStructureInstanceKHR instance{};
        for (int j = 0; j < 3; j++)
            instance.transform.matrix[j][j] = 1;
        instance.mask = 255;
        instance.instanceCustomIndex = uint32_t(i);
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(g.device, &ai);
        inst.push_back(instance);
    }
    auto tempScratch = makeScratch(temporary, maxScratch);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = g.pool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(g.device, &ai, &cmd));
    temporary.add([&g, cmd] { vkFreeCommandBuffers(g.device, g.pool, 1, &cmd); });
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(cmd, &bi));
    for (size_t i = 0; i < builds.size(); i++) {
        auto &b = builds[i];
        b.scratchData.deviceAddress = tempScratch;
        VkAccelerationStructureBuildRangeInfoKHR r{parts[i].prim};
        const auto *range = &r;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &b, &range);
        dependency(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                   VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                       VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    }
    check(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE));
    check(vkQueueWaitIdle(g.queue));
    count = uint32_t(inst.size());
    auto buf = g.buffer(resources, inst.size() * sizeof(inst[0]), usage, true);
    memcpy(buf.mapped, inst.data(), inst.size() * sizeof(inst[0]));
    check(vmaFlushAllocation(g.allocator, buf.allocation, 0, VK_WHOLE_SIZE));
    instances = address(g, buf);
    auto geom = instanceGeometry(instances);
    auto b = buildInfo(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geom);
    VkAccelerationStructureBuildSizesInfoKHR sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &b,
                                            &count, &sizes);
    for (int i = 0; i < 2; i++) {
        top[i] = makeAS(b.type, sizes.accelerationStructureSize);
        scratch[i] = makeScratch(resources, sizes.buildScratchSize);
    }
}
void Rays::record(Frame &f, int slot) const {
    auto geom = instanceGeometry(instances);
    auto b = buildInfo(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geom);
    b.dstAccelerationStructure = top[slot];
    b.scratchData.deviceAddress = scratch[slot];
    VkAccelerationStructureBuildRangeInfoKHR r{count};
    const auto *range = &r;
    vkCmdBuildAccelerationStructuresKHR(f.cmd, 1, &b, &range);
    dependency(f.cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}
