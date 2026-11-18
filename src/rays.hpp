#pragma once
#include "gpu.hpp"
#include "world.hpp"
struct Rays {
    Lifetime resources;
    std::array<VkAccelerationStructureKHR, 2> top{};
    std::array<VkDeviceAddress, 2> scratch{};
    VkDeviceAddress instances{};
    uint32_t count{};
    void build(Gpu &g, const std::vector<Mesh> &meshes, Buffer vertices);
    void record(Frame &f, int slot) const;
};
