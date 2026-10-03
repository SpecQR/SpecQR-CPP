#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <specqr/scan.hpp>
#include <specqr/gs1.hpp>
#include <specqr/structured_append.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F&& f, const char* message) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}
std::string scalar(std::uint32_t cp) {
    std::string s;
    if (cp < 0x80U) s += static_cast<char>(cp);
    else if (cp < 0x800U) {
        s += static_cast<char>(0xC0U | (cp >> 6U));
        s += static_cast<char>(0x80U | (cp & 63U));
    } else if (cp < 0x10000U) {
        s += static_cast<char>(0xE0U | (cp >> 12U));
        s += static_cast<char>(0x80U | ((cp >> 6U) & 63U));
        s += static_cast<char>(0x80U | (cp & 63U));
    } else {
        s += static_cast<char>(0xF0U | (cp >> 18U));
        s += static_cast<char>(0x80U | ((cp >> 12U) & 63U));
        s += static_cast<char>(0x80U | ((cp >> 6U) & 63U));
        s += static_cast<char>(0x80U | (cp & 63U));
    }
    return s;
}
void unicode_and_controls() {
    using namespace specqr;
    Options byte; byte.mode = Mode::Byte; byte.version = 2; byte.mask = 3;
    std::vector<std::uint32_t> points{0,1,0x1A,0x1D,0x7F,0x80,0x7FF,0x800,0xD7FF,0xE000,0xFFFF,0x10000,0x10FFFF};
    std::mt19937 random(0x52455649U);
    for (int i = 0; i < 500; ++i) {
        const auto cp = random() % 0x110000U;
        if (cp < 0xD800U || cp > 0xDFFFU) points.push_back(cp);
    }
    for (const auto cp : points) {
        const auto text = scalar(cp);
        const auto bytes = std::vector<std::uint8_t>(text.begin(), text.end());
        const auto t = QRCode::generate(text, byte), b = QRCode::generate(bytes, byte);
        require(t.matrix() == b.matrix() && t.codewords() == b.codewords(), "strict UTF-8 text preserves original bytes");
        require(Segment::byte(text).character_count() == 1, "Unicode scalar count");
    }
    for (std::uint32_t cp = 0xD800; cp <= 0xDFFF; cp += 37)
        rejects([&] { Segment::byte(scalar(cp)); }, "surrogates must be rejected");
    for (int lead = 0x80; lead <= 0xFF; ++lead) {
        const std::string s(1, static_cast<char>(lead));
        rejects([&] { QRCode::estimate(s); }, "lone non-ASCII byte must fail UTF-8");
        require(Segment::byte(std::vector<std::uint8_t>{static_cast<std::uint8_t>(lead)}).byte_count() == 1, "binary path preserves lone bytes");
    }
    for (const auto& invalid : {std::string("\xC0\x80",2),std::string("\xE0\x9F\xBF",3),std::string("\xF0\x8F\xBF\xBF",4),std::string("\xF4\x90\x80\x80",4),std::string("\xF5\x80\x80\x80",4),std::string("\xC2X",2),std::string("\xE1\x80X",3)})
        rejects([&] { QRCode::generate(invalid); }, "malformed UTF-8 class");
    for (int assignment : {0,127,128,16383,16384,999999}) {
        const auto eci = Segment::eci(assignment);
        require(eci.bit_length(1) == (assignment < 128 ? 12 : assignment < 16384 ? 20 : 28), "ECI prefix transition lengths");
        require(QRCode::generate_segments({eci, Segment::byte("x")}).planning().eci_assignment_number() == assignment, "ECI metadata");
    }
    for (bool optimize : {false,true}) for (bool gs1 : {false,true}) {
        Options o; o.optimize_segments = optimize;
        const auto text = gs1 ? "10ABC%DEF" : "ABC%DEF";
        if (gs1) o.gs1 = true; else o.fnc1_second = "A";
        const auto qr = QRCode::generate(text,o);
        require(qr.segments().size() == 2 && qr.segments()[1].mode() == Mode::Byte, "high-level FNC1 percent byte safety");
        require(qr.segments()[1].text() == text, "high-level FNC1 percent exact bytes");
        o.mode = Mode::Alphanumeric;
        rejects([&] { QRCode::generate(text,o); }, "forced unsafe FNC1 percent rejected");
    }
}
void rendering_edges() {
    using namespace specqr;
    const auto qr = QRCode::generate("review");
    RenderOptions o;
    o.title = "<script>\"&</script>";
    const auto svg = to_svg(qr,o);
    require(svg.find("<script>") == std::string::npos && svg.find("&lt;script&gt;&quot;&amp;") != std::string::npos, "SVG title XML escaping");
    for (const auto& title : {std::string("\0",1),std::string("\x01",1),std::string("\xEF\xBF\xBE",3),std::string("\xED\xA0\x80",3),std::string("\xF4\x90\x80\x80",4)}) {
        o.title = title;
        std::ostringstream destination;
        rejects([&] { save_svg(qr,destination,o); }, "invalid XML title");
        require(destination.str().empty(), "invalid render leaves stream untouched");
    }
    o = {};
    for (int margin : {INT_MIN,-1,INT_MAX}) {
        o.margin = margin;
        rejects([&] { to_svg(qr,o); }, "render margin arithmetic boundary");
    }
    o = {}; o.scale = INT_MAX;
    rejects([&] { to_svg(qr,o); }, "render scale arithmetic boundary");
    o = {}; o.width = INT_MAX;
    rejects([&] { to_rgba(qr,o); }, "render width arithmetic boundary");
    for (double dpi : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::denorm_min()}) {
        o = {}; o.print_dpi = dpi;
        rejects([&] { to_svg(qr,o); }, "DPI finite physical result");
    }
    o = {}; o.scale = 100;
    rejects([&] { to_png(qr,o); }, "raster preallocation bound");
    require(!to_svg(qr,o).empty(), "large vector output supported");
    o = {}; o.foreground = "url(https://example.invalid/a)";
    rejects([&] { to_svg(qr,o); }, "SVG external-reference color rejected");
    o = {}; o.title = std::string(65537,'x');
    rejects([&] { to_svg(qr,o); }, "title resource cap");

    Plan arbitrary;
    arbitrary.capacity_bits = INT_MAX; arbitrary.data_bit_length = INT_MIN;
    require(arbitrary.remaining_bits() == static_cast<std::int64_t>(INT_MAX) - INT_MIN, "public Plan subtraction cannot overflow");
    rejects([&] { render_diagnostics(arbitrary); }, "inconsistent public Plan rejected by diagnostics");
    Capacity capacity{}; capacity.version = INT_MAX; capacity.data_codewords = INT_MIN; capacity.total_codewords = INT_MAX;
    require(capacity.size() == static_cast<std::int64_t>(INT_MAX) * 4 + 17, "public Capacity geometry arithmetic widened");
    require(capacity.capacity_bits() == static_cast<std::int64_t>(INT_MIN) * 8, "public Capacity bit arithmetic widened");
    require(capacity.error_correction_codewords() == static_cast<std::int64_t>(INT_MAX) - INT_MIN, "public Capacity codeword arithmetic widened");
    StructuredAppendSymbolDiagnostics diagnostic{}; diagnostic.index = INT_MIN; diagnostic.total = INT_MAX;
    require(diagnostic.sequence_indicator() == (static_cast<std::int64_t>(INT_MIN) - 1) * 16 + static_cast<std::int64_t>(INT_MAX) - 1, "public SA diagnostic arithmetic widened");
    auto movable = QRCode::generate("moved-from safety");
    const auto moved = std::move(movable);
    require(moved.size() >= 21, "moved-to QR remains valid");
    if (movable.size() == 0) {
        rejects([&] { movable.module(0,0); }, "moved-from module lookup safely rejected");
        rejects([&] { to_svg(movable); }, "moved-from render safely rejected");
    } else require(movable.module(0,0) == moved.module(0,0), "valid retained source after move");
}
std::string payload(const specqr::QRCode& qr) {
    std::string result;
    for (const auto& segment : qr.segments()) if (!segment.is_control()) result += segment.text();
    return result;
}
std::string first_scalar(const std::string& text) {
    const auto c = static_cast<unsigned char>(text.at(0));
    const auto n = c < 0x80 ? 1U : c < 0xE0 ? 2U : c < 0xF0 ? 3U : 4U;
    return text.substr(0,n);
}
void append_edges() {
    using namespace specqr;
    std::mt19937 random(0x53415256U);
    const std::vector<std::string> alphabet{"1","9","A","%","x",u8"é",u8"漢",u8"😀",std::string("\0",1)};
    for (int iteration = 0; iteration < 40; ++iteration) {
        std::string source;
        for (int i = 0; i < 90; ++i) source += alphabet[random() % alphabet.size()];
        StructuredAppendOptions o; o.qr_options.version = 2; o.qr_options.ecc = Ecc::L;
        o.qr_options.optimize_segments = iteration % 2 == 0;
        o.qr_options.mask = iteration % 8; o.diagnostics = true;
        const auto set = generate_structured_append(source,o);
        require(set.total() >= 2 && set.total() <= 16, "SA symbol bound");
        std::string reconstructed;
        std::vector<StructuredAppendPart> parts;
        for (std::size_t i = 0; i < set.symbols().size(); ++i) {
            const auto& qr = set.symbols()[i];
            const auto chunk = payload(qr);
            reconstructed += chunk;
            const auto header = qr.planning().structured_append();
            require(header && header->index == static_cast<int>(i) + 1 && header->total == set.total(), "SA header indexing");
            Options check = o.qr_options; check.structured_append = header;
            require(QRCode::estimate(chunk,check).data_bit_length == qr.planning().data_bit_length, "SA tracker agrees with planner");
            parts.emplace_back(static_cast<int>(i) + 1,set.total(),set.parity(),chunk);
            if (i + 1 < set.symbols().size()) {
                const auto next = payload(set.symbols()[i+1]);
                require(!QRCode::estimate(chunk + first_scalar(next),check).ok, "SA greedy prefix cannot absorb next scalar");
            }
        }
        require(reconstructed == source, "SA Unicode boundaries preserve exact input");
        std::reverse(parts.begin(),parts.end());
        const auto merged = merge_structured_append_parts(parts);
        require(merged.text() && *merged.text() == source && merged.diagnostics().parity_check.matches(), "SA reordered merge exact bytes and parity");
    }
    StructuredAppendOptions o; o.qr_options.version = 2; o.qr_options.ecc = Ecc::L;
    o.diagnostics = true;
    const std::vector<Segment> segments{Segment::byte(std::string(300,'a'))};
    const auto summary = generate_segments_structured_append(segments,o);
    o.split_units = SplitUnitsDetail::Full;
    const auto full = generate_segments_structured_append(segments,o);
    require(summary.total() == 10 && full.total() == 10, "manual byte SA count field splits before validation");
    require(!summary.diagnostics().split_units && full.diagnostics().split_units && full.diagnostics().split_units->size() == 300, "SA summary/full storage contract");
    for (int i = 0; i < full.total(); ++i)
        require(summary.symbols()[static_cast<std::size_t>(i)].codewords() == full.symbols()[static_cast<std::size_t>(i)].codewords(), "SA diagnostic detail does not alter encoding");
    const std::vector<std::uint8_t> a{0xFF,0x00,0x1A}, b{0x80,0x0D,0x0A};
    auto all = a; all.insert(all.end(),b.begin(),b.end());
    const auto parity = structured_append_parity(all);
    std::vector<StructuredAppendPart> parts{StructuredAppendPart(2,2,parity,b),StructuredAppendPart(1,2,parity,a)};
    require(merge_structured_append_parts(parts).bytes() == all, "binary merge preserves all bytes");
    rejects([&] { merge_structured_append_parts({parts[0]}); }, "incomplete merge rejected");
    rejects([&] { merge_structured_append_parts({parts[0],parts[0]}); }, "duplicate merge rejected");
    rejects([&] { merge_structured_append_parts({StructuredAppendPart(1,2,static_cast<std::uint8_t>(parity^1U),a),StructuredAppendPart(2,2,static_cast<std::uint8_t>(parity^1U),b)}); }, "wrong claimed parity rejected");
}
void gs1_edges() {
    using namespace specqr::gs1;
    require(supported_ais().size() == 50, "bounded GS1 catalog");
    for (const std::string& value : {std::string("."),std::string("..")}) {
        const std::vector<Element> elements{{"01","09506000134352"},{"10",value}};
        DigitalLinkOptions o; o.base_url = "https://example.com";
        rejects([&] { create_digital_link(elements,o); }, "literal dot-segment path rejected");
        o.path_ais = std::vector<std::string>{};
        const auto url = create_digital_link(elements,o);
        require(parse_digital_link(url).elements == elements, "literal dot-only query survives roundtrip");
        require(parse_digital_link(normalize_digital_link(url)).elements == elements, "literal dot-only query survives normalization");
    }
    for (const std::string& value : {std::string("%2e"),std::string("%2e%2e"),std::string("A% B/C?D#E")}) {
        const std::vector<Element> elements{{"01","09506000134352"},{"10",value}};
        DigitalLinkOptions o; o.base_url = "https://example.com";
        require(parse_digital_link(create_digital_link(elements,o)).elements == elements, "reserved Digital Link data survives roundtrip");
    }
    for (const std::string& suffix : {std::string("?10=%"),std::string("?10=%0"),std::string("?10=%GG"),std::string("?10=%C0%AF"),std::string("?10=%ED%A0%80"),std::string("?10=%00"),std::string("?10=A&10=B")})
        rejects([&] { parse_digital_link("https://example.com/01/09506000134352" + suffix); }, "malformed Digital Link escape/duplicate rejected");
    ValidationOptions opts; opts.allow_unsupported_ai = true;
    require(!validate_elements({{"01","09506000134352"}},opts).ok, "unsupported validation policy cannot silently broaden catalog");
}
void structured_uri_properties() {
    using namespace specqr::gs1;
    const std::vector<std::string> starts{"https://","HTTP://"," https://","https:/","https:","https:////","https://\t","https://"};
    const std::vector<std::string> hosts{"example.com","EXAMPLE.com:443","%65xample.com","user:pass@example.com","user%20x:pa%23ss@example.com","user:p@ss@example.com","127.0.0.1","127.1","0x7f.1","0177.0.0.1","2130706433","0x","0x+1","0+7","0x10000000000000000z","[::1]","[0:0:0:0:0:0:0:1]","[::ffff:192.168.1.1]","[1:2:3:4:5:6:7:8:9]","example.com:65536","example.com:","xn--9ca.com",u8"é.com","example.com."};
    const std::vector<std::string> prefixes{"","/stem","/a/../b","/a/%2e%2E/b","/a/./b","/%2e","/","/x//y"};
    const std::vector<std::string> tails{"","/10/ABC%25D","/10/%252e","/10/.","/10/..","/10/%2e","/10/.%2e","/10/A%2FB","/10/%F0%9F%98%80"};
    const std::vector<std::string> queries{"","?","?x=a+b","?x=a%2Bb&x=b","?x=hello%00world","?x=a\\tb","?x=%F0%9F%98%80","?x=%","?x=%C0%AF","?17=251231","?10=ABC%25D","?x=1#","#","#fragment"};
    const auto sorted = [](std::vector<Element> elements) {
        std::sort(elements.begin(),elements.end(),[](const Element& a,const Element& b) { return a.ai < b.ai; });
        return elements;
    };
    std::mt19937 random(0x55524953U);
    for (int iteration = 0; iteration < 4000; ++iteration) {
        const auto uri = starts[random()%starts.size()] + hosts[random()%hosts.size()] + prefixes[random()%prefixes.size()] + "/01/09506000134352" + tails[random()%tails.size()] + queries[random()%queries.size()];
        try {
            const auto before = parse_digital_link(uri);
            const auto normalized = normalize_digital_link(uri);
            const auto after = parse_digital_link(normalized);
            require(normalize_digital_link(normalized) == normalized, "structured URI normalization idempotence");
            require(before.primary == after.primary && sorted(before.elements) == sorted(after.elements), "structured URI normalization preserves GS1 data");
            require(before.unknown_query == after.unknown_query, "structured URI normalization preserves unknown query bytes/order");
        } catch (const std::invalid_argument&) {
            require(!validate_digital_link(uri).ok, "structured URI rejection agrees with validator");
        }
    }
}
}
int main() {
    try {
        unicode_and_controls(); rendering_edges(); append_edges(); gs1_edges(); structured_uri_properties();
        std::cout << "specqr-independent-review: " << checks << " assertions passed; 0 skipped\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "specqr-independent-review failed after " << checks << " assertions: " << e.what() << '\n';
        return 1;
    }
}
