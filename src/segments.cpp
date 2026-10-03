#include "internal.hpp"
#include <algorithm>
#include <limits>

namespace specqr::detail {

bool is_data(Mode mode) noexcept {
    return mode == Mode::Numeric || mode == Mode::Alphanumeric || mode == Mode::Byte || mode == Mode::Kanji;
}
void validate_version(int version) {
    if (version < 1 || version > 40) throw Error("INVALID_VERSION", "Version must be in 1...40.");
}
void validate_ecc(Ecc ecc) {
    if (ecc < Ecc::L || ecc > Ecc::H) throw Error("INVALID_INPUT", "Unknown error correction level.");
}

std::vector<Scalar> decode_utf8(std::string_view text) {
    if (text.size() > max_input_bytes) throw Error("INVALID_INPUT", "Text exceeds the 1,000,000 byte resource limit.");
    std::vector<Scalar> result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto start = i;
        const auto lead = static_cast<std::uint8_t>(text[i++]);
        std::uint32_t value;
        int count;
        if (lead < 0x80) { value = lead; count = 1; }
        else if (lead >= 0xC2 && lead <= 0xDF) { value = lead & 31U; count = 2; }
        else if (lead >= 0xE0 && lead <= 0xEF) { value = lead & 15U; count = 3; }
        else if (lead >= 0xF0 && lead <= 0xF4) { value = lead & 7U; count = 4; }
        else throw Error("INVALID_INPUT", "Text must contain strict UTF-8.");
        if (text.size() - start < static_cast<std::size_t>(count)) throw Error("INVALID_INPUT", "Truncated UTF-8 sequence.");
        for (int j = 1; j < count; ++j) {
            const auto next = static_cast<std::uint8_t>(text[i++]);
            if ((next & 0xC0U) != 0x80U) throw Error("INVALID_INPUT", "Invalid UTF-8 continuation byte.");
            value = (value << 6U) | (next & 63U);
        }
        if ((count == 2 && value < 0x80U) || (count == 3 && value < 0x800U) ||
            (count == 4 && value < 0x10000U) || (value >= 0xD800U && value <= 0xDFFFU) || value > 0x10FFFFU)
            throw Error("INVALID_INPUT", "UTF-8 must encode Unicode scalar values without overlong sequences.");
        result.push_back({value, start, count});
    }
    return result;
}

int alpha_value(std::uint32_t scalar) noexcept {
    constexpr std::string_view alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    if (scalar > 127U) return -1;
    const auto p = alphabet.find(static_cast<char>(scalar));
    return p == std::string_view::npos ? -1 : static_cast<int>(p);
}

std::optional<int> kanji_value(std::uint32_t scalar) noexcept {
    struct Mapping { std::uint16_t scalar, value; };
    static constexpr Mapping mappings[] = {
#include "kanji_map.inc"
    };
    if (scalar > 0xFFFFU) return {};
    const auto found = std::lower_bound(std::begin(mappings), std::end(mappings), scalar,
        [](const Mapping& item, std::uint32_t value) { return item.scalar < value; });
    if (found != std::end(mappings) && found->scalar == scalar) return found->value;
    return {};
}

int application_indicator(std::string_view text) {
    if (text.size() == 2 && text[0] >= '0' && text[0] <= '9' && text[1] >= '0' && text[1] <= '9')
        return (text[0] - '0') * 10 + text[1] - '0';
    if (text.size() == 1 && ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z')))
        return static_cast<unsigned char>(text[0]) + 100;
    throw Error("INVALID_MODE", "FNC1 second position requires two ASCII digits or one Latin letter.");
}

void validate_options(const Options& options) {
    validate_ecc(options.ecc);
    validate_version(options.min_version); validate_version(options.max_version);
    if (options.version) validate_version(*options.version);
    if (options.min_version > options.max_version) throw Error("INVALID_VERSION", "min_version must not exceed max_version.");
    if (options.mask && (*options.mask < 0 || *options.mask > 7)) throw Error("INVALID_INPUT", "Mask must be in 0...7.");
    if (options.mode != Mode::Auto && !is_data(options.mode)) throw Error("INVALID_MODE", "Mode must be Auto, Numeric, Alphanumeric, Byte, or Kanji.");
    if (options.eci) (void)Segment::eci(*options.eci);
    if (options.fnc1_second) (void)application_indicator(*options.fnc1_second);
    if (options.structured_append && !options.structured_append->valid()) throw Error("INVALID_MODE", "Structured Append requires total 2...16 and index 1...total.");
    const int controls = (options.eci ? 1 : 0) + (options.gs1 ? 1 : 0) + (options.fnc1_second ? 1 : 0) + (options.structured_append ? 1 : 0);
    if (controls > 1) throw Error(options.gs1 ? "INVALID_GS1" : "INVALID_MODE", "ECI, FNC1, and Structured Append options cannot be combined.");
}

void validate_controls(const std::vector<Segment>& segments) {
    if (segments.size() > max_segments) throw Error("INVALID_INPUT", "Manual segments exceed the 65,536 segment resource limit.");
    std::optional<Mode> leading;
    bool eci = false;
    std::size_t input = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        input += static_cast<std::size_t>(segment.byte_count());
        if (input > max_input_bytes) throw Error("INVALID_INPUT", "Manual segment payload exceeds the 1,000,000 byte resource limit.");
        if (segment.mode() == Mode::Eci) { eci = true; continue; }
        if (!segment.is_control()) continue;
        if (i != 0 || leading) throw Error(segment.mode() == Mode::Fnc1 ? "INVALID_GS1" : "INVALID_MODE", "FNC1 and Structured Append headers must occur once at the beginning.");
        leading = segment.mode();
    }
    if (leading && eci) throw Error(*leading == Mode::Fnc1 ? "INVALID_GS1" : "INVALID_MODE", "ECI cannot be combined with FNC1 or Structured Append.");
}

int payload_length(Mode mode, int count) {
    if (count < 0 || count > static_cast<int>(max_input_bytes)) throw Error("INVALID_INPUT", "Payload count exceeds the resource limit.");
    switch (mode) {
        case Mode::Numeric: return count / 3 * 10 + (count % 3 == 0 ? 0 : count % 3 == 1 ? 4 : 7);
        case Mode::Alphanumeric: return count / 2 * 11 + count % 2 * 6;
        case Mode::Kanji: return count * 13;
        case Mode::Byte: return count * 8;
        default: throw Error("INVALID_MODE", "A data mode is required.");
    }
}

std::vector<Segment> applying_options(std::vector<Segment> segments, const Options& options) {
    std::optional<Segment> control;
    if (options.eci) control = Segment::eci(*options.eci);
    if (options.gs1) control = Segment::fnc1();
    if (options.fnc1_second) control = Segment::fnc1_second(*options.fnc1_second);
    if (options.structured_append) {
        const auto& h = *options.structured_append;
        control = Segment::structured_append(h.index, h.total, h.parity);
    }
    if (control) segments.insert(segments.begin(), std::move(*control));
    validate_controls(segments);
    return segments;
}

namespace {
Segment make_segment(std::string_view text, Mode mode) {
    switch (mode) {
        case Mode::Numeric: return Segment::numeric(text);
        case Mode::Alphanumeric: return Segment::alphanumeric(text);
        case Mode::Byte: return Segment::byte(text);
        case Mode::Kanji: return Segment::kanji(text);
        default: throw Error("INVALID_MODE", "A data mode is required.");
    }
}
struct State {
    Mode mode;
    int remainder, bits, segments, previous;
    bool precedes(const State& other) const noexcept {
        return bits < other.bits || (bits == other.bits && segments < other.segments);
    }
};
struct Layer { std::array<State, 7> states{}; int size = 0; };
}

std::vector<Segment> single_segment(std::string_view text, Mode requested, bool allow_kanji) {
    auto mode = requested;
    if (mode == Mode::Auto) {
        const auto scalars = decode_utf8(text);
        if (!scalars.empty() && std::all_of(scalars.begin(), scalars.end(), [](const Scalar& s) { return s.value >= '0' && s.value <= '9'; })) mode = Mode::Numeric;
        else if (!scalars.empty() && std::all_of(scalars.begin(), scalars.end(), [](const Scalar& s) { return alpha_value(s.value) >= 0; })) mode = Mode::Alphanumeric;
        else if (allow_kanji && !scalars.empty() && std::all_of(scalars.begin(), scalars.end(), [](const Scalar& s) { return kanji_value(s.value).has_value(); })) mode = Mode::Kanji;
        else mode = Mode::Byte;
    }
    return {make_segment(text, mode)};
}

std::vector<Segment> optimize(std::string_view text, int version, bool allow_kanji) {
    const auto scalars = decode_utf8(text);
    if (scalars.empty()) return {Segment::byte("")};
    // Callers use a numeric lower-bound preflight, so DP only sees potentially encodable input.
    // This independent guard also bounds internal use to the maximum Model 2 numeric capacity.
    if (scalars.size() > 7100) return single_segment(text, Mode::Auto, allow_kanji);
    const std::array<Mode, 4> modes{Mode::Numeric, Mode::Alphanumeric, Mode::Kanji, Mode::Byte};
    std::vector<Layer> layers(scalars.size() + 1);
    layers[0].states[0] = {Mode::Auto, 0, 0, 0, 0}; layers[0].size = 1;
    for (std::size_t i = 0; i < scalars.size(); ++i) {
        const auto& scalar = scalars[i];
        const bool numeric = scalar.value >= '0' && scalar.value <= '9';
        const bool alpha = alpha_value(scalar.value) >= 0;
        const bool kanji = allow_kanji && kanji_value(scalar.value).has_value();
        auto& next = layers[i + 1];
        for (int previous = 0; previous < layers[i].size; ++previous) {
            const auto& state = layers[i].states[static_cast<std::size_t>(previous)];
            for (const auto mode : modes) {
                if ((mode == Mode::Numeric && !numeric) || (mode == Mode::Alphanumeric && !alpha) || (mode == Mode::Kanji && !kanji)) continue;
                const bool same = state.mode == mode;
                const int remainder = same ? state.remainder : 0;
                const int extra = mode == Mode::Numeric ? (remainder == 0 ? 4 : 3) : mode == Mode::Alphanumeric ? (remainder == 0 ? 6 : 5) : mode == Mode::Kanji ? 13 : scalar.bytes * 8;
                const int next_remainder = mode == Mode::Numeric ? (remainder + 1) % 3 : mode == Mode::Alphanumeric ? (remainder + 1) % 2 : 0;
                const State candidate{mode, next_remainder, state.bits + extra + (same ? 0 : 4 + count_bits(mode, version)), state.segments + (same ? 0 : 1), previous};
                int index = 0;
                while (index < next.size && (next.states[static_cast<std::size_t>(index)].mode != mode || next.states[static_cast<std::size_t>(index)].remainder != next_remainder)) ++index;
                if (index == next.size) { next.states[static_cast<std::size_t>(next.size++)] = candidate; }
                else if (candidate.precedes(next.states[static_cast<std::size_t>(index)])) next.states[static_cast<std::size_t>(index)] = candidate;
            }
        }
    }
    int best = 0;
    const auto& final = layers.back();
    for (int i = 1; i < final.size; ++i) if (final.states[static_cast<std::size_t>(i)].precedes(final.states[static_cast<std::size_t>(best)])) best = i;
    std::vector<Mode> assignments(scalars.size());
    for (std::size_t i = scalars.size(); i > 0; --i) {
        const auto& state = layers[i].states[static_cast<std::size_t>(best)];
        assignments[i - 1] = state.mode; best = state.previous;
    }
    std::vector<Segment> result;
    std::size_t start = 0;
    for (std::size_t end = 1; end <= scalars.size(); ++end) {
        if (end < scalars.size() && assignments[end] == assignments[start]) continue;
        const auto begin_byte = scalars[start].offset;
        const auto end_byte = end == scalars.size() ? text.size() : scalars[end].offset;
        result.push_back(make_segment(text.substr(begin_byte, end_byte - begin_byte), assignments[start]));
        start = end;
    }
    return result;
}

namespace {
class BitBuffer {
public:
    void append(std::uint32_t value, int width) {
        if (width < 0 || width > 31 || (width < 31 && value >= (std::uint32_t{1} << width)))
            throw Error("INVALID_INPUT", "Internal bit value does not fit the requested width.");
        for (int bit = width - 1; bit >= 0; --bit) {
            if ((count_ % 8) == 0) data_.push_back(0);
            if (((value >> static_cast<unsigned>(bit)) & 1U) != 0)
                data_.back() = static_cast<std::uint8_t>(data_.back() | (1U << (7 - count_ % 8)));
            ++count_;
        }
    }
    int count() const noexcept { return count_; }
    std::vector<std::uint8_t> finish() && { return std::move(data_); }
private:
    std::vector<std::uint8_t> data_;
    int count_ = 0;
};
}

std::vector<std::uint8_t> encode(const std::vector<Segment>& segments, int version, Ecc ecc) {
    validate_version(version); validate_ecc(ecc); validate_controls(segments);
    const int capacity = data_codewords(version, ecc) * 8;
    int required = 0;
    for (const auto& segment : segments) required += segment.bit_length(version);
    if (required > capacity) throw Error(required, capacity, version);
    BitBuffer buffer;
    for (const auto& segment : segments) {
        switch (segment.mode()) {
            case Mode::Eci: {
                const auto assignment = static_cast<std::uint32_t>(*segment.assignment_number());
                buffer.append(7, 4);
                if (assignment < 128U) buffer.append(assignment, 8);
                else if (assignment < 16384U) { buffer.append(2, 2); buffer.append(assignment, 14); }
                else { buffer.append(6, 3); buffer.append(assignment, 21); }
                break;
            }
            case Mode::Fnc1: buffer.append(5, 4); break;
            case Mode::Fnc1Second: buffer.append(9, 4); buffer.append(static_cast<std::uint32_t>(application_indicator(*segment.application_indicator())), 8); break;
            case Mode::StructuredAppend: {
                const auto& h = *segment.header();
                buffer.append(3, 4); buffer.append(static_cast<std::uint32_t>(h.index - 1), 4); buffer.append(static_cast<std::uint32_t>(h.total - 1), 4); buffer.append(h.parity, 8); break;
            }
            default: {
                const int count = segment.mode() == Mode::Byte ? segment.byte_count() : segment.character_count();
                const int width = count_bits(segment.mode(), version);
                if (count >= (1 << width)) throw Error(required, capacity, version);
                const auto indicator = segment.mode() == Mode::Numeric ? 1U : segment.mode() == Mode::Alphanumeric ? 2U : segment.mode() == Mode::Byte ? 4U : 8U;
                buffer.append(indicator, 4); buffer.append(static_cast<std::uint32_t>(count), width);
                const auto& text = segment.text();
                if (segment.mode() == Mode::Numeric) {
                    for (std::size_t i = 0; i < text.size(); i += 3) {
                        const auto length = std::min<std::size_t>(3, text.size() - i);
                        std::uint32_t value = 0;
                        for (std::size_t j = 0; j < length; ++j) value = value * 10U + static_cast<unsigned>(text[i + j] - '0');
                        buffer.append(value, length == 1 ? 4 : length == 2 ? 7 : 10);
                    }
                } else if (segment.mode() == Mode::Alphanumeric) {
                    for (std::size_t i = 0; i < text.size(); i += 2) {
                        const auto first = alpha_value(static_cast<unsigned char>(text[i]));
                        if (i + 1 < text.size()) buffer.append(static_cast<std::uint32_t>(first * 45 + alpha_value(static_cast<unsigned char>(text[i + 1]))), 11);
                        else buffer.append(static_cast<std::uint32_t>(first), 6);
                    }
                } else if (segment.mode() == Mode::Kanji) {
                    for (const auto& scalar : decode_utf8(text)) buffer.append(static_cast<std::uint32_t>(*kanji_value(scalar.value)), 13);
                } else {
                    for (const auto b : segment.bytes()) buffer.append(b, 8);
                }
                break;
            }
        }
    }
    buffer.append(0, std::min(4, capacity - buffer.count()));
    buffer.append(0, (8 - buffer.count() % 8) % 8);
    int pad = 0;
    while (buffer.count() < capacity) buffer.append((pad++ % 2) == 0 ? 0xECU : 0x11U, 8);
    return std::move(buffer).finish();
}
} // namespace specqr::detail

namespace specqr {
void Segment::swap(Segment& other) noexcept {
    using std::swap;
    swap(mode_, other.mode_); swap(text_, other.text_); swap(bytes_, other.bytes_);
    swap(characters_, other.characters_); swap(byte_count_, other.byte_count_);
    swap(assignment_, other.assignment_); swap(indicator_, other.indicator_); swap(header_, other.header_);
}
Segment::Segment(Segment&& other) noexcept : Segment(Mode::Byte) { swap(other); }
Segment& Segment::operator=(Segment&& other) noexcept {
    if (this != &other) { Segment moved(std::move(other)); swap(moved); }
    return *this;
}
Segment Segment::from_text(Mode mode, std::string_view text) {
    const auto scalars = detail::decode_utf8(text);
    for (const auto& scalar : scalars) {
        if (mode == Mode::Numeric && (scalar.value < '0' || scalar.value > '9')) throw Error("INVALID_MODE", "Numeric mode permits only ASCII digits 0-9.");
        if (mode == Mode::Alphanumeric && detail::alpha_value(scalar.value) < 0) throw Error("INVALID_MODE", "Unsupported character in alphanumeric segment.");
        if (mode == Mode::Kanji && !detail::kanji_value(scalar.value)) throw Error("INVALID_MODE", "Kanji mode requires characters in the QR Shift_JIS ranges.");
    }
    Segment result(mode);
    result.text_ = std::string(text);
    result.characters_ = static_cast<int>(scalars.size());
    result.byte_count_ = mode == Mode::Kanji ? result.characters_ * 2 : static_cast<int>(text.size());
    if (mode == Mode::Byte) result.bytes_.assign(text.begin(), text.end());
    return result;
}
Segment Segment::numeric(std::string_view text) { return from_text(Mode::Numeric, text); }
Segment Segment::alphanumeric(std::string_view text) { return from_text(Mode::Alphanumeric, text); }
Segment Segment::byte(std::string_view text) { return from_text(Mode::Byte, text); }
Segment Segment::kanji(std::string_view text) { return from_text(Mode::Kanji, text); }
Segment Segment::byte(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > max_input_bytes) throw Error("INVALID_INPUT", "Binary input exceeds the 1,000,000 byte resource limit.");
    Segment result(Mode::Byte); result.bytes_ = bytes; result.byte_count_ = static_cast<int>(bytes.size()); return result;
}
Segment Segment::eci(int assignment) {
    if (assignment < 0 || assignment > 999999) throw Error("INVALID_ECI", "ECI assignment must be in 0...999999.");
    Segment result(Mode::Eci); result.assignment_ = assignment; return result;
}
Segment Segment::fnc1() { return Segment(Mode::Fnc1); }
Segment Segment::fnc1_second(std::string_view indicator) {
    (void)detail::application_indicator(indicator);
    Segment result(Mode::Fnc1Second); result.indicator_ = std::string(indicator); return result;
}
Segment Segment::structured_append(int index, int total, std::uint8_t parity) {
    StructuredAppendHeader header{index, total, parity};
    if (!header.valid()) throw Error("INVALID_MODE", "Structured Append requires total 2...16 and one-based index 1...total.");
    Segment result(Mode::StructuredAppend); result.header_ = header; return result;
}
bool Segment::is_control() const noexcept { return !detail::is_data(mode_); }
int Segment::bit_length(int version) const {
    detail::validate_version(version);
    switch (mode_) {
        case Mode::Eci: return *assignment_ < 128 ? 12 : *assignment_ < 16384 ? 20 : 28;
        case Mode::Fnc1: return 4;
        case Mode::Fnc1Second: return 12;
        case Mode::StructuredAppend: return 20;
        default: return 4 + detail::count_bits(mode_, version) + detail::payload_length(mode_, mode_ == Mode::Byte ? byte_count_ : characters_);
    }
}
} // namespace specqr
