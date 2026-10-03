#pragma once
#include <specqr/render.hpp>
#include <specqr/specqr.hpp>
#include <map>
#include <variant>

namespace specqr {
enum class WarningSeverity { Info, Warning };
using WarningValue = std::variant<int, double, std::string, std::vector<std::string>>;
struct ScanWarning {
    std::string code;
    WarningSeverity severity;
    std::string message;
    std::map<std::string, WarningValue> details;
};
/// Heuristic rendering advice; no guarantee about a particular scanner.
struct RenderDiagnostics {
    int dimension = 0, quiet_zone_modules = 0;
    std::optional<double> contrast_ratio;
    std::optional<std::uint8_t> foreground_alpha, background_alpha;
    std::optional<double> module_size_mm, symbol_size_mm;
    std::vector<std::string> warnings;
    std::vector<ScanWarning> warning_details;
    bool quiet_zone_sufficient() const noexcept { return quiet_zone_modules >= 4; }
    bool color_inspectable() const noexcept { return contrast_ratio.has_value(); }
    bool color_strong() const noexcept { return contrast_ratio && *contrast_ratio >= 7; }
    bool color_sufficient() const noexcept { return contrast_ratio && *contrast_ratio >= 4.5 && foreground_alpha == 255 && background_alpha == 255; }
    static constexpr double recommended_minimum_module_size_mm = 0.25;
    std::optional<bool> module_size_sufficient() const noexcept { return module_size_mm ? std::optional<bool>(*module_size_mm >= 0.25) : std::nullopt; }
};
RenderDiagnostics render_diagnostics(const QRCode& code, const RenderOptions& options = {}, bool raster = true);
/// Evaluates overflow plans at evaluated_version; omits raster-scale advice.
RenderDiagnostics render_diagnostics(const Plan& plan, const RenderOptions& options = {});
} // namespace specqr
