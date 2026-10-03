#include "internal.hpp"
#include <climits>
#include <iostream>
#include <random>
#include <string>
#include <utility>

using namespace specqr;
namespace {
std::size_t assertions = 0;
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& fn, std::string_view expected) {
    try { fn(); } catch (const Error& e) { check(e.code() == expected, "Wrong stable Error code"); return; }
    throw std::runtime_error("Expected Error was not thrown");
}
std::string utf8(std::uint32_t cp) {
    std::string result;
    if (cp < 0x80U) result.push_back(static_cast<char>(cp));
    else if (cp < 0x800U) { result.push_back(static_cast<char>(0xC0U | cp / 64U)); result.push_back(static_cast<char>(0x80U | cp % 64U)); }
    else if (cp < 0x10000U) { result.push_back(static_cast<char>(0xE0U | cp / 4096U)); result.push_back(static_cast<char>(0x80U | cp / 64U % 64U)); result.push_back(static_cast<char>(0x80U | cp % 64U)); }
    else { result.push_back(static_cast<char>(0xF0U | cp / 262144U)); result.push_back(static_cast<char>(0x80U | cp / 4096U % 64U)); result.push_back(static_cast<char>(0x80U | cp / 64U % 64U)); result.push_back(static_cast<char>(0x80U | cp % 64U)); }
    return result;
}
void unicode() {
    std::size_t scalars = 0, kanji = 0;
    for (std::uint32_t cp = 0; cp <= 0x10FFFFU; ++cp) {
        if (cp >= 0xD800U && cp <= 0xDFFFU) continue;
        const auto text = utf8(cp);
        const auto decoded = detail::decode_utf8(text);
        check(decoded.size() == 1 && decoded[0].value == cp && decoded[0].offset == 0 && decoded[0].bytes == static_cast<int>(text.size()), "Unicode scalar round trip");
        if (const auto value = detail::kanji_value(cp)) { check(*value >= 0 && *value < 8192, "Kanji 13-bit range"); ++kanji; }
        ++scalars;
    }
    check(scalars == 1112064 && kanji == 6953, "Unicode or Kanji coverage count");
    const std::vector<std::string> invalid{"\x80", "\xBF", "\xC0\x80", "\xC1\xBF", "\xC2", "\xC2\x20", "\xE0\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF", "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFE", "\xFF", "\xE2\x82", "\xF0\x9F\x98"};
    for (const auto& text : invalid) {
        rejects([&]{ (void)detail::decode_utf8(text); }, "INVALID_INPUT");
        rejects([&]{ (void)Segment::byte(text); }, "INVALID_INPUT");
        rejects([&]{ (void)QRCode::estimate(text); }, "INVALID_INPUT");
    }
    const std::string nul("A\0B", 3);
    const auto segment = Segment::byte(nul);
    check(segment.character_count() == 3 && segment.byte_count() == 3 && segment.bytes()[1] == 0, "Embedded NUL retained");
    check(QRCode::generate(nul).data_codewords() == QRCode::generate(std::vector<std::uint8_t>{'A',0,'B'}).data_codewords(), "Embedded NUL encodes identically");
    std::mt19937 engine(0x51A7U);
    for (int trial = 0; trial < 100000; ++trial) {
        const auto length = engine() % 17U;
        std::string text;
        for (unsigned i = 0; i < length; ++i) text.push_back(static_cast<char>(engine() & 255U));
        try {
            const auto values = detail::decode_utf8(text);
            std::string rebuilt;
            for (const auto& value : values) {
                check(value.value <= 0x10FFFFU && !(value.value >= 0xD800U && value.value <= 0xDFFFU), "Fuzz accepted non-scalar");
                check(value.offset == rebuilt.size(), "UTF8 offset continuity");
                rebuilt += utf8(value.value);
            }
            check(rebuilt == text, "Fuzz accepted noncanonical UTF8");
        } catch (const Error& e) { check(e.code() == "INVALID_INPUT", "Fuzz UTF8 error code"); }
    }
}
void arithmetic_and_resources() {
    Plan plan;
    plan.capacity_bits = INT_MIN; plan.data_bit_length = INT_MAX;
    check(plan.remaining_bits() == -4294967295LL && plan.overflow_bits() == 4294967295LL, "Plan arithmetic extremes");
    plan.capacity_bits = INT_MAX; plan.data_bit_length = INT_MIN;
    check(plan.remaining_bits() == 4294967295LL && plan.overflow_bits() == 0, "Plan reversed arithmetic extremes");
    Capacity cap{INT_MAX,Ecc::M,INT_MIN,INT_MAX,{}, {},0,{}, {},{}};
    check(cap.size() == 8589934605LL && cap.capacity_bits() == -17179869184LL && cap.error_correction_codewords() == 4294967295LL, "Capacity arithmetic extremes");
    const auto limited = QRCode::capacity(1,Ecc::M,Mode::Byte,INT_MAX);
    check(limited.payload_bits == 0 && limited.max_bytes == 0, "Capacity with huge controls");
    const std::string huge(max_input_bytes, '1');
    const auto estimate = QRCode::estimate(huge);
    check(!estimate.ok && estimate.data_bit_length > estimate.capacity_bits, "Bounded huge input preflight");
    rejects([&]{ (void)QRCode::generate(huge); }, "DATA_TOO_LONG");
    rejects([&]{ (void)QRCode::estimate(huge + '1'); }, "INVALID_INPUT");
    rejects([&]{ (void)Segment::byte(std::vector<std::uint8_t>(max_input_bytes + 1)); }, "INVALID_INPUT");
    for (auto source : {Segment::numeric("123"), Segment::alphanumeric("ABC"), Segment::byte("UTF8"), Segment::byte(std::vector<std::uint8_t>{0,128,255}), Segment::kanji("漢字"), Segment::eci(26), Segment::fnc1(), Segment::fnc1_second("A"), Segment::structured_append(1,2,123)}) {
        const auto expected_mode = source.mode();
        const auto expected_bits = source.bit_length(1);
        auto target = std::move(source);
        check(target.mode() == expected_mode && target.bit_length(1) == expected_bits, "Segment move retains target");
        check(source.mode() == Mode::Byte && source.character_count() == 0 && source.byte_count() == 0 && source.text().empty() && source.bytes().empty() && !source.assignment_number() && !source.application_indicator() && !source.header(), "Moved source resets to empty Byte");
        check(QRCode::generate_segments({source}).data_codewords() == QRCode::generate_segments({Segment::byte("")}).data_codewords(), "Moved source emits coherent empty Byte");
        source = std::move(target);
        check(source.mode() == expected_mode && source.bit_length(1) == expected_bits && target.mode() == Mode::Byte && target.byte_count() == 0, "Segment move assignment resets source");
    }
    auto original = QRCode::generate("MOVE");
    auto moved = std::move(original);
    check(moved.size() == 21, "Moved symbol remains usable");
    // Moved-from storage is valid but unspecified; accessing a missing module must not index empty storage.
    if (original.matrix().empty()) rejects([&]{ (void)original.module(0,0); }, "INVALID_INPUT");
    rejects([&]{ (void)moved.module(INT_MIN,INT_MAX); }, "INVALID_INPUT");
    for (const int version : {INT_MIN,0,41,INT_MAX}) rejects([&]{ (void)QRCode::capacity(version); }, "INVALID_VERSION");
    for (const int ecc : {INT_MIN,-1,4,INT_MAX}) rejects([&]{ (void)QRCode::capacity(1,static_cast<Ecc>(ecc)); }, "INVALID_INPUT");
    for (const int mode : {INT_MIN,-1,10,INT_MAX}) { Options options; options.mode=static_cast<Mode>(mode); rejects([&]{ (void)QRCode::estimate("X",options); }, "INVALID_MODE"); }
}
void controls() {
    for (const int assignment : {0,127,128,16383,16384,999999}) {
        const auto segment = Segment::eci(assignment);
        check(segment.bit_length(1) == (assignment < 128 ? 12 : assignment < 16384 ? 20 : 28), "ECI width transitions");
        (void)QRCode::generate_segments({segment,Segment::byte("X")});
    }
    for (const int assignment : {INT_MIN,-1,1000000,INT_MAX}) rejects([&]{ (void)Segment::eci(assignment); }, "INVALID_ECI");
    for (const auto& text : {std::string("%"),std::string("ABC%123"),std::string("ABC%%DEF")}) {
        Options options; options.fnc1_second = "A";
        const auto plan = QRCode::estimate(text,options);
        check(plan.segments.size() == 2 && plan.segments[1].mode() == Mode::Byte && plan.segments[1].text() == text, "FNC1 literal percent safety");
        options.mode = Mode::Alphanumeric;
        rejects([&]{ (void)QRCode::estimate(text,options); }, "INVALID_MODE");
    }
    rejects([]{ (void)QRCode::generate_segments({Segment::fnc1(), Segment::eci(26), Segment::byte("X")}); }, "INVALID_GS1");
    rejects([]{ (void)QRCode::generate_segments({Segment::byte("X"), Segment::structured_append(1,2,0)}); }, "INVALID_MODE");
    rejects([]{ (void)QRCode::generate_segments({Segment::fnc1(), Segment::fnc1(), Segment::byte("X")}); }, "INVALID_GS1");
    std::vector<Segment> too_many(max_segments + 1, Segment::eci(26));
    rejects([&]{ (void)QRCode::analyze_segments(too_many); }, "INVALID_INPUT");
    std::vector<Segment> too_large{Segment::byte(std::string(600000,'A')),Segment::byte(std::string(600000,'A'))};
    rejects([&]{ (void)QRCode::analyze_segments(too_large); }, "INVALID_INPUT");
}
}
int main() {
    try { unicode(); arithmetic_and_resources(); controls(); std::cout << "PASS " << assertions << " core-edge assertions; 1112064 Unicode scalars, 6953 Kanji entries, 100000 malformed-input fuzz trials\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
