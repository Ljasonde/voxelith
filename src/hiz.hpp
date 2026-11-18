#pragma once
#include "gpu.hpp"
#include <glm/glm.hpp>
struct DepthHistory {
    Gpu &g;
    Lifetime fixed, resources;
    struct Pyramid {
        VkImage image{};
        VkImageView all{};
        std::vector<VkImageView> mips;
        std::vector<VkDescriptorSet> reduce;
        glm::mat4 vp{};
        bool built{};
    };
    std::array<Pyramid, 2> pyramids;
    VkExtent2D extent{};
    int levels{};
    bool fresh = true;
    VkSampler sampler{};
    VkPipelineLayout layout{};
    VkDescriptorSetLayout setLayout{};
    VkPipeline pipeline{};
    explicit DepthHistory(Gpu &gpu) : g(gpu) {}
    void init();
    void resize(VkExtent2D source, std::array<Image, 2> depths);
    void prepare(VkCommandBuffer cmd);
    void record(Frame &f, int slot, glm::mat4 vp);
};
