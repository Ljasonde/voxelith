#pragma once
#include "gpu.hpp"
#include "world.hpp"
#include <glm/gtc/matrix_transform.hpp>
struct Camera { glm::vec3 eye{},dir{};float fov=72; };
struct alignas(16) View {glm::mat4 vp;glm::vec4 eyeTime;glm::vec4 direction;glm::vec4 right;glm::vec4 up;};
struct alignas(16) ChunkInfo {glm::vec4 lo,hi;glm::uvec4 draw;};
struct Scene {
    Gpu& g; World& world; Lifetime fixed,geometry;
    VkPipelineLayout layout{},computeLayout{}; VkPipeline terrain{},sky{},cull{};
    VkDescriptorSetLayout cullSetLayout{}; std::array<VkDescriptorSet,2> sets{};
    Buffer vertices,indices,info;std::array<Buffer,2> commands,counts;
    std::vector<ChunkInfo> chunks;std::vector<Mesh> meshes;
    uint32_t triangles{},visible{},visibleTriangles{}; bool culling=true;int distance=12;
    Scene(Gpu& gpu,World& w);
    void upload();
    void changed(glm::ivec3 p);
    View view(Camera cam,float time)const;
    void draw(Frame& f,Camera cam,float time);
    ~Scene(){vkDeviceWaitIdle(g.device);}
};
Camera tour(float t,int shot=0);

