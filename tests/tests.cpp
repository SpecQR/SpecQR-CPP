#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <algorithm>
#include <atomic>
#include <climits>
#include <future>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const specqr::Error&) { rejected = true; }
    check(rejected, message);
}
void capacity_boundaries() {
    using namespace specqr;
    for (int version = 1; version <= 40; ++version) for (auto level : {Ecc::L,Ecc::M,Ecc::Q,Ecc::H}) {
        Options o; o.version = version; o.ecc = level; o.mask = version % 8;
        for (auto mode : {Mode::Numeric,Mode::Alphanumeric,Mode::Byte,Mode::Kanji}) {
            const auto cap = QRCode::capacity(version, level, mode);
            check(cap.capacity_bits() > 0 && cap.total_codewords > cap.data_codewords, "capacity layout");
            const int n = mode == Mode::Byte ? *cap.max_bytes : *cap.max_characters;
            o.mode = mode;
            std::string text;
            for (int i=0; i<n; ++i) text += mode == Mode::Kanji ? u8"漢" : mode == Mode::Numeric ? "1" : "A";
            auto plan = QRCode::estimate(text, o);
            check(plan.ok && plan.selected_version == version && plan.overflow_bits() == 0, "exact capacity must fit");
            text += mode == Mode::Kanji ? u8"漢" : "1";
            plan = QRCode::estimate(text, o);
            check(!plan.ok && plan.overflow_bits() > 0, "over capacity must fail planning");
            rejects([&]{ QRCode::generate(text, o); }, "over capacity generation");
        }
    }
    for (int v : {1,9,10,26,27,40}) {
        check(QRCode::capacity(v,Ecc::M,Mode::Byte).character_count_bits == (v < 10 ? 8 : 16), "byte count band");
        check(QRCode::capacity(v,Ecc::M,Mode::Numeric).character_count_bits == (v < 10 ? 10 : v < 27 ? 12 : 14), "numeric count band");
    }
}
void malformed() {
    using namespace specqr;
    for (auto s : {std::string("\x80",1),std::string("\xC0\xAF",2),std::string("\xED\xA0\x80",3),std::string("\xF4\x90\x80\x80",4),std::string("\xF0\x9F",2),std::string("\xE0\x80\x80",3)})
        rejects([&]{QRCode::generate(s);}, "invalid UTF8");
    for (int v : {INT_MIN,-1,0,41,INT_MAX}) { Options o; o.version=v; rejects([&]{QRCode::generate("a",o);},"invalid version"); }
    for (int m : {INT_MIN,-1,8,INT_MAX}) { Options o; o.mask=m; rejects([&]{QRCode::generate("a",o);},"invalid mask"); }
    for (int n : {INT_MIN,-1,4,INT_MAX}) { Options o; o.ecc=static_cast<Ecc>(n); rejects([&]{QRCode::generate("a",o);},"invalid ECC enum"); }
    for (int n : {INT_MIN,-1,5,9,10,INT_MAX}) { Options o; o.mode=static_cast<Mode>(n); rejects([&]{QRCode::generate("a",o);},"invalid input mode enum"); }
    for (int n : {-1,1000000,INT_MAX}) rejects([&]{Segment::eci(n);},"invalid ECI");
    for (int n : {0,127,128,16383,16384,999999}) check(Segment::eci(n).assignment_number()==n,"ECI valid boundaries");
    rejects([]{Segment::numeric("12a");},"invalid numeric");
    rejects([]{Segment::alphanumeric("abc");},"invalid alphanumeric");
    rejects([]{Segment::kanji(u8"😀");},"unsupported Kanji");
    rejects([]{Segment::fnc1_second("123");},"invalid indicator");
    rejects([]{Segment::structured_append(0,2,0);},"invalid SA index");
    rejects([]{Segment::structured_append(1,1,0);},"invalid SA total");
    rejects([]{QRCode::generate_segments({Segment::byte("x"),Segment::fnc1()});},"late FNC1");
    rejects([]{QRCode::generate_segments({Segment::fnc1(),Segment::fnc1()});},"duplicate FNC1");
    rejects([]{QRCode::generate_segments({Segment::eci(3),Segment::fnc1()});},"misordered FNC1");
    rejects([]{Options o; o.min_version=20; o.max_version=10; QRCode::estimate("a",o);},"reversed versions");
    rejects([]{Options o; o.fnc1_second="A"; o.gs1=true; QRCode::generate("a",o);},"conflicting FNC1");
    rejects([]{QRCode::capacity(1,Ecc::M,Mode::Auto);},"invalid capacity mode");
    rejects([]{QRCode::capacity(1,Ecc::M,Mode::Byte,-1);},"negative control bits");
    rejects([]{QRCode::generate(std::string(max_input_bytes+1,'1'));},"text limit");
    rejects([]{QRCode::generate(std::vector<std::uint8_t>(max_input_bytes+1));},"byte limit");
    const auto huge = QRCode::estimate(std::string(100000,'1'));
    check(!huge.ok && huge.evaluated_version==40,"oversize preflight");
    const auto qr=QRCode::generate("abc");
    rejects([&]{qr.module(-1,0);},"negative module");
    rejects([&]{qr.module(qr.size(),0);},"module bound");
    for (int n : {INT_MIN,-1,0,INT_MAX}) { RenderOptions r; r.scale=n; rejects([&]{to_png(qr,r);},"invalid raster scale"); }
    for (int n : {INT_MIN,-1,INT_MAX}) { RenderOptions r; r.margin=n; rejects([&]{to_svg(qr,r);},"invalid margin"); }
}
void properties() {
    using namespace specqr;
    std::mt19937 rng(0x535152);
    for (int iteration=0; iteration<300; ++iteration) {
        std::vector<std::uint8_t> data(static_cast<std::size_t>(rng()%150));
        for (auto& b:data) b=static_cast<std::uint8_t>(rng());
        Options o; o.ecc=static_cast<Ecc>(rng()%4); o.mask=static_cast<int>(rng()%8);
        const auto a=QRCode::generate(data,o), b=QRCode::generate(data,o);
        check(a.matrix()==b.matrix() && a.codewords()==b.codewords(),"deterministic binary");
        check(a.size()==4*a.version()+17 && a.data_codewords().size()==static_cast<std::size_t>(a.planning().capacity_bits/8),"shape contract");
        check(a.planning().ok && a.diagnostics().total_codewords==static_cast<int>(a.codewords().size()),"diagnostics contract");
        auto segments=std::vector<Segment>{Segment::byte(data)};
        const auto c=QRCode::generate_segments(segments,o);
        check(a.matrix()==c.matrix(),"manual binary equivalence");
    }
    for (auto s : {"", "12345678901234567890", "HELLO WORLD", u8"日本語😀é\0", "abc123456789012345678901234567890XYZ"}) {
        const auto a=QRCode::generate(s);
        auto p=QRCode::estimate(s);
        check(p.ok && p.data_bit_length==a.planning().data_bit_length,"planning generation consistency");
        Options o; o.optimize_segments=false;
        const auto p_single=QRCode::estimate(s,o);
        check(p.data_bit_length <= p_single.data_bit_length,"optimizer not worse than single");
    }
    Options fn; fn.fnc1_second="A";
    const auto literal=QRCode::generate("ABC%DEF",fn);
    check(literal.segments().back().mode()==Mode::Byte,"literal percent safe FNC1");
    fn.mode=Mode::Alphanumeric;
    rejects([&]{QRCode::generate("ABC%DEF",fn);},"unsafe highlevel percent");
    rejects([&]{QRCode::generate_segments({Segment::alphanumeric("ABC%DEF")},fn);},"unsafe manual option percent");
    const auto low=QRCode::generate_segments({Segment::fnc1(),Segment::alphanumeric("ABC%%DEF")});
    check(low.planning().gs1(),"lowlevel escaped percent");
    Options boost; boost.ecc=Ecc::L; boost.boost_ecc=true;
    check(QRCode::generate("1",boost).ecc()==Ecc::H,"ECC boost");
    const auto ref=QRCode::generate(u8"Thread safe 日本語 123456789");
    const auto png=to_png(ref);
    std::vector<std::future<bool>> futures;
    for (int i=0;i<8;++i) futures.push_back(std::async(std::launch::async,[&]{
        for(int k=0;k<25;++k) {
            const auto q=QRCode::generate(u8"Thread safe 日本語 123456789");
            if(q.matrix()!=ref.matrix() || to_png(ref)!=png) return false;
        }
        return true;
    }));
    for(auto& future:futures) check(future.get(),"parallel shared immutable result");
    auto copied=ref.matrix(); copied[0][0]=!copied[0][0];
    check(copied!=ref.matrix(),"detached matrix copy");
    static_assert(std::is_same<decltype(ref.matrix()),const std::vector<std::vector<bool>>&>::value,"const matrix API");
}
}
int main() {
    try {
        capacity_boundaries(); malformed(); properties();
        std::cout << "specqr-unit: " << checks << " checks passed; 0 skipped\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "specqr-unit failed after " << checks << " checks: " << e.what() << '\n'; return 1;
    }
}
