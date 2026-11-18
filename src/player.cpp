#include "player.hpp"
#include <windows.h>
#include <fstream>
#include <cmath>
#include <algorithm>
glm::vec3 Player::forward() const {
    return {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
}
void Player::portal(World &w) {
    if (mask == 7)
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 3; dy++)
                w.set(w.gateway + glm::ivec3(dx, dy, 0), Portal);
}
void Player::step(World &w, glm::vec3 input, float dt, bool jump, bool sprint) {
    elapsed += dt;
    glm::vec3 flat(std::cos(yaw), 0, std::sin(yaw));
    auto side = glm::cross(flat, glm::vec3(0, 1, 0));
    auto movement = side * input.x + flat * input.z;
    if (glm::dot(movement, movement) > 1)
        movement = glm::normalize(movement);
    movement *= flying ? (sprint ? 28.f : 14.f) : (sprint ? 8.f : 4.6f);
    velocity.x = movement.x;
    velocity.z = movement.z;
    if (flying)
        velocity.y = input.y * 12;
    else {
        if (jump && grounded)
            velocity.y = 8.8f;
        velocity.y = std::max(-38.f, velocity.y - 24 * dt);
    }
    for (int axis : {0, 2, 1}) {
        float total = velocity[axis] * dt;
        int steps = std::max(1, int(std::ceil(std::abs(total) / .2f)));
        for (int k = 0; k < steps; k++) {
            auto dest = feet;
            dest[axis] += total / steps;
            if (!w.overlaps(dest)) {
                feet = dest;
                continue;
            }
            if (axis != 1 && !flying && grounded) {
                auto up = dest + glm::vec3(0, 1.01f, 0);
                if (!w.overlaps(up) && !w.overlaps(feet + glm::vec3(0, 1.01, 0))) {
                    feet = up;
                    continue;
                }
            }
            if (axis == 1) {
                float lo = 0, hi = 1;
                for (int b = 0; b < 10; b++) {
                    auto p = feet;
                    float mid = (lo + hi) * .5f;
                    p.y += total / steps * mid;
                    if (w.overlaps(p))
                        hi = mid;
                    else
                        lo = mid;
                }
                feet.y += total / steps * lo;
            }
            velocity[axis] = 0;
            break;
        }
    }
    grounded = w.overlaps(feet - glm::vec3(0, .012, 0));
    if (feet.y < 1) {
        feet = w.spawn();
        velocity = {};
    }
    if (mask == 7 && glm::distance(eye(), glm::vec3(w.gateway) + glm::vec3(.5, 1.2, .5)) < 2.5f)
        won = true;
}
bool Player::use(World &w, bool place, glm::ivec3 &dirty, std::string &msg) {
    auto hit = w.pick(eye(), forward());
    if (!hit.found)
        return false;
    auto b = w.get(hit.block);
    if (b >= CoreBlue && b <= CoreGold) {
        if (!place) {
            msg = "Right-click to recover this core";
            return false;
        }
        mask |= 1 << (b - CoreBlue);
        w.edit(hit.block, Air);
        dirty = hit.block;
        portal(w);
        msg = mask == 7 ? "All cores recovered  /  Return to the gateway" : "Relic core recovered";
        return true;
    }
    if (b == Portal)
        return false;
    dirty = place ? hit.previous : hit.block;
    if (!World::inside(dirty))
        return false;
    if (place) {
        if (w.get(dirty) != Air)
            return false;
        w.set(dirty, Hotbar[slot]);
        if (w.overlaps(feet)) {
            w.set(dirty, Air);
            return false;
        }
    }
    w.edit(dirty, place ? Hotbar[slot] : Air);
    return true;
}
void Player::save(const World &w, const std::filesystem::path &path) const {
    std::filesystem::create_directories(path.parent_path());
    auto tmp = path;
    tmp += L".tmp";
    std::ofstream out(tmp);
    out.precision(9);
    out << "Voxelith 1\n"
        << w.seed << " " << feet.x << " " << feet.y << " " << feet.z << " " << yaw << " " << pitch << " "
        << elapsed << " " << mask << " " << slot << " " << flying << " " << won << "\n"
        << w.edits.size() << "\n";
    for (auto [key, b] : w.edits)
        out << key << " " << b << "\n";
    out.close();
    if (!out || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Save failed");
}
bool Player::load(World &w, const std::filesystem::path &path) {
    std::ifstream in(path);
    Player p;
    std::string magic;
    int version, seed;
    size_t count;
    if (!(in >> magic >> version >> seed >> p.feet.x >> p.feet.y >> p.feet.z >> p.yaw >> p.pitch >>
          p.elapsed >> p.mask >> p.slot >> p.flying >> p.won >> count))
        return false;
    if (magic != "Voxelith" || version != 1 || seed != w.seed || count > w.cells.size() || p.slot < 0 ||
        p.slot > 8 || p.mask < 0 || p.mask > 7 || !std::isfinite(p.elapsed) || !std::isfinite(p.yaw) ||
        !std::isfinite(p.pitch) || p.elapsed < 0)
        return false;
    for (int a = 0; a < 3; a++)
        if (!std::isfinite(p.feet[a]) || std::abs(p.feet[a]) > 100000)
            return false;
    std::map<int, int> edits;
    for (size_t i = 0; i < count; i++) {
        int key, b;
        if (!(in >> key >> b) || key < 0 || size_t(key) >= w.cells.size() || b < Air || b > Portal)
            return false;
        edits[key] = b;
    }
    for (auto [k, b] : edits) {
        auto pos = World::unpack(k);
        w.edit(pos, Block(b));
        w.occupied[World::chunkId(pos)] = true;
    }
    *this = p;
    portal(w);
    return true;
}
