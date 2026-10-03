#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <charconv>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
int number(const std::string& value) {
    int n = 0;
    auto r = std::from_chars(value.data(), value.data() + value.size(), n);
    if (r.ec != std::errc{} || r.ptr != value.data() + value.size())
        throw specqr::Error("INVALID_ARGUMENT", "Expected an integer.");
    return n;
}
std::string read_input(std::istream& stream) {
    std::string value;
    char buffer[4096];
    while (stream.read(buffer, sizeof buffer) || stream.gcount()) {
        const auto n = static_cast<std::size_t>(stream.gcount());
        if (n > specqr::max_input_bytes - value.size())
            throw specqr::Error("INVALID_INPUT", "Input exceeds resource limit.");
        value.append(buffer, n);
    }
    if (stream.bad()) throw specqr::Error("IO_ERROR", "Could not read input.");
    return value;
}
void usage() {
    std::cout << "SpecQR C++17 QR Code Model 2 generator\n"
        "Usage: specqr-cli [options] TEXT\n"
        "  --input FILE | --stdin    Read exact bytes (no newline stripping)\n"
        "  --binary                 Treat input as raw bytes\n"
        "  --format matrix|svg|png|svg-data-url|png-data-url\n"
        "  --output FILE            Required for PNG; stdout otherwise\n"
        "  --ecc L|M|Q|H --version 1..40 --min-version N --max-version N\n"
        "  --mask 0..7 --mode auto|numeric|alphanumeric|byte|kanji\n"
        "  --eci N --gs1 --fnc1-second INDICATOR --boost --no-optimize\n"
        "  --scale N --margin N --foreground COLOR --background COLOR\n"
        "  --estimate               Print planning summary only\n"
        "  --                       End option parsing\n";
}
}
int run(int argc, char** argv) {
    try {
        specqr::Options options;
        specqr::RenderOptions render;
        std::string format = "svg", output, input, file;
        bool binary = false, estimate = false, supplied = false, stdin_input = false, end_options = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (++i == argc) throw specqr::Error("INVALID_ARGUMENT", "Missing option value.");
                return argv[i];
            };
            if (!end_options && a == "--") { end_options = true; continue; }
            if (!end_options && (a == "--help" || a == "-h")) { usage(); return 0; }
            if (!end_options && a == "--binary") binary = true;
            else if (!end_options && a == "--estimate") estimate = true;
            else if (!end_options && a == "--gs1") options.gs1 = true;
            else if (!end_options && a == "--boost") options.boost_ecc = true;
            else if (!end_options && a == "--no-optimize") options.optimize_segments = false;
            else if (!end_options && a == "--stdin") { if (supplied) throw specqr::Error("INVALID_ARGUMENT", "Choose one input source."); supplied = stdin_input = true; }
            else if (!end_options && a == "--input") { if (supplied) throw specqr::Error("INVALID_ARGUMENT", "Choose one input source."); file = next(); if (file.empty()) throw specqr::Error("INVALID_ARGUMENT", "Input file path is empty."); supplied = true; }
            else if (!end_options && a == "--format") format = next();
            else if (!end_options && a == "--output") { output = next(); if (output.empty()) throw specqr::Error("INVALID_ARGUMENT", "Output file path is empty."); }
            else if (!end_options && a == "--version") options.version = number(next());
            else if (!end_options && a == "--min-version") options.min_version = number(next());
            else if (!end_options && a == "--max-version") options.max_version = number(next());
            else if (!end_options && a == "--mask") options.mask = number(next());
            else if (!end_options && a == "--eci") options.eci = number(next());
            else if (!end_options && a == "--scale") render.scale = number(next());
            else if (!end_options && a == "--margin") render.margin = number(next());
            else if (!end_options && a == "--foreground") render.foreground = next();
            else if (!end_options && a == "--background") render.background = next();
            else if (!end_options && a == "--fnc1-second") options.fnc1_second = next();
            else if (!end_options && a == "--ecc") {
                auto e = next();
                if (e == "L") options.ecc = specqr::Ecc::L;
                else if (e == "M") options.ecc = specqr::Ecc::M;
                else if (e == "Q") options.ecc = specqr::Ecc::Q;
                else if (e == "H") options.ecc = specqr::Ecc::H;
                else throw specqr::Error("INVALID_ARGUMENT", "ECC must be L, M, Q or H.");
            } else if (!end_options && a == "--mode") {
                const auto m = next();
                if (m == "auto") options.mode = specqr::Mode::Auto;
                else if (m == "numeric") options.mode = specqr::Mode::Numeric;
                else if (m == "alphanumeric") options.mode = specqr::Mode::Alphanumeric;
                else if (m == "byte") options.mode = specqr::Mode::Byte;
                else if (m == "kanji") options.mode = specqr::Mode::Kanji;
                else throw specqr::Error("INVALID_ARGUMENT", "Unsupported mode.");
            } else {
                if (!end_options && a.rfind("--", 0) == 0) throw specqr::Error("INVALID_ARGUMENT", "Unknown option.");
                if (supplied) throw specqr::Error("INVALID_ARGUMENT", "Choose one input source.");
                supplied = true; input = std::move(a);
            }
        }
        if (!supplied) { usage(); return 2; }
        if (format != "matrix" && format != "svg" && format != "png" && format != "svg-data-url" && format != "png-data-url")
            throw specqr::Error("INVALID_ARGUMENT", "Unsupported output format.");
        if (stdin_input) {
#ifdef _WIN32
            if (_setmode(_fileno(stdin), _O_BINARY) == -1)
                throw specqr::Error("IO_ERROR", "Could not set binary standard input.");
#endif
            input = read_input(std::cin);
        }
        else if (!file.empty()) {
            std::ifstream source(std::filesystem::u8path(file), std::ios::binary);
            if (!source) throw specqr::Error("IO_ERROR", "Could not open input file.");
            input = read_input(source);
        }
        std::vector<std::uint8_t> bytes;
        if (binary) bytes.assign(input.begin(), input.end());
        if (estimate) {
            const auto p = binary ? specqr::QRCode::estimate(bytes, options) : specqr::QRCode::estimate(input, options);
            std::cout << "ok=" << (p.ok ? "true" : "false") << " version=" << p.evaluated_version
                << " ecc=" << specqr::ecc_name(p.ecc) << " bits=" << p.data_bit_length
                << " capacity=" << p.capacity_bits << " mode=" << specqr::mode_name(p.mode()) << '\n';
            return p.ok ? 0 : 3;
        }
        if (format == "png" && output.empty()) throw specqr::Error("INVALID_ARGUMENT", "PNG requires --output FILE.");
        const auto qr = binary ? specqr::QRCode::generate(bytes, options) : specqr::QRCode::generate(input, options);
        // Generate and validate fully before opening the output path.
        std::string text;
        std::vector<std::uint8_t> png;
        if (format == "png") png = specqr::to_png(qr, render);
        else if (format == "svg") text = specqr::to_svg(qr, render);
        else if (format == "svg-data-url") text = specqr::to_svg_data_url(qr, render);
        else if (format == "png-data-url") text = specqr::to_png_data_url(qr, render);
        else for (const auto& row : qr.matrix()) { for (bool dark : row) text += dark ? '1' : '0'; text += '\n'; }
        std::ofstream destination;
        std::ostream* stream = &std::cout;
        if (!output.empty()) {
            destination.open(std::filesystem::u8path(output), std::ios::binary);
            if (!destination) throw specqr::Error("IO_ERROR", "Could not open output file.");
            stream = &destination;
        }
        if (format == "png") stream->write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        else stream->write(text.data(), static_cast<std::streamsize>(text.size()));
        stream->flush();
        if (!*stream) throw specqr::Error("IO_ERROR", "Could not write output.");
        return 0;
    } catch (const specqr::Error& error) { std::cerr << error.code() << ": " << error.what() << '\n'; return 2; }
      catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 2; }
}

#ifdef _WIN32
// Windows command lines are UTF-16; never pass through the active ANSI codepage.
std::string utf8_argument(const wchar_t* argument) {
    std::string result;
    for (std::size_t i = 0; argument[i] != 0; ++i) {
        std::uint32_t value = static_cast<std::uint32_t>(argument[i]);
        if (value >= 0xD800 && value <= 0xDBFF) {
            const auto low = static_cast<std::uint32_t>(argument[++i]);
            if (low < 0xDC00 || low > 0xDFFF)
                throw specqr::Error("INVALID_UTF8", "Command line contains invalid UTF-16.");
            value = 0x10000 + ((value - 0xD800) << 10) + low - 0xDC00;
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            throw specqr::Error("INVALID_UTF8", "Command line contains invalid UTF-16.");
        }
        if (value < 0x80) result.push_back(static_cast<char>(value));
        else if (value < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (value >> 6)));
            result.push_back(static_cast<char>(0x80 | (value & 63)));
        } else if (value < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (value >> 12)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 63)));
            result.push_back(static_cast<char>(0x80 | (value & 63)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (value >> 18)));
            result.push_back(static_cast<char>(0x80 | ((value >> 12) & 63)));
            result.push_back(static_cast<char>(0x80 | ((value >> 6) & 63)));
            result.push_back(static_cast<char>(0x80 | (value & 63)));
        }
    }
    return result;
}
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> values;
        values.reserve(static_cast<std::size_t>(argc));
        for (int i=0; i<argc; ++i) values.push_back(utf8_argument(argv[i]));
        std::vector<char*> arguments;
        for (auto& value: values) arguments.push_back(value.data());
        return run(argc, arguments.data());
    } catch (const std::exception& error) {
        std::cerr << "INVALID_ARGUMENT: " << error.what() << '\n';
        return 2;
    }
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
