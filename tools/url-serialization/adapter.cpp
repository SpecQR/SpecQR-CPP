#include <specqr/gs1.hpp>
#include <specqr/specqr.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace specqr::gs1;
std::string j(const std::string &s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<(int)c<<std::dec;else o<<c;}return o.str()+'"';}
std::string j(const char*s){return j(std::string(s));}
std::string j(char c){return j(std::string(1,c));}
std::string j(bool b){return b?"true":"false";}
std::string j(std::size_t n){return std::to_string(n);}
std::string obj(std::initializer_list<std::pair<std::string,std::string>> p){std::string s="{";bool first=true;for(auto&e:p){if(!first)s+=",";first=false;s+=j(e.first)+":"+e.second;}return s+"}";}
std::string j(const Element&e){return obj({{"ai",j(e.ai)},{"value",j(e.value)}});}
std::string j(const UnknownQuery&e){return obj({{"key",j(e.key)},{"value",j(e.value)}});}
std::string j(const ValidationIssue&e){return obj({{"code",j(e.code)},{"message",j(e.message)},{"reason",j(e.reason)},{"count",e.count?j(*e.count):"null"}});}
std::string jstrings(const std::vector<std::string>&v){std::string s="[";bool first=true;for(auto &x:v){if(!first)s+=",";first=false;s+=j(x);}return s+"]";}
std::string j(const AiInfo&e){auto variable=e.length.is_variable();return obj({{"ai",j(e.ai)},{"label",j(e.label)},{"length",variable?obj({{"type",j("variable")},{"min",j(e.length.min)},{"max",j(e.length.max)}}):obj({{"type",j("fixed")},{"exact",j(*e.length.exact)}})},{"valueKind",j(e.value_kind==ValueKind::Numeric?"numeric":"text")},{"checkDigitRule",j(e.check_digit_rule==CheckDigitRule::Gtin?"gtin":e.check_digit_rule==CheckDigitRule::Sscc?"sscc":"none")},{"digitalLinkRole",j(e.digital_link_role==DigitalLinkRole::PrimaryKey?"primary-key":e.digital_link_role==DigitalLinkRole::KeyQualifier?"key-qualifier":"data-attribute")},{"separator",j(variable?"required-when-followed":"none")},{"digitalLinkPathForPrimary",e.digital_link_path_for_primary.empty()?"null":jstrings(e.digital_link_path_for_primary)}});}
template<class T>std::string j(const std::vector<T>&v){std::string s="[";bool first=true;for(auto&e:v){if(!first)s+=",";first=false;s+=j(e);}return s+"]";}
std::string j(const AiInfo *e){return e?j(*e):"null";}
std::string j(const ElementStringResult&e){return obj({{"elements",j(e.elements)},{"hasSeparators",j(e.has_separators)}});}
std::string j(const ValidationResult&e){return obj({{"ok",j(e.ok)},{"elements",e.ok?j(e.elements):"null"},{"hasSeparators",e.has_separators?j(*e.has_separators):"null"},{"errors",j(e.errors)},{"warnings",j(e.warnings)}});}
std::string j(const DigitalLinkResult&e){return obj({{"elements",j(e.elements)},{"primary",j(e.primary)},{"pathElements",j(e.path_elements)},{"queryElements",j(e.query_elements)},{"unknownQuery",j(e.unknown_query)}});}
std::string j(const DigitalLinkValidationResult&e){return obj({{"ok",j(e.ok)},{"result",e.result?j(*e.result):"null"},{"errors",j(e.errors)},{"warnings",j(e.warnings)}});}
template<class F>void emit(F f){try{std::cout<<j(f())<<'\n';}catch(const specqr::Error&e){std::cout<<obj({{"throws",obj({{"code",j(e.code())},{"message",j(e.what())}})}})<<'\n';}}
