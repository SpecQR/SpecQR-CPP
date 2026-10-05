#include <specqr/gs1.hpp>
#include <stdexcept>
#include <string>

int main() {
    using namespace specqr::gs1;
    const std::string suffix = "/01/04912345678904";
    for (const auto* base : {"https://example.com/a^b", "https://example.com/a%5Eb"}) {
        DigitalLinkOptions options; options.base_url = base;
        if (create_digital_link({{"01", "04912345678904"}}, options) != "https://example.com/a%5Eb" + suffix)
            throw std::runtime_error("caret path creation");
        if (normalize_digital_link(std::string(base) + suffix) != "https://example.com/a%5Eb" + suffix)
            throw std::runtime_error("caret path normalization");
    }
    DigitalLinkOptions credentials; credentials.base_url = "https://user:p@ss@example.com/a^b";
    if (create_digital_link({{"01", "04912345678904"}}, credentials) != "https://user:p%40ss@example.com/a%5Eb" + suffix)
        throw std::runtime_error("credential escaping");
    const auto nul = "https://example.com" + suffix + "?x=%00";
    if (normalize_digital_link(nul) != nul) throw std::runtime_error("query NUL");
}
