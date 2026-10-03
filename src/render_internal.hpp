#pragma once
#include <specqr/render.hpp>

namespace specqr { namespace detail {
struct RenderGeometry { int dimension; int scale; };
struct RgbaColor {
    std::uint8_t r, g, b, a;
    double luminance() const;
};
RenderGeometry render_geometry(int size, const RenderOptions& options, bool raster);
std::optional<RgbaColor> parse_color(const std::string& color);
} } // namespace specqr::detail
