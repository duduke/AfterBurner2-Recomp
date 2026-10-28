#pragma once
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Frontend {
// Strict JSON for custom-stage documents, independent of emulated hardware.
struct StageJson {
    enum class Kind{null,boolean,number,string,array,object};Kind kind=Kind::null;
    bool boolean{};double number{};std::string string;
    std::vector<StageJson> array;std::map<std::string,StageJson> object;
    const StageJson* find(const std::string& key)const{auto p=object.find(key);return p==object.end()?nullptr:&p->second;}
    void keys(std::initializer_list<std::string_view> allowed)const{
        if(kind!=Kind::object)throw std::runtime_error("Expected JSON object");
        for(const auto& [key,value]:object){bool known=false;for(auto a:allowed)known|=a==key;if(!known)throw std::runtime_error("Unknown JSON field: "+key);}
    }
    std::string text()const{if(kind!=Kind::string)throw std::runtime_error("Expected JSON string");return string;}
    double numeric()const{if(kind!=Kind::number)throw std::runtime_error("Expected JSON number");return number;}
    bool flag()const{if(kind!=Kind::boolean)throw std::runtime_error("Expected JSON boolean");return boolean;}
    static StageJson parse(std::string_view source){
        struct Parser{
            std::string_view s;size_t p{};
            [[noreturn]] void error()const{throw std::runtime_error("Invalid JSON at byte "+std::to_string(p));}
            void ws(){while(p<s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\r' || s[p]=='\n'))++p;}
            bool take(char c){if(p<s.size() && s[p]==c){++p;return true;}return false;}
            unsigned hex(){unsigned v=0;for(int i=0;i<4;++i){if(p==s.size())error();char c=s[p++];unsigned n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;if(n==16)error();v=v*16+n;}return v;}
            std::string str(){if(!take('"'))error();std::string out;
                while(p<s.size()){unsigned char c=s[p++];if(c=='"')return out;if(c<32)error();
                    if(c!='\\'){if(c<128){out+=char(c);continue;}
                        unsigned count=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;if(!count || p+count>s.size())error();
                        unsigned code=c&((1u<<(6-count))-1);std::string bytes(1,char(c));
                        for(unsigned n=0;n<count;++n){unsigned char next=s[p++];if((next&0xc0)!=0x80)error();code=code*64+(next&63);bytes+=char(next);}
                        if(code<(count==1?0x80u:count==2?0x800u:0x10000u) || code>0x10ffff || (code>=0xd800&&code<=0xdfff))error();out+=bytes;continue;}
                    if(p==s.size())error();char e=s[p++];
                    switch(e){case '"':case '\\':case '/':out+=e;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
                    case 'u':{unsigned code=hex();if(code>=0xd800&&code<=0xdbff){if(!take('\\')||!take('u'))error();const unsigned low=hex();if(low<0xdc00||low>0xdfff)error();code=0x10000+(code-0xd800)*1024+low-0xdc00;}else if(code>=0xdc00&&code<=0xdfff)error();
                        if(code<128)out+=char(code);else if(code<2048){out+=char(0xc0|(code>>6));out+=char(0x80|(code&63));}else if(code<65536){out+=char(0xe0|(code>>12));out+=char(0x80|((code>>6)&63));out+=char(0x80|(code&63));}else{out+=char(0xf0|(code>>18));out+=char(0x80|((code>>12)&63));out+=char(0x80|((code>>6)&63));out+=char(0x80|(code&63));}break;}
                    default:error();}}
                error();}
            StageJson value(unsigned depth=0){if(depth>64)throw std::runtime_error("JSON nesting exceeds parser safety limit");ws();if(p==s.size())error();StageJson v;
                if(s[p]=='"'){v.kind=Kind::string;v.string=str();return v;}
                if(take('{')){v.kind=Kind::object;ws();if(take('}'))return v;do{ws();auto key=str();ws();if(!take(':'))error();auto child=value(depth+1);if(!v.object.emplace(key,std::move(child)).second)throw std::runtime_error("Duplicate JSON field: "+key);ws();if(take('}'))return v;}while(take(','));error();}
                if(take('[')){v.kind=Kind::array;ws();if(take(']'))return v;do{v.array.push_back(value(depth+1));ws();if(take(']'))return v;}while(take(','));error();}
                for(auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")})if(s.substr(p,literal.size())==literal){p+=literal.size();v.kind=literal=="null"?Kind::null:Kind::boolean;v.boolean=literal=="true";return v;}
                const size_t begin=p;take('-');if(!take('0')){if(p==s.size()||s[p]<'1'||s[p]>'9')error();while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
                auto digits=[&]{size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==start)error();};
                if(take('.'))digits();if(take('e')||take('E')){if(!take('+'))take('-');digits();}
                v.kind=Kind::number;auto result=std::from_chars(s.data()+begin,s.data()+p,v.number);if(result.ec!=std::errc{} || result.ptr!=s.data()+p || !std::isfinite(v.number))error();return v;
            }
        } parser{source};auto v=parser.value();parser.ws();if(parser.p!=source.size())parser.error();return v;
    }
    static StageJson load(const std::string& path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot open stage JSON: "+path);return parse(std::string(std::istreambuf_iterator<char>(in),{}));}
};
}
