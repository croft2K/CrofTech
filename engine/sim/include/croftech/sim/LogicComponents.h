#pragma once

#include "croftech/core/Math.h"
#include "croftech/core/Schema.h"
#include "croftech/sim/EntityId.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ct {

// Components of the logic entities placed in levels (triggers, relays, counters, timers, movers)
// and of the inputs on their way between them. What a level sets (a counter's target, a timer's
// interval, the wiring) is static and stays in the GameLevel; only what changes during play is
// here, so it is saved, rewound and sent like any other state.

// The entity of the level file this entity was made from, which is how systems find its settings
// and outputs in the GameLevel. States can come from untrusted saves, so the index is checked
// against the level every time it is used.
struct LevelEntityRef {
    std::uint32_t index = 0;
};

template <>
struct Schema<LevelEntityRef> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(LevelEntityRef, index).withDescription("Which entity of the level file this is"),
    };
    static constexpr TypeSchema value{"level_entity", sizeof(LevelEntityRef), kFields};
};
static_assert(describesEveryByte(Schema<LevelEntityRef>::value));

enum LogicFlags : std::uint32_t {
    kLogicEnabled = 1u << 0, // triggers and relays: switched on; timers: running
};

inline constexpr std::array<std::string_view, 1> kLogicFlagNames{"enabled"};

struct Trigger {
    std::uint32_t flags = 0;
    std::uint32_t playersInside = 0; // a bit per input slot
};

template <>
struct Schema<Trigger> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Trigger, flags).withFlagNames(kLogicFlagNames),
        CT_FIELD(Trigger, playersInside)
            .withHint(FieldHint::Flags)
            .withDescription("The input slots of the players inside"),
    };
    static constexpr TypeSchema value{"trigger", sizeof(Trigger), kFields};
};
static_assert(describesEveryByte(Schema<Trigger>::value));

struct Relay {
    std::uint32_t flags = 0;
};

template <>
struct Schema<Relay> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Relay, flags).withFlagNames(kLogicFlagNames),
    };
    static constexpr TypeSchema value{"relay", sizeof(Relay), kFields};
};
static_assert(describesEveryByte(Schema<Relay>::value));

struct Counter {
    std::uint32_t value = 0;
};

template <>
struct Schema<Counter> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Counter, value).withDescription("Add inputs so far"),
    };
    static constexpr TypeSchema value{"counter", sizeof(Counter), kFields};
};
static_assert(describesEveryByte(Schema<Counter>::value));

struct Timer {
    std::uint32_t flags = 0;    // kLogicEnabled while running
    std::uint32_t nextTick = 0; // when on_timer fires next
};

template <>
struct Schema<Timer> {
    static constexpr std::array<std::string_view, 1> kFlagNames{"running"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Timer, flags).withFlagNames(kFlagNames),
        CT_FIELD(Timer, nextTick).withDescription("Tick at which it fires next"),
    };
    static constexpr TypeSchema value{"timer", sizeof(Timer), kFields};
};
static_assert(describesEveryByte(Schema<Timer>::value));

enum class MoverPhase : std::uint32_t { AtStart, ToEnd, AtEnd, ToStart };

enum MoverFlags : std::uint32_t {
    kMoverStopped = 1u << 0, // held by a stop input until resumed
};

// A mover's brushes are where they were built in the level, moved by the offset: zero at the
// start, the mover's "move" at the end.
struct Mover {
    Vec3 offset;
    float distance = 0.0f;   // travelled from the start, metres
    std::uint32_t phase = 0; // MoverPhase
    std::uint32_t flags = 0;
    std::uint32_t departTick = 0; // at rest: when it sets off for the other end; 0 is never
};

template <>
struct Schema<Mover> {
    static constexpr std::array<std::string_view, 1> kFlagNames{"stopped"};
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(Mover, offset)
            .withHint(FieldHint::Distance)
            .withDescription("How far its brushes are from where they were built"),
        CT_FIELD(Mover, distance)
            .withHint(FieldHint::Distance)
            .withDescription("Travelled from the start"),
        CT_FIELD(Mover, phase)
            .withRange(0, 3)
            .withDescription("0 at the start, 1 moving to the end, 2 at the end, 3 moving back"),
        CT_FIELD(Mover, flags).withFlagNames(kFlagNames),
        CT_FIELD(Mover, departTick).withDescription("Tick it sets off by itself; 0 is never"),
    };
    static constexpr TypeSchema value{"mover", sizeof(Mover), kFields};
};
static_assert(describesEveryByte(Schema<Mover>::value));

// An output with a delay, waiting to be delivered: an entity of its own, destroyed once delivered.
// The source entity and output number find the targets and input in the GameLevel. Inputs due on
// the same tick are delivered in the order they were sent.
struct PendingInput {
    std::uint32_t deliverTick = 0;
    std::uint32_t sentTick = 0;
    std::uint32_t sentOrder = 0; // among those sent on the same tick
    std::uint32_t source = 0;    // level entity index
    std::uint32_t output = 0;    // index into the source's outputs
    EntityId activator;          // the player who set the chain off, if any
};

template <>
struct Schema<PendingInput> {
    static constexpr FieldInfo kFields[] = {
        CT_FIELD(PendingInput, deliverTick).withDescription("Tick at which it is delivered"),
        CT_FIELD(PendingInput, sentTick),
        CT_FIELD(PendingInput, sentOrder),
        CT_FIELD(PendingInput, source).withDescription("Level entity whose output this is"),
        CT_FIELD(PendingInput, output).withDescription("Which of its outputs"),
        CT_FIELD(PendingInput, activator).withDescription("Who set the chain of events off"),
    };
    static constexpr TypeSchema value{"pending_input", sizeof(PendingInput), kFields};
};
static_assert(describesEveryByte(Schema<PendingInput>::value));

} // namespace ct
