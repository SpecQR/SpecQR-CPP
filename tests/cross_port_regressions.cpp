#include <specqr/specqr.hpp>
#include <specqr/gs1.hpp>
#include <specqr/render.hpp>
#include <specqr/scan.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition) { ++checks; if (!condition) throw std::runtime_error("cross-port regression"); }
template<class F> void rejects(F fn) { bool rejected = false; try { fn(); } catch (const std::invalid_argument&) { rejected = true; } check(rejected); }
}
int main() {
    using namespace specqr;
    using namespace specqr::gs1;
    for (const auto* tail : {".", "..", "%2e", "%2E%2e", ".%2E", "%2e."}) {
        const auto uri = std::string("https://example.com/01/04912345678904/10/") + tail;
        rejects([&] { parse_digital_link(uri); });
        check(!validate_digital_link(uri).ok);
        rejects([&] { normalize_digital_link(uri); });
    }
    const std::string erased = "https://example.com/01/./../01/04912345678904";
    rejects([&] { parse_digital_link(erased); });
    check(!validate_digital_link(erased).ok);
    rejects([&] { normalize_digital_link(erased); });
    const std::string query = "https://example.com/01/04912345678904?10=..&21=.&utm=a&utm=b";
    check(normalize_digital_link(query) == query);
    check(validate_digital_link(query).ok);
    check(normalize_digital_link("https://example.com/01/04912345678904?10=..&21=SERIAL&utm=a") == "https://example.com/01/04912345678904/21/SERIAL?10=..&utm=a");
    DigitalLinkOptions link; link.base_url = "https://example.com/a/../b";
    check(create_digital_link({{"01","04912345678904"},{"10","%2e"}},link) == "https://example.com/b/01/04912345678904/10/%252e");
    for (const auto* dot : {".", ".."}) {
        rejects([&] { create_digital_link({{"01","04912345678904"},{"10",dot}},link); });
    }
    Options options; options.version = 1;
    const auto plan = QRCode::estimate("A",options);
    const auto qr = QRCode::generate("A",options);
    RenderOptions render;
    for (double dpi : {std::numeric_limits<double>::denorm_min(),1e-305}) {
        render.print_dpi = dpi;
        rejects([&] { render_diagnostics(plan,render); });
        rejects([&] { render_diagnostics(qr,render); });
    }
    render.print_dpi = 1e-304;
    check(std::isfinite(*render_diagnostics(plan,render).symbol_size_mm));
    options.version = 40;
    rejects([&] { render_diagnostics(QRCode::estimate("A",options),render); });
    render.print_dpi = 300;
    check(*render_diagnostics(plan,render).symbol_size_mm == 232.0 / 300 * 25.4);
    options = {}; options.ecc = static_cast<Ecc>(99);
    rejects([&] { QRCode::generate("A",options); });
    rejects([&] { QRCode::estimate("A",options); });
    std::cout << "Cross-port regressions: " << checks << " checks passed\n";
}
