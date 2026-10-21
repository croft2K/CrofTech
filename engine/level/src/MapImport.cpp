#include "croftech/level/MapImport.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/core/DVec3.h"
#include "croftech/level/BrushGeometry.h"
#include "croftech/level/LevelFile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <utility>

namespace ct {
namespace {

constexpr std::size_t kMaxQuotedLength = 4096;
// Raw key/value pairs read per entity, before the level's own limits are applied.
constexpr std::size_t kMaxRawProperties = 1024;
// Quake's player box reaches 24 units below its origin, so its spawn origins sit that far above
// the floor; player_spawn origins are the feet.
constexpr double kQuakeSpawnHeightUnits = 24.0;

// A cursor over .map text that counts lines for messages.
class MapReader {
public:
    explicit MapReader(std::string_view text) : m_text(text) {}

    int line() const { return m_line; }

    // Skips whitespace and // comments; false at the end of the text.
    bool skipSpace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                skipLine();
            } else {
                return true;
            }
        }
        return false;
    }

    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    bool consume(char c) {
        if (skipSpace() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    // Moves to the start of the next line.
    void skipLine() {
        while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
            ++m_pos;
        }
    }

    // A run of characters up to the next whitespace, on the current line.
    std::string_view readWordOnLine() {
        while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t')) {
            ++m_pos;
        }
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isWhitespace(m_text[m_pos])) {
            ++m_pos;
        }
        return m_text.substr(start, m_pos - start);
    }

    bool readNumber(double& value) {
        skipSpace();
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && isNumberChar(m_text[m_pos])) {
            ++m_pos;
        }
        const char* first = m_text.data() + start;
        const char* last = m_text.data() + m_pos;
        const auto [ptr, error] = std::from_chars(first, last, value);
        return start != m_pos && error == std::errc{} && ptr == last && std::isfinite(value);
    }

    // A "quoted string". A backslash before a quote escapes it; other backslashes are kept.
    bool readQuoted(std::string& out) {
        if (!consume('"')) {
            return false;
        }
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') {
                return true;
            }
            if (c == '\n' || out.size() >= kMaxQuotedLength) {
                return false;
            }
            if (c == '\\' && m_pos < m_text.size() && m_text[m_pos] == '"') {
                out.push_back('"');
                ++m_pos;
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    // Skips the rest of a block whose opening brace has been read.
    bool skipBlock() {
        int depth = 1;
        while (skipSpace()) {
            const char c = m_text[m_pos];
            if (c == '"') {
                std::string ignored;
                if (!readQuoted(ignored)) {
                    return false;
                }
                continue;
            }
            ++m_pos;
            if (c == '{') {
                ++depth;
            } else if (c == '}' && --depth == 0) {
                return true;
            }
        }
        return false;
    }

private:
    static bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    static bool isNumberChar(char c) {
        return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
};

// Untrusted names in messages: control characters replaced and length capped, so a hostile file
// can't spoof log lines or flood them.
std::string printable(std::string_view text) {
    std::string out;
    for (const char c : text.substr(0, kMaxNameLength)) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(byte < 0x20 || byte == 0x7F ? '?' : c);
    }
    if (text.size() > kMaxNameLength) {
        out += "...";
    }
    return out;
}

class Importer {
public:
    Importer(std::string_view text, const MapImportOptions& options,
             std::vector<std::string>& warnings)
        : m_reader(text), m_options(options), m_warnings(warnings) {}

    std::optional<Level> run(std::string& error);

private:
    struct RawEntity {
        int line = 0;
        std::vector<std::pair<std::string, std::string>> properties;
        std::vector<std::pair<int, Brush>> brushes; // valid ones, with the line each starts on
        bool hasBrushBlocks = false;                // including skipped ones
    };

    bool fail(int line, std::string_view message, std::string& error) {
        error = std::format("line {}: {}", line, message);
        return false;
    }
    void warn(int line, std::string_view message);

    bool readEntity(RawEntity& entity, std::string& error);
    bool readBrush(RawEntity& entity, std::string& error);
    bool readPoint(DVec3& point);
    bool addEntity(const RawEntity& raw, std::string& error);
    std::optional<LevelEntity> convertEntity(const RawEntity& raw, const std::string& className);
    void convertDoor(const RawEntity& raw, LevelEntity& entity);

    MapReader m_reader;
    const MapImportOptions& m_options;
    std::vector<std::string>& m_warnings;
    std::size_t m_suppressedWarnings = 0;
    Level m_level;
};

void Importer::warn(int line, std::string_view message) {
    if (m_warnings.size() < kMaxImportWarnings) {
        m_warnings.push_back(std::format("line {}: {}", line, message));
    } else {
        ++m_suppressedWarnings;
    }
}

std::optional<Level> Importer::run(std::string& error) {
    while (m_reader.skipSpace()) {
        if (!m_reader.consume('{')) {
            fail(m_reader.line(), "expected '{' to start an entity", error);
            return std::nullopt;
        }
        RawEntity entity;
        entity.line = m_reader.line();
        if (!readEntity(entity, error) || !addEntity(entity, error)) {
            return std::nullopt;
        }
    }
    if (m_suppressedWarnings > 0) {
        m_warnings.push_back(std::format("and {} more warnings", m_suppressedWarnings));
    }
    return std::move(m_level);
}

bool Importer::readEntity(RawEntity& entity, std::string& error) {
    while (true) {
        if (!m_reader.skipSpace()) {
            return fail(entity.line, "entity has no closing '}'", error);
        }
        const char c = m_reader.peek();
        if (c == '}') {
            m_reader.consume('}');
            return true;
        }
        if (c == '"') {
            std::string key;
            std::string value;
            if (!m_reader.readQuoted(key) || !m_reader.readQuoted(value)) {
                return fail(m_reader.line(), "expected a quoted key and value", error);
            }
            if (entity.properties.size() >= kMaxRawProperties) {
                return fail(m_reader.line(), "entity has too many properties", error);
            }
            entity.properties.emplace_back(std::move(key), std::move(value));
        } else if (c == '{') {
            m_reader.consume('{');
            if (!readBrush(entity, error)) {
                return false;
            }
        } else {
            return fail(m_reader.line(), std::format("unexpected '{}' in entity", c), error);
        }
    }
}

bool Importer::readPoint(DVec3& point) {
    return m_reader.consume('(') && m_reader.readNumber(point.x) && m_reader.readNumber(point.y) &&
           m_reader.readNumber(point.z) && m_reader.consume(')');
}

bool Importer::readBrush(RawEntity& entity, std::string& error) {
    const int brushLine = m_reader.line();
    entity.hasBrushBlocks = true;
    if (!m_reader.skipSpace()) {
        return fail(brushLine, "brush has no closing '}'", error);
    }
    if (m_reader.peek() != '(' && m_reader.peek() != '}') {
        // Quake 3 patches (patchDef2) and brush primitives (brushDef).
        if (!m_reader.skipBlock()) {
            return fail(brushLine, "unterminated block", error);
        }
        warn(brushLine, "patches and brush primitives are not supported; skipped");
        return true;
    }

    std::vector<DPlane> planes;
    bool degenerate = false;
    std::size_t faceCount = 0;
    while (m_reader.skipSpace() && m_reader.peek() == '(') {
        std::array<DVec3, 3> points;
        for (DVec3& point : points) {
            if (!readPoint(point)) {
                return fail(m_reader.line(), "a face starts with three points: ( x y z )", error);
            }
            point = point * m_options.metresPerUnit;
        }
        if (m_reader.readWordOnLine().empty()) {
            return fail(m_reader.line(), "face has no texture name", error);
        }
        m_reader.skipLine(); // texture alignment

        ++faceCount;
        if (faceCount > kMaxBrushFaces) {
            continue;
        }
        // Face points run clockwise seen from outside the brush. The plane is kept unnormalized:
        // through integer points it is exact.
        const DPlane plane = planeThroughPoints(points[0], points[2], points[1]);
        if (!(length(plane.normal) > 1e-12)) {
            degenerate = true;
            continue;
        }
        planes.push_back(plane);
    }
    if (!m_reader.consume('}')) {
        return fail(m_reader.line(), "expected '(' to start a face or '}' to end the brush", error);
    }

    if (faceCount > kMaxBrushFaces) {
        warn(brushLine, std::format("brush has more than {} faces; skipped", kMaxBrushFaces));
        return true;
    }
    if (degenerate) {
        warn(brushLine, "brush has a face whose points are in a line; skipped");
    } else if (std::optional<Brush> brush = brushFromPlanes(planes)) {
        entity.brushes.emplace_back(brushLine, std::move(*brush));
    } else {
        warn(brushLine, "brush is not a closed solid inside the world; skipped");
    }
    return true;
}

bool Importer::addEntity(const RawEntity& raw, std::string& error) {
    const std::string* className = nullptr;
    for (const auto& [key, value] : raw.properties) {
        if (key == "classname") {
            className = &value;
        }
    }
    if (className == nullptr) {
        warn(raw.line, "entity has no classname; skipped");
        return true;
    }

    const bool world =
        *className == "worldspawn" || *className == "func_group" || *className == "func_detail";
    if (m_level.brushes.size() + raw.brushes.size() > kMaxLevelBrushes) {
        return fail(raw.line, std::format("more than {} brushes", kMaxLevelBrushes), error);
    }
    if (world) {
        for (const auto& [line, brush] : raw.brushes) {
            m_level.brushes.push_back(brush);
        }
        return true;
    }
    if (raw.hasBrushBlocks && raw.brushes.empty()) {
        warn(raw.line,
             std::format("brush entity '{}' has no valid brushes; skipped", printable(*className)));
        return true;
    }
    if (m_level.entities.size() >= kMaxLevelEntities) {
        return fail(raw.line, std::format("more than {} entities", kMaxLevelEntities), error);
    }
    std::optional<LevelEntity> entity = convertEntity(raw, *className);
    if (!entity) {
        return true;
    }
    const auto owner = static_cast<std::uint32_t>(m_level.entities.size());
    for (const auto& [line, brush] : raw.brushes) {
        m_level.brushes.push_back(brush);
        m_level.brushes.back().entity = owner;
    }
    m_level.entities.push_back(std::move(*entity));
    return true;
}

// Quake doors slide in the direction of their "angle" (-1 up, -2 down, otherwise degrees round
// Z) by their own size less a "lip", at "speed" units a second.
void Importer::convertDoor(const RawEntity& raw, LevelEntity& entity) {
    double angle = 0.0;
    double lip = 8.0;
    double speed = 100.0;
    for (const auto& [key, value] : raw.properties) {
        double* number = key == "angle"   ? &angle
                         : key == "lip"   ? &lip
                         : key == "speed" ? &speed
                                          : nullptr;
        float parsed = 0.0f;
        if (number != nullptr && parseFloat(value, parsed)) {
            *number = parsed;
        }
    }
    // A func_door with no brushes at all comes in as a point entity, with nothing to slide.
    Aabb bounds;
    if (raw.brushes.empty() || !computeBrushBounds(raw.brushes.front().second, bounds)) {
        return;
    }
    for (const auto& [line, brush] : raw.brushes) {
        Aabb more;
        if (computeBrushBounds(brush, more)) {
            bounds = {min(bounds.min, more.min), max(bounds.max, more.max)};
        }
    }
    DVec3 direction{0.0, 0.0, angle == -2.0 ? -1.0 : 1.0};
    if (angle != -1.0 && angle != -2.0) {
        const double radians = angle * (3.14159265358979323846 / 180.0);
        // Exact zeros for the axis directions, which have a rounding error in cos or sin.
        const auto clean = [](double value) { return std::fabs(value) < 1e-9 ? 0.0 : value; };
        direction = {clean(std::cos(radians)), clean(std::sin(radians)), 0.0};
    }
    const DVec3 size = toDouble(bounds.max - bounds.min);
    const double extent = std::fabs(direction.x) * size.x + std::fabs(direction.y) * size.y +
                          std::fabs(direction.z) * size.z;
    const double distance = std::max(extent - lip * m_options.metresPerUnit, 0.0);
    setProperty(entity, "move", formatVec3(toFloat(direction * distance)));
    const double metresPerSecond = std::clamp(speed * m_options.metresPerUnit, 0.01, 100.0);
    setProperty(entity, "speed", formatFloat(static_cast<float>(metresPerSecond)));
}

std::optional<LevelEntity> Importer::convertEntity(const RawEntity& raw,
                                                   const std::string& className) {
    const bool quakeSpawn =
        className == "info_player_start" || className == "info_player_deathmatch";
    const bool door = className == "func_door";
    const std::string shownClass = printable(className);
    LevelEntity entity;
    entity.className = className;
    if (quakeSpawn) {
        entity.className = kPlayerSpawnClass;
    } else if (door) {
        entity.className = "mover";
        entity.properties.push_back({"activate", "touch"});
    } else if (className == "trigger_multiple" || className == "trigger_once") {
        entity.className = "trigger";
        if (className == "trigger_once") {
            entity.properties.push_back({"once", "1"});
        }
    }
    if (!isValidEntityName(entity.className)) {
        warn(raw.line, "entity has an invalid class name; skipped");
        return std::nullopt;
    }
    if (door) {
        convertDoor(raw, entity);
    }

    for (const auto& [rawKey, rawValue] : raw.properties) {
        if (rawKey == "classname" || rawKey.starts_with("_tb_")) {
            continue; // TrenchBroom's own bookkeeping
        }
        if (door && (rawKey == "angle" || rawKey == "lip" || rawKey == "speed")) {
            continue; // converted to move and speed
        }
        std::string_view value = rawValue;
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        while (!value.empty() && value.back() == ' ') {
            value.remove_suffix(1);
        }
        std::string key = rawKey;
        if (key == "angle") {
            key = "yaw";
        } else if (key == "targetname") {
            key = "name";
        }
        EntityProperty property{std::move(key), std::string(value)};
        if (property.key == "origin") {
            std::array<float, 3> units{};
            if (!parseFloats(property.value, units)) {
                warn(raw.line, std::format("{}: origin is not three numbers; dropped", shownClass));
                continue;
            }
            const double scale = m_options.metresPerUnit;
            const double lift = quakeSpawn ? kQuakeSpawnHeightUnits : 0.0;
            property.value =
                std::format("{} {} {}", formatFloat(static_cast<float>(units[0] * scale)),
                            formatFloat(static_cast<float>(units[1] * scale)),
                            formatFloat(static_cast<float>((units[2] - lift) * scale)));
        }
        if (findProperty(entity, property.key) != nullptr) {
            warn(raw.line, std::format("{}: '{}' appears twice; later one dropped", shownClass,
                                       printable(property.key)));
            continue;
        }
        // Checked on its own; checking the whole entity per property would cost n^2 each time.
        const LevelEntity single{entity.className, {property}};
        std::string problem;
        if (entity.properties.size() >= kMaxEntityProperties || !checkEntity(single, problem)) {
            warn(raw.line, std::format("{}: property '{}' is not supported; dropped", shownClass,
                                       printable(property.key)));
            continue;
        }
        entity.properties.push_back(std::move(property));
    }
    return entity;
}

} // namespace

std::optional<Level> importMap(std::string_view text, const MapImportOptions& options,
                               std::vector<std::string>& warnings, std::string& error) {
    return Importer(text, options, warnings).run(error);
}

std::optional<Level> importMapFile(const std::filesystem::path& path,
                                   const MapImportOptions& options,
                                   std::vector<std::string>& warnings, std::string& error) {
    std::filesystem::path shown = path;
    shown.make_preferred();
    const std::optional<std::string> text = readTextFile(path, kMaxLevelFileBytes);
    if (!text) {
        error = std::format("{}: cannot read (missing, unreadable or over {} MiB)", shown.string(),
                            kMaxLevelFileBytes >> 20);
        return std::nullopt;
    }
    std::optional<Level> level = importMap(*text, options, warnings, error);
    if (!level) {
        error = std::format("{}: {}", shown.string(), error);
    }
    return level;
}

} // namespace ct
