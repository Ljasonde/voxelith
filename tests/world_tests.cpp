#include "player.hpp"
#include <FastNoiseLite.h>
#include <iostream>
#include <stdexcept>
static void require(bool v, const char *msg) {
    if (!v)
        throw std::runtime_error(msg);
}
static uint64_t digest(const Mesh &m) {
    uint64_t h = 1469598103934665603ULL;
    auto bytes = [&](auto &v) {
        auto p = (const uint8_t *)v.data();
        for (size_t n = 0; n < v.size() * sizeof(v[0]); n++)
            h = (h ^ p[n]) * 1099511628211ULL;
    };
    bytes(m.vertices);
    bytes(m.indices);
    return h;
}
int main(int argc, char **argv) {
    try {
        std::string t = argc > 1 ? argv[1] : "";
        World w;
        Player p;
        p.feet = w.spawn();
        if (t == "mesh") {
            World other;
            auto id = World::chunkId(w.cores[0]);
            auto m = w.mesh(id);
            require(!m.indices.empty() && digest(m) == digest(other.mesh(id)), "Mesh determinism");
        } else if (t == "dda") {
            w.set({10, 115, 10}, Stone);
            auto h = w.pick({10.5, 115.5, 15.5}, {0, 0, -1});
            require(h.found && h.block == glm::ivec3(10, 115, 10) && h.previous.z == 11, "Axis ray");
            require(!w.pick({10.5, 115.5, 17.5}, {0, 0, -1}).found, "Ray reach");
        } else if (t == "aabb") {
            auto old = p.feet;
            for (int i = 0; i < 120; i++)
                p.step(w, {}, 1.f / 60, false, false);
            require(p.grounded && !w.overlaps(p.feet), "Floor collision");
            p.step(w, {}, 1.f / 60, true, false);
            require(p.velocity.y > 0, "Jump");
        } else if (t == "noise") {
            FastNoiseLite n(w.seed);
            for (int i = 0; i < 4096; i++) {
                float v = n.GetNoise(float(i % 64), float(i / 64));
                require(v >= -1 && v <= 1, "Noise range");
            }
        } else if (t == "quest") {
            for (auto core : w.cores) {
                p.feet = glm::vec3(core) + glm::vec3(.5f, -1.12f, 3.5f);
                p.yaw = -1.5707963f;
                p.pitch = 0;
                glm::ivec3 dirty;
                std::string msg;
                require(p.use(w, true, dirty, msg), "Collect");
            }
            require(p.mask == 7 && w.get(w.gateway) == Portal, "Portal unlock");
            p.feet = glm::vec3(w.gateway) + glm::vec3(.5, 0, .5);
            p.step(w, {}, .01f, false, false);
            require(p.won, "Victory");
        } else if (t == "save") {
            auto path = std::filesystem::current_path() / "test-output" / "roundtrip.dat";
            w.edit({3, 110, 4}, Crystal);
            p.mask = 3;
            p.save(w, path);
            World other;
            Player q;
            require(q.load(other, path) && q.mask == 3 && other.get({3, 110, 4}) == Crystal,
                    "Save roundtrip");
        } else
            return 2;
        std::cout << t << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
