#pragma once

#include "croftech/core/ByteStream.h"
#include "croftech/sim/EntityId.h"
#include "croftech/sim/UserCmd.h"
#include "croftech/sim/World.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace ct {

// The messages between a game client and its server. Each is one packet: a type byte, then its
// fields in little-endian order. Clients send only their inputs; the server decides everything and
// sends back the state.
//
// Snapshots carry the whole simulation state (writeState) for now. M6 replaces them with
// delta-compressed snapshots small enough for UDP, which is when kMaxPacketBytes comes down.
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kMaxPacketBytes = std::size_t{1} << 20;

enum class MessageType : std::uint8_t { Hello = 1, Input = 2, Goodbye = 3, Snapshot = 4 };

// Client to server. Hello asks to join (repeated until a snapshot arrives, since packets can be
// lost); Input is the player's command for now, numbered so late arrivals can be ignored; Goodbye
// leaves.
struct HelloMessage {
    std::uint16_t protocolVersion = kProtocolVersion;
};

struct InputMessage {
    std::uint32_t sequence = 0;
    UserCmd command;
};

struct GoodbyeMessage {};

using ClientMessage = std::variant<HelloMessage, InputMessage, GoodbyeMessage>;

// Server to client: the state after a tick, and which entity is the client's player.
struct SnapshotMessage {
    TickIndex tick = 0;
    std::uint32_t lastInputSequence = 0; // the newest input the server had received
    EntityId player;
    std::span<const std::byte> state; // writeState bytes, inside the packet; loadState checks them
};

void writeClientMessage(ByteWriter& out, const ClientMessage& message);
void writeSnapshot(ByteWriter& out, std::uint32_t lastInputSequence, EntityId player,
                   const State& state);

// Packets are untrusted: anything malformed (an unknown type, too short or too long, a value out
// of range) gives nullopt, for the caller to count and drop.
std::optional<ClientMessage> readClientMessage(std::span<const std::byte> packet);
std::optional<SnapshotMessage> readSnapshot(std::span<const std::byte> packet);

} // namespace ct
