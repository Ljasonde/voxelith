#include "game.hpp"
#include "player.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <bit>
#include <cstdio>
namespace {
void label(ImDrawList *d, ImVec2 pos, float size, ImU32 col, const char *text) {
    d->AddText(nullptr, size, pos, col, text);
}
constexpr ImU32 ivory = IM_COL32(239, 238, 216, 255), mint = IM_COL32(135, 232, 194, 255),
                panel = IM_COL32(16, 30, 32, 226);
void uiStyle() {
    auto &st = ImGui::GetStyle();
    st.WindowRounding = 5;
    st.FrameRounding = 3;
    st.FramePadding = {14, 8};
    st.WindowPadding = {24, 22};
    st.ItemSpacing = {8, 10};
    st.Colors[ImGuiCol_WindowBg] = {.045f, .09f, .10f, .96f};
    st.Colors[ImGuiCol_Button] = {.14f, .29f, .28f, 1};
    st.Colors[ImGuiCol_ButtonHovered] = {.19f, .43f, .38f, 1};
    st.Colors[ImGuiCol_Text] = {.92f, .94f, .89f, 1};
}
} // namespace
void play(Gpu &g, World &world, Scene &scene, const std::filesystem::path &data, int limit) {
    enum Screen { Title, Active, Pause, Settings, Controls, Victory };
    Screen mode = Title;
    Player p;
    p.feet = world.spawn();
    bool debug = false, photo = false, started = false;
    float fov = 72, clock = 0, toastTime = 0;
    std::string toast;
    auto path = data / "saves" / "world.dat";
    uiStyle();
    double last = glfwGetTime(), oldX = 0, oldY = 0;
    auto cursor = [&](bool lock) {
        glfwSetInputMode(g.window, GLFW_CURSOR, lock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(g.window, &oldX, &oldY);
    };
    auto enter = [&]() {
        mode = Active;
        started = true;
        cursor(true);
    };
    auto refresh = [&]() {
        scene.invalidateStream();
        scene.resident.fill(true);
        parallelFor(Chunks, [&](int i) { scene.meshes[i] = world.mesh(i); });
        scene.upload();
    };
    while (!glfwWindowShouldClose(g.window) && (!limit || g.sequence < uint32_t(limit))) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = std::clamp(float(now - last), .00001f, .05f);
        last = now;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        auto &io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && started) {
            if (mode == Active) {
                mode = Pause;
                cursor(false);
                p.save(world, path);
            } else if (mode == Pause)
                enter();
            else if (mode != Victory)
                mode = Pause;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
            debug = !debug;
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
            photo = !photo;
        if (!photo && (mode == Active || mode == Title))
            clock += dt;
        Camera cam = tour(clock * .5f);
        auto display = io.DisplaySize;
        if (mode == Active) {
            double x, y;
            glfwGetCursorPos(g.window, &x, &y);
            p.yaw += float(x - oldX) * .0022f;
            p.pitch = std::clamp(p.pitch - float(y - oldY) * .0022f, -1.52f, 1.52f);
            oldX = x;
            oldY = y;
            auto key = [&](int k) { return glfwGetKey(g.window, k) == GLFW_PRESS; };
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                p.flying = !p.flying;
                p.velocity.y = 0;
            }
            p.step(world,
                   {float(key(GLFW_KEY_D) - key(GLFW_KEY_A)),
                    float(key(GLFW_KEY_SPACE) - key(GLFW_KEY_LEFT_CONTROL)),
                    float(key(GLFW_KEY_W) - key(GLFW_KEY_S))},
                   dt, key(GLFW_KEY_SPACE), key(GLFW_KEY_LEFT_SHIFT));
            for (int k = 0; k < 9; k++)
                if (key(GLFW_KEY_1 + k))
                    p.slot = k;
            p.slot = (p.slot - int(io.MouseWheel) % 9 + 9) % 9;
            bool place = ImGui::IsMouseClicked(1);
            if (place || ImGui::IsMouseClicked(0)) {
                glm::ivec3 dirty;
                std::string msg;
                int before = p.mask;
                if (p.use(world, place, dirty, msg)) {
                    scene.changed(dirty);
                    if (before != p.mask && p.mask == 7)
                        scene.changed(world.gateway);
                    p.save(world, path);
                }
                if (!msg.empty()) {
                    toast = msg;
                    toastTime = 3;
                }
            }
            if (p.won) {
                p.save(world, path);
                mode = Victory;
                cursor(false);
            }
        }
        if (started) {
            scene.solarPhase = clock * .004f;
            cam = {p.eye(), p.forward(), fov};
            scene.stream(cam.eye);
        }
        if (mode == Active && !photo) {
            auto d = ImGui::GetForegroundDrawList();
            float x = display.x / 2, y = display.y / 2;
            d->AddLine({x - 6, y}, {x + 6, y}, ivory, 2);
            d->AddLine({x, y - 6}, {x, y + 6}, ivory, 2);
            char buf[160];
            snprintf(buf, sizeof(buf), "RELIC CORES    %d / 3", std::popcount(unsigned(p.mask)));
            d->AddRectFilled({24, 24}, {340, 100}, panel, 4);
            label(d, {40, 36}, 21, mint, buf);
            const char *ruins[] = {"Crystal Spire", "Drowned Temple", "Ember Hollow"};
            int target = 0;
            while (target < 3 && (p.mask & (1 << target)))
                target++;
            auto dest = target < 3 ? world.cores[target] : world.gateway;
            snprintf(buf, sizeof(buf), "%s  |  %.0f m   [%d, %d]",
                     target < 3 ? ruins[target] : "Home Gateway", glm::distance(p.feet, glm::vec3(dest)),
                     dest.x, dest.z);
            label(d, {40, 70}, 16, ivory, buf);
            float left = x - 9 * 30;
            for (int i = 0; i < 9; i++) {
                float l = left + i * 60;
                d->AddRectFilled({l, display.y - 70}, {l + 56, display.y - 16},
                                 i == p.slot ? IM_COL32(54, 107, 91, 245) : panel, 3);
                snprintf(buf, sizeof(buf), "%d", i + 1);
                label(d, {l + 6, display.y - 65}, 14, mint, buf);
                label(d, {l + 5, display.y - 42}, 13, ivory, Names[i]);
            }
            if (toastTime > 0) {
                label(d, {x - 150, y + 36}, 18, mint, toast.c_str());
                toastTime -= dt;
            }
        }
        if (mode != Active) {
            ImGui::SetNextWindowPos({display.x * .08f, display.y * .23f}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({385, 0});
            ImGui::Begin("Menu", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoMove);
            auto button = [](const char *s) { return ImGui::Button(s, {-1, 0}); };
            if (mode == Title) {
                ImGui::TextColored({.48f, .88f, .72f, 1}, "THE FLOATING ARCHIPELAGO");
                ImGui::SetWindowFontScale(2.7f);
                ImGui::TextUnformatted("VOXELITH");
                ImGui::SetWindowFontScale(1);
                ImGui::TextWrapped("Recover the three relic cores. Return to the gateway.");
                ImGui::Spacing();
                if (std::filesystem::exists(path) && button("Continue expedition")) {
                    if (p.load(world, path)) {
                        clock = p.elapsed;
                        refresh();
                        enter();
                    } else
                        toast = "Save could not be read";
                }
                if (button("New expedition"))
                    enter();
                ImGui::TextWrapped("WASD move   /   F fly   /   Esc controls");
                if (!toast.empty())
                    ImGui::TextWrapped("%s", toast.c_str());
                if (button("Quit"))
                    glfwSetWindowShouldClose(g.window, 1);
            } else if (mode == Pause) {
                ImGui::TextUnformatted("EXPEDITION PAUSED");
                if (button("Resume"))
                    enter();
                if (button("Settings"))
                    mode = Settings;
                if (button("Controls"))
                    mode = Controls;
                if (button("Save and quit")) {
                    p.save(world, path);
                    glfwSetWindowShouldClose(g.window, 1);
                }
            } else if (mode == Settings) {
                ImGui::TextUnformatted("RENDER SETTINGS");
                ImGui::SliderFloat("FOV", &fov, 48, 95);
                ImGui::SliderInt("View distance", &scene.distance, 8, 12);
                ImGui::Checkbox("GPU culling", &scene.culling);
                ImGui::Checkbox("Hi-Z history", &scene.hiz);
                if (g.rayCapable)
                    ImGui::Checkbox("Hardware ray tracing", &scene.rt);
                ImGui::Checkbox("Sun shadows", &scene.shadows);
                ImGui::SliderFloat("Render scale", &scene.scale, .7f, 1.f, "%.2f");
                if (button("1920 x 1080"))
                    glfwSetWindowSize(g.window, 1920, 1080);
                if (button("2560 x 1440"))
                    glfwSetWindowSize(g.window, 2560, 1440);
                if (button("Toggle borderless"))
                    glfwSetWindowAttrib(g.window, GLFW_DECORATED,
                                        !glfwGetWindowAttrib(g.window, GLFW_DECORATED));
                if (ImGui::Checkbox("VSync", &g.vsync))
                    g.resize();
                if (button("Back"))
                    mode = Pause;
            } else if (mode == Controls) {
                ImGui::TextWrapped(
                    "WASD move   /   Mouse look\nSpace jump   /   Shift sprint\nF toggle flight   /   Ctrl "
                    "descend\nLMB break   /   RMB build or collect\n1-9 or wheel select material\nF2 photo "
                    "mode   /   F3 statistics\nEsc pause and save");
                if (button("Back"))
                    mode = Pause;
            } else {
                ImGui::TextColored({.48f, .88f, .72f, 1}, "GATEWAY RESTORED");
                ImGui::Text("3 / 3 cores   |   %d:%02d", int(p.elapsed) / 60, int(p.elapsed) % 60);
                ImGui::TextUnformatted("The archipelago is yours to explore.");
                if (button("Free build")) {
                    p.won = false;
                    enter();
                    p.feet = world.spawn();
                }
                if (button("Quit expedition"))
                    glfwSetWindowShouldClose(g.window, 1);
            }
            ImGui::End();
        }
        if (debug && !photo) {
            ImGui::SetNextWindowPos({display.x - 335, 22}, ImGuiCond_Always);
            ImGui::Begin("Frame statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("%.1f FPS  %.2f ms/frame", io.Framerate, 1000.f / io.Framerate);
            ImGui::Text("%u / %zu chunks  |  %u triangles", scene.visible, scene.chunks.size(),
                        scene.visibleTriangles);
            ImGui::Text("CPU %.2f / GPU %.2f ms", g.lastCPU, g.lastGPU);
            ImGui::Text("Cull %.2f  Shadow %.2f  G-buffer %.2f", g.times[0], g.times[1], g.times[2]);
            ImGui::Text("AO/TLAS %.2f  Light %.2f  Post %.2f", g.times[3], g.times[4], g.times[5]);
            ImGui::Text("RT %s  scale %.2f  terrain calls %u", scene.rt ? "on" : "off", scene.scale,
                        scene.culling && g.indirect ? 1u : scene.visible);
            ImGui::Text("%s | seed %d", g.properties.deviceName, world.seed);
            ImGui::Text("XYZ %.1f  %.1f  %.1f", p.feet.x, p.feet.y, p.feet.z);
            ImGui::End();
        }
        ImGui::Render();
        g.draw([&](Frame &f) { scene.draw(f, cam, clock); });
    }
    if (started)
        p.save(world, path);
}
