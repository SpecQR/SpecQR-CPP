#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace specqr {

inline constexpr std::size_t max_input_bytes = 1'000'000;
inline constexpr std::size_t max_segments = 65'536;

enum class Ecc { L, M, Q, H };
enum class Mode { Auto, Numeric, Alphanumeric, Byte, Kanji, Eci, Fnc1, Fnc1Second, StructuredAppend, Mixed };

class Error : public std::invalid_argument {
public:
    Error(std::string code, std::string message);
    Error(int required_bits, int capacity_bits, int version);
    const std::string& code() const noexcept { return code_; }
    std::optional<int> required_bits() const noexcept { return required_; }
    std::optional<int> capacity_bits() const noexcept { return capacity_; }
    std::optional<int> version() const noexcept { return version_; }
private:
    std::string code_;
    std::optional<int> required_, capacity_, version_;
};

struct StructuredAppendHeader {
    int index = 1; // One-based index.
    int total = 2;
    std::uint8_t parity = 0;
    bool valid() const noexcept { return total >= 2 && total <= 16 && index >= 1 && index <= total; }
    int sequence_indicator() const;
};

struct Options {
    Ecc ecc = Ecc::M;
    std::optional<int> version;
    int min_version = 1;
    int max_version = 40;
    std::optional<int> mask;
    Mode mode = Mode::Auto;
    bool optimize_segments = true;
    bool boost_ecc = false;
    std::optional<int> eci;
    bool gs1 = false;
    std::optional<std::string> fnc1_second;
    std::optional<StructuredAppendHeader> structured_append;
};

// Immutable value objects. Text arguments are strict UTF-8. Binary arguments are copied.
class Segment {
public:
    Segment(const Segment&) = default;
    Segment& operator=(const Segment&) = default;
    // Moving preserves the target and leaves the source as a valid empty Byte segment.
    Segment(Segment&& other) noexcept;
    Segment& operator=(Segment&& other) noexcept;
    static Segment numeric(std::string_view text);
    static Segment alphanumeric(std::string_view text);
    static Segment byte(std::string_view text);
    static Segment byte(const std::vector<std::uint8_t>& bytes);
    static Segment bytes(const std::vector<std::uint8_t>& bytes) { return byte(bytes); }
    static Segment kanji(std::string_view text);
    static Segment eci(int assignment);
    static Segment fnc1();
    static Segment fnc1_second(std::string_view indicator);
    static Segment structured_append(int index, int total, std::uint8_t parity);
    Mode mode() const noexcept { return mode_; }
    const std::string& text() const noexcept { return text_; }
    const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    int character_count() const noexcept { return characters_; }
    int byte_count() const noexcept { return byte_count_; }
    bool is_control() const noexcept;
    std::optional<int> assignment_number() const noexcept { return assignment_; }
    const std::optional<std::string>& application_indicator() const noexcept { return indicator_; }
    const std::optional<StructuredAppendHeader>& header() const noexcept { return header_; }
    int bit_length(int version) const;
private:
    explicit Segment(Mode mode) : mode_(mode) {}
    void swap(Segment& other) noexcept;
    static Segment from_text(Mode, std::string_view);
    Mode mode_;
    std::string text_;
    std::vector<std::uint8_t> bytes_;
    int characters_ = 0;
    int byte_count_ = 0;
    std::optional<int> assignment_;
    std::optional<std::string> indicator_;
    std::optional<StructuredAppendHeader> header_;
};

struct SegmentDiagnostics {
    Mode mode;
    int character_count, byte_count, bit_length;
    std::optional<int> assignment_number;
    std::optional<std::string> application_indicator;
    std::optional<int> application_indicator_codeword;
    std::optional<StructuredAppendHeader> structured_append;
};
struct Gs1ValidationDiagnostics {
    bool enabled = false;
    std::optional<int> element_count = 0;
    std::vector<std::string> ais;
    bool has_separators = false;
};
struct MaskPenalty { int mask; int penalty; };

// Planning is a standalone value; QRCode exposes its stored copy by const reference.
struct Plan {
    bool ok = false;
    std::optional<int> selected_version;
    int evaluated_version = 1;
    int min_version = 1, max_version = 40;
    Ecc ecc = Ecc::M, requested_ecc = Ecc::M;
    std::string version_selection, version_selection_reason;
    std::vector<Segment> segments;
    std::vector<SegmentDiagnostics> segment_diagnostics;
    int data_bit_length = 0, capacity_bits = 0, input_bytes = 0;
    Gs1ValidationDiagnostics gs1_validation;
    std::int64_t remaining_bits() const noexcept { return static_cast<std::int64_t>(capacity_bits) - data_bit_length; }
    std::int64_t overflow_bits() const noexcept { return data_bit_length > capacity_bits ? static_cast<std::int64_t>(data_bit_length) - capacity_bits : 0; }
    double capacity_utilization() const noexcept { return capacity_bits ? static_cast<double>(data_bit_length) / capacity_bits : 0.0; }
    bool boosted_ecc() const noexcept { return ecc != requested_ecc; }
    Mode mode() const noexcept;
    std::optional<int> eci_assignment_number() const noexcept;
    std::optional<StructuredAppendHeader> structured_append() const noexcept;
    bool gs1() const noexcept;
    std::optional<std::string> fnc1_second() const;
    std::vector<SegmentDiagnostics> control_segments() const;
};
struct Diagnostics {
    Plan planning;
    int mask = 0, mask_penalty = 0;
    std::vector<MaskPenalty> mask_penalties;
    int data_codewords = 0, error_correction_codewords = 0, total_codewords = 0;
    std::string mask_selection_reason;
};
struct Capacity {
    int version;
    Ecc ecc;
    int data_codewords, total_codewords;
    std::optional<Mode> mode;
    std::optional<int> character_count_bits;
    int control_bits;
    std::optional<int> payload_bits, max_characters, max_bytes;
    std::int64_t size() const noexcept { return static_cast<std::int64_t>(version) * 4 + 17; }
    std::int64_t capacity_bits() const noexcept { return static_cast<std::int64_t>(data_codewords) * 8; }
    std::int64_t error_correction_codewords() const noexcept { return static_cast<std::int64_t>(total_codewords) - data_codewords; }
};

class QRCode {
public:
    static QRCode generate(std::string_view text, const Options& options = {});
    static QRCode generate(const std::vector<std::uint8_t>& bytes, const Options& options = {});
    // Low-level FNC1 alphanumeric encoding: % means separator; %% means literal %.
    static QRCode generate_segments(const std::vector<Segment>& segments, const Options& options = {});
    static Plan estimate(std::string_view text, const Options& options = {});
    static Plan estimate(const std::vector<std::uint8_t>& bytes, const Options& options = {});
    static Plan analyze_segments(const std::vector<Segment>& segments, const Options& options = {});
    static Capacity capacity(int version, Ecc ecc = Ecc::M, std::optional<Mode> mode = {}, int control_bits = 0);
    int version() const noexcept { return diagnostics_.planning.evaluated_version; }
    int size() const noexcept { return static_cast<int>(matrix_.size()); }
    Ecc ecc() const noexcept { return diagnostics_.planning.ecc; }
    int mask() const noexcept { return diagnostics_.mask; }
    bool module(int x, int y) const;
    const std::vector<std::vector<bool>>& matrix() const noexcept { return matrix_; }
    const std::vector<std::uint8_t>& data_codewords() const noexcept { return data_; }
    const std::vector<std::uint8_t>& codewords() const noexcept { return codewords_; }
    const Plan& planning() const noexcept { return diagnostics_.planning; }
    const Diagnostics& diagnostics() const noexcept { return diagnostics_; }
    const std::vector<Segment>& segments() const noexcept { return diagnostics_.planning.segments; }
private:
    QRCode() = default;
    static QRCode build(Plan plan, const Options& options);
    std::vector<std::vector<bool>> matrix_;
    std::vector<std::uint8_t> data_, codewords_;
    Diagnostics diagnostics_;
};

const char* mode_name(Mode mode) noexcept;
const char* ecc_name(Ecc ecc) noexcept;

} // namespace specqr
