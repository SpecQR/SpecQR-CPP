#include <specqr/structured_append.hpp>
#include "internal.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace specqr {
namespace {
constexpr std::size_t maximum_manual_segments = 32768;
[[noreturn]] void fail(const char* code, const char* message) { throw Error(code, message); }
void check_budget(std::size_t bytes) {
    if (bytes > max_input_bytes) fail("INVALID_INPUT", "Structured Append data exceeds the 1,000,000-byte resource budget.");
}
void validate_text(std::string_view text) {
    check_budget(text.size());
    (void)detail::decode_utf8(text);
}
void validate_options(const StructuredAppendOptions& options, bool manual) {
    const auto& qr = options.qr_options;
    detail::validate_options(qr);
    if (options.max_symbols < 2 || options.max_symbols > 16) fail("INVALID_MODE", "Max symbols must be 2–16.");
    if (options.split_units != SplitUnitsDetail::Summary && options.split_units != SplitUnitsDetail::Full)
        fail("INVALID_INPUT", "Unknown split-unit detail.");
    if (!manual && options.split_units != SplitUnitsDetail::Summary)
        fail("INVALID_INPUT", "Full split-unit diagnostics require manual segments.");
    if (manual && (qr.mode != Mode::Auto || !qr.optimize_segments))
        fail("INVALID_MODE", "Manual Structured Append preserves its source segment modes.");
    if (qr.gs1) fail("INVALID_GS1", "High-level Structured Append cannot be combined with GS1.");
    if (qr.eci || qr.fnc1_second || qr.structured_append || qr.boost_ecc)
        fail("INVALID_MODE", "High-level Structured Append owns its headers and does not support ECI, FNC1 or ECC boosting.");
}
int canonical_length(const Segment& segment) {
    return static_cast<int>(segment.text().empty() ? segment.bytes().size() : segment.text().size());
}
void validate_segments(const std::vector<Segment>& segments) {
    if (segments.empty() || segments.size() > maximum_manual_segments)
        fail("INVALID_INPUT", "Structured Append requires 1–32,768 nonempty data segments.");
    std::size_t total_bytes = 0;
    for (const auto& segment : segments) {
        if (segment.mode() == Mode::Fnc1 || segment.mode() == Mode::Fnc1Second)
            fail("INVALID_GS1", "Structured Append cannot be combined with FNC1.");
        if (segment.is_control()) fail("INVALID_MODE", "High-level Structured Append accepts data segments only.");
        const int size = canonical_length(segment);
        if (size == 0) fail("INVALID_INPUT", "Structured Append segments must be nonempty.");
        total_bytes += static_cast<std::size_t>(size);
        if (total_bytes > max_input_bytes) fail("DATA_TOO_LONG", "Manual data exceeds the Structured Append resource budget.");
    }
}
[[noreturn]] void too_long() { fail("DATA_TOO_LONG", "Input cannot be split into 2–max_symbols symbols in the selected version range."); }
int single_bits(Mode mode, int count, int version) { return 4 + detail::count_bits(mode, version) + detail::payload_length(mode, count); }

struct TextIndex {
    std::string text;
    std::vector<detail::Scalar> scalars;
    std::vector<int> offsets;
    std::optional<Mode> uniform;
    explicit TextIndex(std::string_view source) : text(source), scalars(detail::decode_utf8(source)) {
        offsets.reserve(scalars.size() + 1);
        bool numeric = true, alpha_non_numeric = true, kanji = true, only_byte = true;
        for (const auto& scalar : scalars) {
            offsets.push_back(static_cast<int>(scalar.offset));
            const bool n = scalar.value >= '0' && scalar.value <= '9';
            const bool a = detail::alpha_value(scalar.value) >= 0;
            const bool k = detail::kanji_value(scalar.value).has_value();
            numeric = numeric && n; alpha_non_numeric = alpha_non_numeric && a && !n;
            kanji = kanji && k; only_byte = only_byte && !a && !k;
        }
        offsets.push_back(static_cast<int>(text.size()));
        if (numeric) uniform = Mode::Numeric;
        else if (alpha_non_numeric) uniform = Mode::Alphanumeric;
        else if (kanji) uniform = Mode::Kanji;
        else if (only_byte) uniform = Mode::Byte;
    }
    int count() const { return static_cast<int>(scalars.size()); }
    int byte_count(int start, int length) const { return offsets[static_cast<std::size_t>(start + length)] - offsets[static_cast<std::size_t>(start)]; }
    std::string slice(int start, int length) const {
        return text.substr(static_cast<std::size_t>(offsets[static_cast<std::size_t>(start)]), static_cast<std::size_t>(byte_count(start, length)));
    }
};
class TextBitTracker {
    static constexpr int infinity = std::numeric_limits<int>::max() / 4;
    std::array<int, 7> costs_{{infinity, infinity, infinity, infinity, infinity, infinity, infinity}};
    int best_ = 0, version_;
public:
    explicit TextBitTracker(int version) : version_(version) {}
    int append(const detail::Scalar& scalar) {
        std::array<int, 7> next{{infinity, infinity, infinity, infinity, infinity, infinity, infinity}};
        if (scalar.value >= '0' && scalar.value <= '9') {
            next[0] = costs_[2] + 3;
            next[1] = std::min(costs_[0] + 4, best_ + 8 + detail::count_bits(Mode::Numeric, version_));
            next[2] = costs_[1] + 3;
        }
        if (detail::alpha_value(scalar.value) >= 0) {
            next[3] = costs_[4] + 5;
            next[4] = std::min(costs_[3] + 6, best_ + 10 + detail::count_bits(Mode::Alphanumeric, version_));
        }
        if (detail::kanji_value(scalar.value)) next[5] = std::min(costs_[5], best_ + 4 + detail::count_bits(Mode::Kanji, version_)) + 13;
        next[6] = std::min(costs_[6], best_ + 4 + detail::count_bits(Mode::Byte, version_)) + scalar.bytes * 8;
        costs_ = next; best_ = *std::min_element(next.begin(), next.end()); return best_;
    }
};
struct Descriptor {
    Segment segment;
    int source_index, byte_start, byte_length, unit_start, unit_count;
    std::optional<TextIndex> text;
    Descriptor(const Segment& s, int index, int byte_offset, int unit_offset)
        : segment(s), source_index(index), byte_start(byte_offset), byte_length(canonical_length(s)),
          unit_start(unit_offset), unit_count(1) {
        if (s.mode() == Mode::Byte && !s.text().empty()) { text.emplace(s.text()); unit_count = text->count(); }
        else if (s.mode() == Mode::Byte) unit_count = static_cast<int>(s.bytes().size());
    }
    std::optional<std::pair<int, int>> overlap(int start, int length) const {
        const int lo = std::max(start, unit_start), hi = std::min(start + length, unit_start + unit_count);
        if (lo >= hi) return std::nullopt;
        return std::make_pair(lo - unit_start, hi - lo);
    }
    int byte_offset(int unit) const { return byte_start + (text ? text->offsets[static_cast<std::size_t>(unit)] : segment.mode() == Mode::Byte ? unit : 0); }
    int byte_count(int start, int length) const { return text ? text->byte_count(start, length) : segment.mode() == Mode::Byte ? length : byte_length; }
    int bits(int start, int length, int version) const { return segment.mode() == Mode::Byte ? single_bits(Mode::Byte, byte_count(start, length), version) : segment.bit_length(version); }
    Segment materialize(int start, int length) const {
        if (text) return Segment::byte(text->slice(start, length));
        if (segment.mode() == Mode::Byte) return Segment::byte(std::vector<std::uint8_t>(segment.bytes().begin() + start, segment.bytes().begin() + start + length));
        return segment;
    }
};
struct Chunk {
    std::optional<std::string> text;
    std::optional<std::vector<std::uint8_t>> bytes;
    std::vector<Segment> segments;
    int byte_start = 0, byte_length = 0;
    std::optional<int> segment_start, segment_end;
};
struct Source {
    std::optional<TextIndex> text;
    std::optional<std::vector<std::uint8_t>> binary;
    std::vector<Descriptor> descriptors;
    int unit_count = 0, byte_length = 0;
    bool manual = false;
    explicit Source(std::string_view source) : text(std::in_place, source), unit_count(text->count()), byte_length(static_cast<int>(source.size())) {}
    explicit Source(const std::vector<std::uint8_t>& source) : binary(source), unit_count(static_cast<int>(source.size())), byte_length(unit_count) {}
    explicit Source(const std::vector<Segment>& source) : manual(true) {
        descriptors.reserve(source.size());
        for (std::size_t i = 0; i < source.size(); ++i) {
            descriptors.emplace_back(source[i], static_cast<int>(i), byte_length, unit_count);
            unit_count += descriptors.back().unit_count; byte_length += descriptors.back().byte_length;
        }
    }
    int input_length() const { return manual ? static_cast<int>(descriptors.size()) : unit_count; }
    int data_bits(int start, int length, const Options& options, int version) const {
        if (binary) return single_bits(Mode::Byte, length, version);
        if (text) {
            const auto uniform = options.mode == Mode::Auto ? text->uniform : std::optional<Mode>(options.mode);
            if (uniform) return single_bits(*uniform, *uniform == Mode::Byte ? text->byte_count(start, length) : length, version);
            if (options.optimize_segments) {
                TextBitTracker tracker(version); int bits = 0;
                for (int i = start; i < start + length; ++i) bits = tracker.append(text->scalars[static_cast<std::size_t>(i)]);
                return bits;
            }
            int bits = 0;
            for (const auto& segment : detail::single_segment(text->slice(start, length), Mode::Auto, true)) bits += segment.bit_length(version);
            return bits;
        }
        int bits = 0;
        for (const auto& d : descriptors) if (const auto span = d.overlap(start, length)) bits += d.bits(span->first, span->second, version);
        return bits;
    }
    std::optional<int> auto_prefix(int start, int max_length, const Options& options, int version, int capacity) const {
        if (!text || options.mode != Mode::Auto || text->uniform) return std::nullopt;
        TextBitTracker tracker(version);
        int best = 0, bytes = 0;
        bool numeric = true, alpha = true, kanji = true;
        for (int offset = 0; offset < max_length; ++offset) {
            const auto& scalar = text->scalars[static_cast<std::size_t>(start + offset)];
            int bits;
            if (options.optimize_segments) bits = tracker.append(scalar);
            else {
                numeric = numeric && scalar.value >= '0' && scalar.value <= '9';
                alpha = alpha && detail::alpha_value(scalar.value) >= 0;
                kanji = kanji && detail::kanji_value(scalar.value).has_value();
                bytes += scalar.bytes;
                const Mode mode = numeric ? Mode::Numeric : alpha ? Mode::Alphanumeric : kanji ? Mode::Kanji : Mode::Byte;
                bits = single_bits(mode, mode == Mode::Byte ? bytes : offset + 1, version);
            }
            if (bits > capacity) break;
            best = offset + 1;
        }
        return best;
    }
    Chunk materialize(int start, int length) const {
        Chunk chunk;
        if (text) { chunk.text = text->slice(start, length); chunk.byte_start = text->offsets[static_cast<std::size_t>(start)]; chunk.byte_length = text->byte_count(start, length); }
        else if (binary) { chunk.bytes.emplace(binary->begin() + start, binary->begin() + start + length); chunk.byte_start = start; chunk.byte_length = length; }
        else for (const auto& d : descriptors) if (const auto span = d.overlap(start, length)) {
            chunk.segments.push_back(d.materialize(span->first, span->second));
            if (!chunk.segment_start) { chunk.segment_start = d.source_index; chunk.byte_start = d.byte_offset(span->first); }
            chunk.segment_end = d.source_index + 1;
            chunk.byte_length += d.byte_count(span->first, span->second);
        }
        return chunk;
    }
    std::vector<StructuredAppendSplitUnit> split_units() const {
        std::vector<StructuredAppendSplitUnit> units;
        units.reserve(static_cast<std::size_t>(unit_count));
        for (const auto& d : descriptors) for (int unit = 0; unit < d.unit_count; ++unit)
            units.push_back({d.source_index, d.segment.mode(), unit, d.segment.mode() == Mode::Byte ? 1 : d.segment.character_count(), d.byte_offset(unit), d.byte_count(unit, 1)});
        return units;
    }
};
enum class AttemptKind { Single, TooLong, Split };
struct Attempt { AttemptKind kind; std::vector<std::pair<int, int>> ranges; };
Attempt try_split(const Source& source, const StructuredAppendOptions& options, int version) {
    const int capacity = detail::data_codewords(version, options.qr_options.ecc) * 8 - 20;
    const auto fits = [&](int start, int length) {
        // Numeric is the cheapest possible text encoding; bound expensive mixed analysis.
        if (source.text && detail::payload_length(Mode::Numeric, length) + 4 + std::min({detail::count_bits(Mode::Numeric, version), detail::count_bits(Mode::Alphanumeric, version), detail::count_bits(Mode::Byte, version), detail::count_bits(Mode::Kanji, version)}) > capacity) return false;
        return source.data_bits(start, length, options.qr_options, version) <= capacity;
    };
    if (fits(0, source.unit_count)) return {AttemptKind::Single, {}};
    if (source.unit_count < 2) return {AttemptKind::TooLong, {}};
    Attempt result{AttemptKind::Split, {}};
    int position = 0;
    while (position < source.unit_count) {
        if (result.ranges.size() == static_cast<std::size_t>(options.max_symbols)) return {AttemptKind::TooLong, {}};
        const int max_length = source.unit_count - position - (result.ranges.empty() ? 1 : 0);
        const auto prefix = source.auto_prefix(position, max_length, options.qr_options, version, capacity);
        int best = prefix.value_or(0);
        if (!prefix) {
            int low = 1, high = max_length;
            while (low <= high) {
                const int middle = low + (high - low) / 2;
                if (fits(position, middle)) { best = middle; low = middle + 1; }
                else high = middle - 1;
            }
        }
        if (best == 0) return {AttemptKind::TooLong, {}};
        result.ranges.emplace_back(position, best); position += best;
    }
    if (result.ranges.size() < 2) result.kind = AttemptKind::Single;
    return result;
}
void preflight(int count, int byte_count, Mode mode, const StructuredAppendOptions& options) {
    const int version = options.qr_options.version.value_or(options.qr_options.max_version);
    const int capacity = detail::data_codewords(version, options.qr_options.ecc) * 8;
    const std::int64_t payload = detail::payload_length(mode == Mode::Auto ? Mode::Numeric : mode, mode == Mode::Byte ? byte_count : count);
    const int count_bits = mode == Mode::Auto ? std::min({detail::count_bits(Mode::Numeric, version), detail::count_bits(Mode::Alphanumeric, version), detail::count_bits(Mode::Byte, version), detail::count_bits(Mode::Kanji, version)}) : detail::count_bits(mode, version);
    if (payload > static_cast<std::int64_t>(std::max(0, capacity - 24 - count_bits)) * options.max_symbols) too_long();
}
StructuredAppendResult build(const Source& source, std::uint8_t parity, const StructuredAppendOptions& options) {
    const auto& qr = options.qr_options;
    const int lower = qr.version.value_or(qr.min_version), upper = qr.version.value_or(qr.max_version);
    bool saw_too_long = false;
    Attempt selected{AttemptKind::TooLong, {}};
    int version = lower;
    for (; version <= upper; ++version) {
        auto attempt = try_split(source, options, version);
        if (attempt.kind == AttemptKind::Split) { selected = std::move(attempt); break; }
        saw_too_long = saw_too_long || attempt.kind == AttemptKind::TooLong;
    }
    if (selected.kind != AttemptKind::Split) {
        if (saw_too_long) too_long();
        fail("INVALID_INPUT", "Input fits one header-bearing symbol; use generate or the low-level Structured Append header.");
    }
    const int total = static_cast<int>(selected.ranges.size());
    StructuredAppendDiagnostics diagnostics{};
    diagnostics.version = version; diagnostics.ecc = qr.ecc;
    diagnostics.version_selection = qr.version ? "fixed" : "auto-minimum";
    diagnostics.version_selection_reason = qr.version ? "Version " + std::to_string(version) + " was requested explicitly." : "Version " + std::to_string(version) + " is the smallest version in " + std::to_string(lower) + ".." + std::to_string(upper) + " that splits the payload into " + std::to_string(total) + " symbols at " + ecc_name(qr.ecc) + ".";
    diagnostics.total = total; diagnostics.parity = parity; diagnostics.byte_length = source.byte_length;
    diagnostics.input_length = source.input_length(); diagnostics.max_symbols = options.max_symbols;
    diagnostics.split_strategy = source.manual ? "segment-boundary-byte-chunk" : "greedy-largest-fitting";
    if (source.manual) {
        diagnostics.segment_count = source.input_length(); diagnostics.split_unit_count = source.unit_count; diagnostics.split_units_detail = options.split_units;
        if (options.split_units == SplitUnitsDetail::Full) diagnostics.split_units = source.split_units();
    }
    std::vector<QRCode> symbols;
    symbols.reserve(selected.ranges.size()); diagnostics.symbols.reserve(selected.ranges.size());
    for (int i = 0; i < total; ++i) {
        const auto [start, length] = selected.ranges[static_cast<std::size_t>(i)];
        const auto chunk = source.materialize(start, length);
        Options symbol_options = qr;
        symbol_options.version = version; symbol_options.min_version = version; symbol_options.max_version = version;
        symbol_options.structured_append = StructuredAppendHeader{i + 1, total, parity};
        auto code = chunk.text ? QRCode::generate(*chunk.text, symbol_options) : chunk.bytes ? QRCode::generate(*chunk.bytes, symbol_options) : QRCode::generate_segments(chunk.segments, symbol_options);
        const auto& plan = code.planning();
        diagnostics.symbols.push_back({i + 1, total, parity, source.manual ? std::nullopt : std::optional<int>(start), source.manual ? std::nullopt : std::optional<int>(length), chunk.segment_start, chunk.segment_end, source.manual ? std::optional<int>(start) : std::nullopt, source.manual ? std::optional<int>(length) : std::nullopt, chunk.byte_start, chunk.byte_length, version, code.ecc(), plan.data_bit_length, plan.capacity_bits, static_cast<int>(plan.remaining_bits()), code.mask()});
        symbols.push_back(std::move(code));
    }
    if (total == options.max_symbols) diagnostics.warnings.push_back({"STRUCTURED_APPEND_MAX_SYMBOLS_NEAR_LIMIT", "info", "The set uses the configured maximum number of symbols.", total, options.max_symbols});
    if (options.diagnostics) diagnostics.warnings.push_back({"STRUCTURED_APPEND_DECODER_SUPPORT_VARIES", "info", "Decoder APIs vary in their exposure of Structured Append metadata.", total, std::nullopt});
    return StructuredAppendResult(std::move(symbols), std::move(diagnostics));
}
} // namespace

StructuredAppendResult::StructuredAppendResult(std::vector<QRCode> symbols, StructuredAppendDiagnostics diagnostics)
    : symbols_(std::move(symbols)), diagnostics_(std::move(diagnostics)) {}
std::uint8_t structured_append_parity(std::string_view text) {
    validate_text(text);
    std::uint8_t parity = 0;
    for (char byte : text) parity ^= static_cast<std::uint8_t>(byte);
    return parity;
}
std::uint8_t structured_append_parity(const std::vector<std::uint8_t>& bytes) {
    check_budget(bytes.size());
    std::uint8_t parity = 0;
    for (auto byte : bytes) parity ^= byte;
    return parity;
}
std::uint8_t structured_append_segments_parity(const std::vector<Segment>& segments) {
    validate_segments(segments);
    std::uint8_t parity = 0;
    for (const auto& segment : segments) parity ^= segment.text().empty() ? structured_append_parity(segment.bytes()) : structured_append_parity(segment.text());
    return parity;
}
StructuredAppendResult generate_structured_append(std::string_view text, const StructuredAppendOptions& options) {
    validate_options(options, false); validate_text(text);
    if (text.empty()) fail("INVALID_INPUT", "Structured Append input must be nonempty.");
    if (options.qr_options.mode != Mode::Auto) (void)detail::single_segment(text, options.qr_options.mode, true);
    const Source source(text);
    preflight(source.unit_count, source.byte_length, options.qr_options.mode, options);
    return build(source, structured_append_parity(text), options);
}
StructuredAppendResult generate_structured_append(const std::vector<std::uint8_t>& bytes, const StructuredAppendOptions& options) {
    validate_options(options, false); check_budget(bytes.size());
    if (options.qr_options.mode != Mode::Auto && options.qr_options.mode != Mode::Byte) fail("INVALID_MODE", "Binary input requires byte or auto mode.");
    if (bytes.empty()) fail("INVALID_INPUT", "Structured Append input must be nonempty.");
    preflight(static_cast<int>(bytes.size()), static_cast<int>(bytes.size()), Mode::Byte, options);
    return build(Source(bytes), structured_append_parity(bytes), options);
}
StructuredAppendResult generate_segments_structured_append(const std::vector<Segment>& segments, const StructuredAppendOptions& options) {
    validate_options(options, true); validate_segments(segments);
    const int version = options.qr_options.version.value_or(options.qr_options.max_version);
    std::int64_t required = 0;
    for (const auto& segment : segments) required += segment.bit_length(version);
    const std::int64_t available = static_cast<std::int64_t>(options.max_symbols) * std::max(0, detail::data_codewords(version, options.qr_options.ecc) * 8 - 20);
    if (required > available) too_long();
    return build(Source(segments), structured_append_segments_parity(segments), options);
}
StructuredAppendPart::StructuredAppendPart(int index, int total, std::uint8_t parity, std::string_view text)
    : index_(index), total_(total), parity_(parity) {
    validate_text(text); text_ = std::string(text); bytes_.assign(text.begin(), text.end());
}
StructuredAppendPart::StructuredAppendPart(int index, int total, std::uint8_t parity, const std::vector<std::uint8_t>& bytes)
    : index_(index), total_(total), parity_(parity) { check_budget(bytes.size()); bytes_ = bytes; }
StructuredAppendMergeResult::StructuredAppendMergeResult(std::optional<std::string> text, std::vector<std::uint8_t> bytes,
        std::vector<StructuredAppendPartMetadata> parts, StructuredAppendMergeDiagnostics diagnostics)
    : text_(std::move(text)), bytes_(std::move(bytes)), parts_(std::move(parts)), diagnostics_(std::move(diagnostics)) {}
StructuredAppendMergeResult merge_structured_append_parts(const std::vector<StructuredAppendPart>& parts) {
    if (parts.size() < 2 || parts.size() > 16) fail("INVALID_INPUT", "Structured Append requires 2–16 parts.");
    const auto& first = parts.front();
    std::array<bool, 16> seen{};
    std::size_t byte_length = 0;
    std::vector<const StructuredAppendPart*> sorted;
    sorted.reserve(parts.size());
    for (const auto& part : parts) {
        if (part.total() < 2 || part.total() > 16 || part.index() < 1 || part.index() > part.total())
            fail("INVALID_INPUT", "Structured Append indices are one-based; total must be 2–16.");
        if (part.total() != first.total() || part.parity() != first.parity() || part.text().has_value() != first.text().has_value())
            fail("INVALID_INPUT", "Structured Append total, parity and data type must agree.");
        if (seen[static_cast<std::size_t>(part.index() - 1)]) fail("INVALID_INPUT", "Duplicate Structured Append part index.");
        seen[static_cast<std::size_t>(part.index() - 1)] = true;
        byte_length += part.bytes().size(); check_budget(byte_length); sorted.push_back(&part);
    }
    if (parts.size() != static_cast<std::size_t>(first.total())) fail("INVALID_INPUT", "Structured Append set has missing parts.");
    std::sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) { return left->index() < right->index(); });
    std::vector<std::uint8_t> bytes;
    bytes.reserve(byte_length);
    std::optional<std::string> text;
    if (first.text()) { text.emplace(); text->reserve(byte_length); }
    std::vector<StructuredAppendPartMetadata> metadata;
    metadata.reserve(parts.size());
    for (const auto* part : sorted) {
        bytes.insert(bytes.end(), part->bytes().begin(), part->bytes().end());
        if (text) *text += *part->text();
        metadata.push_back({part->index(), part->total(), part->parity(), part->text() ? "string" : "binary", static_cast<int>(part->bytes().size())});
    }
    const std::uint8_t actual = structured_append_parity(bytes);
    if (actual != first.parity()) fail("INVALID_INPUT", "Structured Append payload parity does not match metadata.");
    StructuredAppendMergeDiagnostics diagnostics{static_cast<int>(parts.size()), first.total(), first.parity(), first.text() ? "string" : "binary", static_cast<int>(byte_length), {}, {}, {first.parity(), actual}};
    return StructuredAppendMergeResult(std::move(text), std::move(bytes), std::move(metadata), std::move(diagnostics));
}
} // namespace specqr
