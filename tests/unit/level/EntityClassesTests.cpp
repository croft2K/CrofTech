#include "croftech/level/EntityClasses.h"

#include <doctest/doctest.h>

#include <ostream>
#include <string>
#include <utility>

TEST_CASE("every class default is valid, except positions an entity must set") {
    for (const ct::EntityClassInfo& entityClass : ct::entityClasses()) {
        CHECK(ct::findEntityClass(entityClass.name) == &entityClass);
        CHECK(ct::isValidEntityName(entityClass.name));
        for (const ct::PropertyInfo& property : entityClass.properties) {
            CAPTURE(entityClass.name);
            CAPTURE(property.key);
            CHECK(ct::isValidEntityName(property.key));
            std::string problem;
            const bool required =
                property.type == ct::PropertyType::Vector && property.defaultValue.empty();
            CHECK(ct::checkPropertyValue(property, property.defaultValue, problem) != required);
        }
        for (const ct::SignalInfo& signal : entityClass.events) {
            CHECK(ct::isValidEntityName(signal.name));
        }
        for (const ct::SignalInfo& signal : entityClass.inputs) {
            CHECK(ct::isValidEntityName(signal.name));
        }
        CHECK((entityClass.shape == ct::EntityShape::Point) ==
              (ct::findPropertyInfo(entityClass, "origin") != nullptr));
    }
    CHECK(ct::findEntityClass("light") == nullptr);
}

TEST_CASE("properties read their value or the class default") {
    const ct::EntityClassInfo& mover = *ct::findEntityClass("mover");
    ct::LevelEntity entity = ct::makeEntity(mover);
    CHECK(entity.className == "mover");
    CHECK(ct::numberProperty(entity, mover, "speed") == 3.0f);
    CHECK_FALSE(ct::flagProperty(entity, mover, "loop"));
    CHECK(ct::choiceProperty(entity, mover, "activate") == 0);
    CHECK(ct::vectorProperty(entity, mover, "move") == ct::Vec3{});
    CHECK(ct::textProperty(entity, mover, "name").empty());

    ct::setProperty(entity, "speed", "1.5");
    ct::setProperty(entity, "loop", "1");
    ct::setProperty(entity, "activate", "stand");
    ct::setProperty(entity, "move", "0 0 2.5");
    ct::setProperty(entity, "name", "gate");
    CHECK(ct::numberProperty(entity, mover, "speed") == 1.5f);
    CHECK(ct::flagProperty(entity, mover, "loop"));
    CHECK(ct::choiceProperty(entity, mover, "activate") == 2);
    CHECK(ct::vectorProperty(entity, mover, "move") == ct::Vec3{0.0f, 0.0f, 2.5f});
    CHECK(ct::textProperty(entity, mover, "name") == "gate");

    const ct::EntityClassInfo& counter = *ct::findEntityClass("counter");
    ct::LevelEntity count = ct::makeEntity(counter);
    CHECK(ct::countProperty(count, counter, "count") == 2);
    ct::setProperty(count, "count", "7");
    CHECK(ct::countProperty(count, counter, "count") == 7);
}

TEST_CASE("bad property values and unknown events are reported") {
    const ct::EntityClassInfo& mover = *ct::findEntityClass("mover");
    const auto problemWith = [&](const char* key, const char* value) {
        ct::LevelEntity entity = ct::makeEntity(mover);
        ct::setProperty(entity, key, value);
        std::string problem;
        CHECK_FALSE(ct::checkEntityClassProperties(entity, mover, problem));
        return problem;
    };
    CHECK(problemWith("speed", "fast") == "speed must be a number from 0.01 to 100");
    CHECK(problemWith("speed", "0") == "speed must be a number from 0.01 to 100");
    CHECK(problemWith("loop", "yes") == "loop must be 0 or 1");
    CHECK(problemWith("activate", "shout") == "activate must be one of: input, touch, stand");
    CHECK(problemWith("move", "0 0") == "move must be three numbers inside the world: x y z");
    CHECK(problemWith("move", "0 0 5000") == "move must be three numbers inside the world: x y z");
    CHECK(problemWith("name", "the gate") == "name must be letters, digits, '_', '.' or '-'");

    const ct::EntityClassInfo& counter = *ct::findEntityClass("counter");
    ct::LevelEntity count = ct::makeEntity(counter);
    ct::setProperty(count, "origin", "0 0 0");
    ct::setProperty(count, "count", "2.5");
    std::string problem;
    CHECK_FALSE(ct::checkEntityClassProperties(count, counter, problem));
    CHECK(problem == "count must be a whole number from 1 to 65535");
    CHECK(ct::countProperty(count, counter, "count") == 2);

    ct::LevelEntity entity = ct::makeEntity(mover);
    entity.outputs.push_back({"on_reached_end", "light", "on", 0.0f});
    CHECK(ct::checkEntityClassProperties(entity, mover, problem));
    entity.outputs.push_back({"on_trigger", "light", "on", 0.0f});
    CHECK_FALSE(ct::checkEntityClassProperties(entity, mover, problem));
    CHECK(problem == "mover has no event 'on_trigger'");
}

TEST_CASE("every preset makes a valid entity of a known class") {
    REQUIRE_FALSE(ct::entityPresets().empty());
    for (const ct::EntityPreset& preset : ct::entityPresets()) {
        CAPTURE(preset.name);
        const ct::EntityClassInfo* entityClass = ct::findEntityClass(preset.className);
        REQUIRE(entityClass != nullptr);
        ct::LevelEntity entity = ct::makePresetEntity(preset);
        if (entityClass->shape == ct::EntityShape::Point) {
            // Placed where the editor puts it.
            ct::setProperty(entity, "origin", "1 2 3");
        }
        std::string problem;
        CHECK(ct::checkEntity(entity, problem));
        CHECK_MESSAGE(ct::checkEntityClassProperties(entity, *entityClass, problem), problem);
        if (entityClass->shape == ct::EntityShape::Brush) {
            CHECK(preset.size.x > 0.0f);
            CHECK(preset.size.y > 0.0f);
            CHECK(preset.size.z > 0.0f);
        }
    }
}

TEST_CASE("pickups name a weapon of the game by its file name") {
    const ct::EntityClassInfo* pickup = ct::findEntityClass(ct::kPickupClass);
    REQUIRE(pickup != nullptr);
    const ct::PropertyInfo* weapon = ct::findPropertyInfo(*pickup, "weapon");
    REQUIRE(weapon != nullptr);
    CHECK(weapon->type == ct::PropertyType::Weapon);
    std::string problem;
    CHECK(ct::checkPropertyValue(*weapon, "rocket_launcher", problem));
    CHECK(ct::checkPropertyValue(*weapon, "", problem));
    CHECK_FALSE(ct::checkPropertyValue(*weapon, "Rocket Launcher", problem));
    CHECK(problem.find("weapon") != std::string::npos);
}

TEST_CASE("enemies name the first point of their patrol, and each point the next") {
    for (const auto& [className, key] :
         {std::pair{ct::kEnemyClass, "patrol"}, std::pair{ct::kPatrolPointClass, "next"}}) {
        CAPTURE(className);
        const ct::EntityClassInfo* entityClass = ct::findEntityClass(className);
        REQUIRE(entityClass != nullptr);
        const ct::PropertyInfo* property = ct::findPropertyInfo(*entityClass, key);
        REQUIRE(property != nullptr);
        CHECK(property->type == ct::PropertyType::Reference);
        REQUIRE(property->choices.size() == 1);
        CHECK(property->choices[0] == ct::kPatrolPointClass);
        std::string problem;
        CHECK(ct::checkPropertyValue(*property, "guard_2", problem));
        CHECK(ct::checkPropertyValue(*property, "", problem));
        CHECK_FALSE(ct::checkPropertyValue(*property, "two words", problem));
    }
}

TEST_CASE("spawners name their encounter, and encounters run waves") {
    const ct::EntityClassInfo* spawner = ct::findEntityClass(ct::kSpawnerClass);
    const ct::EntityClassInfo* encounter = ct::findEntityClass(ct::kEncounterClass);
    REQUIRE(spawner != nullptr);
    REQUIRE(encounter != nullptr);
    const ct::PropertyInfo* named = ct::findPropertyInfo(*spawner, "encounter");
    REQUIRE(named != nullptr);
    CHECK(named->type == ct::PropertyType::Reference);
    REQUIRE(named->choices.size() == 1);
    CHECK(named->choices[0] == ct::kEncounterClass);
    CHECK(ct::findSignal(spawner->inputs, "spawn") != nullptr);
    CHECK(ct::findSignal(spawner->events, "on_death") != nullptr);
    for (const char* event : {"on_start", "on_wave", "on_cleared"}) {
        CHECK(ct::findSignal(encounter->events, event) != nullptr);
    }
    for (const char* input : {"start", "stop"}) {
        CHECK(ct::findSignal(encounter->inputs, input) != nullptr);
    }
}

TEST_CASE("script entities name a script, whose own events their outputs may use") {
    const ct::EntityClassInfo* script = ct::findEntityClass(ct::kScriptClass);
    REQUIRE(script != nullptr);
    CHECK(script->scripted);
    const ct::PropertyInfo* name = ct::findPropertyInfo(*script, "script");
    REQUIRE(name != nullptr);
    CHECK(name->type == ct::PropertyType::Script);

    ct::LevelEntity entity = ct::makeEntity(*script);
    ct::setProperty(entity, "origin", "0 0 0");
    ct::setProperty(entity, "script", "door_puzzle");
    entity.outputs.push_back({"on_anything", "door", "open", 0.0f});
    std::string problem;
    CHECK_MESSAGE(ct::checkEntityClassProperties(entity, *script, problem), problem);
    ct::setProperty(entity, "script", "../door");
    CHECK_FALSE(ct::checkEntityClassProperties(entity, *script, problem));
    CHECK(problem == "script must be a script's name: lowercase letters, digits, '-' or '_'");
}
