#pragma once
#include <specqr/specqr.hpp>
#include <utility>

namespace specqr {
enum class SplitUnitsDetail { Summary, Full };
struct StructuredAppendOptions {
    Options qr_options;
    int max_symbols = 16;
    bool diagnostics = false;
    SplitUnitsDetail split_units = SplitUnitsDetail::Summary;
};
struct StructuredAppendSplitUnit {
    int source_segment_index;
    Mode mode;
    int unit_start, unit_length, byte_start, byte_length;
};
struct StructuredAppendWarning {
    std::string code, severity, message;
    int total;
    std::optional<int> max_symbols;
};
struct StructuredAppendSymbolDiagnostics {
    int index, total;
    std::uint8_t parity;
    std::optional<int> input_start, input_length, source_segment_start, source_segment_end, split_unit_start, split_unit_length;
    int byte_start, byte_length, version;
    Ecc ecc;
    int data_bit_length, capacity_bits, remaining_bits, mask;
    std::int64_t sequence_index() const noexcept { return static_cast<std::int64_t>(index) - 1; }
    std::int64_t sequence_total() const noexcept { return static_cast<std::int64_t>(total) - 1; }
    std::int64_t sequence_indicator() const noexcept { return sequence_index() * 16 + sequence_total(); }
};
struct StructuredAppendDiagnostics {
    int version;
    Ecc ecc;
    std::string version_selection, version_selection_reason;
    int total;
    std::uint8_t parity;
    int byte_length, input_length;
    std::optional<int> segment_count;
    int max_symbols;
    std::string split_strategy;
    std::optional<int> split_unit_count;
    std::optional<SplitUnitsDetail> split_units_detail;
    std::optional<std::vector<StructuredAppendSplitUnit>> split_units;
    std::vector<StructuredAppendSymbolDiagnostics> symbols;
    std::vector<StructuredAppendWarning> warnings;
};
class StructuredAppendResult {
public:
    StructuredAppendResult(std::vector<QRCode> symbols, StructuredAppendDiagnostics diagnostics);
    const std::vector<QRCode>& symbols() const noexcept { return symbols_; }
    int total() const noexcept { return diagnostics_.total; }
    std::uint8_t parity() const noexcept { return diagnostics_.parity; }
    int input_length() const noexcept { return diagnostics_.input_length; }
    int byte_length() const noexcept { return diagnostics_.byte_length; }
    const StructuredAppendDiagnostics& diagnostics() const noexcept { return diagnostics_; }
private:
    std::vector<QRCode> symbols_;
    StructuredAppendDiagnostics diagnostics_;
};

StructuredAppendResult generate_structured_append(std::string_view text, const StructuredAppendOptions& options = {});
StructuredAppendResult generate_structured_append(const std::vector<std::uint8_t>& bytes, const StructuredAppendOptions& options = {});
/// Numeric/alphanumeric/Kanji segments are atomic; text-byte segments split at Unicode scalar boundaries.
StructuredAppendResult generate_segments_structured_append(const std::vector<Segment>& segments, const StructuredAppendOptions& options = {});
/// XOR over original UTF-8, including Kanji; binary segments contribute raw bytes.
std::uint8_t structured_append_parity(std::string_view text);
std::uint8_t structured_append_parity(const std::vector<std::uint8_t>& bytes);
std::uint8_t structured_append_segments_parity(const std::vector<Segment>& segments);

class StructuredAppendPart {
public:
    StructuredAppendPart(int index, int total, std::uint8_t parity, std::string_view text);
    StructuredAppendPart(int index, int total, std::uint8_t parity, const std::vector<std::uint8_t>& bytes);
    int index() const noexcept { return index_; }
    int total() const noexcept { return total_; }
    std::uint8_t parity() const noexcept { return parity_; }
    const std::optional<std::string>& text() const noexcept { return text_; }
    const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
private:
    int index_, total_;
    std::uint8_t parity_;
    std::optional<std::string> text_;
    std::vector<std::uint8_t> bytes_;
};
struct StructuredAppendPartMetadata {
    int index, total;
    std::uint8_t parity;
    std::string data_type;
    int byte_length;
};
struct StructuredAppendParityCheck {
    std::uint8_t expected, actual;
    bool matches() const noexcept { return expected == actual; }
};
struct StructuredAppendMergeDiagnostics {
    int part_count, total;
    std::uint8_t parity;
    std::string data_type;
    int byte_length;
    std::vector<int> missing, duplicate;
    StructuredAppendParityCheck parity_check;
};
class StructuredAppendMergeResult {
public:
    StructuredAppendMergeResult(std::optional<std::string> text, std::vector<std::uint8_t> bytes,
                                std::vector<StructuredAppendPartMetadata> parts, StructuredAppendMergeDiagnostics diagnostics);
    const std::optional<std::string>& text() const noexcept { return text_; }
    const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    int total() const noexcept { return diagnostics_.total; }
    std::uint8_t parity() const noexcept { return diagnostics_.parity; }
    const std::vector<StructuredAppendPartMetadata>& parts() const noexcept { return parts_; }
    const StructuredAppendMergeDiagnostics& diagnostics() const noexcept { return diagnostics_; }
private:
    std::optional<std::string> text_;
    std::vector<std::uint8_t> bytes_;
    std::vector<StructuredAppendPartMetadata> parts_;
    StructuredAppendMergeDiagnostics diagnostics_;
};
/// Validates a complete decoded set; this function does not read images.
StructuredAppendMergeResult merge_structured_append_parts(const std::vector<StructuredAppendPart>& parts);
} // namespace specqr
