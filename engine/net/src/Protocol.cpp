#include "croftech/net/Protocol.h"

#include "croftech/core/Angle.h"
#include "croftech/sim/StateSave.h"

#include <cstdlib>
#include <type_traits>

namespace ct {
namespace {

constexpr std::uint16_t kKnownButtons = kButtonJump | kButtonAttack | kButtonCrouch;

void writeCommand(ByteWriter& out, const UserCmd& command) {
    out.writeU16(command.yaw);
    out.writeU16(static_cast<std::uint16_t>(command.pitch));
    out.writeU8(static_cast<std::uint8_t>(command.forward));
    out.writeU8(static_cast<std::uint8_t>(command.side));
    out.writeU16(command.buttons);
}

// -128 never comes from a client's keys or stick, and would make movement asymmetric.
bool readCommand(ByteReader& in, UserCmd& command) {
    command.yaw = in.readU16();
    command.pitch = static_cast<std::int16_t>(in.readU16());
    command.forward = static_cast<std::int8_t>(in.readU8());
    command.side = static_cast<std::int8_t>(in.readU8());
    command.buttons = in.readU16();
    return in.ok() && std::abs(command.pitch) <= kMaxPitch && command.forward != -128 &&
           command.side != -128 && (command.buttons & ~kKnownButtons) == 0;
}

} // namespace

void writeClientMessage(ByteWriter& out, const ClientMessage& message) {
    std::visit(
        [&out](const auto& m) {
            using T = std::remove_cvref_t<decltype(m)>;
            if constexpr (std::is_same_v<T, HelloMessage>) {
                out.writeU8(static_cast<std::uint8_t>(MessageType::Hello));
                out.writeU16(m.protocolVersion);
            } else if constexpr (std::is_same_v<T, InputMessage>) {
                out.writeU8(static_cast<std::uint8_t>(MessageType::Input));
                out.writeU32(m.sequence);
                writeCommand(out, m.command);
            } else {
                out.writeU8(static_cast<std::uint8_t>(MessageType::Goodbye));
            }
        },
        message);
}

void writeSnapshot(ByteWriter& out, std::uint32_t lastInputSequence, EntityId player,
                   const State& state) {
    out.writeU8(static_cast<std::uint8_t>(MessageType::Snapshot));
    out.writeU32(state.tick);
    out.writeU32(lastInputSequence);
    out.writeU32(player.index);
    out.writeU32(player.generation);
    writeState(out, state);
}

std::optional<ClientMessage> readClientMessage(std::span<const std::byte> packet) {
    if (packet.empty() || packet.size() > kMaxPacketBytes) {
        return std::nullopt;
    }
    ByteReader in(packet);
    std::optional<ClientMessage> message;
    switch (static_cast<MessageType>(in.readU8())) {
    case MessageType::Hello: {
        HelloMessage hello;
        hello.protocolVersion = in.readU16();
        message = hello;
        break;
    }
    case MessageType::Input: {
        InputMessage input;
        input.sequence = in.readU32();
        if (!readCommand(in, input.command)) {
            return std::nullopt;
        }
        message = input;
        break;
    }
    case MessageType::Goodbye:
        message = GoodbyeMessage{};
        break;
    case MessageType::Snapshot:
    default:
        return std::nullopt;
    }
    if (!in.ok() || !in.atEnd()) {
        return std::nullopt;
    }
    return message;
}

std::optional<SnapshotMessage> readSnapshot(std::span<const std::byte> packet) {
    if (packet.empty() || packet.size() > kMaxPacketBytes) {
        return std::nullopt;
    }
    ByteReader in(packet);
    if (static_cast<MessageType>(in.readU8()) != MessageType::Snapshot) {
        return std::nullopt;
    }
    SnapshotMessage snapshot;
    snapshot.tick = in.readU32();
    snapshot.lastInputSequence = in.readU32();
    snapshot.player.index = in.readU32();
    snapshot.player.generation = in.readU32();
    snapshot.state = in.readBytes(in.remaining());
    if (!in.ok() || snapshot.state.empty() ||
        (snapshot.player.isValid() && snapshot.player.index >= kMaxEntities)) {
        return std::nullopt;
    }
    return snapshot;
}

} // namespace ct
