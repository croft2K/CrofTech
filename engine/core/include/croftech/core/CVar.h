#pragma once

#include <limits>
#include <string>
#include <string_view>

namespace ct {

// A console variable: a named, typed setting that config files, the command line and the developer
// console can change at runtime. Its value is always valid for its type and range. Variables are
// owned by a Console, which is the only thing that changes them.
class CVar {
public:
    enum class Type { Bool, Int, Float, String };

    static CVar makeBool(std::string name, bool value, std::string description);
    static CVar makeInt(std::string name, int value, int min, int max, std::string description);
    static CVar makeFloat(std::string name, float value, float min, float max,
                          std::string description);
    static CVar makeString(std::string name, std::string value, std::string description);

    static constexpr float kNoLimit = std::numeric_limits<float>::max();

    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_description; }
    Type type() const { return m_type; }
    const std::string& text() const { return m_text; }
    const std::string& defaultText() const { return m_defaultText; }
    bool isDefault() const { return m_text == m_defaultText; }

    bool asBool() const { return m_int != 0; }
    int asInt() const { return m_int; }
    float asFloat() const { return m_float; }

    // What set() accepts, e.g. "a number from 60 to 140".
    std::string describeRange() const;

    // Returns false and leaves the value unchanged if text is not valid for the type and range.
    bool set(std::string_view text);
    void reset();

private:
    CVar(std::string name, Type type, std::string description);

    std::string m_name;
    std::string m_description;
    Type m_type;
    std::string m_text;
    std::string m_defaultText;
    int m_int = 0;
    float m_float = 0.0f;
    float m_min = -kNoLimit;
    float m_max = kNoLimit;
};

} // namespace ct
