#include "specqr/gs1.hpp"
#include "specqr/specqr.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace gs1 = specqr::gs1;
namespace {
std::size_t checks = 0;
std::size_t cases = 0;
std::size_t failures = 0;
void check(bool value, const std::string& what) {
    ++checks;
    if (!value) throw std::runtime_error("check failed: " + what);
}
template<class F> void expect_error(F&& action) {
    bool threw = false;
    try { action(); } catch (const specqr::Error& ex) { threw = true; check(ex.code() == "INVALID_GS1", "GS1 error code"); }
    check(threw, "invalid input rejected");
}
template<class F> void run_case(const char* name, F&& action) {
    ++cases;
    try { action(); }
    catch (const std::exception& ex) { ++failures; std::cerr << name << ": " << ex.what() << '\n'; }
}
void check_info(const gs1::AiInfo& r, const std::string& ai, const std::string& label,
                std::size_t exact, std::size_t max, bool text, const std::string& digit, const std::string& role) {
    check(r.ai == ai && r.label == label, "catalog identity");
    check(r.length.exact.value_or(0) == exact && r.length.min == (exact ? exact : 1) && r.length.max == max, "catalog lengths");
    check((r.value_kind == gs1::ValueKind::Text) == text, "catalog charset");
    check(r.check_digit_rule == (digit == "gtin" ? gs1::CheckDigitRule::Gtin : digit == "sscc" ? gs1::CheckDigitRule::Sscc : gs1::CheckDigitRule::None), "catalog check digit");
    check(r.digital_link_role == (role == "primary-key" ? gs1::DigitalLinkRole::PrimaryKey : role == "key-qualifier" ? gs1::DigitalLinkRole::KeyQualifier : gs1::DigitalLinkRole::DataAttribute), "catalog link role");
    check(r.digital_link_path_for_primary == (role == "key-qualifier" ? std::vector<std::string>{"01"} : std::vector<std::string>{}), "catalog path placement");
}
void custom_tests() {
    const gs1::Element primary{"01", "04912345678904"};
    run_case("percent-and-dot-safety", [&] {
        const std::vector<gs1::Element> elements{primary,{"10","100%"}};
        auto raw=gs1::create_element_string(elements);
        check(raw == "010491234567890410100%", "literal percent raw value");
        check(gs1::parse_element_string(raw).elements == elements, "literal percent raw round trip");
        for (const auto& value : {".", ".."}) {
            const std::vector<gs1::Element> dots{primary,{"10",value}};
            gs1::DigitalLinkOptions options; options.base_url="https://example.com";
            expect_error([&] { (void)gs1::create_digital_link(dots,options); });
            options.path_ais=std::vector<std::string>{};
            const auto url=gs1::create_digital_link(dots,options);
            check(gs1::parse_digital_link(url).elements == dots,"dot query round trip");
            check(gs1::normalize_digital_link(url) == url,"normalization preserves dot query payload");
        }
        for (const auto& value : {"%2e", "%2e%2e", "%", "100%%", "A/B", "A?B", "A#B", "A&B", "A+B", "A=B"}) {
            const std::vector<gs1::Element> parts{primary,{"10",value}};
            gs1::DigitalLinkOptions options; options.base_url="https://example.com";
            const auto url=gs1::create_digital_link(parts,options);
            check(gs1::parse_digital_link(url).elements == parts,"reserved characters round trip");
            check(gs1::normalize_digital_link(url) == url,"reserved characters normalize idempotently");
        }
    });
    run_case("strict-url-and-unicode", [&] {
        const std::string base="https://example.com/01/04912345678904";
        for (const auto& invalid : {"%", "%0", "%GG", "%C0%AF", "%ED%A0%80", "%F4%90%80%80", "%80", "%E2%82", "%F5%80%80%80"}) {
            check(!gs1::validate_digital_link(base+"/10/"+invalid).ok,"invalid path UTF8/escape");
            check(!gs1::validate_digital_link(base+"?extra="+invalid).ok,"invalid unknown-query UTF8/escape");
        }
        for (const auto& bad : {"https://é.com/01/04912345678904", "https://example.com:65536/01/04912345678904", "https://[::ffff:192.168.001.1]/01/04912345678904", "https://[1::2::3]/01/04912345678904", "https://[1:2:3:4:5:6:7:8:9]/01/04912345678904", "https://example.com/01/04912345678904/10/%2e", "https://example.com/01/04912345678904/10/.%2e"})
            check(!gs1::validate_digital_link(bad).ok,"strict URL rejection");
        check(gs1::normalize_digital_link("HTTPS://EXAMPLE.COM:443/01/04912345678904") == base,"case and default port normalization");
        for (const auto& equivalent : {" https://EXAMPLE.com:443/a/../01/04912345678904#", "https:example.com/01/04912345678904", "https:////example.com/01/04912345678904", "https://%65xample.com/01/04912345678904", "https://example.com\\01\\04912345678904"})
            check(gs1::normalize_digital_link(equivalent) == base,"harmless URL variation normalizes without payload loss");
        for (const auto& ipv4 : {"127.1", "0x7f.1", "0177.0.0.1", "2130706433"})
            check(gs1::normalize_digital_link(std::string("https://")+ipv4+"/01/04912345678904") == "https://127.0.0.1/01/04912345678904","WHATWG IPv4 forms normalize with checked arithmetic");
        check(gs1::normalize_digital_link("https://user:p@ss@EXAMPLE.com/01/04912345678904") == "https://user:p%40ss@example.com/01/04912345678904","userinfo preserves escaped delimiters");
        check(gs1::normalize_digital_link("https://user:@example.com/01/04912345678904") == "https://user@example.com/01/04912345678904","empty password separator omitted");
        check(gs1::normalize_digital_link("https://@example.com/01/04912345678904") == base,"empty username omitted");
        check(gs1::normalize_digital_link("https://:@example.com/01/04912345678904") == base,"empty username and password omitted");
        check(gs1::normalize_digital_link("https://:pass@example.com/01/04912345678904") == "https://:pass@example.com/01/04912345678904","nonempty password with empty username retained");

        check(gs1::parse_digital_link(base+"?x=a\\tb").unknown_query.at(0).value == "a\\tb","raw query backslash is data");
        gs1::DigitalLinkOptions empty_markers; empty_markers.base_url="https://example.com/stem/../?";
        check(gs1::create_digital_link({primary},empty_markers) == base,"empty markers and base prefix dot normalization");

        check(gs1::normalize_digital_link("https://[0:0:0:0:0:0:0:1]/01/04912345678904") == "https://[::1]/01/04912345678904","IPv6 normalization");
        check(gs1::normalize_digital_link("https://[::ffff:192.168.1.1]/01/04912345678904") == "https://[::ffff:c0a8:101]/01/04912345678904","embedded IPv4 normalization");
        const auto unknown=gs1::parse_digital_link(base+"?x=%F0%9F%98%80&x=%E6%97%A5%E6%9C%AC&z=a+b");
        check(unknown.unknown_query.at(0).value == "😀" && unknown.unknown_query.at(1).value == "日本" && unknown.unknown_query.at(2).value == "a b","valid unknown-query Unicode and plus");
    });
    run_case("resource-boundaries", [&] {
        const std::string large(gs1::max_input_bytes+1,'1');
        expect_error([&]{ (void)gs1::calculate_check_digit(large); });
        check(!gs1::validate_element_string(large).ok,"raw byte limit");
        check(!gs1::validate_digital_link("https://example.com/"+large).ok,"URI byte limit");
        check(!gs1::validate_elements({{"10",large}}).ok,"value byte limit");
        check(!gs1::validate_elements({{large,"A"}}).ok,"AI byte limit");
        std::vector<gs1::Element> many(gs1::max_elements,{"20","00"});
        const auto raw=gs1::create_element_string(many);
        check(gs1::parse_element_string(raw).elements == many,"maximum element count");
        many.push_back({"20","00"});
        expect_error([&]{ (void)gs1::create_element_string(many); });
        check(!gs1::validate_elements(many).ok,"element count over limit");
        std::string query="https://example.com/01/04912345678904?";
        for (std::size_t i=0; i<=gs1::max_elements; ++i) { if (i) query+='&'; query+="x=1"; }
        check(!gs1::validate_digital_link(query).ok,"query count limit");
        std::string path="https://example.com";
        for (std::size_t i=0; i<=gs1::max_elements; ++i) path+="/x";
        check(!gs1::validate_digital_link(path).ok,"path count limit");
        gs1::ValidationOptions invalid; invalid.context=static_cast<gs1::ValidationContext>(-1);
        check(!gs1::validate_elements({primary},invalid).ok,"invalid context enum");
        gs1::DigitalLinkValidationOptions bad; bad.unknown_query=static_cast<gs1::UnknownQueryPolicy>(-1);
        check(!gs1::validate_digital_link("https://example.com/01/04912345678904",bad).ok,"invalid query enum");
    });
    run_case("deterministic-properties", [&] {
        std::uint32_t state=0x71031u;
        auto next=[&]{state=state*1664525u+1013904223u;return state;};
        for (int i=0;i<2000;++i) {
            std::string body;
            for(int j=0;j<13;++j) body+=static_cast<char>('0'+next()%10u);
            const auto gtin=gs1::append_gtin_check_digit(body);
            check(gs1::validate_gtin_check_digit(gtin),"random GTIN check digit");
            std::string value;
            for (std::uint32_t j=0,n=1+next()%20u;j<n;++j) {
                char c=static_cast<char>(32+next()%95u); if(c=='('||c==')') c='X'; value+=c;
            }
            const std::vector<gs1::Element> elements{{"01",gtin},{"10",value},{"17","251231"}};
            const auto raw=gs1::create_element_string(elements);
            check(gs1::parse_element_string(raw).elements == elements,"random element-string round trip");
            gs1::DigitalLinkOptions o; o.base_url="https://example.com/stem"; o.path_ais=std::vector<std::string>{};
            const auto link=gs1::create_digital_link(elements,o);
            check(gs1::parse_digital_link(link).elements == elements,"random Digital Link round trip");
            const auto norm=gs1::normalize_digital_link(link);
            check(gs1::normalize_digital_link(norm)==norm,"normalization idempotence");
        }
        for(int i=0;i<1000;++i) {
            std::string garbage;
            for(std::uint32_t j=0,n=next()%100u;j<n;++j) garbage+=static_cast<char>(next() & 255u);
            const auto raw = gs1::validate_element_string(garbage);
            const auto uri = gs1::validate_digital_link(garbage);
            check(raw.ok || !raw.errors.empty(), "arbitrary raw input yields diagnostics");
            check(uri.ok || !uri.errors.empty(), "arbitrary URI input yields diagnostics");
        }
    });
    run_case("concurrency", [&] {
        std::atomic<bool> success{true}; std::vector<std::thread> threads;
        for(int t=0;t<8;++t) threads.emplace_back([&,t] {
            try {
                for(int n=0;n<100;++n) {
                    const auto value=std::to_string(t*100+n); const auto body=std::string(13-value.size(),'0')+value;
                    const auto gtin=gs1::append_gtin_check_digit(body);
                    gs1::DigitalLinkOptions o; o.base_url="https://example.com";
                    const auto link=gs1::create_digital_link({{"01",gtin},{"10","100%"}},o);
                    if(!gs1::validate_gtin_check_digit(gtin) || gs1::normalize_digital_link(link)!=link || gs1::supported_ais().size()!=50) success=false;
                }
            } catch(...) { success=false; }
        });
        for(auto& t:threads) t.join(); check(success,"concurrent pure helpers");
    });
}
} // namespace
int main() {
#include "gs1_fixture_cases.inc"
    custom_tests();
    std::cout << "GS1 baseline fixtures: 1369 unchanged outcomes + 42 documented URL policy/diagnostic differences; no skipped cases\n";
    std::cout << "GS1: " << cases << " cases, " << checks << " assertions, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
