#pragma once
#include <specqr/specqr.hpp>
#include <array>
#include <utility>

namespace specqr::detail {
struct Scalar { std::uint32_t value; std::size_t offset; int bytes; };
std::vector<Scalar> decode_utf8(std::string_view text);
void validate_version(int version);
void validate_ecc(Ecc ecc);
void validate_options(const Options& options);
void validate_controls(const std::vector<Segment>& segments);
bool is_data(Mode mode) noexcept;
int alpha_value(std::uint32_t scalar) noexcept;
std::optional<int> kanji_value(std::uint32_t scalar) noexcept;
int application_indicator(std::string_view text);
int payload_length(Mode mode, int count);
int count_bits(Mode mode, int version);
int raw_codewords(int version);
std::uint8_t gf_multiply(std::uint8_t,std::uint8_t);
std::vector<std::uint8_t> rs_generator(int degree);
std::vector<std::uint8_t> rs_remainder(const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&);
std::vector<int> alignment_positions(int version);
int data_codewords(int version, Ecc ecc);
std::vector<Segment> single_segment(std::string_view, Mode requested, bool allow_kanji);
std::vector<Segment> optimize(std::string_view, int version, bool allow_kanji);
std::vector<Segment> applying_options(std::vector<Segment>, const Options&);
std::vector<std::uint8_t> encode(const std::vector<Segment>&, int version, Ecc ecc);
std::vector<std::uint8_t> interleave(const std::vector<std::uint8_t>&, int version, Ecc ecc);
struct MatrixResult {
    std::vector<std::vector<bool>> matrix;
    int mask;
    int penalty;
    std::vector<MaskPenalty> penalties;
};
MatrixResult build_matrix(const std::vector<std::uint8_t>&, int version, Ecc ecc, std::optional<int> mask);
} // namespace specqr::detail
