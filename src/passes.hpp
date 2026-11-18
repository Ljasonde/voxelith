#pragma once
#include "rays.hpp"
#include <glm/glm.hpp>
struct Scene;
struct Camera;
struct View;
struct Uniforms {
    glm::mat4 inverse, light[3];
    glm::vec4 eyeTime, dir, right, up, settings;
};
struct Surfaces {
    Image albedo, normal, depth, ao, hdr;
    std::array<Image, 6> bloom;
    std::array<Image, 3> shadow;
    Buffer uniform;
    VkDescriptorSet set{}, rtSet{};
    std::array<glm::vec3, 3> centers{};
    std::array<glm::mat4, 3> matrices{};
    std::array<bool, 3> cached{};
};
struct Passes {
    Gpu &g;
    Lifetime fixed, targets;
    std::array<Surfaces, 2> frame;
    VkExtent2D size{}, output{};
    VkSampler point{}, linear{};
    VkDescriptorSetLayout setLayout{}, rtSetLayout{};
    VkPipelineLayout layout{}, rtLayout{}, shadowLayout{};
    VkPipeline light{}, traced{}, ssao{}, post{}, shadow{};
    explicit Passes(Gpu &gpu) : g(gpu) {}
    void init();
    void allocate(Scene &s);
    void start(Scene &s, Frame &f, int slot, const View &view, bool rt);
    void finish(Scene &s, Frame &f, int slot, bool rt);
};
