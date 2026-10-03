#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <iostream>
int main() {
    specqr::Options options;
    options.version = 1;
    options.ecc = specqr::Ecc::M;
    options.mask = 0;
    const auto qr = specqr::QRCode::generate("HELLO WORLD", options);
    if (qr.size() != 21 || qr.data_codewords().size() != 16 || qr.codewords().size() != 26)
        return 1;
    const auto image = specqr::to_png(qr);
    if (image.size() < 8 || image[0] != 137 || image[1] != 'P') return 2;
    std::cout << "installed-consumer: passed, size=" << qr.size() << '\n';
}
