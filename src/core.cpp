#include "internal.hpp"
#include <specqr/gs1.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace specqr {
Error::Error(std::string code, std::string message) : std::invalid_argument(std::move(message)), code_(std::move(code)) {}
Error::Error(int required_bits, int capacity_bits, int version)
    : Error("DATA_TOO_LONG", "Input requires " + std::to_string(required_bits) + " bits, but version " + std::to_string(version) + " has " + std::to_string(capacity_bits) + " data bits.") {
    required_ = required_bits; capacity_ = capacity_bits; version_ = version;
}
int StructuredAppendHeader::sequence_indicator() const {
    if (!valid()) throw Error("INVALID_MODE", "Invalid Structured Append header.");
    return (index - 1) * 16 + total - 1;
}
const char* mode_name(Mode mode) noexcept {
    switch (mode) {
        case Mode::Auto: return "auto";
        case Mode::Numeric: return "numeric";
        case Mode::Alphanumeric: return "alphanumeric";
        case Mode::Byte: return "byte";
        case Mode::Kanji: return "kanji";
        case Mode::Eci: return "eci";
        case Mode::Fnc1: return "fnc1";
        case Mode::Fnc1Second: return "fnc1-second";
        case Mode::StructuredAppend: return "structured-append";
        case Mode::Mixed: return "mixed";
        default: return "unknown";
    }
}
const char* ecc_name(Ecc ecc) noexcept {
    switch (ecc) { case Ecc::L: return "L"; case Ecc::M: return "M"; case Ecc::Q: return "Q"; case Ecc::H: return "H"; default: return "unknown"; }
}
Mode Plan::mode() const noexcept {
    std::optional<Mode> found;
    for (const auto& segment : segments) {
        if (segment.is_control()) continue;
        if (found && *found != segment.mode()) return Mode::Mixed;
        found = segment.mode();
    }
    return found.value_or(Mode::Byte);
}
std::optional<int> Plan::eci_assignment_number() const noexcept {
    for (const auto& segment : segments) if (segment.assignment_number()) return segment.assignment_number();
    return {};
}
std::optional<StructuredAppendHeader> Plan::structured_append() const noexcept {
    for (const auto& segment : segments) if (segment.header()) return segment.header();
    return {};
}
bool Plan::gs1() const noexcept {
    for (const auto& segment : segments) if (segment.mode() == Mode::Fnc1) return true;
    return false;
}
std::optional<std::string> Plan::fnc1_second() const {
    for (const auto& segment : segments) if (segment.application_indicator()) return segment.application_indicator();
    return {};
}
std::vector<SegmentDiagnostics> Plan::control_segments() const {
    std::vector<SegmentDiagnostics> result;
    for (const auto& segment : segment_diagnostics) if (!detail::is_data(segment.mode)) result.push_back(segment);
    return result;
}
namespace {
SegmentDiagnostics diagnostics_for(const Segment& segment, int version) {
    return {segment.mode(), segment.character_count(), segment.byte_count(), segment.bit_length(version), segment.assignment_number(), segment.application_indicator(),
        segment.application_indicator() ? std::optional<int>{detail::application_indicator(*segment.application_indicator())} : std::nullopt, segment.header()};
}
using SegmentFactory = std::function<const std::vector<Segment>&(int)>;
Plan plan_input(const Options& options, int input_bytes, const std::optional<Gs1ValidationDiagnostics>& gs1_validation, const SegmentFactory& create) {
    const int lower = options.version.value_or(options.min_version);
    const int upper = options.version.value_or(options.max_version);
    for (int version = lower; version <= upper; ++version) {
        const auto& segments = create(version);
        int bits = 0;
        bool counts_fit = true;
        for (const auto& segment : segments) {
            bits += segment.bit_length(version);
            if (!segment.is_control()) {
                const int count = segment.mode() == Mode::Byte ? segment.byte_count() : segment.character_count();
                if (count >= (1 << detail::count_bits(segment.mode(), version))) counts_fit = false;
            }
        }
        const bool fits = counts_fit && bits <= detail::data_codewords(version, options.ecc) * 8;
        if (!fits && version != upper) continue;
        auto ecc = options.ecc;
        if (fits && options.boost_ecc) {
            for (int candidate = static_cast<int>(ecc) + 1; candidate <= static_cast<int>(Ecc::H); ++candidate) {
                const auto level = static_cast<Ecc>(candidate);
                if (bits <= detail::data_codewords(version, level) * 8) ecc = level;
            }
        }
        Plan result;
        result.ok = fits;
        if (fits || options.version) result.selected_version = version;
        result.evaluated_version = version;
        result.min_version = options.min_version; result.max_version = options.max_version;
        result.ecc = ecc; result.requested_ecc = options.ecc;
        result.version_selection = options.version ? "fixed" : fits ? "auto-minimum" : "auto-range";
        if (options.version) result.version_selection_reason = "Version " + std::to_string(version) + " was requested explicitly.";
        else if (fits) result.version_selection_reason = "Version " + std::to_string(version) + " is the smallest version in " + std::to_string(lower) + ".." + std::to_string(upper) + " that fits at error correction " + ecc_name(options.ecc) + ".";
        else result.version_selection_reason = "No version in " + std::to_string(lower) + ".." + std::to_string(upper) + " fits at error correction " + ecc_name(options.ecc) + "; capacity is reported for version " + std::to_string(version) + ".";
        result.segments = segments;
        result.segment_diagnostics.reserve(segments.size());
        for (const auto& segment : segments) result.segment_diagnostics.push_back(diagnostics_for(segment, version));
        result.data_bit_length = bits; result.capacity_bits = detail::data_codewords(version, ecc) * 8; result.input_bytes = input_bytes;
        if (gs1_validation) result.gs1_validation = *gs1_validation;
        else if (result.gs1()) { result.gs1_validation.enabled = true; result.gs1_validation.element_count = std::nullopt; }
        return result;
    }
    throw Error("INVALID_VERSION", "Empty version range.");
}
}

Plan QRCode::estimate(std::string_view text, const Options& options) {
    detail::validate_options(options);
    const auto scalars = detail::decode_utf8(text);
    std::optional<Gs1ValidationDiagnostics> gs1_validation;
    if (options.gs1) {
        const auto parsed = gs1::parse_element_string(text);
        Gs1ValidationDiagnostics diagnostics;
        diagnostics.enabled = true;
        diagnostics.element_count = static_cast<int>(parsed.elements.size());
        diagnostics.has_separators = parsed.has_separators;
        for (const auto& element : parsed.elements) diagnostics.ais.push_back(element.ai);
        gs1_validation = std::move(diagnostics);
    }
    auto encoding_mode = options.mode;
    if ((options.gs1 || options.fnc1_second) && text.find('%') != std::string_view::npos) {
        if (encoding_mode == Mode::Alphanumeric) throw Error(options.gs1 ? "INVALID_GS1" : "INVALID_MODE", "Literal percent in high-level FNC1 text requires Byte or Auto mode.");
        if (encoding_mode == Mode::Auto) encoding_mode = Mode::Byte;
    }
    const int limit_version = options.version.value_or(options.max_version);
    const int minimum_count_bits = std::min({detail::count_bits(Mode::Numeric, limit_version), detail::count_bits(Mode::Alphanumeric, limit_version), detail::count_bits(Mode::Byte, limit_version), options.eci ? 32 : detail::count_bits(Mode::Kanji, limit_version)});
    int control_bits = 0;
    for (const auto& segment : detail::applying_options({}, options)) control_bits += segment.bit_length(limit_version);
    const bool preflight_overflow = 4 + minimum_count_bits + detail::payload_length(Mode::Numeric, static_cast<int>(scalars.size())) + control_bits > detail::data_codewords(limit_version, options.ecc) * 8;
    std::array<std::optional<std::vector<Segment>>, 3> cache;
    return plan_input(options, static_cast<int>(text.size()), gs1_validation, [&](int version) -> const std::vector<Segment>& {
        const auto band = static_cast<std::size_t>(version < 10 ? 0 : version < 27 ? 1 : 2);
        if (!cache[band]) {
            auto segments = encoding_mode != Mode::Auto || !options.optimize_segments || preflight_overflow
                ? detail::single_segment(text, encoding_mode, !options.eci)
                : detail::optimize(text, version, !options.eci);
            cache[band] = detail::applying_options(std::move(segments), options);
        }
        return *cache[band];
    });
}

Plan QRCode::estimate(const std::vector<std::uint8_t>& bytes, const Options& options) {
    detail::validate_options(options);
    if (bytes.size() > max_input_bytes) throw Error("INVALID_INPUT", "Binary input exceeds the 1,000,000 byte resource limit.");
    if (options.gs1) throw Error("INVALID_GS1", "GS1 input must be a raw element string, not binary input.");
    if (options.mode != Mode::Auto && options.mode != Mode::Byte) throw Error("INVALID_MODE", "Binary input requires Byte or Auto mode.");
    const auto prepared = detail::applying_options({Segment::byte(bytes)}, options);
    return plan_input(options, static_cast<int>(bytes.size()), {}, [&](int) -> const std::vector<Segment>& { return prepared; });
}
Plan QRCode::analyze_segments(const std::vector<Segment>& segments, const Options& options) {
    detail::validate_options(options); detail::validate_controls(segments);
    int input_bytes = 0;
    for (const auto& segment : segments) {
        input_bytes += segment.byte_count();
        if ((options.gs1 || options.fnc1_second) && segment.mode() == Mode::Alphanumeric && segment.text().find('%') != std::string::npos)
            throw Error(options.gs1 ? "INVALID_GS1" : "INVALID_MODE", "FNC1 options with percent in an alphanumeric segment are ambiguous; use Byte or explicit low-level FNC1 with QR escaping.");
    }
    const auto prepared = detail::applying_options(segments, options);
    return plan_input(options, input_bytes, {}, [&](int) -> const std::vector<Segment>& { return prepared; });
}
Capacity QRCode::capacity(int version, Ecc ecc, std::optional<Mode> mode, int control_bits) {
    detail::validate_version(version); detail::validate_ecc(ecc);
    if (control_bits < 0) throw Error("INVALID_INPUT", "Control bits must be nonnegative.");
    if (mode && !detail::is_data(*mode)) throw Error("INVALID_MODE", "Capacity mode must be Numeric, Alphanumeric, Byte, or Kanji.");
    Capacity result{version, ecc, detail::data_codewords(version, ecc), detail::raw_codewords(version), mode, {}, control_bits, {}, {}, {}};
    if (mode) {
        result.character_count_bits = detail::count_bits(*mode, version);
        const int capacity_bits = result.data_codewords * 8; // Validated QR capacity, at most 23,648 bits.
        const int payload = std::max(0, capacity_bits - std::min(control_bits, capacity_bits) - 4 - *result.character_count_bits);
        result.payload_bits = payload;
        int maximum = 0;
        switch (*mode) {
            case Mode::Numeric: maximum = payload / 10 * 3 + (payload % 10 >= 7 ? 2 : payload % 10 >= 4 ? 1 : 0); break;
            case Mode::Alphanumeric: maximum = payload / 11 * 2 + (payload % 11 >= 6 ? 1 : 0); break;
            case Mode::Byte: maximum = payload / 8; break;
            case Mode::Kanji: maximum = payload / 13; break;
            default: break;
        }
        maximum = std::min(maximum, (1 << *result.character_count_bits) - 1);
        if (*mode == Mode::Byte) result.max_bytes = maximum;
        else result.max_characters = maximum;
    }
    return result;
}
QRCode QRCode::generate(std::string_view text, const Options& options) { return build(estimate(text, options), options); }
QRCode QRCode::generate(const std::vector<std::uint8_t>& bytes, const Options& options) { return build(estimate(bytes, options), options); }
QRCode QRCode::generate_segments(const std::vector<Segment>& segments, const Options& options) { return build(analyze_segments(segments, options), options); }
QRCode QRCode::build(Plan plan, const Options& options) {
    if (!plan.ok) throw Error(plan.data_bit_length, plan.capacity_bits, plan.evaluated_version);
    QRCode result;
    result.data_ = detail::encode(plan.segments, plan.evaluated_version, plan.ecc);
    result.codewords_ = detail::interleave(result.data_, plan.evaluated_version, plan.ecc);
    auto built = detail::build_matrix(result.codewords_, plan.evaluated_version, plan.ecc, options.mask);
    result.matrix_ = std::move(built.matrix);
    auto& diagnostics = result.diagnostics_;
    diagnostics.planning = std::move(plan);
    diagnostics.mask = built.mask; diagnostics.mask_penalty = built.penalty; diagnostics.mask_penalties = std::move(built.penalties);
    diagnostics.data_codewords = static_cast<int>(result.data_.size());
    diagnostics.total_codewords = static_cast<int>(result.codewords_.size());
    diagnostics.error_correction_codewords = diagnostics.total_codewords - diagnostics.data_codewords;
    diagnostics.mask_selection_reason = options.mask ? "Mask " + std::to_string(*options.mask) + " was requested explicitly."
        : "Mask " + std::to_string(built.mask) + " had the lowest penalty (" + std::to_string(built.penalty) + ") among the evaluated masks.";
    return result;
}
bool QRCode::module(int x, int y) const {
    if (x < 0 || y < 0 || static_cast<std::size_t>(y) >= matrix_.size() || static_cast<std::size_t>(x) >= matrix_[static_cast<std::size_t>(y)].size()) throw Error("INVALID_INPUT", "Module coordinates are outside the symbol.");
    return matrix_[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
}
} // namespace specqr
