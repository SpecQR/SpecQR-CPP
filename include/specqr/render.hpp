#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace specqr {
class QRCode;

/// Portable rendering. Margin is in modules, scale in pixels per module.
struct RenderOptions {
    int margin = 4;
    int scale = 8;
    /// Exact width; must be a positive multiple of size + 2 * margin.
    std::optional<int> width;
    std::string foreground = "#000000";
    std::string background = "#ffffff";
    std::optional<double> print_dpi;
    std::optional<std::string> title;
};

/// Immutable row-major, straight (unpremultiplied) RGBA8 pixels.
class RgbaImage {
public:
    RgbaImage(int width, int height, std::vector<std::uint8_t> pixels);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }
private:
    int width_, height_;
    std::vector<std::uint8_t> pixels_;
};

/// Application-owned graphics adapter; the library has no graphics dependency.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void resize(int width, int height) = 0;
    virtual void fill_rectangle(int x, int y, int width, int height, const std::string& color) = 0;
};

std::string to_svg(const QRCode& code, const RenderOptions& options = {});
std::string to_svg_data_url(const QRCode& code, const RenderOptions& options = {});
RgbaImage to_rgba(const QRCode& code, const RenderOptions& options = {});
/// RGBA PNG with independent PNG framing, CRC-32, Adler-32 and stored DEFLATE.
/// Raster dimensions are bounded to 2048 pixels per side.
std::vector<std::uint8_t> to_png(const QRCode& code, const RenderOptions& options = {});
std::string to_png_data_url(const QRCode& code, const RenderOptions& options = {});
/// Validate options before writing; streams stay open and write failures throw.
void save_svg(const QRCode& code, std::ostream& destination, const RenderOptions& options = {});
void save_png(const QRCode& code, std::ostream& destination, const RenderOptions& options = {});
void draw(const QRCode& code, Canvas& canvas, const RenderOptions& options = {});
} // namespace specqr
