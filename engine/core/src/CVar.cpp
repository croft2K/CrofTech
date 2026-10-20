#include "croftech/core/CVar.h"

#include "croftech/core/Assert.h"
#include "croftech/core/ConfigFile.h"

#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace ct {
namespace {

bool parseInt(std::string_view text, int& value) {
    int parsed = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, parsed);
    if (error != std::errc{} || ptr != end || text.empty()) {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace

CVar::CVar(std::string name, Type type, std::string description)
    : m_name(std::move(name)), m_description(std::move(description)), m_type(type) {}

CVar CVar::makeBool(std::string name, bool value, std::string description) {
    CVar cvar(std::move(name), Type::Bool, std::move(description));
    cvar.m_defaultText = value ? "1" : "0";
    cvar.reset();
    return cvar;
}

CVar CVar::makeInt(std::string name, int value, int min, int max, std::string description) {
    CT_ASSERT(min <= value && value <= max);
    CVar cvar(std::move(name), Type::Int, std::move(description));
    cvar.m_min = static_cast<float>(min);
    cvar.m_max = static_cast<float>(max);
    cvar.m_defaultText = std::format("{}", value);
    cvar.reset();
    return cvar;
}

CVar CVar::makeFloat(std::string name, float value, float min, float max, std::string description) {
    CT_ASSERT(min <= value && value <= max);
    CVar cvar(std::move(name), Type::Float, std::move(description));
    cvar.m_min = min;
    cvar.m_max = max;
    cvar.m_defaultText = std::format("{}", value);
    cvar.reset();
    return cvar;
}

CVar CVar::makeString(std::string name, std::string value, std::string description) {
    CVar cvar(std::move(name), Type::String, std::move(description));
    cvar.m_defaultText = std::move(value);
    cvar.reset();
    return cvar;
}

std::string CVar::describeRange() const {
    switch (m_type) {
    case Type::Bool:
        return "0 or 1";
    case Type::String:
        return "any text";
    case Type::Int:
    case Type::Float:
        break;
    }
    const char* kind = m_type == Type::Int ? "a whole number" : "a number";
    if (m_min > -kNoLimit && m_max < kNoLimit) {
        return std::format("{} from {} to {}", kind, m_min, m_max);
    }
    if (m_min > -kNoLimit) {
        return std::format("{} of at least {}", kind, m_min);
    }
    if (m_max < kNoLimit) {
        return std::format("{} of at most {}", kind, m_max);
    }
    return kind;
}

bool CVar::set(std::string_view text) {
    switch (m_type) {
    case Type::Bool: {
        bool value = false;
        if (!parseBool(text, value)) {
            return false;
        }
        m_int = value ? 1 : 0;
        m_float = static_cast<float>(m_int);
        m_text = value ? "1" : "0";
        return true;
    }
    case Type::Int: {
        int value = 0;
        if (!parseInt(text, value) || static_cast<float>(value) < m_min ||
            static_cast<float>(value) > m_max) {
            return false;
        }
        m_int = value;
        m_float = static_cast<float>(value);
        m_text = std::format("{}", value);
        return true;
    }
    case Type::Float: {
        float value = 0.0f;
        if (!parseFloat(text, value) || value < m_min || value > m_max) {
            return false;
        }
        m_float = value;
        m_int = static_cast<int>(value);
        m_text = std::format("{}", value);
        return true;
    }
    case Type::String:
        m_text = std::string(text);
        return true;
    }
    return false;
}

void CVar::reset() {
    const bool valid = set(m_defaultText);
    CT_ASSERT(valid);
    static_cast<void>(valid);
}

} // namespace ct
