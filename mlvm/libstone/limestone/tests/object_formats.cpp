#include "test.hpp"
#include "bin2bin/object.hpp"
#include <fstream>

using namespace limestone;
using namespace limestone::bin2bin;

static uint64_t read_word(std::span<const uint8_t> bytes,size_t offset,size_t width,ByteOrder order) {
  uint64_t value=0;
  for(size_t index=0;index<width;++index)value|=uint64_t(bytes[offset+index])<<(8*(order==ByteOrder::Little?index:width-index-1));
  return value;
}
static void write_word(std::vector<uint8_t>& bytes,size_t offset,size_t width,uint64_t value,ByteOrder order) {
  for(size_t index=0;index<width;++index)bytes.at(offset+index)=uint8_t(value>>(8*(order==ByteOrder::Little?index:width-index-1)));
}

int main(int argc,char** argv) {return test_main([&] {
  CHECK(argc==2||argc==4);
  auto target=take(object_target(take(metacode::load_isa_file(std::string(argv[1])+"/object-i386.isa"))));
  CHECK(target.format.elf_class==ElfClass::Elf32&&target.relocations.at(1).implicit_addend_signed==true);
  for(auto elf_class:{ElfClass::Elf32,ElfClass::Elf64})for(auto order:{ByteOrder::Little,ByteOrder::Big}) {
    auto contract=target;contract.format.elf_class=elf_class;contract.format.byte_order=order;
    auto file=take(code_object(contract,std::vector<uint8_t>(12),"entry"));
    file.symbols.push_back({"external"});
    file.sections.push_back({".bss",8,3,4,0,{},4});
    write_word(file.sections[0].bytes,0,4,uint32_t(-4),order);
    write_word(file.sections[0].bytes,4,4,7,order);
    file.relocations={{0,1,2,0,0,true},{0,1,1,4,0,true},{0,1,1,8,-2},{1,1,1,0,0,true}};
    auto encoded=take(emit_elf(file));auto decoded=take(load_elf(encoded,"mixed.o"));
    CHECK(decoded.format==contract.format&&decoded.relocations.size()==4);
    CHECK(take(emit_elf(decoded))==encoded&&decoded.sections[0].bytes==file.sections[0].bytes);
    CHECK(std::count_if(decoded.relocations.begin(),decoded.relocations.end(),[](auto& relocation){return relocation.implicit_addend;})==3);
    CHECK(print_object(decoded).find("addend implicit")!=std::string::npos);
    LinkOptions options{4096,1024,{{"external",4128}}};
    auto linked=take(link_objects(std::span{&decoded,1},contract,options));
    CHECK(read_word(linked.bytes,0,4,order)==28);
    CHECK(read_word(linked.bytes,4,4,order)==4135);
    CHECK(read_word(linked.bytes,8,4,order)==4126);
    CHECK(read_word(linked.bytes,12,4,order)==4128);
    CHECK(take(emit_elf(decoded))==encoded);
    auto ordered=file;ordered.relocations={{0,1,1,8,0,true},{0,1,1,0,0,true},{0,1,1,4,0,true}};
    auto preserved=take(load_elf(take(emit_elf(ordered))));
    CHECK(preserved.relocations[0].offset==8&&preserved.relocations[1].offset==0&&preserved.relocations[2].offset==4);
    auto unknown=file;unknown.relocations[0].type=77;
    auto retained=take(load_elf(take(emit_elf(unknown))));
    CHECK(std::any_of(retained.relocations.begin(),retained.relocations.end(),[](auto& relocation){return relocation.type==77&&relocation.implicit_addend;}));
    fails(link_objects(std::span{&retained,1},contract,options),Error::Code::Unsupported);
    auto unsupported=contract;unsupported.relocations[1].implicit_addend_signed.reset();
    fails(link_objects(std::span{&decoded,1},unsupported,options),Error::Code::Unsupported);
    auto mismatch=contract;mismatch.format.elf_class=elf_class==ElfClass::Elf32?ElfClass::Elf64:ElfClass::Elf32;
    fails(link_objects(std::span{&decoded,1},mismatch,options),Error::Code::Conflict);
    auto invalid=file;invalid.relocations[0].addend=1;fails(validate(invalid),Error::Code::InvalidArgument);
    invalid=file;invalid.relocations[0].offset=11;fails(link_objects(std::span{&invalid,1},contract,options),Error::Code::InvalidArgument);
    invalid=file;invalid.relocations.push_back(file.relocations[0]);fails(link_objects(std::span{&invalid,1},contract,options),Error::Code::Conflict);
    auto header_size=elf_class==ElfClass::Elf32?52u:64u;
    for(size_t length=0;length<header_size;++length)fails(load_elf(std::span{encoded}.first(length)),Error::Code::Parse);
    fails(load_elf(std::span{encoded}.first(encoded.size()-1)),Error::Code::Parse);
    auto width=elf_class==ElfClass::Elf32?4u:8u;
    auto table=read_word(encoded,24+2*width,width,order);
    auto section_size=elf_class==ElfClass::Elf32?40u:64u;
    auto count=read_word(encoded,36+3*width,2,order);
    for(size_t index=1;index<count;++index) {
      auto section=table+index*section_size;auto type=read_word(encoded,section+4,4,order);
      if(type!=2&&type!=4&&type!=9)continue;
      auto malformed=encoded;write_word(malformed,section+16+5*width,width,1,order);
      fails(load_elf(malformed),Error::Code::Parse);
      if(type==4||type==9) {
        malformed=encoded;write_word(malformed,section+8+4*width,4,0,order);fails(load_elf(malformed),Error::Code::Parse);
        malformed=encoded;auto offset=read_word(encoded,section+8+2*width,width,order);
        write_word(malformed,offset+width,width,1,order);fails(load_elf(malformed),Error::Code::Parse);
      }
    }
    for(int64_t endpoint:{int64_t(INT32_MIN),int64_t(INT32_MAX)}) {
      auto boundary=file;boundary.relocations={{0,1,2,0,endpoint}};
      CHECK(take(load_elf(take(emit_elf(boundary)))).relocations[0].addend==endpoint);
    }
  }
  auto narrow=take(code_object(target,std::vector<uint8_t>(4),"entry"));
  narrow.symbols.push_back({"absolute",object_absolute,UINT32_MAX});narrow.relocations={{0,1,1,0,INT32_MAX}};
  CHECK(take(load_elf(take(emit_elf(narrow)))).symbols[1].value==UINT32_MAX);
  for(auto addend:{int64_t(INT32_MIN)-1,int64_t(INT32_MAX)+1}) {auto invalid=narrow;invalid.relocations[0].addend=addend;fails(emit_elf(invalid),Error::Code::InvalidArgument);}
  auto invalid=narrow;invalid.symbols[1].value=uint64_t(UINT32_MAX)+1;fails(validate(invalid),Error::Code::InvalidArgument);
  invalid=narrow;invalid.sections[0].flags=uint64_t{1}<<32;fails(validate(invalid),Error::Code::InvalidArgument);
  invalid=narrow;invalid.sections[0].alignment=uint64_t{1}<<32;fails(validate(invalid),Error::Code::InvalidArgument);
  invalid=narrow;invalid.relocations[0].type=256;fails(validate(invalid),Error::Code::InvalidArgument);
  auto invalid_target=target;invalid_target.format.elf_class=static_cast<ElfClass>(3);fails(validate(invalid_target),Error::Code::InvalidArgument);
  invalid_target=target;invalid_target.text_alignment=uint64_t{1}<<32;fails(validate(invalid_target),Error::Code::InvalidArgument);
  invalid_target=target;invalid_target.relocations[256]={256,"large",RelocationKind::Absolute,4,0,32};fails(validate(invalid_target),Error::Code::InvalidArgument);

  for(auto order:{ByteOrder::Little,ByteOrder::Big}) {
    auto fields=target;fields.format.byte_order=order;
    fields.relocations[3]={3,"scaled",RelocationKind::PCRelative,4,4,8,4,true,0,true};
    fields.relocations[4]={4,"unsigned",RelocationKind::Absolute,4,16,8,1,false,0,false};
    auto file=take(code_object(fields,std::vector<uint8_t>(4),"entry"));
    write_word(file.sections[0].bytes,0,4,0xaa020fea,order);
    file.symbols.push_back({"near",object_absolute,4100});file.symbols.push_back({"small",object_absolute,3});
    file.relocations={{0,1,3,0,0,true},{0,2,4,0,0,true}};
    auto linked=take(link_objects(std::span{&file,1},fields,{4096}));
    CHECK(read_word(linked.bytes,0,4,order)==0xaa050ffa);
    CHECK(read_word(file.sections[0].bytes,0,4,order)==0xaa020fea);
  }
  auto wide=target;wide.format.elf_class=ElfClass::Elf64;
  wide.relocations[3]={3,"full",RelocationKind::Absolute,8,0,64,1,false,0,false};
  auto file=take(code_object(wide,std::vector<uint8_t>(8,255),"entry"));
  file.symbols.push_back({"zero",object_absolute,0});file.relocations={{0,1,3,0,0,true}};
  CHECK(read_word(take(link_objects(std::span{&file,1},wide)).bytes,0,8,ByteOrder::Little)==UINT64_MAX);
  wide.relocations[3].scale=2;fails(link_objects(std::span{&file,1},wide),Error::Code::Conflict);
  wide.relocations[3].scale=1;wide.relocations[3].signed_value=true;wide.relocations[3].implicit_addend_signed=true;
  for(auto value:{uint64_t{1}<<63,uint64_t(INT64_MAX),UINT64_MAX}) {
    write_word(file.sections[0].bytes,0,8,value,ByteOrder::Little);
    CHECK(read_word(take(link_objects(std::span{&file,1},wide)).bytes,0,8,ByteOrder::Little)==value);
  }
  auto malformed=take(metacode::load_isa_file(std::string(argv[1])+"/object-i386.isa"));
  auto& tooling=std::get<metacode::Value::Object>(malformed.fields.at("tooling").data);
  auto& contract=std::get<metacode::Value::Object>(tooling.at("object_file").data);
  auto& encodings=std::get<metacode::Value::Array>(contract.at("relocations").data);
  std::get<metacode::Value::Object>(encodings[0].data).at("implicit_addend_signed").data=int64_t{1};
  fails(object_target(malformed),Error::Code::InvalidArgument);

  if(argc==4) {
    std::ifstream input(argv[2],std::ios::binary);CHECK(input.good());std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
    auto native=take(load_elf(bytes,argv[2]));CHECK(native.format==target.format&&!native.relocations.empty());
    CHECK(std::all_of(native.relocations.begin(),native.relocations.end(),[](auto& relocation){return relocation.implicit_addend;}));
    auto linked=take(link_objects(std::span{&native,1},target,{4096,1024,{{"object32_helper",4160}}}));
    CHECK(linked.symbols.at("object32_entry")==4096&&read_word(linked.bytes,1,4,ByteOrder::Little)==59);
    CHECK(read_word(linked.bytes,linked.symbols.at("object32_pointer")-4096,4,ByteOrder::Little)==4099);
    auto emitted=take(emit_elf(native));std::ofstream output(argv[3],std::ios::binary);CHECK(output.good());
    output.write(reinterpret_cast<const char*>(emitted.data()),emitted.size());CHECK(output.good());
  }
});}
