#include "croftech/level/EntityClasses.h"

#include <doctest/doctest.h>

#include <ostream>
#include <string>

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
        CHECK(entityClass->shape == ct::EntityShape::Brush);
        const ct::LevelEntity entity = ct::makePresetEntity(preset);
        std::string problem;
        CHECK(ct::checkEntity(entity, problem));
        CHECK_MESSAGE(ct::checkEntityClassProperties(entity, *entityClass, problem), problem);
        CHECK(preset.size.x > 0.0f);
        CHECK(preset.size.y > 0.0f);
        CHECK(preset.size.z > 0.0f);
    }
}
