#include "test.hpp"
#include "limestone/limestone.hpp"
#include "bin2bin/decompiler.hpp"
#include <bit>
#include <charconv>
#include <cstring>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#endif

int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;using namespace bin2bin;CHECK(argc==2);
  auto metadata=take(metacode::load_isa_file(std::string(argv[1])+"/native-adapter.isa"));
  CodecAdapter adapter;adapter.identity="fixture:x86-movabs:1";
  adapter.decode_one=[](std::span<const uint8_t> input,uint64_t)->Result<DecodedOperands>{
    if(input.empty())return Result<DecodedOperands>::err({Error::Code::Parse,"empty native instruction"});
    if(input[0]==0xc3)return Result<DecodedOperands>::ok({1,{}});
    if(input.size()<10)return Result<DecodedOperands>::err({Error::Code::Parse,"truncated movabs"});
    if(input[0]!=0x48||input[1]!=0xb8)return Result<DecodedOperands>::err({Error::Code::Unsupported,"unknown native prefix/opcode"});
    uint64_t value=0;for(unsigned k=0;k<8;++k)value|=uint64_t(input[2+k])<<(k*8);return Result<DecodedOperands>::ok({0,{{"imm",std::to_string(std::bit_cast<int64_t>(value))}}});
  };
  adapter.encode_form=[](uint32_t form,const auto& operands,uint64_t)->Result<std::vector<uint8_t>>{
    if(form==1)return Result<std::vector<uint8_t>>::ok({0xc3});
    auto& text=operands.at("imm");int64_t value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);CHECK(error==std::errc{}&&end==text.data()+text.size());
    auto bits=std::bit_cast<uint64_t>(value);std::vector<uint8_t> bytes{0x48,0xb8};for(unsigned k=0;k<8;++k)bytes.push_back(uint8_t(bits>>(k*8)));return Result<std::vector<uint8_t>>::ok(std::move(bytes));
  };
  auto codec=take(from_metacode(metadata,adapter));auto target=take(make_target(metadata,adapter));auto bytes=take(encode(codec,"CONST",{{"imm","42"}}));bytes.push_back(0xc3);
  CHECK(bytes.size()==11&&take(decode(codec,bytes))[0].bytes.size()==10&&take(lift(codec,bytes))[0].semantics=="(set rax 42)");
  CHECK(take(translate(codec,codec,bytes))==bytes);TranslationCache cache;take(translate(codec,codec,bytes,&cache));CHECK(cache.entries.size()==1);
  auto changed=codec;auto changed_adapter=std::make_shared<CodecAdapter>(*codec.codec);changed_adapter->identity+="updated";changed.codec=changed_adapter;take(translate(codec,changed,bytes,&cache));CHECK(cache.entries.size()==2);changed_adapter->cacheable=false;take(translate(codec,changed,bytes,&cache));CHECK(cache.entries.size()==2);
  auto mismatched=adapter;mismatched.identity="wrong";fails(from_metacode(metadata,mismatched),Error::Code::Conflict);
  auto malformed=codec;auto bad=std::make_shared<CodecAdapter>(adapter);bad->encode_form=[](auto,const auto&,auto){return Result<std::vector<uint8_t>>::ok({0xc3});};malformed.codec=bad;fails(encode(malformed,"CONST",{{"imm","42"}}),Error::Code::Conflict);
  bad->encode_form=adapter.encode_form;bad->decode_one=[](auto,auto){return Result<DecodedOperands>::ok({99,{}});};fails(decode(malformed,bytes),Error::Code::Conflict);
  bad->decode_one=[](auto,auto)->Result<DecodedOperands>{throw std::runtime_error("failed native decoder");};fails(decode(malformed,bytes),Error::Code::Internal);
  fails(decode(codec,std::span(bytes).first(9)),Error::Code::Parse);fails(encode(codec,"CONST",{{"imm","42"}},UINT64_MAX-5),Error::Code::InvalidArgument);
  DecompilerPlugin plugin{"fixture:high-level:1","C",[](const DecompilationFacts& facts){CHECK(facts.cfg.instructions.size()==2&&facts.semantics[0].semantics=="(set rax 42)");return Result<DecompilerOutput>::ok({"C","long long entry(void) { return 42; }","deterministic fixture adapter"});}};
  auto output=take(decompile_with_plugins(codec,bytes,std::span{&plugin,1}));CHECK(output.facts.bytes==bytes&&output.interpretations[0].language=="C");
  auto untrusted=plugin;untrusted.identity="llm:fixture";untrusted.apply=[](const auto&){return Result<DecompilerOutput>::ok({"C","return 99;","supplemental LLM fixture: model/version"});};output=take(decompile_with_plugins(codec,bytes,std::span{&untrusted,1}));CHECK(output.facts.semantics[0].semantics=="(set rax 42)"&&output.interpretations[0].text=="return 99;");
  auto invalid=plugin;invalid.apply=[](const auto&){return Result<DecompilerOutput>::ok({"Rust","wrong language","host"});};fails(decompile_with_plugins(codec,bytes,std::span{&invalid,1}),Error::Code::Conflict);DecompilationLimits limit;limit.output_bytes=1;fails(decompile_with_plugins(codec,bytes,std::span{&plugin,1},0,limit),Error::Code::ResourceLimit);
  PipelineOptions options;options.allocate=options.encode=true;
  for(auto strategy:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS}){
    options.selector=strategy;auto module=take(run_pipeline("((lambda x (add x 2)) 40)",target,options));CHECK(module.encoded->bytes==bytes);
#if defined(__linux__) && defined(__x86_64__)
    auto memory=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(memory!=MAP_FAILED);std::memcpy(memory,bytes.data(),bytes.size());CHECK(mprotect(memory,4096,PROT_READ|PROT_EXEC)==0);auto entry=reinterpret_cast<int64_t(*)()>(memory);CHECK(entry()==42);CHECK(munmap(memory,4096)==0);
#endif
  }
});}
