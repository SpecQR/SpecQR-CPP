// Test-only line adapter. Standard library only; deliberately no JSON input parser.
#include <specqr/specqr.hpp>
#include <specqr/render.hpp>
#include <specqr/structured_append.hpp>
#include "internal.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
using namespace specqr;
namespace {
std::vector<std::string> split(const std::string& value,char separator='\t') {
 std::vector<std::string> result;std::size_t start=0;
 for(;;){auto end=value.find(separator,start);result.push_back(value.substr(start,end-start));if(end==std::string::npos)return result;start=end+1;}
}
int number(const std::string& value){std::size_t used=0;int n=std::stoi(value,&used);if(used!=value.size())throw std::invalid_argument("integer protocol field");return n;}
std::vector<std::uint8_t> unhex(const std::string& value){
 if(value.size()%2)throw std::invalid_argument("odd hex protocol field");std::vector<std::uint8_t> out;out.reserve(value.size()/2);
 auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::invalid_argument("bad hex protocol field");};
 for(std::size_t i=0;i<value.size();i+=2)out.push_back(static_cast<std::uint8_t>(digit(value[i])*16+digit(value[i+1])));return out;
}
std::string unhex_text(const std::string& h){auto bytes=unhex(h);return std::string(bytes.begin(),bytes.end());}
std::string hex(const std::vector<std::uint8_t>& bytes){const char* digits="0123456789abcdef";std::string s;s.reserve(bytes.size()*2);for(auto b:bytes){s+=digits[b>>4];s+=digits[b&15];}return s;}
std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<'?';else o<<c;}o<<'"';return o.str();}
Ecc ecc(const std::string& s){if(s=="L")return Ecc::L;if(s=="M")return Ecc::M;if(s=="Q")return Ecc::Q;if(s=="H")return Ecc::H;throw std::invalid_argument("ECC protocol field");}
Mode mode(const std::string& s){if(s=="auto")return Mode::Auto;if(s=="numeric")return Mode::Numeric;if(s=="alphanumeric")return Mode::Alphanumeric;if(s=="byte")return Mode::Byte;if(s=="kanji")return Mode::Kanji;throw std::invalid_argument("mode protocol field");}
void matrix(const std::vector<std::vector<bool>>& m){std::cout<<"[";bool first=true;for(const auto& row:m){if(!first)std::cout<<',';first=false;std::cout<<'"';for(bool b:row)std::cout<<(b?'1':'0');std::cout<<'"';}std::cout<<']';}
void penalties(const std::vector<MaskPenalty>& values){std::cout<<'[';bool first=true;for(const auto& p:values){if(!first)std::cout<<',';first=false;std::cout<<p.penalty;}std::cout<<']';}
int render_scale(const std::vector<std::string>& f){return number(split(f.at(11),',').at(0));}
int max_symbols(const std::vector<std::string>& f){return number(split(f.at(11),',').at(3));}
Options options(const std::vector<std::string>& f){Options o;auto extra=split(f.at(11),',');o.min_version=number(extra.at(1));o.max_version=number(extra.at(2));if(number(f.at(1))!=-1)o.version=number(f[1]);o.ecc=ecc(f.at(2));if(number(f.at(3))!=-1)o.mask=number(f[3]);o.mode=mode(f.at(4));o.optimize_segments=number(f.at(5))!=0;o.boost_ecc=number(f.at(6))!=0;o.gs1=number(f.at(7))!=0;if(number(f.at(8))!=-1)o.eci=number(f[8]);if(!f.at(9).empty())o.fnc1_second=unhex_text(f[9]);if(f.at(10)!="-"){auto a=split(f[10],',');o.structured_append=StructuredAppendHeader{number(a.at(0)),number(a.at(1)),static_cast<std::uint8_t>(number(a.at(2)))};}return o;}
Segment segment(const std::string& field){auto pos=field.find(':');auto type=field.substr(0,pos),value=field.substr(pos+1);if(type=="numeric")return Segment::numeric(unhex_text(value));if(type=="alphanumeric")return Segment::alphanumeric(unhex_text(value));if(type=="byte")return Segment::byte(unhex(value));if(type=="byte-text")return Segment::byte(unhex_text(value));if(type=="kanji")return Segment::kanji(unhex_text(value));if(type=="eci")return Segment::eci(number(value));if(type=="fnc1")return Segment::fnc1();if(type=="fnc1-second")return Segment::fnc1_second(unhex_text(value));if(type=="structured-append"){auto a=split(value,',');return Segment::structured_append(number(a.at(0)),number(a.at(1)),static_cast<std::uint8_t>(number(a.at(2))));}throw std::invalid_argument("segment protocol field");}
void output(const QRCode& q,int png_scale){std::cout<<"{\"version\":"<<q.version()<<",\"ecc\":\""<<ecc_name(q.ecc())<<"\",\"mask\":"<<q.mask()<<",\"data\":\""<<hex(q.data_codewords())<<"\",\"codewords\":\""<<hex(q.codewords())<<"\",\"matrix\":";matrix(q.matrix());std::cout<<",\"penalty\":"<<q.diagnostics().mask_penalty<<",\"penalties\":";penalties(q.diagnostics().mask_penalties);if(png_scale>0){RenderOptions render;render.scale=png_scale;std::cout<<",\"png\":\""<<hex(to_png(q,render))<<'"';}std::cout<<'}';}
void output_sa(const StructuredAppendResult& result,int scale){std::cout<<"{\"total\":"<<result.total()<<",\"parity\":"<<static_cast<int>(result.parity())<<",\"inputLength\":"<<result.input_length()<<",\"byteLength\":"<<result.byte_length()<<",\"symbols\":[";bool first=true;for(const auto& code:result.symbols()){if(!first)std::cout<<',';first=false;output(code,scale);}std::cout<<"]}";}
void handle(const std::vector<std::string>& f){
 if(f.at(0)=="raw") {int v=number(f.at(1));Ecc e=ecc(f.at(2));int mask=number(f.at(3)),seed=number(f.at(4));int size=detail::data_codewords(v,e);std::vector<std::uint8_t> data;for(int i=0;i<size;i++)data.push_back(static_cast<std::uint8_t>(seed==0?0:seed==1?255:((i*149+v*43+static_cast<int>(e)*89+seed*67)^(i>>(seed+1)))&255));auto words=detail::interleave(data,v,e);auto m=detail::build_matrix(words,v,e,mask<0?std::optional<int>{}:mask);std::cout<<"{\"data\":\""<<hex(data)<<"\",\"codewords\":\""<<hex(words)<<"\",\"matrix\":";matrix(m.matrix);std::cout<<",\"mask\":"<<m.mask<<",\"penalty\":"<<m.penalty<<",\"penalties\":";penalties(m.penalties);std::cout<<'}';return;}
 if(f[0]=="gf"){std::vector<std::uint8_t> b;for(int a=0;a<256;a++)for(int c=0;c<256;c++)b.push_back(detail::gf_multiply(static_cast<std::uint8_t>(a),static_cast<std::uint8_t>(c)));std::cout<<"{\"bytes\":\""<<hex(b)<<"\"}";return;}
 if(f[0]=="rs"){int degree=number(f.at(1));auto g=detail::rs_generator(degree);std::vector<std::uint8_t> data;for(int i=0;i<300;i++)data.push_back(static_cast<std::uint8_t>((i*61+degree)&255));std::cout<<"{\"generator\":\""<<hex(g)<<"\",\"remainder\":\""<<hex(detail::rs_remainder(data,g))<<"\"}";return;}
 auto o=options(f);
 if(f[0]=="capacity"){auto c=QRCode::capacity(o.version.value(),o.ecc,o.mode);std::cout<<"{\"maximum\":"<<(c.max_characters?*c.max_characters:c.max_bytes.value())<<",\"dataCodewords\":"<<c.data_codewords<<",\"capacityBits\":"<<c.capacity_bits()<<",\"countBits\":"<<c.character_count_bits.value()<<'}';return;}
 if(f[0]=="estimate"){auto p=QRCode::estimate(unhex_text(f.at(12)),o);std::cout<<"{\"fits\":"<<(p.ok?"true":"false")<<",\"version\":"<<p.evaluated_version<<",\"requiredBits\":"<<p.data_bit_length<<",\"capacityBits\":"<<p.capacity_bits<<'}';return;}
 if(f[0]=="sa-text"){StructuredAppendOptions sa;sa.qr_options=o;sa.max_symbols=max_symbols(f);output_sa(generate_structured_append(unhex_text(f.at(12)),sa),render_scale(f));}
 else if(f[0]=="sa-bytes"){StructuredAppendOptions sa;sa.qr_options=o;sa.max_symbols=max_symbols(f);output_sa(generate_structured_append(unhex(f.at(12)),sa),render_scale(f));}
 else if(f[0]=="sa-segments"){StructuredAppendOptions sa;sa.qr_options=o;sa.max_symbols=max_symbols(f);std::vector<Segment> segments;for(std::size_t i=12;i<f.size();i++)segments.push_back(segment(f[i]));output_sa(generate_segments_structured_append(segments,sa),render_scale(f));}
 else if(f[0]=="text")output(QRCode::generate(unhex_text(f.at(12)),o),render_scale(f));
 else if(f[0]=="bytes")output(QRCode::generate(unhex(f.at(12)),o),render_scale(f));
 else if(f[0]=="segments"){std::vector<Segment> segments;for(std::size_t i=12;i<f.size();i++)segments.push_back(segment(f[i]));output(QRCode::generate_segments(segments,o),render_scale(f));}
 else throw std::invalid_argument("command protocol field");
}
}
int main(){std::string line;while(std::getline(std::cin,line)){try{handle(split(line));}catch(const specqr::Error& e){std::cout<<"{\"error\":"<<quote(e.code())<<",\"message\":"<<quote(e.what())<<'}';}catch(const std::exception& e){std::cout<<"{\"error\":\"ADAPTER\",\"message\":"<<quote(e.what())<<'}';}std::cout<<'\n';}return std::cin.bad()?1:0;}
