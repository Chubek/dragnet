#include "object_internal.hpp"
#include "c_api_internal.hpp"

struct limestone_object_data { std::vector<uint8_t> bytes; };
struct limestone_linked_image { limestone::bin2bin::LinkedImage image; };
struct limestone_archive { limestone::bin2bin::ObjectArchive archive; };
namespace {
using namespace limestone;
using namespace limestone::c_api_internal;
void argument(bool valid) {if(!valid)throw Error{Error::Code::InvalidArgument,"invalid object API argument"};}
template<class T> const T& at(const std::vector<T>& items,size_t index) {
  if(index>=items.size())throw Error{Error::Code::NotFound,"object inspection index out of range"};return items[index];
}
template<class F> void change(limestone_object& object,F operation) {
  auto copy=object.file;operation(copy);checked(bin2bin::validate(copy));auto text=bin2bin::print_object(copy);object.file=std::move(copy);object.text=std::move(text);
}
}
extern "C" limestone_object_target* limestone_object_target_load_isa(const char* source,limestone_error* error) {
  return boundary(error,[&]()->limestone_object_target*{argument(source);return new limestone_object_target{checked(bin2bin::object_target(checked(metacode::parse_isa(source))))};});
}
extern "C" void limestone_object_target_destroy(limestone_object_target* target){delete target;}
extern "C" limestone_object* limestone_object_create(const limestone_object_target* target,limestone_error* error) {
  return boundary(error,[&]()->limestone_object*{argument(target);bin2bin::ObjectFile file;file.format=target->target.format;return new limestone_object(std::move(file));});
}
extern "C" limestone_object* limestone_object_load_elf(const uint8_t* bytes,size_t size,const char* source,limestone_error* error) {
  return boundary(error,[&]()->limestone_object*{argument(bytes||!size);return new limestone_object(checked(bin2bin::load_elf({bytes,size},source?source:"<elf>")));});
}
extern "C" limestone_object* limestone_object_from_code(const limestone_object_target* target,const uint8_t* bytes,size_t size,const char* symbol,limestone_error* error) {
  return boundary(error,[&]()->limestone_object*{argument(target&&symbol&&(bytes||!size));return new limestone_object(checked(bin2bin::code_object(target->target,{bytes,size},symbol)));});
}
extern "C" void limestone_object_destroy(limestone_object* object){delete object;}
extern "C" const char* limestone_object_text(const limestone_object* object){return object?object->text.c_str():nullptr;}
extern "C" uint32_t limestone_object_elf_class(const limestone_object* object){return object?(object->file.format.elf_class==bin2bin::ElfClass::Elf32?32:64):0;}
extern "C" size_t limestone_object_section_count(const limestone_object* object){return object?object->file.sections.size():0;}
extern "C" size_t limestone_object_symbol_count(const limestone_object* object){return object?object->file.symbols.size():0;}
extern "C" size_t limestone_object_relocation_count(const limestone_object* object){return object?object->file.relocations.size():0;}
extern "C" limestone_status limestone_object_get_section(const limestone_object* object,size_t index,limestone_object_section* out,limestone_error* error) {
  return boundary(error,[&](){argument(object&&out);auto& s=at(object->file.sections,index);*out={s.name.c_str(),s.type,s.flags,s.alignment,s.entry_size,s.bytes.empty()?nullptr:s.bytes.data(),s.bytes.size(),s.zero_fill};return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_get_symbol(const limestone_object* object,size_t index,limestone_object_symbol* out,limestone_error* error) {
  return boundary(error,[&](){argument(object&&out);auto& s=at(object->file.symbols,index);*out={s.name.c_str(),s.section,s.value,s.size,static_cast<limestone_symbol_binding>(s.binding),s.type,s.visibility};return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_get_relocation(const limestone_object* object,size_t index,limestone_object_relocation* out,limestone_error* error) {
  return boundary(error,[&](){argument(object&&out);auto& relocation=at(object->file.relocations,index);if(relocation.implicit_addend)throw Error{Error::Code::Unsupported,"REL inspection requires limestone_object_get_relocation_ex"};*out={relocation.section,relocation.symbol,relocation.type,relocation.offset,relocation.addend};return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_get_relocation_ex(const limestone_object* object,size_t index,limestone_object_relocation* out,uint32_t* implicit_addend,limestone_error* error) {
  return boundary(error,[&](){argument(object&&out&&implicit_addend);auto& relocation=at(object->file.relocations,index);*out={relocation.section,relocation.symbol,relocation.type,relocation.offset,relocation.addend};*implicit_addend=relocation.implicit_addend?1:0;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_add_section(limestone_object* object,const limestone_object_section* input,uint32_t* id,limestone_error* error) {
  return boundary(error,[&](){argument(object&&input&&id&&input->name&&(input->bytes||!input->byte_count));argument(input->byte_count<=64*1024*1024);
    bin2bin::ObjectSection s{input->name,input->type,input->flags,input->alignment,input->entry_size,{},input->zero_fill};if(input->byte_count)s.bytes.assign(input->bytes,input->bytes+input->byte_count);
    auto index=uint32_t(object->file.sections.size());change(*object,[&](auto& f){f.sections.push_back(std::move(s));});*id=index;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_add_symbol(limestone_object* object,const limestone_object_symbol* input,uint32_t* id,limestone_error* error) {
  return boundary(error,[&](){argument(object&&input&&id&&input->name);bin2bin::ObjectSymbol s{input->name,input->section,input->value,input->size,static_cast<bin2bin::SymbolBinding>(input->binding),input->type,input->visibility};
    auto index=uint32_t(object->file.symbols.size());change(*object,[&](auto& f){f.symbols.push_back(std::move(s));});*id=index;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_object_add_relocation(limestone_object* object,const limestone_object_relocation* input,limestone_error* error) {
  return limestone_object_add_relocation_ex(object,input,0,error);
}
extern "C" limestone_status limestone_object_add_relocation_ex(limestone_object* object,const limestone_object_relocation* input,uint32_t implicit_addend,limestone_error* error) {
  return boundary(error,[&](){argument(object&&input&&implicit_addend<=1);bin2bin::ObjectRelocation relocation{input->section,input->symbol,input->type,input->offset,input->addend,implicit_addend!=0};change(*object,[&](auto& file){file.relocations.push_back(relocation);});return LIMESTONE_OK;});
}
extern "C" limestone_object_data* limestone_object_emit_elf(const limestone_object* object,limestone_error* error) {
  return boundary(error,[&]()->limestone_object_data*{argument(object);return new limestone_object_data{checked(bin2bin::emit_elf(object->file))};});
}
extern "C" void limestone_object_data_destroy(limestone_object_data* data){delete data;}
extern "C" limestone_archive* limestone_archive_create(limestone_error* error){return boundary(error,[](){return new limestone_archive;});}
extern "C" limestone_archive* limestone_archive_load(const uint8_t* bytes,size_t size,const char* source,limestone_error* error){return boundary(error,[&](){argument(bytes||!size);return new limestone_archive{checked(bin2bin::load_archive({bytes,size},source?source:"<archive>"))};});}
extern "C" void limestone_archive_destroy(limestone_archive* archive){delete archive;}
extern "C" size_t limestone_archive_member_count(const limestone_archive* archive){return archive?archive->archive.members.size():0;}
extern "C" const char* limestone_archive_member_name(const limestone_archive* archive,size_t index){return archive&&index<archive->archive.members.size()?archive->archive.members[index].name.c_str():nullptr;}
extern "C" limestone_object* limestone_archive_get_member(const limestone_archive* archive,size_t index,limestone_error* error){return boundary(error,[&](){argument(archive);return new limestone_object(at(archive->archive.members,index).object);});}
extern "C" limestone_status limestone_archive_add_member(limestone_archive* archive,const char* name,const limestone_object* object,limestone_error* error){return boundary(error,[&](){argument(archive&&name&&object);auto copy=archive->archive;copy.members.push_back({name,object->file});checked(bin2bin::emit_archive(copy));archive->archive=std::move(copy);return LIMESTONE_OK;});}
extern "C" limestone_object_data* limestone_archive_emit(const limestone_archive* archive,limestone_error* error){return boundary(error,[&](){argument(archive);return new limestone_object_data{checked(bin2bin::emit_archive(archive->archive))};});}
extern "C" const uint8_t* limestone_object_data_bytes(const limestone_object_data* data){return data&&!data->bytes.empty()?data->bytes.data():nullptr;}
extern "C" size_t limestone_object_data_size(const limestone_object_data* data){return data?data->bytes.size():0;}
extern "C" limestone_linked_image* limestone_object_link(const limestone_object_target* target,const limestone_object* const* objects,size_t count,uint64_t base,uint64_t max_size,const limestone_external_symbol* externals,size_t external_count,limestone_error* error) {
  return boundary(error,[&]()->limestone_linked_image*{argument(target&&count&&objects&&(externals||!external_count));argument(count<=16384&&external_count<=1048576);std::vector<const bin2bin::ObjectFile*> files;files.reserve(count);for(size_t k=0;k<count;++k){argument(objects[k]);files.push_back(&objects[k]->file);}
    bin2bin::LinkOptions options;options.base_address=base;options.max_size=max_size;
    for(size_t k=0;k<external_count;++k){argument(externals[k].name&&*externals[k].name);if(!options.externals.emplace(externals[k].name,externals[k].address).second)throw Error{Error::Code::Conflict,"duplicate external symbol"};}
    return new limestone_linked_image{checked(bin2bin::link_objects(files,target->target,options))};});
}
extern "C" void limestone_linked_image_destroy(limestone_linked_image* image){delete image;}
extern "C" limestone_linked_image* limestone_archive_link(const limestone_object_target* target,const limestone_object* const* objects,size_t count,const limestone_archive* const* archives,size_t archive_count,uint64_t base,uint64_t max_size,const limestone_external_symbol* externals,size_t external_count,limestone_error* error) {
  return boundary(error,[&](){argument(target&&objects&&count&&(archives||!archive_count)&&(externals||!external_count));argument(count<=16384&&archive_count<=4096&&external_count<=1048576);std::vector<bin2bin::ObjectFile> roots;std::vector<bin2bin::ObjectArchive> libraries;
    for(size_t k=0;k<count;++k){argument(objects[k]);roots.push_back(objects[k]->file);}for(size_t k=0;k<archive_count;++k){argument(archives[k]);libraries.push_back(archives[k]->archive);}
    bin2bin::LinkOptions options;options.base_address=base;options.max_size=max_size;for(size_t k=0;k<external_count;++k){argument(externals[k].name&&*externals[k].name);if(!options.externals.emplace(externals[k].name,externals[k].address).second)throw Error{Error::Code::Conflict,"duplicate external symbol"};}
    return new limestone_linked_image{checked(bin2bin::link_archives(roots,libraries,target->target,options))};});
}
extern "C" const uint8_t* limestone_linked_image_bytes(const limestone_linked_image* image){return image&&!image->image.bytes.empty()?image->image.bytes.data():nullptr;}
extern "C" size_t limestone_linked_image_size(const limestone_linked_image* image){return image?image->image.bytes.size():0;}
extern "C" uint64_t limestone_linked_image_base(const limestone_linked_image* image){return image?image->image.base_address:0;}
extern "C" size_t limestone_linked_image_symbol_count(const limestone_linked_image* image){return image?image->image.symbols.size():0;}
extern "C" limestone_status limestone_linked_image_get_symbol(const limestone_linked_image* image,size_t index,limestone_external_symbol* out,limestone_error* error) {
  return boundary(error,[&](){argument(image&&out);if(index>=image->image.symbols.size())throw Error{Error::Code::NotFound,"image symbol index out of range"};auto it=image->image.symbols.begin();std::advance(it,index);*out={it->first.c_str(),it->second};return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_linked_image_find_symbol(const limestone_linked_image* image,const char* name,uint64_t* address,limestone_error* error) {
  return boundary(error,[&](){argument(image&&name&&address);auto it=image->image.symbols.find(name);if(it==image->image.symbols.end())throw Error{Error::Code::NotFound,"unknown linked symbol"};*address=it->second;return LIMESTONE_OK;});
}
