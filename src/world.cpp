#include "world.hpp"
#include <FastNoiseLite.h>
#include <atomic>
#include <condition_variable>
#include <future>
#include <queue>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const Island islands[] = {{208, 216, 36, 62, 0}, {105, 116, 44, 72, 4}, {307, 115, 43, 57, 1},
                          {315, 310, 47, 65, 5}, {107, 322, 36, 71, 3}, {181, 72, 23, 83, 2},
                          {62, 212, 23, 56, 1},  {252, 345, 19, 84, 4}, {366, 204, 20, 74, 2},
                          {201, 155, 14, 66, 1}, {259, 176, 13, 58, 0}, {260, 264, 17, 66, 0}};
uint32_t hash(int x, int z, int seed) {
    uint32_t n = uint32_t(x) * 0x8da6b343u ^ uint32_t(z) * 0xd8163841u ^ uint32_t(seed);
    n = (n ^ (n >> 16)) * 0x7feb352du;
    return n ^ (n >> 15);
}
} // namespace
void parallelFor(int count, const std::function<void(int)> &work) {
    class Pool {
      public:
        std::vector<std::thread> workers;
        std::mutex mutex;
        std::condition_variable ready;
        bool stop = false;
        std::queue<std::packaged_task<void()>> jobs;
        Pool() {
            for (unsigned t = 0; t < std::clamp(std::thread::hardware_concurrency(), 1u, 8u); t++)
                workers.emplace_back([this] {
                    for (;;) {
                        std::packaged_task<void()> job;
                        {
                            std::unique_lock lock(mutex);
                            ready.wait(lock, [&] { return stop || !jobs.empty(); });
                            if (jobs.empty())
                                return;
                            job = std::move(jobs.front());
                            jobs.pop();
                        }
                        job();
                    }
                });
        }
        ~Pool() {
            {
                std::lock_guard lock(mutex);
                stop = true;
            }
            ready.notify_all();
            for (auto &t : workers)
                t.join();
        }
    };
    static Pool pool;
    std::atomic<int> next{};
    std::vector<std::future<void>> results;
    {
        std::lock_guard lock(pool.mutex);
        for (size_t i = 0; i < pool.workers.size(); i++) {
            std::packaged_task<void()> job([&] {
                for (int j = next++; j < count; j = next++)
                    work(j);
            });
            results.push_back(job.get_future());
            pool.jobs.push(std::move(job));
        }
    }
    pool.ready.notify_all();
    std::exception_ptr failed;
    for (auto &r : results) {
        try {
            r.get();
        } catch (...) {
            failed = std::current_exception();
        }
    }
    if (failed)
        std::rethrow_exception(failed);
}
void World::set(glm::ivec3 p, Block b) {
    if (inside(p))
        cells[key(p)] = b;
}
World::World(int s) : seed(s), cells(size_t(Side) * Side * Height) {
    FastNoiseLite terrain(s), detail(s + 1), cave(s + 9);
    terrain.SetFrequency(.045f);
    terrain.SetFractalType(FastNoiseLite::FractalType_FBm);
    detail.SetFrequency(.12f);
    cave.SetFrequency(.08f);
    std::vector<int> top(Side * Side, -1), biome(Side * Side);
    parallelFor(Side, [&](int z) {
        for (int x = 0; x < Side; x++) {
            float a = terrain.GetNoise(float(x), float(z)), b = detail.GetNoise(float(x), float(z));
            float best = -2;
            const Island *chosen = nullptr;
            for (auto &i : islands) {
                float dx = float(x - i.x), dz = float(z - i.z);
                float f = 1 - std::sqrt(dx * dx + dz * dz) / i.r + .16f * a;
                if (f > best) {
                    best = f;
                    chosen = &i;
                }
            }
            if (best <= 0)
                continue;
            int yTop = chosen->level + int(std::floor(4 * a + 2 * b + 4 * best));
            int bottom = yTop - std::max(2, int(38 * std::pow(best, .69f) + 4 * a + 2 * b));
            int k = z * Side + x;
            top[k] = yTop;
            biome[k] = chosen->biome;
            Block surface = chosen->biome == 5   ? Basalt
                            : chosen->biome == 3 ? Snow
                            : chosen->biome == 2 ? Sand
                                                 : Grass;
            for (int y = std::max(0, bottom); y <= yTop; y++) {
                if (y < yTop - 4 && cave.GetNoise(float(x), float(y), float(z)) > .49f)
                    continue;
                Block block = y == yTop       ? surface
                              : y >= yTop - 3 ? (surface == Grass ? Soil : surface)
                                              : Stone;
                if (chosen->biome == 5 && y < yTop - 3)
                    block = Basalt;
                set({x, y, z}, block);
            }
        }
    });
    auto box = [&](glm::ivec3 a, glm::ivec3 b, Block m) {
        for (int z = a.z; z <= b.z; z++)
            for (int y = a.y; y <= b.y; y++)
                for (int x = a.x; x <= b.x; x++)
                    set({x, y, z}, m);
    };
    for (int z = 5; z < Side - 5; z++)
        for (int x = 5; x < Side - 5; x++) {
            int k = z * Side + x, y = top[k], bio = biome[k];
            if (y < 0)
                continue;
            auto h = hash(x, z, s);
            if (h % 99 != 0)
                continue;
            if (bio == 2 || bio == 5) {
                if (h % 4)
                    continue;
                int n = 2 + (h >> 8) % 6;
                box({x, y + 1, z}, {x + 1, y + n, z + 1}, bio == 5 ? Lava : Amber);
                continue;
            }
            int trunk = 4 + (h >> 8) % 4;
            for (int j = 0; j < 4; j++) {
                int r = j == 3 ? 1 : j == 0 ? 2 : 3;
                for (int dz = -r; dz <= r; dz++)
                    for (int dx = -r; dx <= r; dx++)
                        if (dx * dx + dz * dz <= r * r + 1)
                            set({x + dx, y + trunk + j - 1, z + dz}, bio == 4             ? Petals
                                                                     : bio == 3 && j >= 2 ? Snow
                                                                                          : Leaves);
            }
            box({x, y + 1, z}, {x, y + trunk, z}, Wood);
        }
    auto platform = [&](const Island &i, int radius) {
        box({i.x - radius, i.level - 2, i.z - radius}, {i.x + radius, i.level + 4, i.z + radius}, Brick);
        box({i.x - radius, i.level + 5, i.z - radius}, {i.x + radius, i.level + 18, i.z + radius}, Air);
    };
    for (int t = 0; t < 4; t++)
        platform(islands[t], t ? 10 : 8);
    // Bridges share their exact endpoints with each ruin's foundation.
    auto bridge = [&](const Island &a, const Island &b) {
        int steps = std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
        for (int k = 0; k <= steps; k++) {
            float f = float(k) / steps;
            int x = int(std::round(a.x + (b.x - a.x) * f)), z = int(std::round(a.z + (b.z - a.z) * f));
            int y = int(std::round(a.level + 4 + (b.level - a.level) * f - 3 * std::sin(3.14159f * f)));
            box({x - 1, y, z - 1}, {x + 1, y, z + 1}, Planks);
            box({x - 1, y + 1, z - 1}, {x + 1, y + 3, z + 1}, Air);
            if (k % 10 == 0) {
                box({x - 2, y - 2, z}, {x - 2, y + 2, z}, Wood);
                set({x - 2, y + 3, z}, Amber);
            }
        }
    };
    for (int t = 1; t <= 4; t++)
        bridge(islands[0], islands[t]);
    for (int t = 1; t <= 3; t++) {
        auto i = islands[t];
        int floor = i.level + 4;
        Block accent = t == 1 ? Crystal : t == 2 ? Moss : Lava;
        for (int dx : {-8, 8})
            for (int dz : {-8, 8}) {
                int h = t == 1 ? 25 : t == 2 ? 10 : 15;
                for (int j = 1; j <= h; j++) {
                    int r = j <= 3 || j >= h - 2 ? 2 : 1;
                    box({i.x + dx - r, floor + j, i.z + dz - r}, {i.x + dx + r, floor + j, i.z + dz + r},
                        j % 6 == 0 ? Trim : Brick);
                    if (j > 4 && j % 6 != 0)
                        set({i.x + dx, floor + j, i.z + dz - 1}, accent);
                }
                box({i.x + dx - 1, floor + h + 1, i.z + dz - 1}, {i.x + dx + 1, floor + h + 3, i.z + dz + 1},
                    accent);
                set({i.x + dx, floor + h + 4, i.z + dz}, accent);
            }
        if (t == 1) {
            // Four stepped arches hold a suspended crystal above the courtyard.
            for (int axis = 0; axis < 2; axis++)
                for (int side : {-8, 8})
                    for (int k = -7; k <= 7; k++) {
                        int arch = 17 + int(std::sqrt(float(49 - k * k)) * .9f);
                        glm::ivec3 p = axis ? glm::ivec3(i.x + side, floor + arch, i.z + k)
                                            : glm::ivec3(i.x + k, floor + arch, i.z + side);
                        box(p, p + glm::ivec3(1, 2, 1), Trim);
                        set(p + glm::ivec3(0, 3, 0), Brick);
                    }
            for (int y = 0; y < 20; y++) {
                int rad = 1 + int(4 * (1.f - std::abs(y - 8.f) / 12.f));
                rad = std::max(0, rad);
                for (int z = -rad; z <= rad; z++)
                    for (int x = -rad; x <= rad; x++)
                        if (abs(x) + abs(z) <= rad + 1)
                            set({i.x + x, floor + 17 + y, i.z + z}, Crystal);
            }
        }
        if (t == 2) {
            for (int ring = 0; ring < 5; ring++) {
                int r = 12 - ring, y = floor + 11 + ring;
                for (int j = -r; j <= r; j++)
                    for (int a : {-r, r}) {
                        set({i.x + j, y, i.z + a}, ring % 2 ? Moss : Brick);
                        set({i.x + a, y, i.z + j}, ring % 2 ? Moss : Brick);
                    }
            }
            for (int side : {-1, 1}) {
                box({i.x + side * 11, floor - 1, i.z - 8}, {i.x + side * 11, floor, i.z + 8}, Water);
                for (int y = floor - 25; y < floor; y++)
                    set({i.x + side * 11, y, i.z + 8}, Water);
            }
            for (int z = -7; z <= 7; z++)
                for (int x = -7; x <= 7; x++)
                    if (abs(x) > 2 && abs(z) > 2)
                        set({i.x + x, floor, i.z + z}, Water);
            box({i.x - 9, floor + 10, i.z - 9}, {i.x + 9, floor + 11, i.z - 7}, Brick);
        }
        if (t == 3) {
            for (int z = -12; z <= 7; z++)
                for (int x = -13; x <= 13; x++) {
                    float a = std::abs(float(x)) * .075f;
                    int roof = 14 - int(a * a * 10) + int(hash(x, z, s) % 3);
                    if (abs(x) > 8 || z < 0)
                        box({i.x + x, floor + roof, i.z + z}, {i.x + x, floor + roof + 4, i.z + z}, Basalt);
                    if (abs(x) > 7 && hash(x, z, s) % 8 == 0)
                        box({i.x + x, floor + 1, i.z + z}, {i.x + x, floor + roof + 1, i.z + z},
                            hash(x, z, 1) % 3 ? Basalt : Lava);
                    if (abs(x) > 4 && (x + z) % 9 == 0)
                        set({i.x + x, floor, i.z + z}, Lava);
                }
            for (int z = 1; z <= 8; z++)
                box({i.x - 8, floor + 15, i.z - z}, {i.x + 8, floor + 16, i.z - z}, Basalt);
        }
        for (int j = 0; j < 3; j++)
            box({i.x - 3 + j, floor + j + 1, i.z - 3 + j}, {i.x + 3 - j, floor + j + 1, i.z + 3 - j},
                j == 2 ? Trim : Brick);
        cores[t - 1] = {i.x, floor + 4, i.z};
        set(cores[t - 1], Block(CoreBlue + t - 1));
    }
    auto i = islands[0];
    gateway = {i.x, i.level + 6, i.z + 5};
    box(gateway + glm::ivec3(-3, -1, 0), gateway + glm::ivec3(-2, 4, 1), Brick);
    box(gateway + glm::ivec3(2, -1, 0), gateway + glm::ivec3(3, 4, 1), Brick);
    box(gateway + glm::ivec3(-3, 4, 0), gateway + glm::ivec3(3, 5, 1), Trim);
    for (auto p : {glm::ivec3(-2, 3, 0), glm::ivec3(2, 3, 0), glm::ivec3(0, 5, 0)})
        set(gateway + p, Crystal);
    for (auto &a : islands)
        if (a.biome == 1 || a.biome == 4 || a.biome == 5) {
            int x = a.x + a.r - 3, z = a.z;
            int y = top[z * Side + x];
            if (y < 0)
                continue;
            for (int j = std::max(4, y - 48); j <= y + 1; j++)
                box({x, j, z - 1}, {x + 1, j, z + 1}, a.biome == 5 ? Lava : Water);
        }
    parallelFor(Chunks, [&](int id) {
        auto o = origin(id);
        for (int z = 0; z < 32; z++)
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 32; x++)
                    if (get(o + glm::ivec3(x, y, z))) {
                        occupied[id] = true;
                        return;
                    }
    });
}
glm::vec3 World::spawn() const { return {208.5f, 67.01f, 212.5f}; }
Mesh World::mesh(int id) const {
    Mesh out;
    if (!occupied[id])
        return out;
    auto base = origin(id);
    struct Face {
        uint32_t key{};
    };
    std::array<Face, 1024> mask;
    for (int axis = 0; axis < 3; axis++)
        for (int sign : {-1, 1}) {
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            glm::ivec3 n(0), du(0), dv(0);
            n[axis] = sign;
            du[u] = 1;
            dv[v] = 1;
            for (int slice = 0; slice < 32; slice++) {
                auto start = base;
                start[axis] += slice;
                for (int j = 0; j < 32; j++)
                    for (int i = 0; i < 32; i++) {
                        auto p = start + i * du + j * dv;
                        auto b = get(p), adj = get(p + n);
                        uint32_t face = 0;
                        if (b != Air && (adj == Air || (adj == Water && b != Water))) {
                            face = b;
                            for (int corner = 0; corner < 4; corner++) {
                                int a = corner == 1 || corner == 2 ? 1 : -1, c = corner >= 2 ? 1 : -1;
                                auto e = p + n;
                                int sa = solid(e + a * du), sb = solid(e + c * dv),
                                    sc = solid(e + a * du + c * dv);
                                face |= uint32_t(sa && sb ? 3 : sa + sb + sc) << (8 + 2 * corner);
                            }
                        }
                        mask[j * 32 + i].key = face;
                    }
                for (int j = 0; j < 32; j++)
                    for (int i = 0; i < 32;) {
                        uint32_t k = mask[j * 32 + i].key;
                        if (!k) {
                            i++;
                            continue;
                        }
                        int w = 1, h = 1;
                        while (i + w < 32 && mask[j * 32 + i + w].key == k)
                            w++;
                        while (j + h < 32) {
                            int a = 0;
                            for (; a < w && mask[(j + h) * 32 + i + a].key == k; a++)
                                ;
                            if (a < w)
                                break;
                            h++;
                        }
                        auto p = start + i * du + j * dv;
                        if (sign > 0)
                            p += n;
                        uint32_t idx = uint32_t(out.vertices.size());
                        glm::ivec3 corners[] = {p, p + w * du, p + w * du + h * dv, p + h * dv};
                        for (int c = 0; c < 4; c++) {
                            uint32_t ao = (k >> (8 + c * 2)) & 3;
                            out.vertices.push_back({corners[c], (k & 255) |
                                                                    (uint32_t(axis * 2 + (sign > 0)) << 8) |
                                                                    (ao << 12)});
                        }
                        for (int vtx : {0, 1, 2, 0, 2, 3})
                            out.indices.push_back(idx + (sign > 0 ? vtx : (4 - vtx) % 4));
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++)
                                mask[(j + y) * 32 + i + x].key = 0;
                        i += w;
                    }
            }
        }
    return out;
}
Hit World::pick(glm::vec3 pos, glm::vec3 dir, float reach) const {
    if (glm::dot(dir, dir) < 1e-12f)
        return {};
    dir = glm::normalize(dir);
    glm::ivec3 cell = glm::floor(pos), prev = cell, step = glm::sign(dir);
    glm::vec3 delta, next;
    for (int a = 0; a < 3; a++) {
        delta[a] = dir[a] == 0 ? std::numeric_limits<float>::infinity() : std::abs(1 / dir[a]);
        next[a] = dir[a] == 0 ? delta[a] : ((step[a] > 0 ? cell[a] + 1 : cell[a]) - pos[a]) / dir[a];
    }
    for (float d = 0; d <= reach;) {
        if (get(cell) != Air)
            return {true, cell, prev};
        int a = next.x <= next.y && next.x <= next.z ? 0 : next.y <= next.z ? 1 : 2;
        prev = cell;
        d = next[a];
        cell[a] += step[a];
        next[a] += delta[a];
    }
    return {};
}
bool World::overlaps(glm::vec3 feet) const {
    glm::ivec3 lo = glm::floor(feet + glm::vec3(-.2999f, .0001f, -.2999f)),
               hi = glm::floor(feet + glm::vec3(.2999f, 1.7999f, .2999f));
    for (int z = lo.z; z <= hi.z; z++)
        for (int y = lo.y; y <= hi.y; y++)
            for (int x = lo.x; x <= hi.x; x++)
                if (solid({x, y, z}))
                    return true;
    return false;
}
