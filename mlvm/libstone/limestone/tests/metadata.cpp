#include "test.hpp"
#include "metacode.hpp"
#include "metacode/json.hpp"
#include <filesystem>

static void json_contracts() {
  using namespace limestone;
  using metacode::Value;
  const std::string source="\n{\n  \"outer\": [\n    7,\n    {\"flag\": true}\n  ]\n}";
  auto document=take(metacode::parse_json(source,"locations.json"));
  CHECK(document.source.file=="locations.json"&&document.source.offset==1&&document.source.line==2&&document.source.column==1);
  const auto& array=std::get<Value::Object>(document.data).at("outer");
  CHECK(array.source.offset==source.find('[')&&array.source.line==3&&array.source.column==12);
  const auto& flag=std::get<Value::Object>(std::get<Value::Array>(array.data)[1].data).at("flag");
  CHECK(flag.source.offset==source.find("true")&&flag.source.line==5&&flag.source.column==14);
  auto malformed=metacode::parse_json("{\n  \"value\": ]\n}","invalid.json");
  fails(malformed,Error::Code::Parse);
  CHECK(malformed.error().message.starts_with("invalid.json:2:12:"));
  const std::string unicode="\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf";
  auto encoded=take(metacode::print_json(Value(unicode)));
  CHECK(std::get<std::string>(take(metacode::parse_json(encoded)).data)==unicode);
  CHECK(std::get<std::string>(take(metacode::parse_json("\"\\udbff\\udfff\"")).data)=="\xf4\x8f\xbf\xbf");
  for(const std::string invalid:{"\x80","\xc0\x80","\xc1\xbf","\xc2","\xc2\x20","\xe0\x9f\xbf","\xed\xa0\x80","\xe2\x82","\xf0\x8f\xbf\xbf","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xff"}) {
    fails(metacode::parse_json("\""+invalid+"\""),Error::Code::Parse);
    fails(metacode::parse_json("{\""+invalid+"\":1}"),Error::Code::Parse);
    fails(metacode::print_json(Value(invalid)),Error::Code::InvalidArgument);
    fails(metacode::print_json(Value(Value::Object{{invalid,Value(true)}})),Error::Code::InvalidArgument);
  }
  const std::string limit(256,'['), endings(256,']');
  auto nested=take(metacode::parse_json(limit+"0"+endings));
  CHECK(take(metacode::print_json(nested))==limit+"0"+endings);
  fails(metacode::parse_json("["+limit+"0"+endings+"]"),Error::Code::ResourceLimit);
  Value::Array wrapper;wrapper.push_back(std::move(nested));
  fails(metacode::print_json(Value(std::move(wrapper))),Error::Code::ResourceLimit);
}

int main(int argc,char** argv) {return test_main([&]{
  using namespace limestone;
  json_contracts();
  auto a=take(metacode::parse_isa(R"(arch toy { word_size = 64; }
profile { family = "test"; model = "v1"; version = "2"; }
regclass G { r0(64)=0, r1(32)=1, }
alias acc = r0;
encoding E { width = 8; base = 0xff; }
op NOP { encoding = E; operands = ; semantics = (nop);
  tooling = { unknown = { preserved = [1, true, "x", { nested = 7; }]; }; }
}
)","toy.isa"));
  CHECK(a.name=="toy"&&a.family=="test"&&a.model=="v1"&&a.version=="2");
  CHECK(a.registers.size()==2&&a.registers[1].width==32);
  CHECK(a.aliases.at("acc")=="r0");CHECK(a.encodings.at("E").at("base").text()=="255");
  CHECK(a.operations.size()==1&&a.operations[0].semantics=="(nop)");
  CHECK(a.operations[0].source.file=="toy.isa"&&a.operations[0].source.line==6);
  const auto& tooling=std::get<metacode::Value::Object>(a.operations[0].fields.at("tooling").data);
  CHECK(tooling.at("unknown").text().find("nested=7")!=std::string::npos);
  CHECK(a.operations[0].fields.at("operands").text().empty());
  auto reordered=take(metacode::parse_isa("profile { family=before; } tooling { retained=true; } arch late { word_size=64; }"));
  CHECK(reordered.family=="before"&&reordered.fields.at("word_size").text()=="64");
  CHECK(std::get<metacode::Value::Object>(reordered.fields.at("tooling").data).at("retained").text()=="true");
  fails(metacode::parse_isa("profile { family=before; } arch duplicate { profile={family=after;}; }"),Error::Code::Parse);
  auto escaped=take(metacode::parse_isa(R"ISA(arch strings { x = "a\"b\\c\n"; } op eor { semantics = "(intrinsic \"eor\" x y)"; })ISA"));
  CHECK(escaped.fields.at("x").text()=="a\"b\\c\n");CHECK(escaped.operations[0].semantics=="(intrinsic \"eor\" x y)");
  auto unicode=take(metacode::parse_isa(R"(arch strings { x = "\u0000\b\f\/\uD83D\uDE00"; })"));
  CHECK(unicode.fields.at("x").text()==std::string("\0\b\f/\xf0\x9f\x98\x80",8));
  auto unicode_copy=take(metacode::parse_isa("arch strings { x = "+metacode::Value(unicode.fields).text()+"; }"));
  CHECK(std::get<metacode::Value::Object>(unicode_copy.fields.at("x").data).at("x").text()==unicode.fields.at("x").text());
  auto wide=take(metacode::parse_isa("arch wide { maximum=18446744073709551615; minimum=-9223372036854775808; }"));
  CHECK(std::get<uint64_t>(wide.fields.at("maximum").data)==UINT64_MAX);
  CHECK(std::get<int64_t>(wide.fields.at("minimum").data)==INT64_MIN);
  auto invalid_escape=metacode::parse_isa("arch strings {\n x=\"\\q\";\n}","escape.isa");
  fails(invalid_escape,Error::Code::Parse);CHECK(invalid_escape.error().message.starts_with("escape.isa:2:"));
  for(auto invalid:{R"(arch strings { x="\uD800"; })",R"(arch strings { x="\uDC00"; })",R"(arch strings { x="\uZZZZ"; })","arch strings { x=\"line\nbreak\"; }","arch strings { x=\"\xff\"; }","arch wide { x=-9223372036854775809; }"})
    fails(metacode::parse_isa(invalid),Error::Code::Parse);
  fails(metacode::parse_isa(std::string("arch a {}")+'\0'+"arch b {}"),Error::Code::Parse);
  auto roundtrip=take(metacode::parse_isa("arch a { x = "+metacode::Value(a.fields).text()+"; }"));
  CHECK(std::get<metacode::Value::Object>(roundtrip.fields.at("x").data).at("word_size").text()=="64");
  for(auto bad:{"", "arch a { x = 1; x = 2; }", "arch a {} arch b {}", "arch a {} regclass G { r0(0)=0, }", "arch a {} op x {} op x {}", "arch a { profile = 3; }", "arch a { x = 18446744073709551616; }", "arch a { x = [1,; }", "arch a { x = \"unterminated; }"})
    fails(metacode::parse_isa(bad),Error::Code::Parse);
  CHECK(argc==2);
  std::vector<std::filesystem::path> corpus;
  for(auto& entry:std::filesystem::directory_iterator(argv[1]))if(entry.path().extension()==".isa"&&entry.path().filename().string().front()!='.')corpus.push_back(entry.path());
  std::sort(corpus.begin(),corpus.end());size_t loaded=0;
  for(auto& path:corpus) {
    if(path.filename()=="dsl.lua.isa"){fails(metacode::load_isa_file(path.string()),Error::Code::Parse);continue;}
    auto isa=take(metacode::load_isa_file(path.string()));
    CHECK(!isa.name.empty()&&!isa.operations.empty());
    const auto& profile=std::get<metacode::Value::Object>(isa.fields.at("profile").data);
    CHECK(std::stoull(profile.at("instruction_count").text())==isa.operations.size());
    const auto& global=std::get<metacode::Value::Object>(isa.fields.at("tooling").data);
    CHECK(std::get<metacode::Value::Object>(global.at("bin2bin").data).at("schema_version").text()=="1");
    for(auto& op:isa.operations){CHECK(!op.semantics.empty());CHECK(std::get<metacode::Value::Object>(op.fields.at("tooling").data).contains("binary_translation"));}
    ++loaded;
  }
  CHECK(loaded==23);
});}
