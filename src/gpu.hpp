#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <functional>
#include <vector>
#include <array>
#include <stdexcept>
#include <string>
#include <atomic>

inline void check(VkResult r) {
    if (r != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(r));
}
struct Lifetime {
    std::vector<std::function<void()>> destructors;
    void add(std::function<void()> f) { destructors.push_back(std::move(f)); }
    void clear() {
        while (!destructors.empty()) {
            destructors.back()();
            destructors.pop_back();
        }
    }
    ~Lifetime() { clear(); }
    Lifetime() = default;
    Lifetime(const Lifetime &) = delete;
};
struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void *mapped{};
};
struct Image {
    VkImage handle{};
    VkImageView view{};
    VmaAllocation allocation{};
};
struct Frame {
    VkCommandBuffer cmd{};
    VkFence fence{};
    VkSemaphore acquired{};
    Image color, depth;
    VkQueryPool queries{};
    Buffer readback;
};
struct Gpu {
    Lifetime life, targets;
    std::filesystem::path shaders;
    GLFWwindow *window{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    uint32_t family{};
    VkDevice device{};
    VkQueue queue{};
    VmaAllocator allocator{};
    VkCommandPool pool{};
    VkSwapchainKHR swap{};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
    VkExtent2D size{};
    std::array<Frame, 2> frames{};
    std::vector<VkImage> images;
    std::vector<VkSemaphore> presented;
    bool validation{}, vsync{}, gui{}, indirect{}, rayCapable{};
    uint32_t sequence{};
    std::array<double, 6> times{};
    double lastGPU{}, lastCPU{};
    bool timingScene = false, rawOutput = false;
    void timings(Frame &f);
    VkPipelineLayout triangleLayout{};
    VkPipeline triangle{};
    void init(int width, int height, bool interactive, bool validate, bool sync);
    void resize();
    void startGui();
    void trianglePass(Frame &f);
    Buffer buffer(Lifetime &owner, size_t bytes, VkBufferUsageFlags usage, bool host);
    Image image(Lifetime &owner, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                VkExtent2D extent = {});
    VkShaderModule module(Lifetime &owner, const char *file);
    VkPipeline graphics(Lifetime &owner, VkPipelineLayout layout, const char *vertex, const char *fragment,
                        VkPipelineVertexInputStateCreateInfo inputs, bool depth, int pass = 0);
    bool draw(const std::function<void(Frame &)> &record, const std::filesystem::path &capture = {},
              double *gpuMs = nullptr);
    void render(Frame &f, bool depth, bool load = false);
    ~Gpu() {
        if (device)
            vkDeviceWaitIdle(device);
        targets.clear();
        life.clear();
    }
};
extern std::atomic<int> validationWarnings, validationErrors;
void transition(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout before,
                VkImageLayout after, VkPipelineStageFlags2 src, VkAccessFlags2 written,
                VkPipelineStageFlags2 dst, VkAccessFlags2 needed);
