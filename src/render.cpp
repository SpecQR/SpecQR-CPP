#include <specqr/render.hpp>
#include <specqr/specqr.hpp>
#include "render_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace specqr {
namespace {
[[noreturn]] void invalid(const char* message) { throw Error("INVALID_RENDER", message); }

void validate_xml_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i++]);
        std::uint32_t cp = first;
        unsigned following = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1fU; following = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0fU; following = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 0x07U; following = 3; minimum = 0x10000; }
        else if (first > 0x7f) invalid("Title must be valid UTF-8.");
        if (following > s.size() - i) invalid("Title must be valid UTF-8.");
        for (unsigned j = 0; j < following; ++j) {
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xc0U) != 0x80U) invalid("Title must be valid UTF-8.");
            cp = (cp << 6U) | (next & 0x3fU);
        }
        if (cp < minimum || cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU))
            invalid("Title must be valid UTF-8.");
        if (!(cp == 9 || cp == 10 || cp == 13 || (cp >= 0x20 && cp <= 0xd7ff) ||
              (cp >= 0xe000 && cp <= 0xfffd) || cp >= 0x10000))
            invalid("Title contains a character forbidden in XML 1.0.");
    }
}
std::string escape_xml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            default: output += c; break;
        }
    }
    return output;
}
detail::RgbaColor required_color(const std::string& text) {
    const auto color = detail::parse_color(text);
    if (!color) invalid("Use #RGB, #RGBA, #RRGGBB, #RRGGBBAA, black, white, or transparent.");
    return *color;
}
std::string svg_fill(detail::RgbaColor color) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "fill=\"#";
    for (auto byte : {color.r, color.g, color.b}) {
        result += hex[byte >> 4U]; result += hex[byte & 15U];
    }
    result += '"';
    if (color.a != 255) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9) << (color.a / 255.0);
        result += " fill-opacity=\"" + out.str() + '"';
    }
    return result;
}
void fill_row(const QRCode& code, std::uint8_t* row, int y, detail::RenderGeometry geometry,
              int margin, detail::RgbaColor foreground, detail::RgbaColor background) {
    const int my = y / geometry.scale - margin;
    for (int x = 0; x < geometry.dimension; ++x) {
        const int mx = x / geometry.scale - margin;
        const bool dark = mx >= 0 && my >= 0 && mx < code.size() && my < code.size() && code.module(mx, my);
        const auto color = dark ? foreground : background;
        const auto offset = static_cast<std::size_t>(x) * 4U;
        row[offset] = color.r; row[offset + 1] = color.g; row[offset + 2] = color.b; row[offset + 3] = color.a;
    }
}
void append_u32(std::vector<std::uint8_t>& target, std::uint32_t n) {
    for (int shift : {24, 16, 8, 0}) target.push_back(static_cast<std::uint8_t>((n >> shift) & 0xffU));
}
std::uint32_t update_crc(std::uint32_t crc, std::uint8_t byte) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    return crc;
}
void append_chunk(std::vector<std::uint8_t>& output, const std::array<std::uint8_t, 4>& type,
                  const std::vector<std::uint8_t>& bytes) {
    append_u32(output, static_cast<std::uint32_t>(bytes.size()));
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), bytes.begin(), bytes.end());
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : type) crc = update_crc(crc, byte);
    for (auto byte : bytes) crc = update_crc(crc, byte);
    append_u32(output, crc ^ 0xffffffffU);
}
std::string base64(const std::vector<std::uint8_t>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < bytes.size(); i += 3U) {
        const std::size_t count = std::min<std::size_t>(3, bytes.size() - i);
        const std::uint32_t value = (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (count > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) << 8U : 0U) |
            (count > 2 ? static_cast<std::uint32_t>(bytes[i + 2]) : 0U);
        output += alphabet[(value >> 18U) & 63U]; output += alphabet[(value >> 12U) & 63U];
        output += count > 1 ? alphabet[(value >> 6U) & 63U] : '=';
        output += count > 2 ? alphabet[value & 63U] : '=';
    }
    return output;
}
void write_bytes(std::ostream& out, const char* bytes, std::size_t length) {
    if (!out.good()) throw std::ios_base::failure("The output stream is not writable.");
    out.write(bytes, static_cast<std::streamsize>(length));
    if (!out.good()) throw std::ios_base::failure("Writing the rendered QR code failed.");
}
} // namespace

namespace detail {
RenderGeometry render_geometry(int size, const RenderOptions& options, bool raster) {
    if (size < 21 || size > 177) invalid("Invalid QR module size.");
    if (options.margin < 0) invalid("Margin must be nonnegative.");
    if (options.scale < 1) invalid("Scale must be positive.");
    if (options.foreground.size() > 65536 || options.background.size() > 65536 ||
        (options.title && options.title->size() > 65536))
        invalid("Color and title strings are limited to 65,536 UTF-8 bytes.");
    if (options.title) validate_xml_utf8(*options.title);
    if (options.print_dpi && (!std::isfinite(*options.print_dpi) || *options.print_dpi <= 0))
        invalid("Print DPI must be finite and positive.");
    const std::int64_t span = static_cast<std::int64_t>(size) + static_cast<std::int64_t>(options.margin) * 2;
    std::int64_t scale = options.scale;
    if (options.width) {
        if (*options.width <= 0 || static_cast<std::int64_t>(*options.width) % span != 0)
            invalid("Width must be a positive integer multiple of the module span including the quiet zone.");
        scale = *options.width / span;
    }
    if (span > std::numeric_limits<int>::max() / scale) invalid("Render geometry exceeds the signed coordinate range.");
    const auto dimension = static_cast<int>(span * scale);
    if (raster && dimension > 2048) invalid("Raster output is limited to 2048 pixels per side.");
    if (options.print_dpi && !std::isfinite(dimension / *options.print_dpi * 25.4))
        invalid("Physical print dimensions exceed the numeric range.");
    return {dimension, static_cast<int>(scale)};
}
std::optional<RgbaColor> parse_color(const std::string& color) {
    // ASCII-only normalization is independent of process locale and unsigned-char safe.
    const auto whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    std::size_t first = 0, end = color.size();
    while (first < end && whitespace(color[first])) ++first;
    while (end > first && whitespace(color[end - 1])) --end;
    if (end - first > 11) return std::nullopt;
    std::string text = color.substr(first, end - first);
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (text == "black") return RgbaColor{0, 0, 0, 255};
    if (text == "white") return RgbaColor{255, 255, 255, 255};
    if (text == "transparent") return RgbaColor{0, 0, 0, 0};
    if (text.empty() || text[0] != '#' || (text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9)) return std::nullopt;
    const auto nibble = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 1; i < text.size(); ++i) if (nibble(text[i]) < 0) return std::nullopt;
    const bool short_form = text.size() <= 5;
    const auto channel = [&](std::size_t index) { return static_cast<std::uint8_t>(short_form ? nibble(text[index + 1]) * 17 : nibble(text[2 * index + 1]) * 16 + nibble(text[2 * index + 2])); };
    return RgbaColor{channel(0), channel(1), channel(2), text.size() == 5 || text.size() == 9 ? channel(3) : static_cast<std::uint8_t>(255)};
}
double RgbaColor::luminance() const {
    const auto linear = [](std::uint8_t c) { const double n = c / 255.0; return n <= 0.03928 ? n / 12.92 : std::pow((n + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
}
} // namespace detail

RgbaImage::RgbaImage(int width, int height, std::vector<std::uint8_t> pixels)
    : width_(width), height_(height), pixels_(std::move(pixels)) {
    if (width < 1 || width > 2048 || height < 1 || height > 2048 ||
        pixels_.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U)
        invalid("RGBA image dimensions and pixel length do not agree, or exceed 2048 pixels per side.");
}
std::string to_svg(const QRCode& code, const RenderOptions& options) {
    const auto geometry = detail::render_geometry(code.size(), options, false);
    const auto foreground = required_color(options.foreground), background = required_color(options.background);
    std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(geometry.dimension) + "\" height=\"" + std::to_string(geometry.dimension) + "\" viewBox=\"0 0 " + std::to_string(geometry.dimension) + " " + std::to_string(geometry.dimension) + "\" role=\"img\">";
    svg.reserve(512U + static_cast<std::size_t>(code.size()) * static_cast<std::size_t>(code.size()) * 48U);
    if (options.title) svg += "<title>" + escape_xml(*options.title) + "</title>";
    svg += "<rect width=\"100%\" height=\"100%\" " + svg_fill(background) + "/><path " + svg_fill(foreground) + " d=\"";
    for (int y = 0; y < code.size(); ++y) for (int x = 0; x < code.size(); ++x) if (code.module(x, y))
        svg += "M" + std::to_string((x + options.margin) * geometry.scale) + "," + std::to_string((y + options.margin) * geometry.scale) + "h" + std::to_string(geometry.scale) + "v" + std::to_string(geometry.scale) + "h-" + std::to_string(geometry.scale) + "z";
    return svg + "\"/></svg>";
}
std::string to_svg_data_url(const QRCode& code, const RenderOptions& options) {
    const auto svg = to_svg(code, options);
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output = "data:image/svg+xml;charset=utf-8,";
    output.reserve(output.size() + svg.size() * 3U);
    for (char byte : svg) {
        const auto c = static_cast<unsigned char>(byte);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') output += static_cast<char>(c);
        else { output += '%'; output += hex[c >> 4U]; output += hex[c & 15U]; }
    }
    return output;
}
RgbaImage to_rgba(const QRCode& code, const RenderOptions& options) {
    const auto geometry = detail::render_geometry(code.size(), options, true);
    const auto foreground = required_color(options.foreground), background = required_color(options.background);
    const auto stride = static_cast<std::size_t>(geometry.dimension) * 4U;
    std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(geometry.dimension));
    for (int y = 0; y < geometry.dimension; ++y) fill_row(code, pixels.data() + static_cast<std::size_t>(y) * stride, y, geometry, options.margin, foreground, background);
    return RgbaImage(geometry.dimension, geometry.dimension, std::move(pixels));
}
std::vector<std::uint8_t> to_png(const QRCode& code, const RenderOptions& options) {
    const auto geometry = detail::render_geometry(code.size(), options, true);
    const auto foreground = required_color(options.foreground), background = required_color(options.background);
    const auto stride = static_cast<std::size_t>(geometry.dimension) * 4U + 1U;
    const auto raw_length = stride * static_cast<std::size_t>(geometry.dimension);
    // DEFLATE stored blocks are at most 65,535 bytes; memory is bounded by geometry.
    std::vector<std::uint8_t> zlib;
    zlib.reserve(6U + raw_length + ((raw_length + 65534U) / 65535U) * 5U);
    zlib.push_back(0x78); zlib.push_back(0x01);
    std::vector<std::uint8_t> row(stride, 0);
    std::uint32_t adler_a = 1, adler_b = 0;
    std::size_t consumed = 0, remaining_in_block = 0;
    for (int y = 0; y < geometry.dimension; ++y) {
        fill_row(code, row.data() + 1, y, geometry, options.margin, foreground, background);
        for (auto byte : row) {
            if (remaining_in_block == 0) {
                const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(65535, raw_length - consumed));
                zlib.push_back(raw_length - consumed <= 65535 ? 0x01 : 0x00);
                zlib.push_back(static_cast<std::uint8_t>(length & 0xffU)); zlib.push_back(static_cast<std::uint8_t>(length >> 8U));
                const auto complement = static_cast<std::uint16_t>(length ^ 0xffffU);
                zlib.push_back(static_cast<std::uint8_t>(complement & 0xffU)); zlib.push_back(static_cast<std::uint8_t>(complement >> 8U));
                remaining_in_block = length;
            }
            zlib.push_back(byte); ++consumed; --remaining_in_block;
            adler_a = (adler_a + byte) % 65521U; adler_b = (adler_b + adler_a) % 65521U;
        }
    }
    append_u32(zlib, (adler_b << 16U) | adler_a);
    std::vector<std::uint8_t> output{137, 80, 78, 71, 13, 10, 26, 10};
    output.reserve(57U + zlib.size());
    std::vector<std::uint8_t> header;
    append_u32(header, static_cast<std::uint32_t>(geometry.dimension)); append_u32(header, static_cast<std::uint32_t>(geometry.dimension));
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(output, {{'I', 'H', 'D', 'R'}}, header);
    append_chunk(output, {{'I', 'D', 'A', 'T'}}, zlib);
    append_chunk(output, {{'I', 'E', 'N', 'D'}}, {});
    return output;
}
std::string to_png_data_url(const QRCode& code, const RenderOptions& options) { return "data:image/png;base64," + base64(to_png(code, options)); }
void save_svg(const QRCode& code, std::ostream& destination, const RenderOptions& options) {
    const auto svg = to_svg(code, options); write_bytes(destination, svg.data(), svg.size());
}
void save_png(const QRCode& code, std::ostream& destination, const RenderOptions& options) {
    const auto png = to_png(code, options); write_bytes(destination, reinterpret_cast<const char*>(png.data()), png.size());
}
void draw(const QRCode& code, Canvas& canvas, const RenderOptions& options) {
    const auto geometry = detail::render_geometry(code.size(), options, true);
    (void)required_color(options.foreground); (void)required_color(options.background);
    canvas.resize(geometry.dimension, geometry.dimension);
    canvas.fill_rectangle(0, 0, geometry.dimension, geometry.dimension, options.background);
    for (int y = 0; y < code.size(); ++y) for (int x = 0; x < code.size(); ++x) if (code.module(x, y))
        canvas.fill_rectangle((x + options.margin) * geometry.scale, (y + options.margin) * geometry.scale, geometry.scale, geometry.scale, options.foreground);
}
} // namespace specqr
