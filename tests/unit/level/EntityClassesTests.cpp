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
    const ct::EntityClassInfo& door = *ct::findEntityClass("door");
    ct::LevelEntity entity = ct::makeEntity(door);
    CHECK(entity.className == "door");
    CHECK(ct::numberProperty(entity, door, "speed") == 3.0f);
    CHECK(ct::flagProperty(entity, door, "touch_open"));
    CHECK(ct::vectorProperty(entity, door, "move") == ct::Vec3{});
    CHECK(ct::textProperty(entity, door, "name").empty());

    ct::setProperty(entity, "speed", "1.5");
    ct::setProperty(entity, "touch_open", "0");
    ct::setProperty(entity, "move", "0 0 2.5");
    ct::setProperty(entity, "name", "gate");
    CHECK(ct::numberProperty(entity, door, "speed") == 1.5f);
    CHECK_FALSE(ct::flagProperty(entity, door, "touch_open"));
    CHECK(ct::vectorProperty(entity, door, "move") == ct::Vec3{0.0f, 0.0f, 2.5f});
    CHECK(ct::textProperty(entity, door, "name") == "gate");

    const ct::EntityClassInfo& counter = *ct::findEntityClass("counter");
    ct::LevelEntity count = ct::makeEntity(counter);
    CHECK(ct::countProperty(count, counter, "count") == 2);
    ct::setProperty(count, "count", "7");
    CHECK(ct::countProperty(count, counter, "count") == 7);
}

TEST_CASE("bad property values and unknown events are reported") {
    const ct::EntityClassInfo& door = *ct::findEntityClass("door");
    const auto problemWith = [&](const char* key, const char* value) {
        ct::LevelEntity entity = ct::makeEntity(door);
        ct::setProperty(entity, key, value);
        std::string problem;
        CHECK_FALSE(ct::checkEntityClassProperties(entity, door, problem));
        return problem;
    };
    CHECK(problemWith("speed", "fast") == "speed must be a number from 0.01 to 100");
    CHECK(problemWith("speed", "0") == "speed must be a number from 0.01 to 100");
    CHECK(problemWith("touch_open", "yes") == "touch_open must be 0 or 1");
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

    ct::LevelEntity entity = ct::makeEntity(door);
    entity.outputs.push_back({"on_opened", "light", "on", 0.0f});
    CHECK(ct::checkEntityClassProperties(entity, door, problem));
    entity.outputs.push_back({"on_trigger", "light", "on", 0.0f});
    CHECK_FALSE(ct::checkEntityClassProperties(entity, door, problem));
    CHECK(problem == "door has no event 'on_trigger'");
}
