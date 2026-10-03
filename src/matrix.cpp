#include "internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace specqr::detail {
namespace {

int format_level(Ecc level) {
    switch (level) {
        case Ecc::L: return 1;
        case Ecc::M: return 0;
        case Ecc::Q: return 3;
        case Ecc::H: return 2;
        default: throw Error("INVALID_INPUT", "Unknown QR error correction level");
    }
}

// Division by the format/version BCH polynomial; all shifted operands are
// unsigned. These bounded callers use degrees 10 and 12 only.
unsigned bch_word(unsigned payload, unsigned degree, unsigned polynomial) {
    unsigned remainder = payload;
    for (unsigned bit = 0; bit < degree; ++bit) {
        const bool top = (remainder & (1U << (degree - 1))) != 0;
        remainder <<= 1U;
        if (top) remainder ^= polynomial;
    }
    return (payload << degree) | remainder;
}

bool mask_inverts(int mask, int column, int row) {
    const int product = column * row; // Coordinates are bounded by 176.
    switch (mask) {
        case 0: return (column + row) % 2 == 0;
        case 1: return row % 2 == 0;
        case 2: return column % 3 == 0;
        case 3: return (column + row) % 3 == 0;
        case 4: return (row / 2 + column / 3) % 2 == 0;
        case 5: return product % 2 + product % 3 == 0;
        case 6: return (product % 2 + product % 3) % 2 == 0;
        case 7: return ((column + row) % 2 + product % 3) % 2 == 0;
        default: throw Error("INVALID_INPUT", "QR mask must be from 0 through 7");
    }
}

// Flat byte buffers avoid vector<bool> proxy operations during construction.
// White function modules must be reserved just as dark function modules are.
class SymbolGrid {
public:
    explicit SymbolGrid(int size)
        : size_(size), modules_(static_cast<std::size_t>(size * size), 0),
          reserved_(modules_.size(), 0) {}

    void draw_functions(int version, Ecc level) {
        draw_finder(3, 3);
        draw_finder(size_ - 4, 3);
        draw_finder(3, size_ - 4);
        for (int coordinate = 8; coordinate < size_ - 8; ++coordinate) {
            function(coordinate, 6, (coordinate & 1) == 0);
            function(6, coordinate, (coordinate & 1) == 0);
        }
        const auto centers = alignment_positions(version);
        for (std::size_t row = 0; row < centers.size(); ++row) {
            for (std::size_t column = 0; column < centers.size(); ++column) {
                const std::size_t last = centers.size() - 1;
                if ((row == 0 && (column == 0 || column == last)) ||
                    (row == last && column == 0)) continue;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        function(centers[column] + dx, centers[row] + dy,
                                 std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
        draw_format(level, 0);
        function(8, size_ - 8, true);
        if (version >= 7) {
            const unsigned bits = bch_word(static_cast<unsigned>(version), 12, 0x1F25U);
            for (unsigned bit = 0; bit < 18; ++bit) {
                const int far = size_ - 11 + static_cast<int>(bit % 3);
                const int near = static_cast<int>(bit / 3);
                const bool dark = (bits & (1U << bit)) != 0;
                function(far, near, dark);
                function(near, far, dark);
            }
        }
    }

    void draw_format(Ecc level, int mask) {
        const auto payload = static_cast<unsigned>((format_level(level) << 3) | mask);
        const unsigned bits = bch_word(payload, 10, 0x537U) ^ 0x5412U;
        for (int bit = 0; bit < 15; ++bit) {
            const bool dark = (bits & (1U << static_cast<unsigned>(bit))) != 0;
            if (bit < 6) function(8, bit, dark);
            else if (bit < 8) function(8, bit + 1, dark);
            else function(14 - bit + (bit == 8 ? 1 : 0), 8, dark);
            if (bit < 8) function(size_ - 1 - bit, 8, dark);
            else function(8, size_ - 15 + bit, dark);
        }
    }

    void place_data(const std::vector<std::uint8_t>& codewords) {
        std::size_t bit = 0;
        const std::size_t codeword_bits = codewords.size() * 8;
        bool upward = true;
        for (int right = size_ - 1; right > 0; right -= 2) {
            if (right == 6) --right;
            for (int step = 0; step < size_; ++step) {
                const int row = upward ? size_ - 1 - step : step;
                for (int column = right; column >= right - 1; --column) {
                    const auto index = position(column, row);
                    if (reserved_[index] != 0) continue;
                    const bool dark = bit < codeword_bits &&
                        (codewords[bit / 8] & (1U << static_cast<unsigned>(7 - bit % 8))) != 0;
                    modules_[index] = static_cast<std::uint8_t>(dark);
                    ++bit;
                }
            }
            upward = !upward;
        }
        // The final zero through seven modules are QR remainder bits.
        if (bit < codeword_bits || bit - codeword_bits > 7)
            throw std::logic_error("QR data placement did not match the symbol's available modules");
    }

    void apply_mask(int mask) {
        for (int row = 0; row < size_; ++row)
            for (int column = 0; column < size_; ++column) {
                const auto index = position(column, row);
                if (reserved_[index] == 0 && mask_inverts(mask, column, row)) modules_[index] ^= 1U;
            }
    }

    int penalty() const {
        int total = 0;
        int dark = 0;
        for (int line = 0; line < size_; ++line) {
            total += line_penalty(line * size_, 1);
            total += line_penalty(line, size_);
        }
        for (int row = 0; row + 1 < size_; ++row)
            for (int column = 0; column + 1 < size_; ++column) {
                const auto index = position(column, row);
                const auto below = index + static_cast<std::size_t>(size_);
                const auto color = modules_[index];
                if (modules_[index + 1] == color && modules_[below] == color &&
                    modules_[below + 1] == color) total += 3;
            }
        for (const auto value : modules_) dark += value != 0 ? 1 : 0;
        const int module_count = size_ * size_;
        return total + std::abs(dark * 20 - module_count * 10) / module_count * 10;
    }

    std::vector<std::vector<bool>> rows() const {
        std::vector<std::vector<bool>> result(static_cast<std::size_t>(size_),
                                            std::vector<bool>(static_cast<std::size_t>(size_)));
        for (int row = 0; row < size_; ++row)
            for (int column = 0; column < size_; ++column)
                result[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                    modules_[position(column, row)] != 0;
        return result;
    }

private:
    std::size_t position(int column, int row) const {
        return static_cast<std::size_t>(row * size_ + column);
    }

    void function(int column, int row, bool dark) {
        // Finder separators beyond a symbol edge are intentionally clipped.
        if (column < 0 || row < 0 || column >= size_ || row >= size_) return;
        const auto index = position(column, row);
        modules_[index] = static_cast<std::uint8_t>(dark);
        reserved_[index] = 1;
    }

    void draw_finder(int center_column, int center_row) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                const int radius = std::max(std::abs(dx), std::abs(dy));
                function(center_column + dx, center_row + dy, radius != 2 && radius != 4);
            }
    }

    int line_penalty(int start, int step) const {
        int total = 0;
        int run_length = 0;
        unsigned window = 0;
        std::uint8_t previous = modules_[static_cast<std::size_t>(start)];
        for (int offset = 0; offset < size_; ++offset) {
            const auto value = modules_[static_cast<std::size_t>(start + offset * step)];
            if (value == previous) ++run_length;
            else {
                if (run_length >= 5) total += run_length - 2;
                previous = value;
                run_length = 1;
            }
            window = ((window << 1U) | value) & 0x7FFU;
            // Baseline N3 policy: exact in-symbol eleven-module windows.
            if (offset >= 10 && (window == 0b10111010000U || window == 0b00001011101U)) total += 40;
        }
        return total + (run_length >= 5 ? run_length - 2 : 0);
    }

    int size_;
    std::vector<std::uint8_t> modules_;
    std::vector<std::uint8_t> reserved_;
};

} // namespace

MatrixResult build_matrix(const std::vector<std::uint8_t>& codewords, int version,
                          Ecc level, std::optional<int> mask) {
    validate_version(version);
    validate_ecc(level);
    if (mask && (*mask < 0 || *mask > 7))
        throw Error("INVALID_INPUT", "QR mask must be from 0 through 7");
    if (codewords.size() != static_cast<std::size_t>(raw_codewords(version)))
        throw Error("INVALID_INPUT", "Interleaved codewords do not exactly fill the selected QR version");

    SymbolGrid base(4 * version + 17);
    base.draw_functions(version, level);
    base.place_data(codewords);
    MatrixResult result{{}, 0, std::numeric_limits<int>::max(), {}};
    result.penalties.reserve(mask ? 1 : 8);
    const int first = mask.value_or(0);
    const int last = mask.value_or(7);
    for (int candidate_mask = first; candidate_mask <= last; ++candidate_mask) {
        auto candidate = base;
        candidate.apply_mask(candidate_mask);
        candidate.draw_format(level, candidate_mask);
        const int score = candidate.penalty();
        result.penalties.push_back({candidate_mask, score});
        // A strict comparison preserves the lowest mask number on ties.
        if (score < result.penalty) {
            result.matrix = candidate.rows();
            result.mask = candidate_mask;
            result.penalty = score;
        }
    }
    return result;
}

} // namespace specqr::detail
