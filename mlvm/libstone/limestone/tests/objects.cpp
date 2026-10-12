#include "test.hpp"
#include "bin2bin/object.hpp"
#include "limestone/limestone.hpp"
#include <cstring>
#include <fstream>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#endif
using namespace limestone;
using namespace limestone::bin2bin;
static std::vector<uint8_t> read(const std::string& name) {std::ifstream f(name,std::ios::binary);CHECK(f.good());return {(std::istreambuf_iterator<char>(f)),{}};}
static void write(const std::string& name,std::span<const uint8_t> bytes) {std::ofstream f(name,std::ios::binary);CHECK(f.good());f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(f.good());}
static uint64_t get(std::span<const uint8_t> bytes,size_t offset,size_t size,ByteOrder order=ByteOrder::Little) {uint64_t n=0;for(size_t k=0;k<size;++k)n|=uint64_t(bytes[offset+k])<<(8*(order==ByteOrder::Little?k:size-1-k));return n;}
static void put(std::vector<uint8_t>& bytes,size_t offset,uint64_t n,size_t size) {for(size_t k=0;k<size;++k)bytes.at(offset+k)=uint8_t(n>>(8*k));}
static ObjectFile object(const ObjectTarget& target,std::string name="answer") {return take(code_object(target,std::vector<uint8_t>{0xb8,42,0,0,0,0xc3},name));}
int main(int argc,char** argv){return test_main([&]{
  CHECK(argc==3||argc==5);const std::string fixtures=argv[1],output=argv[2];auto target=take(object_target(take(metacode::load_isa_file(fixtures+"/object-x86.isa"))));
  for(auto elf_class:{ElfClass::Elf32,ElfClass::Elf64})for(auto order:{ByteOrder::Little,ByteOrder::Big}) {
    auto t=target;t.format.byte_order=order;t.format.elf_class=elf_class;auto f=object(t);f.sections.push_back({".bss",8,3,32,0,{},48});f.sections.push_back({".data",1,3,8,0,std::vector<uint8_t>(16)});
    f.symbols.push_back({"data",2,0,16,SymbolBinding::Global,1});f.symbols.push_back({"local",0,0,1,SymbolBinding::Local,2});f.symbols.push_back({"absolute",object_absolute,123,0,SymbolBinding::Global});f.symbols.push_back({"weak",object_undefined,0,0,SymbolBinding::Weak});
    f.relocations={{2,0,1,0,1},{2,4,1,8,0}};
    auto elf=take(emit_elf(f));auto copied=take(load_elf(elf,"fixture.o"));CHECK(take(emit_elf(copied))==elf);CHECK(copied.sections[1].zero_fill==48&&copied.symbols.size()==5&&copied.relocations.size()==2);
    auto linked=take(link_objects(std::span{&copied,1},t,{0x1003}));CHECK(linked.symbols.at("answer")==0x1010&&linked.symbols.at("absolute")==123&&linked.sections[1].address==0x1020);
    auto data=linked.symbols.at("data")-linked.base_address;CHECK(get(linked.bytes,data,8,order)==0x1011&&get(linked.bytes,data+8,8,order)==0);
    CHECK(std::all_of(linked.bytes.begin()+linked.sections[1].offset,linked.bytes.begin()+linked.sections[1].offset+48,[](auto n){return n==0;}));
    CHECK(f.sections[2].bytes==std::vector<uint8_t>(16)); // Linking is transactional.
  }
  auto file=object(target);file.sections.push_back({".data",1,3,8,0,std::vector<uint8_t>(16)});file.symbols.push_back({"external"});file.relocations={{1,1,1,0,-1},{1,0,2,8,-4}};
  ObjectArchive archive{{{"long archive member name.o",object(target,"external")},{"unused.o",object(target,"unused")}},"fixture.a"};auto ar=take(emit_archive(archive));auto archive_copy=take(load_archive(ar));CHECK(archive_copy.members.size()==2&&archive_copy.members[0].name==archive.members[0].name&&take(emit_archive(archive_copy))==ar);
  auto extracted=take(link_archives(std::span{&file,1},std::span{&archive_copy,1},target,{4096}));CHECK(extracted.symbols.contains("external")&&!extracted.symbols.contains("unused"));
  auto ar_bad=ar;ar_bad[66]='x';fails(load_archive(ar_bad),Error::Code::Parse);ar_bad=ar;ar_bad.pop_back();fails(load_archive(ar_bad),Error::Code::Parse);
  ArchiveLimits ar_limit;ar_limit.members=1;fails(load_archive(ar,"bounded.a",ar_limit),Error::Code::ResourceLimit);ar_limit={};ar_limit.bytes=ar.size()-1;fails(emit_archive(archive,ar_limit),Error::Code::ResourceLimit);
  fails(link_objects(std::span{&file,1},target),Error::Code::NotFound);LinkOptions options{0x1000,1024,{{"external",UINT64_MAX}}};auto image=take(link_objects(std::span{&file,1},target,options));
  CHECK(get(image.bytes,8,8)==UINT64_MAX-1&&get(image.bytes,16,4)==uint32_t(-20));
  auto elf=take(emit_elf(file));write(output+"/object-fixture.o",elf);auto copied=take(load_elf(elf));CHECK(take(emit_elf(copied))==elf);
  auto bad_external=options;bad_external.externals[std::string("invalid\0name",12)]=1;fails(link_objects(std::span{&file,1},target,bad_external),Error::Code::InvalidArgument);
  auto unknown=file;unknown.relocations[0].type=999;CHECK(take(load_elf(take(emit_elf(unknown)))).relocations[0].type==999);fails(link_objects(std::span{&unknown,1},target,options),Error::Code::Unsupported);
  auto overlap=file;overlap.relocations[1]=overlap.relocations[0];fails(link_objects(std::span{&overlap,1},target,options),Error::Code::Conflict);
  auto partial=file;partial.relocations[1].offset=14;fails(link_objects(std::span{&partial,1},target,options),Error::Code::InvalidArgument);
  auto too_large=file;too_large.relocations[0].type=10;fails(link_objects(std::span{&too_large,1},target,options),Error::Code::Conflict);
  auto incompatible=file;incompatible.format.flags=1;fails(link_objects(std::span{&incompatible,1},target,options),Error::Code::Conflict);
  auto tls=file;tls.symbols[0].type=6;fails(link_objects(std::span{&tls,1},target,options),Error::Code::Unsupported);
  auto compressed=file;compressed.sections[0].flags|=2048;fails(link_objects(std::span{&compressed,1},target,options),Error::Code::Unsupported);
  auto outside=file;outside.symbols[0].section=object_undefined;outside.symbols[0].value=0;outside.symbols[0].size=0;outside.symbols[0].visibility=2;fails(link_objects(std::span{&outside,1},target,options),Error::Code::Conflict);
  auto limited=options;limited.max_size=4;fails(link_objects(std::span{&file,1},target,limited),Error::Code::ResourceLimit);limited=options;limited.base_address=UINT64_MAX-4;fails(link_objects(std::span{&file,1},target,limited),Error::Code::ResourceLimit);
  ObjectLimits bound;bound.bytes=64;fails(emit_elf(file,bound),Error::Code::ResourceLimit);fails(load_elf(elf,"bounded.o",bound),Error::Code::ResourceLimit);bound={};bound.symbols=1;fails(load_elf(elf,"bounded.o",bound),Error::Code::ResourceLimit);
  auto huge=file;huge.sections[1].type=8;huge.sections[1].bytes.clear();huge.sections[1].zero_fill=UINT64_MAX;fails(emit_elf(huge),Error::Code::ResourceLimit);
  // Weak definitions lose to a strong one; locals are object-scoped.
  auto weak=object(target,"shared"),strong=object(target,"shared");weak.symbols[0].binding=SymbolBinding::Weak;std::vector<ObjectFile> files{weak,strong};
  CHECK(take(link_objects(files,target,{4096})).symbols.at("shared")==4112);files={strong,strong};fails(link_objects(files,target),Error::Code::Conflict);
  strong.symbols[0].binding=SymbolBinding::Local;strong.relocations={{0,0,10,1,0}};files={strong,strong};auto locals=take(link_objects(files,target,{4096}));CHECK(locals.symbols.empty()&&get(locals.bytes,1,4)==4096&&get(locals.bytes,17,4)==4112);
  // Explicit scaled bitfields preserve opcode bits and may share storage when
  // their bit ranges are disjoint. Negative values and 64-bit endpoints matter.
  auto fields=target;fields.relocations[99]={99,"scaled",RelocationKind::PCRelative,4,4,8,4,true,0};fields.relocations[98]={98,"upper",RelocationKind::Absolute,4,16,8,1,false,0};
  for(auto order:{ByteOrder::Little,ByteOrder::Big}) {
    fields.format.byte_order=order;auto f=take(code_object(fields,std::vector<uint8_t>{0xaa,0xaa,0xaa,0xaa},"base"));f.symbols.push_back({"near",object_absolute,4092});f.symbols.push_back({"upper",object_absolute,0x42});f.relocations={{0,1,99,0,0},{0,2,98,0,0}};
    auto linked=take(link_objects(std::span{&f,1},fields,{4096}));CHECK(get(linked.bytes,0,4,order)==((0xaaaaaaaaULL&~0x00ff0ff0ULL)|0x00420ff0ULL));
    f.symbols[1].value=4091;fails(link_objects(std::span{&f,1},fields,{4096}),Error::Code::Conflict);f.symbols[1].value=4096+512;fails(link_objects(std::span{&f,1},fields,{4096}),Error::Code::Conflict);
  }
  auto signed64=target;signed64.relocations[100]={100,"signed64",RelocationKind::Absolute,8,0,64,1,true};auto end=take(code_object(signed64,std::vector<uint8_t>(8),"end"));end.symbols.push_back({"zero",object_absolute,0});end.relocations={{0,1,100,0,INT64_MIN}};CHECK(get(take(link_objects(std::span{&end,1},signed64)).bytes,0,8)==(uint64_t{1}<<63));end.relocations[0].addend=INT64_MAX;CHECK(get(take(link_objects(std::span{&end,1},signed64)).bytes,0,8)==INT64_MAX);
  // Malformed container fields fail before interpreting payloads.
  for(size_t size:{size_t{0},size_t{16},size_t{63},elf.size()-1})fails(load_elf(std::span{elf}.first(size)),Error::Code::Parse);
  auto bad=elf;bad[4]=3;fails(load_elf(bad),Error::Code::Unsupported);bad=elf;bad[5]=0;fails(load_elf(bad),Error::Code::Parse);bad=elf;put(bad,16,2,2);fails(load_elf(bad),Error::Code::Unsupported);
  auto table=get(elf,40,8);bad=elf;put(bad,table+64+24,UINT64_MAX,8);fails(load_elf(bad),Error::Code::Parse);
  bad=elf;put(bad,table+64+24,64,8);put(bad,table+64+32,elf.size(),8);fails(load_elf(bad),Error::Code::Parse);
  bad=elf;put(bad,table+64+4,6,4);fails(load_elf(bad),Error::Code::Unsupported);
  bad=elf;put(bad,table+64+48,3,8);fails(load_elf(bad),Error::Code::Parse);
  bad=elf;put(bad,table+3*64,UINT32_MAX,4);fails(load_elf(bad),Error::Code::Parse);
  bad=elf;bad[9]=1;fails(load_elf(bad),Error::Code::Unsupported);
  // Pipeline encoding can be wrapped, retained after module destruction, loaded,
  // linked at a chosen base and decoded using the original authoritative codec.
  auto metadata=take(metacode::load_isa_file(fixtures+"/backend-machine.isa"));unisel::Program graph{{{1,"const",{},42,"i64"},{2,"return",{1},{},"",0,true,false,false}}, {}};graph.nodes[1].control=schedrow::ControlFlow::Return;
  PipelineOptions compile;compile.allocate=compile.encode=true;auto module=take(run_pipeline(graph,take(make_target(metadata)),compile));auto pipeline_target=take(object_target(metadata));auto pipeline_file=take(make_object(module,pipeline_target,"pipeline"));CHECK(take(load_elf(take(emit_elf(pipeline_file)))).sections[0].bytes==module.encoded->bytes);
  fails(make_object(module,target,"mismatched"),Error::Code::Conflict);
  auto unencoded=module;unencoded.encoded.reset();fails(make_object(unencoded,pipeline_target,"pipeline"),Error::Code::InvalidArgument);
  auto named=module;named.encoded->relocations={{1,"abs32","import",0}};auto named_target=target;named_target.architecture=module.target;fails(make_object(named,named_target,"pipeline"),Error::Code::InvalidArgument);
  fails(code_object(target,std::vector<uint8_t>{1},"",{}),Error::Code::InvalidArgument);
  fails(object_target(take(metacode::parse_isa("arch x {}"))),Error::Code::Unsupported);
  auto invalid_target=target;invalid_target.relocations[1].bits=65;fails(validate(invalid_target),Error::Code::InvalidArgument);
  auto opaque=object(target);opaque.sections.push_back({".processor",0x70000005,2,4,0,std::vector<uint8_t>{42}});
  CHECK(take(load_elf(take(emit_elf(opaque)))).sections[1].type==0x70000005);fails(link_objects(std::span{&opaque,1},target),Error::Code::Unsupported);
  auto opaque_target=target;opaque_target.opaque_section_types.push_back(0x70000005);CHECK(take(link_objects(std::span{&opaque,1},opaque_target)).bytes.back()==42);
  auto prefix=read(fixtures+"/object-x86.isa");std::string isa(prefix.begin(),prefix.end());
  for(auto [from,to,code]:std::vector<std::tuple<std::string,std::string,Error::Code>>{{"machine=62","machine=0",Error::Code::InvalidArgument},{"text_alignment=16","text_alignment=3",Error::Code::InvalidArgument},{"format=elf64","format=elf128",Error::Code::Unsupported},{"scale=1","scale=0",Error::Code::InvalidArgument},{"bits=64","bits=65",Error::Code::InvalidArgument},{"type=2","type=1",Error::Code::Conflict},{"kind=absolute","kind=got",Error::Code::Unsupported},{"flags=0","flags=0;invented=1",Error::Code::Unsupported}}) {
    auto text=isa;auto at=text.find(from);CHECK(at!=text.npos);text.replace(at,from.size(),to);auto malformed=object_target(take(metacode::parse_isa(text,"contract.isa")));fails(malformed,code);CHECK(malformed.error().message.find("contract.isa")!=std::string::npos);
  }
#if defined(__linux__) && defined(__x86_64__)
  if(argc==5) {
    std::vector<ObjectFile> native{take(load_elf(read(argv[3]),argv[3])),take(load_elf(read(argv[4]),argv[4]))};CHECK(!native[0].relocations.empty());
    ObjectArchive native_archive{{{"native helper with spaces.o",native[1]}}};auto archive_bytes=take(emit_archive(native_archive));write(output+"/native-reemitted.a",archive_bytes);native_archive=take(load_archive(archive_bytes));
    void* memory=mmap(nullptr,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(memory!=MAP_FAILED);
    auto linked=take(link_archives(std::span{native}.first(1),std::span{&native_archive,1},target,{uint64_t(reinterpret_cast<uintptr_t>(memory))+16,65520}));std::memcpy(static_cast<uint8_t*>(memory)+16,linked.bytes.data(),linked.bytes.size());CHECK(mprotect(memory,65536,PROT_READ|PROT_EXEC)==0);
    auto function=reinterpret_cast<int(*)()>(static_cast<uintptr_t>(linked.symbols.at("limestone_object_native")));CHECK(function()==42);CHECK(munmap(memory,65536)==0);
    auto native_bytes=take(emit_elf(native[0]));write(output+"/native-reemitted.o",native_bytes);CHECK(take(load_elf(native_bytes)).relocations.size()==native[0].relocations.size());
  }
#endif
});}
