#include "specqr/gs1.hpp"
#include "specqr/specqr.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace specqr::gs1 {
namespace {
ValidationIssue issue(std::string code, std::string message, std::string reason = "invalid-input", std::string expected = {}) {
    ValidationIssue result;
    result.code = std::move(code); result.message = std::move(message);
    result.reason = std::move(reason); result.expected = std::move(expected);
    return result;
}
class Failure final : public Error {
public:
    explicit Failure(ValidationIssue detail) : Error("INVALID_GS1", detail.message), detail_(std::move(detail)) {}
    const ValidationIssue& detail() const noexcept { return detail_; }
private:
    ValidationIssue detail_;
};
[[noreturn]] void fail(std::string message) { throw Failure(issue("GS1_INVALID_INPUT", std::move(message))); }
[[noreturn]] void fail_issue(std::string code, std::string message, std::string reason, std::string expected = {}) {
    throw Failure(issue(std::move(code), std::move(message), std::move(reason), std::move(expected)));
}
void text_limit(std::string_view text, std::string_view label) {
    if (text.size() > max_input_bytes) fail(std::string(label) + " must contain at most " + std::to_string(max_input_bytes) + " UTF-8 bytes");
}
void element_limit(std::size_t size) {
    if (size > max_elements) fail("GS1 elements must contain at most " + std::to_string(max_elements) + " elements");
}
bool digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool is_ai(std::string_view ai) { return ai.size() >= 2 && ai.size() <= 4 && digits(ai); }
void require_digits(std::string_view value, std::string_view label) {
    text_limit(value, label);
    if (!digits(value)) fail_issue("GS1_INVALID_CHARSET", std::string(label) + " must contain digits only", "invalid-charset", "digits only");
}
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') out.push_back('\\');
        if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        else out += "?";
    }
    return out + '"';
}
const AiInfo& normalize_element_impl(const Element& e, std::size_t index) {
    text_limit(e.ai, "GS1 AI"); text_limit(e.value, "GS1 value");
    if (!is_ai(e.ai)) fail("GS1 element " + std::to_string(index) + " has invalid AI " + quote(e.ai) + "; expected 2 to 4 digits");
    const AiInfo* info = ai_info(e.ai);
    if (!info) fail_issue("GS1_UNSUPPORTED_AI", "Unsupported GS1 AI " + e.ai + ". Add explicit support before using it.", "unsupported-ai", "supported GS1 AI");
    const std::string prefix = "GS1 AI " + e.ai + " value ";
    if (e.value.empty()) fail(prefix + "must not be empty");
    if (e.value.find(fnc1_separator) != std::string::npos)
        fail_issue("GS1_UNEXPECTED_SEPARATOR", prefix + "must not contain the FNC1 separator", "unexpected-separator", "separator only after a non-final variable-length GS1 element");
    if (e.value.find_first_of("()") != std::string::npos) fail(prefix + "must be raw data without human-readable parentheses");
    if (std::any_of(e.value.begin(), e.value.end(), [](unsigned char c) { return c < 32 || c > 126; }))
        fail_issue("GS1_INVALID_CHARSET", prefix + "must use printable ASCII characters", "invalid-charset", "printable ASCII");
    if (info->value_kind == ValueKind::Numeric && !digits(e.value))
        fail_issue("GS1_INVALID_CHARSET", prefix + "must contain digits only", "invalid-charset", "digits only");
    if (info->length.exact && e.value.size() != *info->length.exact) {
        const std::string expected = "exactly " + std::to_string(*info->length.exact) + " characters";
        fail_issue("GS1_INVALID_LENGTH", prefix + "must be " + expected, "invalid-length", expected);
    }
    if (info->length.is_variable() && e.value.size() > info->length.max) {
        const std::string expected = "at most " + std::to_string(info->length.max) + " characters";
        fail_issue("GS1_INVALID_LENGTH", prefix + "must be " + expected, "invalid-length", expected);
    }
    if (info->check_digit_rule == CheckDigitRule::Gtin && !validate_gtin_check_digit(e.value))
        fail_issue("GS1_INVALID_CHECK_DIGIT", prefix + "has an invalid GTIN check digit", "invalid-check-digit", "valid GTIN check digit");
    if (info->check_digit_rule == CheckDigitRule::Sscc && !validate_sscc_check_digit(e.value))
        fail_issue("GS1_INVALID_CHECK_DIGIT", prefix + "has an invalid SSCC check digit", "invalid-check-digit", "valid SSCC check digit");
    return *info;
}
const AiInfo& normalize_element(const Element& e, std::size_t index) {
    try { return normalize_element_impl(e, index); }
    catch (const Failure& ex) {
        auto detail = ex.detail();
        if (e.ai.size() <= 4) detail.ai = e.ai;
        if (e.value.size() <= 90) detail.value = e.value;
        detail.element_index = index;
        throw Failure(std::move(detail));
    }
}
const AiInfo* read_ai(std::string_view text, std::size_t offset) {
    for (std::size_t n = 4; n >= 2; --n)
        if (n <= text.size() - offset) if (const AiInfo* info = ai_info(text.substr(offset, n))) return info;
    return nullptr;
}
std::optional<ValidationIssue> options_issue(const ValidationOptions& o) {
    if (o.context != ValidationContext::ElementString && o.context != ValidationContext::DigitalLink)
        return issue("GS1_INVALID_INPUT", "GS1 validation context must be ElementString or DigitalLink", "invalid-options", "ElementString or DigitalLink");
    if (o.allow_unsupported_ai)
        return issue("GS1_INVALID_INPUT", "GS1 validation allow_unsupported_ai must be false", "invalid-options", "false");
    return std::nullopt;
}
} // namespace

const std::vector<AiInfo>& supported_ais() {
    static const std::vector<AiInfo> catalog = [] {
        std::vector<AiInfo> out;
        auto add = [&](std::string ai, std::string label, std::size_t length, bool variable = false,
                       ValueKind kind = ValueKind::Numeric, CheckDigitRule check = CheckDigitRule::None,
                       DigitalLinkRole role = DigitalLinkRole::DataAttribute) {
            out.push_back({std::move(ai), std::move(label), {variable ? std::nullopt : std::optional<std::size_t>(length), variable ? 1 : length, length}, kind, check, role,
                           role == DigitalLinkRole::KeyQualifier ? std::vector<std::string>{"01"} : std::vector<std::string>{}});
        };
        add("00", "Serial shipping container code", 18, false, ValueKind::Numeric, CheckDigitRule::Sscc, DigitalLinkRole::PrimaryKey);
        add("01", "Global trade item number", 14, false, ValueKind::Numeric, CheckDigitRule::Gtin, DigitalLinkRole::PrimaryKey);
        add("02", "Contained trade item GTIN", 14, false, ValueKind::Numeric, CheckDigitRule::Gtin);
        add("10", "Batch or lot number", 20, true, ValueKind::Text, CheckDigitRule::None, DigitalLinkRole::KeyQualifier);
        add("11", "Production date", 6); add("12", "Due date", 6); add("13", "Packaging date", 6);
        add("15", "Best before date", 6); add("16", "Sell by date", 6); add("17", "Expiration date", 6);
        add("20", "Internal product variant", 2);
        add("21", "Serial number", 20, true, ValueKind::Text, CheckDigitRule::None, DigitalLinkRole::KeyQualifier);
        add("22", "Consumer product variant", 20, true, ValueKind::Text, CheckDigitRule::None, DigitalLinkRole::KeyQualifier);
        add("30", "Variable count", 8, true); add("37", "Count of contained trade items", 8, true);
        add("240", "Additional product identification", 30, true, ValueKind::Text);
        add("241", "Customer part number", 30, true, ValueKind::Text);
        add("400", "Customer purchase order number", 30, true, ValueKind::Text);
        add("410", "Ship to global location number", 13); add("411", "Bill to global location number", 13);
        add("412", "Purchased from global location number", 13); add("413", "Ship for global location number", 13);
        add("414", "Identification of a physical location", 13, false, ValueKind::Numeric, CheckDigitRule::None, DigitalLinkRole::PrimaryKey);
        add("415", "Global location number of the invoicing party", 13);
        add("420", "Ship to postal code", 20, true, ValueKind::Text);
        add("422", "Country of origin", 3); add("424", "Country of processing", 3);
        add("425", "Country of disassembly", 3); add("426", "Country covering full process chain", 3);
        for (int n = 3100; n <= 3105; ++n) add(std::to_string(n), "Net weight in kilograms", 6);
        for (int n = 3200; n <= 3205; ++n) add(std::to_string(n), "Net weight in pounds", 6);
        for (int n = 91; n <= 99; ++n) add(std::to_string(n), "Company internal information", 90, true, ValueKind::Text);
        return out;
    }();
    return catalog;
}
const AiInfo* ai_info(std::string_view ai) {
    const auto& catalog = supported_ais();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const AiInfo& info) { return info.ai == ai; });
    return found == catalog.end() ? nullptr : &*found;
}
char calculate_check_digit(std::string_view value) {
    require_digits(value, "GS1 check digit input");
    unsigned int sum = 0, weight = 3;
    for (std::size_t i = value.size(); i > 0; --i) { sum = (sum + static_cast<unsigned int>(value[i-1] - '0') * weight) % 10; weight = 4 - weight; }
    return static_cast<char>('0' + (10 - sum) % 10);
}
bool validate_check_digit(std::string_view value) {
    require_digits(value, "GS1 check digit value");
    if (value.size() < 2) fail("GS1 check digit value must include body digits and one check digit");
    return calculate_check_digit(value.substr(0, value.size()-1)) == value.back();
}
char calculate_gtin_check_digit(std::string_view body) {
    require_digits(body, "GTIN body");
    if (body.size() != 7 && body.size() != 11 && body.size() != 12 && body.size() != 13) fail("GTIN body must be 7, 11, 12, or 13 digits");
    return calculate_check_digit(body);
}
std::string append_gtin_check_digit(std::string_view body) { const char c = calculate_gtin_check_digit(body); return std::string(body) + c; }
bool validate_gtin_check_digit(std::string_view value) {
    require_digits(value, "GTIN");
    if (value.size() != 8 && value.size() != 12 && value.size() != 13 && value.size() != 14) fail("GTIN must be 8, 12, 13, or 14 digits");
    return validate_check_digit(value);
}
char calculate_sscc_check_digit(std::string_view body) {
    require_digits(body, "SSCC body"); if (body.size() != 17) fail("SSCC body must be exactly 17 digits");
    return calculate_check_digit(body);
}
std::string append_sscc_check_digit(std::string_view body) { const char c = calculate_sscc_check_digit(body); return std::string(body) + c; }
bool validate_sscc_check_digit(std::string_view value) {
    require_digits(value, "SSCC"); if (value.size() != 18) fail("SSCC must be exactly 18 digits"); return validate_check_digit(value);
}
std::vector<Element> parse_human_readable(std::string_view input) {
    text_limit(input, "GS1 human-readable input");
    if (input.empty()) fail("GS1 human-readable input must not be empty");
    std::vector<Element> elements;
    for (std::size_t pos = 0; pos < input.size();) {
        if (input[pos] != '(') fail("GS1 human-readable input must contain an AI in parentheses at offset " + std::to_string(pos));
        const std::size_t close = input.find(')', pos + 1);
        if (close == std::string_view::npos) fail("GS1 AI starting at offset " + std::to_string(pos) + " is missing a closing parenthesis");
        std::size_t end = input.find('(', close + 1);
        if (end == std::string_view::npos) end = input.size();
        element_limit(elements.size() + 1);
        Element e{std::string(input.substr(pos + 1, close - pos - 1)), std::string(input.substr(close + 1, end - close - 1))};
        normalize_element(e, elements.size()); elements.push_back(std::move(e)); pos = end;
    }
    return elements;
}
std::string create_element_string(const std::vector<Element>& elements) {
    element_limit(elements.size()); if (elements.empty()) fail("GS1 elements must not be empty");
    std::string out;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const AiInfo& info = normalize_element(elements[i], i);
        const std::size_t extra = elements[i].ai.size() + elements[i].value.size() + (info.length.is_variable() && i + 1 < elements.size() ? 1u : 0u);
        if (extra > max_input_bytes - out.size()) fail("GS1 element string output exceeds the UTF-8 byte limit");
        out += elements[i].ai; out += elements[i].value;
        if (info.length.is_variable() && i + 1 < elements.size()) out += fnc1_separator;
    }
    return out;
}
ElementStringResult parse_element_string(std::string_view input) {
    text_limit(input, "GS1 element string input"); if (input.empty()) fail("GS1 element string input must not be empty");
    if (input.find_first_of("()") != std::string_view::npos) fail("GS1 element string input must be raw data without human-readable parentheses; use parse_human_readable() and create_element_string() first");
    ElementStringResult out; out.has_separators = input.find(fnc1_separator) != std::string_view::npos;
    for (std::size_t pos = 0; pos < input.size();) {
        if (input[pos] == fnc1_separator) {
            auto detail = issue("GS1_UNEXPECTED_SEPARATOR", "GS1 element string has an unexpected FNC1 separator at offset " + std::to_string(pos), "unexpected-separator", "separator only after a non-final variable-length GS1 element");
            detail.offset = pos; throw Failure(std::move(detail));
        }
        const AiInfo* info = read_ai(input, pos);
        if (!info) { auto detail = issue("GS1_UNSUPPORTED_AI", "Unsupported GS1 AI at offset " + std::to_string(pos), "unsupported-ai", "supported GS1 AI"); detail.offset = pos; throw Failure(std::move(detail)); }
        const std::size_t start = pos + info->ai.size();
        std::size_t end = info->length.exact ? start + std::min(*info->length.exact, input.size() - start) : input.find(fnc1_separator, start);
        if (end == std::string_view::npos) end = input.size();
        if (info->length.is_variable() && end == input.size()) {
            const std::size_t lower = end > 22 ? end - 22 : 0;
            for (std::size_t candidate = std::max(start + 1, lower); candidate < end; ++candidate) {
                const AiInfo* next = read_ai(input, candidate);
                if (next && next->length.exact && next->ai.size() + *next->length.exact == end - candidate) {
                    auto detail = issue("GS1_MISSING_SEPARATOR", "GS1 variable-length element at offset " + std::to_string(start) + " is missing an FNC1 separator before offset " + std::to_string(candidate), "missing-separator", "FNC1 separator before the next GS1 element");
                    detail.offset = start; detail.ai = info->ai; throw Failure(std::move(detail));
                }
            }
        }
        element_limit(out.elements.size() + 1);
        Element e{info->ai, std::string(input.substr(start, end - start))}; normalize_element(e, out.elements.size()); out.elements.push_back(std::move(e)); pos = end;
        if (pos < input.size() && input[pos] == fnc1_separator && info->length.is_variable()) {
            if (++pos == input.size()) fail_issue("GS1_UNEXPECTED_SEPARATOR", "GS1 element string must not end with an FNC1 separator", "unexpected-separator", "separator only after a non-final variable-length GS1 element");
        }
    }
    return out;
}
ValidationResult validate_elements(const std::vector<Element>& elements, const ValidationOptions& options) {
    ValidationResult out;
    if (auto bad = options_issue(options)) { out.errors.push_back(std::move(*bad)); return out; }
    try { element_limit(elements.size()); if (elements.empty()) fail("GS1 elements must not be empty"); }
    catch (const Failure& ex) { out.errors.push_back(ex.detail()); return out; }
    for (std::size_t i = 0; i < elements.size(); ++i) {
        try { normalize_element(elements[i], i); }
        catch (const Failure& ex) {
            auto detail = ex.detail();
            out.errors.push_back(std::move(detail)); if (!options.collect_all_errors) break;
        }
    }
    if (!out.errors.empty()) return out;
    if (options.context == ValidationContext::DigitalLink && std::none_of(elements.begin(), elements.end(), [](const Element& e) { return ai_info(e.ai)->digital_link_role == DigitalLinkRole::PrimaryKey; })) {
        out.errors.push_back(issue("GS1_INVALID_DIGITAL_LINK_PLACEMENT", "GS1 Digital Link elements must include a primary AI 00, 01, or 414", "invalid-digital-link-placement", "primary AI 00, 01, or 414")); return out;
    }
    out.ok = true; out.elements = elements; return out;
}
ValidationResult validate_element_string(std::string_view input, const ValidationOptions& options) {
    ValidationResult out;
    if (auto bad = options_issue(options)) { out.errors.push_back(std::move(*bad)); return out; }
    try { auto parsed = parse_element_string(input); out.ok = true; out.elements = std::move(parsed.elements); out.has_separators = parsed.has_separators; }
    catch (const Failure& ex) { out.errors.push_back(ex.detail()); }
    return out;
}

namespace {
int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
bool ascii_alnum(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
std::string ascii_lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto a = static_cast<unsigned char>(text[i++]);
        if (a < 128) continue;
        std::size_t count = 0;
        std::uint32_t cp = 0, minimum = 0;
        if (a >= 0xC2 && a <= 0xDF) { count = 1; cp = a & 0x1Fu; minimum = 0x80; }
        else if (a >= 0xE0 && a <= 0xEF) { count = 2; cp = a & 0x0Fu; minimum = 0x800; }
        else if (a >= 0xF0 && a <= 0xF4) { count = 3; cp = a & 0x07u; minimum = 0x10000; }
        else return false;
        if (count > text.size() - i) return false;
        for (std::size_t k = 0; k < count; ++k) {
            const auto b = static_cast<unsigned char>(text[i++]);
            if ((b & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}
[[noreturn]] void invalid_percent() {
    fail_issue("GS1_INVALID_PERCENT_ENCODING", "GS1 Digital Link URI must use valid percent-encoding and UTF-8", "invalid-percent-encoding", "valid percent escapes and Unicode scalar UTF-8");
}
std::string percent_decode(std::string_view text, bool form) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%') {
            if (text.size() - i < 3 || hex(text[i+1]) < 0 || hex(text[i+2]) < 0) invalid_percent();
            out.push_back(static_cast<char>(hex(text[i+1]) * 16 + hex(text[i+2]))); i += 2;
        } else out.push_back(form && text[i] == '+' ? ' ' : text[i]);
    }
    if (!valid_utf8(out)) invalid_percent();
    return out;
}
void append_escape(std::string& out, unsigned char c) {
    static constexpr char upper[] = "0123456789ABCDEF";
    out.push_back('%'); out.push_back(upper[c >> 4]); out.push_back(upper[c & 15u]);
}
std::string percent_encode(std::string_view text, bool form) {
    std::string out;
    const std::string_view safe = form ? "*-._" : "-_.!~*'()";
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (ascii_alnum(c) || safe.find(static_cast<char>(c)) != std::string_view::npos) out.push_back(static_cast<char>(c));
        else if (form && c == ' ') out.push_back('+');
        else append_escape(out, c);
    }
    return out;
}
std::string encode_url_part(std::string_view text, bool query) {
    std::string out;
    const std::string_view forbidden = query ? "\"#'<>" : "\"#<>?`{}^";
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c <= 32 || c > 126 || forbidden.find(static_cast<char>(c)) != std::string_view::npos) append_escape(out, c);
        else out.push_back(static_cast<char>(c));
    }
    return out;
}
[[noreturn]] void invalid_uri() {
    fail_issue("GS1_DIGITAL_LINK_INVALID_URI", "GS1 Digital Link URI must be an absolute http or https URL with a valid authority; Unicode hosts require IDNA ASCII", "invalid-uri", "absolute http or https URL");
}
std::vector<std::string_view> split(std::string_view text, char separator, std::size_t limit = max_elements) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    for (;;) {
        if (out.size() == limit) fail("GS1 Digital Link component count exceeds " + std::to_string(limit));
        std::size_t end = text.find(separator, begin);
        if (end == std::string_view::npos) { out.push_back(text.substr(begin)); return out; }
        out.push_back(text.substr(begin, end - begin)); begin = end + 1;
    }
}
std::uint32_t decimal_number(std::string_view text, std::uint32_t maximum, bool canonical) {
    if (!digits(text) || (canonical && text.size() > 1 && text[0] == '0')) invalid_uri();
    std::uint32_t value = 0;
    for (char c : text) {
        const auto digit = static_cast<std::uint32_t>(c - '0');
        if (value > (maximum - digit) / 10) invalid_uri();
        value = value * 10 + digit;
    }
    return value;
}
std::array<std::uint32_t, 4> ipv4_parts(std::string_view host) {
    if (std::count(host.begin(), host.end(), '.') != 3) invalid_uri();
    auto pieces = split(host, '.', 4);
    if (pieces.size() != 4) invalid_uri();
    std::array<std::uint32_t, 4> parts{};
    for (std::size_t i = 0; i < 4; ++i) parts[i] = decimal_number(pieces[i], 255, true);
    return parts;
}
std::vector<std::uint16_t> ipv6_part(std::string_view text, bool allow_ipv4) {
    std::vector<std::uint16_t> out;
    if (text.empty()) return out;
    if (std::count(text.begin(), text.end(), ':') > 7) invalid_uri();
    auto pieces = split(text, ':', 8);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto piece = pieces[i];
        if (piece.find('.') != std::string_view::npos) {
            if (!allow_ipv4 || i + 1 != pieces.size()) invalid_uri();
            const auto p = ipv4_parts(piece);
            out.push_back(static_cast<std::uint16_t>((p[0] << 8) | p[1]));
            out.push_back(static_cast<std::uint16_t>((p[2] << 8) | p[3]));
        } else {
            if (piece.empty() || piece.size() > 4) invalid_uri();
            unsigned int value = 0;
            for (char c : piece) { if (hex(c) < 0) invalid_uri(); value = value * 16 + static_cast<unsigned int>(hex(c)); }
            out.push_back(static_cast<std::uint16_t>(value));
        }
    }
    return out;
}
std::string hex_number(std::uint16_t value) {
    static constexpr char lower[] = "0123456789abcdef";
    std::string result;
    do { result.push_back(lower[value & 15u]); value >>= 4; } while (value != 0);
    std::reverse(result.begin(), result.end()); return result;
}
std::string normalize_ipv6(std::string_view address) {
    const auto compression = address.find("::");
    std::vector<std::uint16_t> groups;
    if (compression == std::string_view::npos) {
        groups = ipv6_part(address, true); if (groups.size() != 8) invalid_uri();
    } else {
        if (address.find("::", compression + 2) != std::string_view::npos) invalid_uri();
        groups = ipv6_part(address.substr(0, compression), false);
        const auto right = ipv6_part(address.substr(compression + 2), true);
        if (groups.size() + right.size() >= 8) invalid_uri();
        groups.insert(groups.end(), 8 - groups.size() - right.size(), 0); groups.insert(groups.end(), right.begin(), right.end());
    }
    std::size_t best_start = 0, best_size = 0;
    for (std::size_t i = 0; i < groups.size();) {
        if (groups[i] != 0) { ++i; continue; }
        std::size_t end = i + 1; while (end < groups.size() && groups[end] == 0) ++end;
        if (end - i > best_size) { best_start = i; best_size = end - i; } i = end;
    }
    std::string out = "[";
    for (std::size_t i = 0; i < groups.size();) {
        if (best_size >= 2 && i == best_start) { out += "::"; i += best_size; }
        else { if (out.back() != '[' && out.back() != ':') out += ':'; out += hex_number(groups[i++]); }
    }
    return out + ']';
}
std::string normalize_host(std::string_view raw_host) {
    if (raw_host.empty()) invalid_uri();
    if (raw_host.front() == '[') {
        if (raw_host.back() != ']') invalid_uri(); return normalize_ipv6(raw_host.substr(1, raw_host.size()-2));
    }
    std::string decoded;
    try { decoded = percent_decode(raw_host, false); } catch (const Failure&) { invalid_uri(); }
    const std::string host = ascii_lower(decoded);
    // IDNA is deliberately left to the caller. ASCII WHATWG domain characters
    // remain permissible; this helper does not perform DNS resolution.
    if (host.empty() || std::any_of(host.begin(), host.end(), [](unsigned char c) {
        return c <= 32 || c >= 127 || std::string_view("#/:<>?@[\\]^|%").find(static_cast<char>(c)) != std::string_view::npos;
    })) invalid_uri();
    std::string_view stripped(host);
    if (stripped.back() == '.') stripped.remove_suffix(1);
    const auto pieces = split(stripped, '.', max_elements);
    const auto last = pieces.back();
    const bool hexadecimal_last = last.size() >= 2 && last.substr(0, 2) == "0x" && std::all_of(last.begin()+2, last.end(), [](char c) { return hex(c) >= 0; });
    if (!(digits(last) || hexadecimal_last)) return host;
    // WHATWG IPv4 numbers: decimal, octal, hexadecimal, and a short final part.
    // Bound each accumulator to 32 bits before multiplication; no host-size shift.
    if (pieces.size() > 4) invalid_uri();
    std::array<std::uint32_t, 4> values{};
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const auto piece = pieces[i];
        if (piece.empty()) invalid_uri();
        std::size_t begin = 0;
        std::uint32_t radix = 10;
        if (piece.size() >= 2 && piece.substr(0, 2) == "0x") { radix = 16; begin = 2; }
        else if (piece.size() >= 2 && piece.front() == '0') { radix = 8; begin = 1; }
        std::uint32_t value = 0;
        for (std::size_t j = begin; j < piece.size(); ++j) {
            const int digit = hex(piece[j]);
            if (digit < 0 || static_cast<std::uint32_t>(digit) >= radix || value > (std::numeric_limits<std::uint32_t>::max() - static_cast<std::uint32_t>(digit)) / radix) invalid_uri();
            value = value * radix + static_cast<std::uint32_t>(digit);
        }
        values[i] = value;
    }
    std::uint32_t address = values[pieces.size()-1];
    const unsigned int final_bits = static_cast<unsigned int>(8 * (5 - pieces.size()));
    if (final_bits < 32 && address >= (std::uint32_t{1} << final_bits)) invalid_uri();
    for (std::size_t i = 0; i + 1 < pieces.size(); ++i) {
        if (values[i] > 255) invalid_uri();
        address += values[i] << static_cast<unsigned int>(8 * (3 - i));
    }
    return std::to_string((address >> 24) & 255u) + '.' + std::to_string((address >> 16) & 255u) + '.' + std::to_string((address >> 8) & 255u) + '.' + std::to_string(address & 255u);
}
std::string encode_credentials(std::string_view text) {
    std::string result;
    bool colon_seen = false;
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == ':' && !colon_seen) { result += ':'; colon_seen = true; }
        else if (c <= 32 || c > 126 || std::string_view("\"#<>?`{}/:;=@[\\]^|").find(static_cast<char>(c)) != std::string_view::npos) append_escape(result, c);
        else result += static_cast<char>(c);
    }
    // WHATWG serialization omits an empty password delimiter.
    if (!result.empty() && result.back() == ':') result.pop_back();
    return result;
}
std::string normalize_path(std::string_view path, bool base_url) {
    std::vector<std::string_view> result;
    const auto parts = split(path, '/', max_elements + 1);
    bool primary_seen = false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto raw = parts[i];
        const auto decoded = percent_decode(raw, false);
        if (!base_url && (raw == "00" || raw == "01" || raw == "414")) primary_seen = true;
        if (decoded == "." || decoded == "..") {
            if (primary_seen) fail_issue("GS1_INVALID_DIGITAL_LINK_PLACEMENT", "GS1 Digital Link path values must not be dot segments; place these values in the query", "invalid-digital-link-placement");
            if (decoded == ".." && result.size() > 1) result.pop_back();
            if (i + 1 == parts.size()) result.emplace_back();
        } else result.push_back(raw);
    }
    std::string out;
    for (std::size_t i = 0; i < result.size(); ++i) { if (i != 0) out += '/'; out += result[i]; }
    if (out.empty() || out.front() != '/') out.insert(out.begin(), '/');
    return encode_url_part(out, false);
}
struct Url {
    std::string scheme;
    std::string authority;
    std::string path;
    std::optional<std::string> query;
    bool empty_fragment = false;
    std::string str() const {
        std::string result = scheme + "://" + authority + path;
        if (query) { result += '?'; result += *query; }
        if (empty_fragment) result += '#';
        text_limit(result, "GS1 Digital Link output"); return result;
    }
    explicit Url(std::string_view input, bool base_url = false) {
        text_limit(input, "GS1 Digital Link URI");
        while (!input.empty() && static_cast<unsigned char>(input.front()) <= 32) input.remove_prefix(1);
        while (!input.empty() && static_cast<unsigned char>(input.back()) <= 32) input.remove_suffix(1);
        std::string cleaned;
        cleaned.reserve(input.size());
        for (char c : input) if (c != '\t' && c != '\r' && c != '\n') cleaned += c;
        if (!valid_utf8(cleaned)) invalid_uri();
        const auto colon = cleaned.find(':');
        if (colon == std::string::npos) invalid_uri();
        scheme = ascii_lower(cleaned.substr(0, colon));
        if (scheme != "http" && scheme != "https") invalid_uri();
        std::string_view rest(cleaned.data() + colon + 1, cleaned.size() - colon - 1);
        const auto fragment = rest.find('#');
        if (fragment != std::string_view::npos) {
            if (fragment + 1 != rest.size()) fail_issue("GS1_DIGITAL_LINK_FRAGMENT_NOT_ALLOWED", "GS1 Digital Link URI must not include a fragment", "fragment-not-allowed", "URI without fragment");
            empty_fragment = true;
            rest = rest.substr(0, fragment);
        }
        const auto question = rest.find('?');
        if (question != std::string_view::npos) {
            const auto raw_query = rest.substr(question + 1);
            (void)percent_decode(raw_query, true); query = encode_url_part(raw_query, true);
            rest = rest.substr(0, question);
        }
        // HTTP(S) URL repair applies to authority/path only; query backslashes
        // carry user data and must stay intact until ordinary percent encoding.
        std::string location(rest);
        std::replace(location.begin(), location.end(), '\\', '/');
        std::string_view location_view(location);
        while (!location_view.empty() && location_view.front() == '/') location_view.remove_prefix(1);
        const auto slash = location_view.find('/');
        const auto raw_authority = location_view.substr(0, slash);
        if (raw_authority.empty()) invalid_uri();
        const auto at = raw_authority.rfind('@');
        std::string credentials = at == std::string_view::npos ? "" : encode_credentials(raw_authority.substr(0, at));
        if (!credentials.empty()) credentials += '@';
        std::string_view host = at == std::string_view::npos ? raw_authority : raw_authority.substr(at + 1);
        if (host.empty()) invalid_uri();
        std::optional<std::string_view> port;
        if (host.front() == '[') {
            const auto bracket = host.find(']');
            if (bracket == std::string_view::npos) invalid_uri();
            if (bracket + 1 < host.size()) {
                if (host[bracket + 1] != ':') invalid_uri(); port = host.substr(bracket + 2);
            }
            host = host.substr(0, bracket + 1);
        } else {
            const auto p = host.find(':');
            if (p != std::string_view::npos) { port = host.substr(p + 1); host = host.substr(0, p); }
        }
        authority = credentials + normalize_host(host);
        if (port && !port->empty()) {
            const auto number = decimal_number(*port, 65535, false);
            if (!((scheme == "http" && number == 80) || (scheme == "https" && number == 443))) authority += ':' + std::to_string(number);
        }
        const auto raw_path = slash == std::string_view::npos ? std::string_view("/") : location_view.substr(slash);
        if (static_cast<std::size_t>(std::count(raw_path.begin(), raw_path.end(), '/')) > max_elements) fail("GS1 Digital Link path component count exceeds limit");
        path = normalize_path(raw_path, base_url);
    }
};
void check_primary(std::string_view ai) {
    if (ai != "00" && ai != "01" && ai != "414") fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link primary_ai must be one of 00, 01, or 414", "invalid-options", "00, 01, or 414");
}
bool path_eligible(std::string_view ai, std::string_view primary) {
    const auto* info = ai_info(ai);
    return info && info->digital_link_role == DigitalLinkRole::KeyQualifier && std::find(info->digital_link_path_for_primary.begin(), info->digital_link_path_for_primary.end(), primary) != info->digital_link_path_for_primary.end();
}
void assert_path_placement(std::string_view ai, std::string_view primary) {
    if (!ai_info(ai)) fail_issue("GS1_UNSUPPORTED_AI", "Unsupported GS1 AI " + std::string(ai) + ". Add explicit support before using it.", "unsupported-ai", "supported GS1 AI");
    if (!path_eligible(ai, primary)) fail_issue("GS1_INVALID_DIGITAL_LINK_PLACEMENT", "GS1 AI " + std::string(ai) + " cannot be placed in the Digital Link path after primary AI " + std::string(primary), "invalid-digital-link-placement");
}
void reject_duplicate(std::set<std::string>& seen, const std::string& ai) {
    if (!seen.insert(ai).second) {
        auto detail = issue("GS1_DUPLICATE_AI", "GS1 Digital Link input must not contain duplicate AI " + ai, "duplicate-ai", "unique GS1 AI within the Digital Link URI"); detail.ai = ai; throw Failure(std::move(detail));
    }
}
std::vector<std::string_view> path_segments(std::string_view path) {
    while (!path.empty() && path.front() == '/') path.remove_prefix(1);
    while (!path.empty() && path.back() == '/') path.remove_suffix(1);
    if (path.empty()) fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link path must include primary AI 00, 01, or 414", "malformed-path", "Digital Link path containing primary AI and AI/value pairs");
    auto segments = split(path, '/');
    if (std::any_of(segments.begin(), segments.end(), [](std::string_view s) { return s.empty(); })) fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link path must not contain empty segments", "malformed-path");
    return segments;
}
std::size_t primary_index(const std::vector<std::string_view>& segments, const std::optional<std::string>& preferred) {
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (preferred) { if (segments[i] == *preferred) return i; }
        else if (const auto* info = ai_info(segments[i])) if (info->digital_link_role == DigitalLinkRole::PrimaryKey) return i;
    }
    fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link path must include primary AI 00, 01, or 414", "malformed-path", "Digital Link path containing primary AI and AI/value pairs");
}
DigitalLinkResult parse_url(const Url& url, const DigitalLinkParseOptions& options) {
    if (options.primary_ai) check_primary(*options.primary_ai);
    if (options.unknown_query != UnknownQueryPolicy::Preserve && options.unknown_query != UnknownQueryPolicy::Reject) fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link unknown_query must be Preserve or Reject", "invalid-options", "Preserve or Reject");
    const auto segments = path_segments(url.path);
    const auto start = primary_index(segments, options.primary_ai);
    if ((segments.size() - start) % 2 != 0) fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link path must contain AI/value pairs", "malformed-path");
    DigitalLinkResult out;
    std::set<std::string> seen;
    for (std::size_t i = start; i < segments.size(); i += 2) {
        if (!is_ai(segments[i])) fail_issue("GS1_INVALID_INPUT", "GS1 Digital Link path segment " + std::to_string(i+1) + " must be a GS1 AI", "malformed-path");
        Element e{std::string(segments[i]), percent_decode(segments[i+1], false)};
        normalize_element(e, out.path_elements.size());
        if (!out.path_elements.empty()) assert_path_placement(e.ai, out.path_elements.front().ai);
        reject_duplicate(seen, e.ai); out.path_elements.push_back(std::move(e));
    }
    if (url.query && !url.query->empty()) {
        for (const auto pair : split(*url.query, '&')) {
            if (pair.empty()) continue;
            const auto equals = pair.find('=');
            std::string key = percent_decode(pair.substr(0, equals), true);
            std::string value = equals == std::string_view::npos ? std::string{} : percent_decode(pair.substr(equals+1), true);
            if (is_ai(key)) {
                Element e{std::move(key), std::move(value)};
                element_limit(out.path_elements.size() + out.query_elements.size() + 1);
                normalize_element(e, out.path_elements.size() + out.query_elements.size());
                reject_duplicate(seen, e.ai); out.query_elements.push_back(std::move(e));
            } else if (options.unknown_query == UnknownQueryPolicy::Preserve) out.unknown_query.push_back({std::move(key), std::move(value)});
            else {
                auto detail = issue("GS1_DIGITAL_LINK_UNKNOWN_QUERY", "GS1 Digital Link query parameter " + quote(key) + " is not a GS1 AI", "unknown-query", "GS1 AI query parameter or unknown_query Preserve"); detail.key = key; throw Failure(std::move(detail));
            }
        }
    }
    out.primary = out.path_elements.front();
    out.elements = out.path_elements; out.elements.insert(out.elements.end(), out.query_elements.begin(), out.query_elements.end());
    return out;
}
} // namespace

std::string create_digital_link(const std::vector<Element>& elements, const DigitalLinkOptions& options) {
    if (options.base_url.empty()) fail("GS1 Digital Link base_url is required");
    Url url(options.base_url, true);
    if (url.query && !url.query->empty()) fail("GS1 Digital Link base_url must not include a query");
    url.query.reset();
    check_primary(options.primary_ai);
    std::optional<std::set<std::string>> path_ais;
    if (options.path_ais) {
        element_limit(options.path_ais->size()); path_ais.emplace();
        for (const auto& ai : *options.path_ais) {
            if (!is_ai(ai)) fail("GS1 Digital Link path_ais entries must be 2 to 4 digit AI strings");
            if (ai != options.primary_ai) { assert_path_placement(ai, options.primary_ai); path_ais->insert(ai); }
        }
    }
    element_limit(elements.size()); if (elements.empty()) fail("GS1 Digital Link input elements must not be empty");
    std::set<std::string> seen;
    const Element* primary = nullptr;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        normalize_element(elements[i], i); reject_duplicate(seen, elements[i].ai);
        if (elements[i].ai == options.primary_ai) primary = &elements[i];
    }
    if (!primary) fail("GS1 Digital Link input must include primary AI " + options.primary_ai);
    std::vector<const Element*> path{primary}, query;
    for (const auto& e : elements) {
        if (e.ai == options.primary_ai) continue;
        if (path_ais ? path_ais->count(e.ai) != 0 : path_eligible(e.ai, options.primary_ai)) path.push_back(&e);
        else query.push_back(&e);
    }
    while (!url.path.empty() && url.path.back() == '/') url.path.pop_back();
    for (const auto* e : path) {
        if (e->value == "." || e->value == "..") fail_issue("GS1_INVALID_DIGITAL_LINK_PLACEMENT", "GS1 Digital Link path values must not be dot segments; set path_ais to an empty vector to place these values in the query", "invalid-digital-link-placement");
        url.path += '/' + percent_encode(e->ai, false) + '/' + percent_encode(e->value, false);
    }
    std::sort(query.begin(), query.end(), [](const Element* a, const Element* b) { return a->ai != b->ai ? a->ai < b->ai : a->value < b->value; });
    if (!query.empty()) {
        url.query.emplace();
        for (const auto* e : query) {
            if (!url.query->empty()) *url.query += '&';
            *url.query += percent_encode(e->ai, true) + '=' + percent_encode(e->value, true);
        }
    }
    return url.str();
}
DigitalLinkResult parse_digital_link(std::string_view uri, const DigitalLinkParseOptions& options) { return parse_url(Url(uri), options); }
DigitalLinkValidationResult validate_digital_link(std::string_view uri, const DigitalLinkValidationOptions& options) {
    DigitalLinkValidationResult out;
    if (options.normalize) { out.errors.push_back(issue("GS1_INVALID_INPUT", "GS1 Digital Link validation normalize is unsupported; call normalize_digital_link", "unsupported-option", "false")); return out; }
    try {
        const Url url(uri); auto parsed = parse_url(url, options);
        if (url.scheme == "http") out.warnings.push_back(issue("GS1_DIGITAL_LINK_HTTP", "GS1 Digital Link URI uses http. Use https when transport security is required.", "http-uri"));
        if (!parsed.unknown_query.empty()) {
            auto warning = issue("GS1_DIGITAL_LINK_UNKNOWN_QUERY_PRESERVED", "GS1 Digital Link URI contains non-GS1 query parameters preserved in unknown_query.", "unknown-query-preserved"); warning.count = parsed.unknown_query.size(); out.warnings.push_back(std::move(warning));
        }
        out.result = std::move(parsed); out.ok = true;
    } catch (const Failure& ex) { out.errors.push_back(ex.detail()); }
    return out;
}
std::string normalize_digital_link(std::string_view uri, const DigitalLinkNormalizeOptions& options) {
    if (options.mode != "specqr-deterministic") fail("GS1 Digital Link normalization mode must be specqr-deterministic");
    Url url(uri); const auto parsed = parse_url(url, options); const auto segments = path_segments(url.path);
    const auto start = primary_index(segments, options.primary_ai);
    std::string prefix;
    for (std::size_t i = 0; i < start; ++i) { prefix += '/'; prefix += segments[i]; }
    url.path = prefix.empty() ? "/" : prefix; url.query.reset(); url.empty_fragment = false;
    DigitalLinkOptions create;
    create.base_url = url.str(); create.primary_ai = parsed.primary.ai;
    create.path_ais.emplace();
    for (const auto& e : parsed.elements)
        if (path_eligible(e.ai, parsed.primary.ai) && e.value != "." && e.value != "..") create.path_ais->push_back(e.ai);
    Url normalized(create_digital_link(parsed.elements, create));
    for (const auto& q : parsed.unknown_query) {
        if (!normalized.query) normalized.query.emplace();
        if (!normalized.query->empty()) *normalized.query += '&';
        *normalized.query += percent_encode(q.key, true) + '=' + percent_encode(q.value, true);
    }
    return normalized.str();
}

} // namespace specqr::gs1
