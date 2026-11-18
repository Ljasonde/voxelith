#pragma once
#include "scene.hpp"
#include "player.hpp"
// The film uses the same interaction and portal state as the interactive game.
struct Film {
    Player actor;
    std::array<bool, 3> recovered{};
    bool ended{};
    Camera frame(Scene &s, int frameIndex);
};
