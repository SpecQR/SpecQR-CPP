#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <specqr/scan.hpp>
#include <specqr/structured_append.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* message) {
    ++checks;
    try { function(); }
    catch (const specqr::Error&) { return; }
    throw std::runtime_error(message);
}
std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t i) {
    return (static_cast<std::uint32_t>(data.at(i)) << 24U) | (static_cast<std::uint32_t>(data.at(i+1)) << 16U) | (static_cast<std::uint32_t>(data.at(i+2)) << 8U) | data.at(i+3);
}
std::vector<std::uint8_t> inflate_stored(const std::vector<std::uint8_t>& zlib) {
    check(zlib.size() >= 6, "zlib framing length");
    check(((static_cast<unsigned>(zlib[0]) << 8U) + zlib[1]) % 31U == 0, "zlib header FCHECK");
    std::vector<std::uint8_t> out;
    std::size_t cursor = 2;
    bool final = false;
    while (!final) {
        const auto header = zlib.at(cursor++);
        final = (header & 1U) != 0;
        check((header & 0xfeU) == 0, "stored DEFLATE block only");
        const unsigned length = zlib.at(cursor) | (static_cast<unsigned>(zlib.at(cursor+1)) << 8U);
        const unsigned inverse = zlib.at(cursor+2) | (static_cast<unsigned>(zlib.at(cursor+3)) << 8U);
        cursor += 4;
        check((length ^ inverse) == 65535U, "DEFLATE NLEN complement");
        check(length <= zlib.size() - cursor, "DEFLATE bounded payload");
        out.insert(out.end(), zlib.begin() + static_cast<std::ptrdiff_t>(cursor), zlib.begin() + static_cast<std::ptrdiff_t>(cursor + length));
        cursor += length;
    }
    check(cursor + 4 == zlib.size(), "zlib final block exact consumption");
    std::uint32_t a = 1, b = 0;
    for (auto byte : out) { a = (a + byte) % 65521U; b = (b + a) % 65521U; }
    check(((b << 16U) | a) == u32(zlib, cursor), "zlib Adler32");
    return out;
}
void verify_png(const specqr::QRCode& code, const specqr::RenderOptions& options) {
    const auto png = specqr::to_png(code, options);
    const auto image = specqr::to_rgba(code, options);
    check(png.size() > 57, "PNG has payload");
    check(std::equal(png.begin(), png.begin()+8, std::array<std::uint8_t,8>{{137,80,78,71,13,10,26,10}}.begin()), "PNG magic");
    std::size_t cursor = 8;
    std::vector<std::uint8_t> compressed;
    std::vector<std::string> chunks;
    while (cursor < png.size()) {
        const auto size = static_cast<std::size_t>(u32(png,cursor)); cursor += 4;
        check(size <= png.size() - cursor - 8, "PNG bounded chunk");
        const std::string type(png.begin()+static_cast<std::ptrdiff_t>(cursor),png.begin()+static_cast<std::ptrdiff_t>(cursor+4));
        chunks.push_back(type); cursor += 4;
        if (type == "IHDR") {
            check(size == 13 && u32(png,cursor) == static_cast<std::uint32_t>(image.width()) && u32(png,cursor+4) == static_cast<std::uint32_t>(image.height()), "PNG dimensions");
            check(png[cursor+8] == 8 && png[cursor+9] == 6, "PNG RGBA8");
        }
        if (type == "IDAT") compressed.insert(compressed.end(),png.begin()+static_cast<std::ptrdiff_t>(cursor),png.begin()+static_cast<std::ptrdiff_t>(cursor+size));
        cursor += size + 4;
    }
    check(chunks == std::vector<std::string>{"IHDR","IDAT","IEND"}, "PNG framing");
    const auto raw = inflate_stored(compressed);
    const auto width = static_cast<std::size_t>(image.width());
    check(raw.size() == (width*4+1)*width, "PNG decoded length");
    for (std::size_t y = 0; y < width; ++y) {
        check(raw[y*(width*4+1)] == 0, "PNG filter type zero");
        check(std::equal(raw.begin()+static_cast<std::ptrdiff_t>(y*(width*4+1)+1),raw.begin()+static_cast<std::ptrdiff_t>((y+1)*(width*4+1)),image.pixels().begin()+static_cast<std::ptrdiff_t>(y*width*4)), "PNG pixels match RGBA");
    }
}
bool warning(const specqr::RenderDiagnostics& d, const std::string& code) { return std::find(d.warnings.begin(),d.warnings.end(),code) != d.warnings.end(); }
void render_tests() {
    using namespace specqr;
    const auto qr = QRCode::generate("SpecQR C++");
    RenderOptions options;
    const auto image = to_rgba(qr);
    check(image.width() == (qr.size()+8)*8 && image.height() == image.width(), "default raster dimensions");
    check(image.pixels()[0] == 255 && image.pixels()[3] == 255, "quiet zone white opaque");
    const auto finder = static_cast<std::size_t>(4*8*image.width()+4*8)*4;
    check(image.pixels()[finder] == 0 && image.pixels()[finder+3] == 255, "finder pixel black");
    verify_png(qr, options);
    options.scale = 3; options.foreground = "#1234"; options.background = "transparent";
    verify_png(qr, options);
    const auto colored = to_rgba(qr,options);
    check(colored.pixels()[0] == 0 && colored.pixels()[3] == 0, "transparent straight RGBA");
    const auto p = static_cast<std::size_t>(4*3*colored.width()+4*3)*4;
    check(colored.pixels()[p] == 17 && colored.pixels()[p+1] == 34 && colored.pixels()[p+2] == 51 && colored.pixels()[p+3] == 68, "short RGBA color channels");
    options.title = "漢字 <x> & \"safe\"";
    const auto svg = to_svg(qr, options);
    check(svg.find("<title>漢字 &lt;x&gt; &amp; &quot;safe&quot;</title>") != std::string::npos, "SVG escaped UTF8 title");
    check(svg.find("fill=\"#112233\" fill-opacity=") != std::string::npos, "SVG canonical alpha fill");
    check(to_svg_data_url(qr,options).find("data:image/svg+xml;charset=utf-8,%3Csvg") == 0, "SVG data URL");
    check(to_png_data_url(qr).find("data:image/png;base64,iVBORw0KGgo") == 0, "PNG data URL");
    std::ostringstream output; save_svg(qr,output,options); check(output.str() == svg, "SVG stream");
    std::ostringstream png_stream; save_png(qr,png_stream,options); check(png_stream.str().size() == to_png(qr,options).size(), "PNG stream");
    RenderOptions invalid;
    invalid.margin = -1; rejects([&]{to_svg(qr,invalid);}, "reject negative margin");
    invalid = {}; invalid.scale = 0; rejects([&]{to_png(qr,invalid);}, "reject zero scale");
    invalid = {}; invalid.margin = std::numeric_limits<int>::max(); rejects([&]{to_svg(qr,invalid);}, "reject span overflow");
    invalid = {}; invalid.scale = std::numeric_limits<int>::max(); rejects([&]{to_svg(qr,invalid);}, "reject dimension overflow");
    invalid = {}; invalid.width = qr.size()+9; rejects([&]{to_rgba(qr,invalid);}, "reject fractional pixel width");
    invalid = {}; invalid.scale = 100; rejects([&]{to_png(qr,invalid);}, "reject raster budget");
    invalid = {}; invalid.foreground = "red\"/><script>"; rejects([&]{to_svg(qr,invalid);}, "reject injected color");
    invalid = {}; invalid.title = std::string("x\0y",3); rejects([&]{to_svg(qr,invalid);}, "reject XML nul");
    invalid = {}; invalid.title = std::string("\xED\xA0\x80",3); rejects([&]{to_svg(qr,invalid);}, "reject UTF8 surrogate");
    invalid = {}; invalid.title = std::string(65537,'a'); rejects([&]{to_png(qr,invalid);}, "reject title budget");
    for (double dpi : {0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::denorm_min()}) {
        invalid = {}; invalid.print_dpi = dpi; rejects([&]{to_svg(qr,invalid);}, "reject invalid DPI");
    }
    invalid = {}; invalid.foreground = "oops"; std::ostringstream untouched;
    rejects([&]{save_png(qr,untouched,invalid);}, "invalid PNG checked before stream"); check(untouched.str().empty(), "no bytes written on invalid options");
    std::ostringstream bad; bad.setstate(std::ios::badbit); bool stream_failed=false;
    try {save_svg(qr,bad);} catch(const std::ios_base::failure&) {stream_failed=true;} check(stream_failed,"stream failure detected");
    options = {}; options.width = (qr.size()+8)*3; options.scale=99;
    check(to_rgba(qr,options).width() == *options.width,"exact width overrides scale");
    struct TestCanvas : Canvas {
        int w=0, h=0, calls=0;
        void resize(int a,int b) override {w=a;h=b;}
        void fill_rectangle(int,int,int,int,const std::string&) override {++calls;}
    } canvas;
    draw(qr,canvas,options); int dark=0; for(const auto& row:qr.matrix()) for(bool cell:row) if(cell) ++dark;
    check(canvas.w == *options.width && canvas.h == canvas.w && canvas.calls == dark+1,"canvas adapter geometry");
    rejects([]{RgbaImage invalid_image(2,2,{1,2,3});},"RGBA length contract");
    auto diagnostics=render_diagnostics(qr);
    check(diagnostics.quiet_zone_sufficient() && diagnostics.color_strong() && diagnostics.color_sufficient(),"default render diagnostics");
    options = {}; options.margin=0;options.scale=1;options.foreground="#fff8";options.background="#000";options.print_dpi=300;
    diagnostics=render_diagnostics(qr,options);
    for(const char* name:{"QUIET_ZONE_TOO_SMALL","COLOR_ALPHA_USED","COLOR_POLARITY_INVERTED","PRINT_MODULE_TOO_SMALL","RASTER_SCALE_SMALL","SCAN_RISK"}) check(warning(diagnostics,name),"expected scan warning");
    check(!warning(render_diagnostics(qr.planning(),options),"RASTER_SCALE_SMALL"),"planning omits raster advice");
    options = {};options.foreground="unsupported";
    check(warning(render_diagnostics(qr,options),"COLOR_CONTRAST_UNKNOWN"),"unknown color diagnostic");
    options.foreground="#777";options.background="#888";
    check(warning(render_diagnostics(qr,options),"COLOR_CONTRAST_LOW"),"low contrast diagnostic");
    auto broken=qr.planning();broken.capacity_bits=std::numeric_limits<int>::max();broken.data_bit_length=std::numeric_limits<int>::min();
    rejects([&]{render_diagnostics(broken);},"reject malformed public plan");
}
void structured_tests() {
    using namespace specqr;
    StructuredAppendOptions options;options.qr_options.version=1;options.qr_options.mask=2;options.diagnostics=true;
    const std::string text = "ABCDEFGHIJ0123456789abcdefghij漢字😀ABCDEFGHIJ0123456789abcdefghij漢字😀";
    const auto result=generate_structured_append(text,options);
    check(result.total()>=2 && result.total()<=16,"text splits bounded");
    check(result.parity()==structured_append_parity(text),"text canonical parity");
    std::vector<StructuredAppendPart> parts;
    int last=0;
    for(std::size_t i=0;i<result.symbols().size();++i) {
        const auto& code=result.symbols()[i];const auto& d=result.diagnostics().symbols[i];
        check(d.byte_start==last,"contiguous UTF8 byte ranges");last+=d.byte_length;
        const auto chunk=text.substr(static_cast<std::size_t>(d.byte_start),static_cast<std::size_t>(d.byte_length));
        parts.emplace_back(d.index,d.total,d.parity,chunk);
        check(code.planning().structured_append()->sequence_indicator()==d.sequence_indicator(),"SA header diagnostic");
        Options low=options.qr_options;low.structured_append=StructuredAppendHeader{d.index,d.total,d.parity};
        check(QRCode::generate(chunk,low).matrix()==code.matrix(),"SA exact per-part low-level matrix");
    }
    check(last==static_cast<int>(text.size()),"all UTF8 bytes retained");
    std::reverse(parts.begin(),parts.end());
    const auto merged=merge_structured_append_parts(parts);
    check(merged.text() && *merged.text()==text && merged.diagnostics().parity_check.matches(),"Unicode merge ordering and parity");
    const auto binary=std::vector<std::uint8_t>{0,255,128,29};const auto parity=structured_append_parity(binary);
    auto binary_merge=merge_structured_append_parts({StructuredAppendPart(2,2,parity,std::vector<std::uint8_t>{128,29}),StructuredAppendPart(1,2,parity,std::vector<std::uint8_t>{0,255})});
    check(!binary_merge.text() && binary_merge.bytes()==binary,"raw binary merge no UTF8 conversion");
    const auto four=structured_append_parity("ABCD");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(1,2,four,"AB")});},"reject missing part");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(1,2,four,"AB"),StructuredAppendPart(1,2,four,"CD")});},"reject duplicate part");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(1,2,0,"AB"),StructuredAppendPart(2,2,0,"CD")});},"reject wrong parity");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(1,2,four,"AB"),StructuredAppendPart(2,2,four,std::vector<std::uint8_t>{67,68})});},"reject mixed merge types");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(0,2,four,"AB"),StructuredAppendPart(2,2,four,"CD")});},"reject zero part index");
    rejects([&]{merge_structured_append_parts({StructuredAppendPart(1,2,four,"AB"),StructuredAppendPart(2,3,four,"CD")});},"reject total disagreement");
    rejects([&]{generate_structured_append("a",options);},"reject unnecessary split");
    rejects([&]{generate_structured_append("",options);},"reject empty text");
    rejects([&]{generate_structured_append(std::string("\xF4\x90\x80\x80",4),options);},"reject UTF8 beyond Unicode");
    rejects([&]{structured_append_parity(std::string("\xC0\x80",2));},"reject overlong UTF8 parity");
    rejects([&]{StructuredAppendPart p(1,2,0,std::string("\xF0\x9F",2));},"reject truncated decoded text");
    auto invalid=options;invalid.max_symbols=1;rejects([&]{generate_structured_append(text,invalid);},"reject max symbols 1");
    invalid=options;invalid.max_symbols=17;rejects([&]{generate_structured_append(text,invalid);},"reject max symbols 17");
    invalid=options;invalid.qr_options.gs1=true;rejects([&]{generate_structured_append(text,invalid);},"reject SA GS1");
    invalid=options;invalid.qr_options.eci=26;rejects([&]{generate_structured_append(text,invalid);},"reject SA ECI");
    invalid=options;invalid.qr_options.boost_ecc=true;rejects([&]{generate_structured_append(text,invalid);},"reject SA boost");
    invalid=options;invalid.split_units=SplitUnitsDetail::Full;rejects([&]{generate_structured_append(text,invalid);},"reject full text split details");
    rejects([&]{generate_segments_structured_append({Segment::eci(26),Segment::byte(text)},options);},"reject manual control");
    rejects([&]{generate_segments_structured_append({Segment::byte("")},options);},"reject empty manual segment");
    rejects([&]{generate_segments_structured_append({Segment::numeric(std::string(100,'1'))},options);},"reject unsplittable numeric atom");
    std::vector<Segment> segments{Segment::numeric("123456"),Segment::byte(text),Segment::kanji("漢字"),Segment::byte(std::vector<std::uint8_t>(25,255))};
    options.split_units=SplitUnitsDetail::Full;
    const auto manual=generate_segments_structured_append(segments,options);
    check(manual.diagnostics().split_units.has_value() && manual.diagnostics().segment_count==4,"manual full diagnostics");
    check(manual.parity()==structured_append_segments_parity(segments),"manual canonical parity");
    check(manual.byte_length()==6+static_cast<int>(text.size())+6+25,"manual Kanji canonical UTF8 bytes");
    int bytes=0;for(const auto& d:manual.diagnostics().symbols){check(d.byte_start==bytes,"manual contiguous byte ranges");bytes+=d.byte_length;}
    check(bytes==manual.byte_length(),"manual all bytes retained");
    options.split_units=SplitUnitsDetail::Summary;
    check(!generate_segments_structured_append(segments,options).diagnostics().split_units,"summary avoids unit allocation");
    std::mt19937 random(0x5a17);
    for(int trial=0;trial<40;++trial) {
        const int length=20+static_cast<int>(random()%150);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));for(auto& byte:payload)byte=static_cast<std::uint8_t>(random()&255U);
        const auto set=generate_structured_append(payload,options);std::vector<StructuredAppendPart> chunks;
        for(const auto& d:set.diagnostics().symbols)chunks.emplace_back(d.index,d.total,d.parity,std::vector<std::uint8_t>(payload.begin()+d.byte_start,payload.begin()+d.byte_start+d.byte_length));
        std::shuffle(chunks.begin(),chunks.end(),random);
        check(merge_structured_append_parts(chunks).bytes()==payload,"deterministic randomized binary split/merge");
    }
}
} // namespace
int main() {
    try {render_tests();structured_tests();std::cout<<"extensions: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"extensions failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
