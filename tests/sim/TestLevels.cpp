#include "TestLevels.h"

namespace ct::test {

Level makeMovementTestLevel() {
    Level level;
    auto box = [&](Vec3 min, Vec3 max) { level.brushes.push_back(makeBoxBrush(min, max)); };
    auto ramp = [&](Vec3 min, Vec3 max) { level.brushes.push_back(makeRampBrush(min, max)); };

    // 80 x 80 m floor with walls.
    box({-40.0f, -40.0f, -1.0f}, {40.0f, 40.0f, 0.0f});
    box({-41.0f, -41.0f, 0.0f}, {41.0f, -40.0f, 8.0f});
    box({-41.0f, 40.0f, 0.0f}, {41.0f, 41.0f, 8.0f});
    box({-41.0f, -40.0f, 0.0f}, {-40.0f, 40.0f, 8.0f});
    box({40.0f, -40.0f, 0.0f}, {41.0f, 40.0f, 8.0f});

    // Stairs: 6 steps of 0.3 m up to a platform at 1.8 m.
    for (int i = 0; i < 6; ++i) {
        const float x = 6.0f + 0.4f * static_cast<float>(i);
        box({x, -1.5f, 0.0f}, {x + 0.4f, 1.5f, 0.3f * static_cast<float>(i + 1)});
    }
    box({8.4f, -1.5f, 0.0f}, {14.0f, 1.5f, 1.8f});

    // A walkable ramp (about 20 degrees) up to a platform.
    ramp({6.0f, 5.0f, 0.0f}, {12.0f, 9.0f, 2.2f});
    box({12.0f, 5.0f, 0.0f}, {16.0f, 9.0f, 2.2f});

    // A slope too steep to walk up (about 60 degrees).
    ramp({6.0f, -9.0f, 0.0f}, {8.0f, -5.0f, 3.5f});

    // Boxes: 0.4 m walks up, 0.6 m and 1.0 m need a jump, 1.5 m is out of reach.
    const float heights[] = {0.4f, 0.6f, 1.0f, 1.5f};
    for (int i = 0; i < 4; ++i) {
        const float x = -14.0f + 3.0f * static_cast<float>(i);
        box({x, -14.0f, 0.0f}, {x + 2.0f, -12.0f, heights[i]});
    }

    // Pillars 1 m high with gaps of 2, 3, 4 and 5 m for jump distance testing.
    float x = -20.0f;
    for (int i = 0; i <= 4; ++i) {
        box({x, 15.0f, 0.0f}, {x + 2.0f, 17.0f, 1.0f});
        x += 2.0f + static_cast<float>(i + 2);
    }

    level.entities.push_back(makePlayerSpawn(kMovementTestSpawn, 0.0f));
    return level;
}

} // namespace ct::test
