#include "film.hpp"
#include <algorithm>
Camera Film::frame(Scene &s, int i) {
    float t = i / 60.f;
    if (t < 6)
        return tour(t, 3);
    if (t < 54) {
        int chapter = std::min(2, int((t - 6) / 16));
        float local = t - (6 + chapter * 16);
        if (local < 9)
            return tour(local + chapter * 3, chapter);
        auto pos = glm::vec3(s.world.cores[chapter]) + glm::vec3(.5f);
        float u = std::clamp((local - 9) / 4.f, 0.f, 1.f);
        u = u * u * (3 - 2 * u);
        auto eye = pos + glm::vec3(0, (1 - u) * 3.0f, glm::mix(12.f, 3.f, u));
        if (local >= 13.5f && !recovered[chapter]) {
            actor.feet = eye - glm::vec3(0, 1.62f, 0);
            actor.yaw = -1.5707963f;
            actor.pitch = 0;
            glm::ivec3 dirty;
            std::string msg;
            if (!actor.use(s.world, true, dirty, msg) || !(actor.mask & (1 << chapter)))
                throw std::runtime_error("Film core interaction missed");
            recovered[chapter] = true;
            s.changed(dirty);
            if (actor.mask == 7)
                s.changed(s.world.gateway);
        }
        return {eye, glm::normalize(pos - eye), 67};
    }
    if (t < 61)
        return tour(t - 54, 3);
    auto gate = glm::vec3(s.world.gateway) + glm::vec3(.5, 1.2, .5);
    float u = std::clamp((t - 61) / 4.f, 0.f, 1.f);
    auto eye = gate + glm::vec3(0, .2f, glm::mix(-12.f, -1.4f, u));
    actor.feet = eye - glm::vec3(0, 1.62f, 0);
    actor.step(s.world, {}, 1.f / 60, false, false);
    if (t >= 65 && !actor.won)
        throw std::runtime_error("Film gateway did not activate");
    ended = actor.won;
    return {eye, glm::normalize(gate - eye), 68};
}
