#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <iostream>
int main() {
    specqr::Options options;
    options.ecc = specqr::Ecc::Q;
    const auto code = specqr::QRCode::generate("Hello, SpecQR!", options);
    std::cout << specqr::to_svg(code);
}
