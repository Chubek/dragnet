#include "object.hpp"
#include <charconv>
#include <set>

namespace limestone::bin2bin {
namespace {
void fail(Error::Code code,std::string message) { throw Error{code,std::move(message)}; }
std::string_view trim(std::string_view value) { while(!value.empty()&&value.back()==' ')value.remove_suffix(1);return value; }
uint64_t decimal(std::string_view text) {
  text=trim(text);uint64_t value=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(text.empty()||error!=std::errc{}||end!=text.data()+text.size())fail(Error::Code::Parse,"invalid archive decimal field");return value;
}
bool name_valid(std::string_view name) { return !name.empty()&&name.find_first_of("\n\r") == std::string_view::npos&&name.find('\0')==std::string_view::npos; }
bool index_name(std::string_view name) { return name=="/"||name=="//"||name=="/SYM64/"||name=="__.SYMDEF"||name=="__.SYMDEF SORTED"||name=="__.SYMDEF_64"||name=="__.SYMDEF_64 SORTED"; }
void append(std::vector<uint8_t>& out,std::string_view text) { out.insert(out.end(),text.begin(),text.end()); }
void header(std::vector<uint8_t>& out,std::string_view name,uint64_t size) {
  auto field=[&](std::string_view value,size_t width){if(value.size()>width)fail(Error::Code::ResourceLimit,"archive header field overflow");append(out,value);out.insert(out.end(),width-value.size(),' ');};
  field(name,16);field("0",12);field("0",6);field("0",6);field("100644",8);field(std::to_string(size),10);append(out,"`\n");
}
uint64_t record_size(uint64_t bytes) { return 60+bytes+(bytes&1); }
}
Result<ObjectArchive> load_archive(std::span<const uint8_t> bytes,std::string_view source,const ArchiveLimits& limits) {
  try {
    if(bytes.size()>limits.bytes)fail(Error::Code::ResourceLimit,"archive byte budget exceeded");
    std::string_view data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(data.starts_with("!<thin>\n"))fail(Error::Code::Unsupported,"thin archives require an external member resolver");
    if(!data.starts_with("!<arch>\n"))fail(Error::Code::Parse,"invalid archive magic");
    ObjectArchive result;result.source=source;std::string_view names;bool name_table=false;std::set<size_t> name_offsets;size_t position=8;
    while(position<data.size()) {
      if(data.size()-position<60)fail(Error::Code::Parse,"truncated archive header");auto h=data.substr(position,60);
      if(h.substr(58)!="`\n")fail(Error::Code::Parse,"invalid archive header terminator");
      auto name=trim(h.substr(0,16));auto size=decimal(h.substr(48,10));position+=60;
      if(size>data.size()-position)fail(Error::Code::Parse,"truncated archive member");auto payload=data.substr(position,size);position+=size;
      if(size&1){if(position==data.size()||data[position]!='\n')fail(Error::Code::Parse,"invalid archive alignment padding");++position;}
      if(name=="//") {
        if(name_table)fail(Error::Code::Conflict,"duplicate GNU archive name table");name_table=true;names=payload;size_t offset=0;
        while(offset<names.size()){name_offsets.insert(offset);auto end=names.find("/\n",offset);if(end==std::string_view::npos)fail(Error::Code::Parse,"unterminated GNU archive name");if(!name_valid(names.substr(offset,end-offset)))fail(Error::Code::Parse,"invalid GNU archive name");offset=end+2;}
        continue;
      }
      if(index_name(name))continue;
      std::string member_name;
      if(name.starts_with("#1/")){auto length=decimal(name.substr(3));if(length>payload.size())fail(Error::Code::Parse,"truncated BSD archive name");auto n=payload.substr(0,length);while(!n.empty()&&n.back()=='\0')n.remove_suffix(1);member_name=n;payload.remove_prefix(length);}
      else if(name.starts_with('/')){auto offset=decimal(name.substr(1));if(!name_offsets.contains(offset))fail(Error::Code::Parse,"invalid GNU archive name offset");auto end=names.find("/\n",offset);member_name=names.substr(offset,end-offset);}
      else {if(name.ends_with('/'))name.remove_suffix(1);member_name=name;}
      if(index_name(member_name))continue;
      if(!name_valid(member_name))fail(Error::Code::Parse,"invalid archive member name");
      if(result.members.size()>=limits.members)fail(Error::Code::ResourceLimit,"archive member budget exceeded");
      auto object=load_elf({reinterpret_cast<const uint8_t*>(payload.data()),payload.size()},std::string(source)+"("+member_name+")",limits.object);
      if(!object)return Result<ObjectArchive>::err(object.error());result.members.push_back({std::move(member_name),std::move(object.value())});
    }
    return Result<ObjectArchive>::ok(std::move(result));
  }catch(const Error& error){return Result<ObjectArchive>::err(error);}catch(const std::exception& error){return Result<ObjectArchive>::err({Error::Code::ResourceLimit,error.what()});}
}
Result<std::vector<uint8_t>> emit_archive(const ObjectArchive& archive,const ArchiveLimits& limits) {
  try {
    if(archive.members.size()>limits.members)fail(Error::Code::ResourceLimit,"archive member budget exceeded");
    std::vector<std::vector<uint8_t>> objects;std::vector<std::string> headers;std::string names;
    std::vector<std::pair<std::string,size_t>> symbols;uint64_t object_bytes=0,index_bytes=4;
    for(size_t k=0;k<archive.members.size();++k){auto& member=archive.members[k];if(!name_valid(member.name)||index_name(member.name))fail(Error::Code::InvalidArgument,"invalid or reserved archive member name");
      auto encoded=emit_elf(member.object,limits.object);if(!encoded)return Result<std::vector<uint8_t>>::err(encoded.error());auto bytes=record_size(encoded.value().size());if(bytes>limits.bytes||object_bytes>limits.bytes-bytes)fail(Error::Code::ResourceLimit,"archive byte budget exceeded");object_bytes+=bytes;objects.push_back(std::move(encoded.value()));
      if(member.name.size()>15||member.name.find_first_of(" /")!=std::string::npos||index_name(member.name)){if(member.name.size()>limits.bytes||limits.bytes-member.name.size()<2||names.size()>limits.bytes-member.name.size()-2)fail(Error::Code::ResourceLimit,"archive name budget exceeded");headers.push_back("/"+std::to_string(names.size()));names+=member.name+"/\n";}else headers.push_back(member.name+"/");
      for(auto& symbol:member.object.symbols)if(symbol.binding!=SymbolBinding::Local&&symbol.section!=object_undefined&&!symbol.name.empty()){auto growth=5+symbol.name.size();if(growth>limits.bytes||index_bytes>limits.bytes-growth)fail(Error::Code::ResourceLimit,"archive symbol index budget exceeded");index_bytes+=growth;symbols.emplace_back(symbol.name,k);}
    }
    uint64_t total=8+record_size(index_bytes)+(names.empty()?0:record_size(names.size()));
    if(total>limits.bytes||object_bytes>limits.bytes-total||total+object_bytes>UINT32_MAX||symbols.size()>UINT32_MAX)fail(Error::Code::ResourceLimit,"archive byte budget exceeded");total+=object_bytes;
    std::vector<uint32_t> offsets;uint64_t position=8+record_size(index_bytes)+(names.empty()?0:record_size(names.size()));for(auto& object:objects){offsets.push_back(uint32_t(position));position+=record_size(object.size());}
    std::vector<uint8_t> out;out.reserve(total);append(out,"!<arch>\n");header(out,"/",index_bytes);
    auto word=[&](uint32_t value){for(int shift=24;shift>=0;shift-=8)out.push_back(uint8_t(value>>shift));};word(uint32_t(symbols.size()));for(auto& [name,member]:symbols)word(offsets[member]);for(auto& [name,member]:symbols){append(out,name);out.push_back(0);}if(index_bytes&1)out.push_back('\n');
    if(!names.empty()){header(out,"//",names.size());append(out,names);if(names.size()&1)out.push_back('\n');}
    for(size_t k=0;k<objects.size();++k){header(out,headers[k],objects[k].size());out.insert(out.end(),objects[k].begin(),objects[k].end());if(objects[k].size()&1)out.push_back('\n');}
    return Result<std::vector<uint8_t>>::ok(std::move(out));
  }catch(const Error& error){return Result<std::vector<uint8_t>>::err(error);}catch(const std::exception& error){return Result<std::vector<uint8_t>>::err({Error::Code::ResourceLimit,error.what()});}
}
Result<LinkedImage> link_archives(std::span<const ObjectFile> roots,std::span<const ObjectArchive> archives,const ObjectTarget& target,const LinkOptions& options) {
  std::vector<const ObjectFile*> objects;std::set<std::string> defined,required;
  for(auto& [name,address]:options.externals)defined.insert(name);
  auto add=[&](const ObjectFile& object){objects.push_back(&object);for(auto& symbol:object.symbols)if(symbol.binding!=SymbolBinding::Local){if(symbol.section!=object_undefined)defined.insert(symbol.name);else if(symbol.binding!=SymbolBinding::Weak)required.insert(symbol.name);}};
  for(auto& object:roots)add(object);std::set<std::pair<size_t,size_t>> selected;bool changed=true;
  while(changed){changed=false;for(size_t a=0;a<archives.size();++a)for(size_t m=0;m<archives[a].members.size();++m){if(selected.contains({a,m}))continue;auto& object=archives[a].members[m].object;
    if(std::any_of(object.symbols.begin(),object.symbols.end(),[&](auto& symbol){return symbol.binding!=SymbolBinding::Local&&symbol.section!=object_undefined&&required.contains(symbol.name)&&!defined.contains(symbol.name);})){selected.emplace(a,m);add(object);changed=true;}
  }}
  return link_objects(objects,target,options);
}
}
