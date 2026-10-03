#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace specqr::gs1 {

// Resource limits count UTF-8 bytes and concrete elements, respectively.
inline constexpr std::size_t max_input_bytes = 1000000;
inline constexpr std::size_t max_elements = 16384;
inline constexpr char fnc1_separator = '\x1D';

struct Element {
    std::string ai;
    std::string value;
    bool operator==(const Element& other) const { return ai == other.ai && value == other.value; }
    bool operator!=(const Element& other) const { return !(*this == other); }
};
enum class ValueKind { Numeric, Text };
enum class CheckDigitRule { None, Gtin, Sscc };
enum class DigitalLinkRole { PrimaryKey, KeyQualifier, DataAttribute };
struct AiLength {
    std::optional<std::size_t> exact;
    std::size_t min;
    std::size_t max;
    bool is_variable() const noexcept { return !exact.has_value(); }
};
struct AiInfo {
    std::string ai;
    std::string label;
    AiLength length;
    ValueKind value_kind;
    CheckDigitRule check_digit_rule;
    DigitalLinkRole digital_link_role;
    std::vector<std::string> digital_link_path_for_primary;
};

// The deliberately bounded 50-AI catalog; dates/GLNs receive shape validation only.
const std::vector<AiInfo>& supported_ais();
const AiInfo* ai_info(std::string_view ai);
char calculate_check_digit(std::string_view digits);
bool validate_check_digit(std::string_view digits);
char calculate_gtin_check_digit(std::string_view body);
std::string append_gtin_check_digit(std::string_view body);
bool validate_gtin_check_digit(std::string_view gtin);
char calculate_sscc_check_digit(std::string_view body);
std::string append_sscc_check_digit(std::string_view body);
bool validate_sscc_check_digit(std::string_view sscc);

struct ElementStringResult {
    std::vector<Element> elements;
    bool has_separators = false;
};
std::vector<Element> parse_human_readable(std::string_view input);
std::string create_element_string(const std::vector<Element>& elements);
// A final variable-length value ending in an apparent fixed AI is conservatively rejected.
ElementStringResult parse_element_string(std::string_view input);

enum class ValidationContext { ElementString, DigitalLink };
struct ValidationOptions {
    ValidationContext context = ValidationContext::ElementString;
    bool allow_unsupported_ai = false; // Unsupported: setting true yields an option error.
    bool collect_all_errors = true;
};
struct ValidationIssue {
    std::string code;
    std::string message;
    std::string reason;
    std::string expected;
    std::optional<std::string> ai;
    std::optional<std::string> value;
    std::optional<std::string> key;
    std::optional<std::size_t> offset; // UTF-8 byte offset.
    std::optional<std::size_t> element_index;
    std::optional<std::size_t> count;
};
struct ValidationResult {
    bool ok = false;
    std::vector<Element> elements;
    std::optional<bool> has_separators;
    std::vector<ValidationIssue> errors;
    std::vector<ValidationIssue> warnings;
};
// These capture malformed-input errors, but do not suppress std::bad_alloc.
ValidationResult validate_elements(const std::vector<Element>& elements, const ValidationOptions& options = {});
ValidationResult validate_element_string(std::string_view input, const ValidationOptions& options = {});

struct DigitalLinkOptions {
    std::string base_url;
    std::string primary_ai = "01";
    // nullopt selects eligible qualifiers in input order; an empty vector selects none.
    std::optional<std::vector<std::string>> path_ais;
};
enum class UnknownQueryPolicy { Preserve, Reject };
struct DigitalLinkParseOptions {
    std::optional<std::string> primary_ai;
    UnknownQueryPolicy unknown_query = UnknownQueryPolicy::Preserve;
};
struct DigitalLinkValidationOptions : DigitalLinkParseOptions {
    bool normalize = false; // Unsupported: call normalize_digital_link explicitly.
};
struct DigitalLinkNormalizeOptions : DigitalLinkParseOptions {
    std::string mode = "specqr-deterministic";
};
struct UnknownQuery {
    std::string key;
    std::string value;
    bool operator==(const UnknownQuery& other) const { return key == other.key && value == other.value; }
};
struct DigitalLinkResult {
    std::vector<Element> elements;
    Element primary;
    std::vector<Element> path_elements;
    std::vector<Element> query_elements;
    std::vector<UnknownQuery> unknown_query;
};
struct DigitalLinkValidationResult {
    bool ok = false;
    std::optional<DigitalLinkResult> result;
    std::vector<ValidationIssue> errors;
    std::vector<ValidationIssue> warnings;
};
std::string create_digital_link(const std::vector<Element>& elements, const DigitalLinkOptions& options);
DigitalLinkResult parse_digital_link(std::string_view uri, const DigitalLinkParseOptions& options = {});
DigitalLinkValidationResult validate_digital_link(std::string_view uri, const DigitalLinkValidationOptions& options = {});
// Deterministic SpecQR normalization, not GS1 canonicalization. Unknown query order is retained.
std::string normalize_digital_link(std::string_view uri, const DigitalLinkNormalizeOptions& options = {});

} // namespace specqr::gs1
