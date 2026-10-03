#include <specqr/scan.hpp>
#include "render_internal.hpp"
#include <algorithm>
#include <utility>

namespace specqr {
namespace {
RenderDiagnostics inspect(const Plan& plan, const RenderOptions& options, bool raster) {
    if (plan.evaluated_version < 1 || plan.evaluated_version > 40)
        throw Error("INVALID_VERSION", "The evaluated plan version must be 1–40.");
    if (plan.capacity_bits < 0 || plan.data_bit_length < 0)
        throw Error("INVALID_INPUT", "Plan bit counts must be nonnegative.");
    const auto geometry = detail::render_geometry(plan.evaluated_version * 4 + 17, options, false);
    const auto foreground = detail::parse_color(options.foreground), background = detail::parse_color(options.background);
    RenderDiagnostics result;
    result.dimension = geometry.dimension; result.quiet_zone_modules = options.margin;
    if (foreground && background) {
        result.contrast_ratio = (std::max(foreground->luminance(), background->luminance()) + 0.05) /
            (std::min(foreground->luminance(), background->luminance()) + 0.05);
        result.foreground_alpha = foreground->a; result.background_alpha = background->a;
    }
    const auto add = [&](std::string code, WarningSeverity severity, std::string message,
                         std::map<std::string, WarningValue> details = {}) {
        result.warnings.push_back(code);
        result.warning_details.push_back({std::move(code), severity, std::move(message), std::move(details)});
    };
    if (options.margin < 4)
        add("QUIET_ZONE_TOO_SMALL", WarningSeverity::Warning, "QR Code Model 2 readers expect a quiet zone of at least 4 modules.", {{"margin", options.margin}, {"recommendedModules", 4}});
    if (!result.contrast_ratio)
        add("COLOR_CONTRAST_UNKNOWN", WarningSeverity::Info, "Color contrast could not be inspected because one or both colors are not supported portable colors.", {{"foreground", options.foreground}, {"background", options.background}});
    else if (*result.contrast_ratio < 4.5)
        add("COLOR_CONTRAST_LOW", WarningSeverity::Warning, "Foreground and background contrast is low for reliable scanning.", {{"ratio", *result.contrast_ratio}, {"recommendedMinimumRatio", 4.5}});
    else if (*result.contrast_ratio < 7)
        add("COLOR_CONTRAST_MODERATE", WarningSeverity::Info, "Stronger contrast is recommended for damaged, small, or printed QR codes.", {{"ratio", *result.contrast_ratio}, {"strongRecommendedRatio", 7.0}});
    if (foreground && background) {
        if (foreground->a < 255 || background->a < 255)
            add("COLOR_ALPHA_USED", WarningSeverity::Warning, "Transparent colors can reduce scanner reliability.", {{"foregroundAlpha", static_cast<int>(foreground->a)}, {"backgroundAlpha", static_cast<int>(background->a)}});
        if (foreground->luminance() > background->luminance())
            add("COLOR_POLARITY_INVERTED", WarningSeverity::Warning, "A light foreground on a dark background is not supported by every scanner.");
    }
    // Overflow is not successful near-capacity planning.
    if (plan.capacity_bits > 0 && plan.remaining_bits() >= 0 && static_cast<double>(plan.remaining_bits()) / plan.capacity_bits < 0.05)
        add("CAPACITY_NEAR_LIMIT", WarningSeverity::Info, "The selected version is close to full capacity.", {{"remainingBits", static_cast<int>(plan.remaining_bits())}, {"capacityBits", plan.capacity_bits}});
    if (options.print_dpi) {
        result.module_size_mm = geometry.scale / *options.print_dpi * 25.4;
        result.symbol_size_mm = geometry.dimension / *options.print_dpi * 25.4;
        if (*result.module_size_mm < 0.25)
            add("PRINT_MODULE_TOO_SMALL", WarningSeverity::Warning, "The configured scale and DPI produce modules smaller than the print recommendation.", {{"dpi", *options.print_dpi}, {"moduleSizeMm", *result.module_size_mm}, {"recommendedMinimumModuleSizeMm", 0.25}});
    }
    if (raster && geometry.scale < 3)
        add("RASTER_SCALE_SMALL", WarningSeverity::Info, "Raster output with fewer than 3 pixels per module may scan poorly after resizing.", {{"scale", geometry.scale}, {"recommendedMinimumScale", 3}});
    std::vector<std::string> blocking;
    for (const auto& warning : result.warning_details) if (warning.severity == WarningSeverity::Warning) blocking.push_back(warning.code);
    if (!blocking.empty()) add("SCAN_RISK", WarningSeverity::Warning, "One or more settings may reduce scan reliability.", {{"blockingWarnings", std::move(blocking)}});
    return result;
}
} // namespace
RenderDiagnostics render_diagnostics(const QRCode& code, const RenderOptions& options, bool raster) { return inspect(code.planning(), options, raster); }
RenderDiagnostics render_diagnostics(const Plan& plan, const RenderOptions& options) { return inspect(plan, options, false); }
} // namespace specqr
