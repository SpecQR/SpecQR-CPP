#include "internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace specqr::detail {
namespace {

// QR Model 2 parameters from the user-owned SpecQR reference. Rows are
// L, M, Q, H; version zero is deliberately unused.
constexpr std::array<std::array<int, 41>, 4> ecc_lengths{{
    {{0,7,10,15,20,26,18,20,24,30,18,20,24,26,30,22,24,28,30,28,28,28,28,30,30,26,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30}},
    {{0,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28}},
    {{0,13,22,18,26,18,24,18,22,20,24,28,26,24,20,30,24,28,28,26,30,28,30,30,30,30,28,30,30,30,30,30,30,30,30,30,30,30,30,30,30}},
    {{0,17,28,22,16,22,28,26,26,24,28,24,28,22,24,24,30,28,28,26,28,30,24,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30}}
}};

constexpr std::array<std::array<int, 41>, 4> block_counts{{
    {{0,1,1,1,1,1,2,2,2,2,4,4,4,4,4,6,6,6,6,7,8,8,9,9,10,12,12,12,13,14,15,16,17,18,19,19,20,21,22,24,25}},
    {{0,1,1,1,2,2,4,4,4,5,5,5,8,9,9,10,10,11,13,14,16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49}},
    {{0,1,1,2,2,4,4,6,6,8,8,8,10,12,16,12,17,16,18,21,20,23,23,25,27,29,34,34,35,38,40,43,45,48,51,53,56,59,62,65,68}},
    {{0,1,1,2,4,4,4,5,6,8,8,11,11,16,16,18,16,19,21,25,25,25,34,30,32,35,37,40,42,45,48,51,54,57,60,63,66,70,74,77,81}}
}};

void check_version(int version) {
    validate_version(version);
}

std::size_t level_index(Ecc level) {
    const auto index = static_cast<int>(level);
    validate_ecc(level);
    return static_cast<std::size_t>(index);
}

// Used internally only with checked, bounded block slices and generators.
std::vector<std::uint8_t> remainder_slice(const std::uint8_t* data,
                                        std::size_t length,
                                        const std::vector<std::uint8_t>& generator) {
    const std::size_t degree = generator.size() - 1;
    std::vector<std::uint8_t> remainder(degree, 0);
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto factor = static_cast<std::uint8_t>(data[offset] ^ remainder[0]);
        for (std::size_t i = 0; i + 1 < degree; ++i)
            remainder[i] = static_cast<std::uint8_t>(remainder[i + 1] ^
                                                    gf_multiply(generator[i + 1], factor));
        remainder[degree - 1] = gf_multiply(generator[degree], factor);
    }
    return remainder;
}

} // namespace

int raw_codewords(int version) {
    check_version(version);
    int modules = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int centers = version / 7 + 2;
        modules -= (25 * centers - 10) * centers - 55;
        if (version >= 7) modules -= 36;
    }
    return modules / 8;
}

int data_codewords(int version, Ecc level) {
    const int raw = raw_codewords(version);
    const std::size_t row = level_index(level);
    const auto column = static_cast<std::size_t>(version);
    return raw - ecc_lengths[row][column] * block_counts[row][column];
}

int count_bits(Mode mode, int version) {
    check_version(version);
    switch (mode) {
        case Mode::Numeric: return version <= 9 ? 10 : version <= 26 ? 12 : 14;
        case Mode::Alphanumeric: return version <= 9 ? 9 : version <= 26 ? 11 : 13;
        case Mode::Byte: return version <= 9 ? 8 : 16;
        case Mode::Kanji: return version <= 9 ? 8 : version <= 26 ? 10 : 12;
        case Mode::Eci:
        case Mode::Fnc1:
        case Mode::Fnc1Second:
        case Mode::StructuredAppend: return 0;
        default: throw Error("INVALID_INPUT", "The QR mode is not an encodable segment mode");
    }
}

std::vector<int> alignment_positions(int version) {
    check_version(version);
    if (version == 1) return {};
    const int count = version / 7 + 2;
    const int denominator = 2 * (count - 1);
    const int step = version == 32 ? 26 : 2 * ((4 * version + 4 + denominator - 1) / denominator);
    std::vector<int> centers(static_cast<std::size_t>(count));
    centers[0] = 6;
    for (int i = count - 1, position = 4 * version + 10; i > 0; --i, position -= step)
        centers[static_cast<std::size_t>(i)] = position;
    return centers;
}

std::uint8_t gf_multiply(std::uint8_t left, std::uint8_t right) {
    // Unsigned arithmetic makes the GF(256) reduction independent of char
    // signedness. The polynomial is x^8 + x^4 + x^3 + x^2 + 1.
    unsigned multiplicand = left;
    unsigned multiplier = right;
    unsigned product = 0;
    while (multiplier != 0) {
        if ((multiplier & 1U) != 0) product ^= multiplicand;
        multiplier >>= 1U;
        multiplicand <<= 1U;
        if ((multiplicand & 0x100U) != 0) multiplicand ^= 0x11DU;
    }
    return static_cast<std::uint8_t>(product);
}

std::vector<std::uint8_t> rs_generator(int degree) {
    if (degree < 1 || degree > 255)
        throw Error("INVALID_INPUT", "Reed-Solomon generator degree must be from 1 through 255");
    std::vector<std::uint8_t> coefficients(static_cast<std::size_t>(degree) + 1, 0);
    coefficients[0] = 1;
    std::uint8_t root = 1;
    for (int factor = 0; factor < degree; ++factor) {
        for (int i = factor + 1; i > 0; --i) {
            const auto index = static_cast<std::size_t>(i);
            coefficients[index] ^= gf_multiply(coefficients[index - 1], root);
        }
        root = gf_multiply(root, 2);
    }
    return coefficients;
}

std::vector<std::uint8_t> rs_remainder(const std::vector<std::uint8_t>& data,
                                       const std::vector<std::uint8_t>& generator) {
    if (generator.size() < 2 || generator.size() > 256 || generator[0] != 1)
        throw Error("INVALID_INPUT", "Reed-Solomon generator must be monic with degree from 1 through 255");
    return remainder_slice(data.data(), data.size(), generator);
}

std::vector<std::uint8_t> interleave(const std::vector<std::uint8_t>& data, int version, Ecc level) {
    const int expected_length = data_codewords(version, level);
    if (data.size() != static_cast<std::size_t>(expected_length))
        throw Error("INVALID_INPUT", "Data codewords do not exactly fill the selected QR version and level");
    const std::size_t row = level_index(level);
    const auto column = static_cast<std::size_t>(version);
    const int block_count = block_counts[row][column];
    const int ecc_length = ecc_lengths[row][column];
    const int raw_length = raw_codewords(version);
    const int short_count = block_count - raw_length % block_count;
    const int short_length = expected_length / block_count;
    const auto generator = rs_generator(ecc_length);

    std::vector<std::vector<std::uint8_t>> checks;
    std::vector<std::size_t> starts;
    std::vector<std::size_t> lengths;
    checks.reserve(static_cast<std::size_t>(block_count));
    starts.reserve(static_cast<std::size_t>(block_count));
    lengths.reserve(static_cast<std::size_t>(block_count));
    std::size_t offset = 0;
    for (int block = 0; block < block_count; ++block) {
        const auto length = static_cast<std::size_t>(short_length + (block < short_count ? 0 : 1));
        if (offset > data.size() || length > data.size() - offset)
            throw std::logic_error("QR block table exceeds the input buffer");
        starts.push_back(offset);
        lengths.push_back(length);
        checks.push_back(remainder_slice(data.data() + offset, length, generator));
        offset += length;
    }

    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(raw_length));
    for (std::size_t position = 0; position <= static_cast<std::size_t>(short_length); ++position)
        for (std::size_t block = 0; block < lengths.size(); ++block)
            if (position < lengths[block]) result.push_back(data[starts[block] + position]);
    for (std::size_t position = 0; position < static_cast<std::size_t>(ecc_length); ++position)
        for (const auto& check : checks) result.push_back(check[position]);

    if (offset != data.size() || result.size() != static_cast<std::size_t>(raw_length))
        throw std::logic_error("QR block interleaving produced an inconsistent length");
    return result;
}

} // namespace specqr::detail
