#include "limestone/object.h"
#include <assert.h>
#include <string.h>
static const char *isa="arch x {} tooling {object_file={format=elf64;version=1;machine=62;endianness=little;flags=0;osabi=0;abi_version=0;text_alignment=8;relocations=[{type=1;name=abs64;kind=absolute;storage_bytes=8;bit_offset=0;bits=64;scale=1;signed=false;}];};}";
static const char *isa32="arch x {} tooling {object_file={format=elf32;version=1;machine=3;endianness=little;flags=0;osabi=0;abi_version=0;text_alignment=4;relocations=[{type=1;name=abs32;kind=absolute;storage_bytes=4;bit_offset=0;bits=32;scale=1;signed=false;implicit_addend_signed=true;}];};}";
int main(void) {
  limestone_error error;limestone_object_target *target=limestone_object_target_load_isa(isa,&error);assert(target);
  limestone_object *object=limestone_object_create(target,&error);assert(object);uint8_t bytes[8]={0};uint32_t section=99,symbol=99;
  assert(limestone_object_elf_class(object)==64&&limestone_object_elf_class(NULL)==0);
  limestone_object_section data={".data",1,3,8,0,bytes,8,0};assert(limestone_object_add_section(object,&data,&section,&error)==LIMESTONE_OK&&section==0);
  limestone_object_symbol imported={"imported",LIMESTONE_OBJECT_UNDEFINED,0,0,LIMESTONE_SYMBOL_GLOBAL,0,0};assert(limestone_object_add_symbol(object,&imported,&symbol,&error)==LIMESTONE_OK&&symbol==0);
  limestone_object_relocation relocation={0,0,1,0,2};assert(limestone_object_add_relocation(object,&relocation,&error)==LIMESTONE_OK);
  imported.section=0;imported.size=100;symbol=99;assert(limestone_object_add_symbol(object,&imported,&symbol,&error)==LIMESTONE_INVALID_ARGUMENT&&symbol==99&&limestone_object_symbol_count(object)==1);
  imported.section=LIMESTONE_OBJECT_UNDEFINED;imported.size=0;imported.binding=(limestone_symbol_binding)99;
  assert(limestone_object_add_symbol(object,&imported,&symbol,&error)==LIMESTONE_INVALID_ARGUMENT&&symbol==99&&limestone_object_symbol_count(object)==1);
  limestone_object_section view;assert(limestone_object_get_section(object,0,&view,&error)==LIMESTONE_OK&&view.byte_count==8&&!strcmp(view.name,".data"));
  limestone_object_relocation copied;assert(limestone_object_get_relocation(object,0,&copied,&error)==LIMESTONE_OK&&copied.addend==2);copied.offset=99;assert(limestone_object_get_relocation(object,1,&copied,&error)==LIMESTONE_NOT_FOUND&&copied.offset==99);
  limestone_object_data *elf=limestone_object_emit_elf(object,&error);assert(elf);limestone_object_destroy(object);
  object=limestone_object_load_elf(limestone_object_data_bytes(elf),limestone_object_data_size(elf),"api.o",&error);limestone_object_data_destroy(elf);assert(object&&strstr(limestone_object_text(object),"imported"));
  const limestone_object *objects[]={object};limestone_external_symbol external={"imported",42};limestone_linked_image *image=limestone_object_link(target,objects,1,1000,1024,&external,1,&error);assert(image);
  limestone_object_destroy(object);limestone_object_target_destroy(target);assert(limestone_linked_image_size(image)==8&&limestone_linked_image_bytes(image)[0]==44&&limestone_linked_image_base(image)==1000);
  uint64_t address=123;assert(limestone_linked_image_find_symbol(image,"missing",&address,&error)==LIMESTONE_NOT_FOUND&&address==123);limestone_linked_image_destroy(image);
  target=limestone_object_target_load_isa(isa,&error);object=limestone_object_from_code(target,bytes,8,"entry",&error);assert(object);objects[0]=object;image=limestone_object_link(target,objects,1,1027,1024,NULL,0,&error);assert(image);
  limestone_archive *archive=limestone_archive_create(&error);assert(archive);assert(limestone_archive_add_member(archive,"member with spaces.o",object,&error)==LIMESTONE_OK);assert(limestone_archive_member_count(archive)==1);
  assert(limestone_archive_add_member(archive,"bad\nname",object,&error)==LIMESTONE_INVALID_ARGUMENT&&limestone_archive_member_count(archive)==1);
  elf=limestone_archive_emit(archive,&error);assert(elf);limestone_archive_destroy(archive);archive=limestone_archive_load(limestone_object_data_bytes(elf),limestone_object_data_size(elf),"api.a",&error);limestone_object_data_destroy(elf);assert(archive&&!strcmp(limestone_archive_member_name(archive,0),"member with spaces.o"));
  limestone_object *member=limestone_archive_get_member(archive,0,&error);assert(member);limestone_archive_destroy(archive);assert(limestone_object_symbol_count(member)==1);limestone_object_destroy(member);
  limestone_object_destroy(object);limestone_object_target_destroy(target);assert(limestone_linked_image_find_symbol(image,"entry",&address,&error)==LIMESTONE_OK&&address==1032);assert(limestone_linked_image_size(image)==13);
  limestone_external_symbol output={NULL,0};assert(limestone_linked_image_get_symbol(image,0,&output,&error)==LIMESTONE_OK&&!strcmp(output.name,"entry")&&output.address==1032);limestone_linked_image_destroy(image);
  target=limestone_object_target_load_isa(isa32,&error);assert(target);object=limestone_object_create(target,&error);assert(object&&limestone_object_elf_class(object)==32);
  bytes[0]=bytes[1]=bytes[2]=bytes[3]=255;data.byte_count=4;data.alignment=4;
  assert(limestone_object_add_section(object,&data,&section,&error)==LIMESTONE_OK);
  imported.binding=LIMESTONE_SYMBOL_GLOBAL;assert(limestone_object_add_symbol(object,&imported,&symbol,&error)==LIMESTONE_OK);
  relocation.addend=0;assert(limestone_object_add_relocation_ex(object,&relocation,1,&error)==LIMESTONE_OK);
  uint32_t implicit=99;copied.offset=99;
  assert(limestone_object_get_relocation(object,0,&copied,&error)==LIMESTONE_UNSUPPORTED&&copied.offset==99);
  assert(limestone_object_get_relocation_ex(object,1,&copied,&implicit,&error)==LIMESTONE_NOT_FOUND&&copied.offset==99&&implicit==99);
  assert(limestone_object_get_relocation_ex(object,0,&copied,NULL,&error)==LIMESTONE_INVALID_ARGUMENT&&copied.offset==99);
  assert(limestone_object_get_relocation_ex(object,0,&copied,&implicit,&error)==LIMESTONE_OK&&implicit==1&&copied.addend==0&&copied.offset==0);
  assert(limestone_object_add_relocation_ex(object,&relocation,2,&error)==LIMESTONE_INVALID_ARGUMENT);
  relocation.addend=1;assert(limestone_object_add_relocation_ex(object,&relocation,1,&error)==LIMESTONE_INVALID_ARGUMENT);
  assert(limestone_object_relocation_count(object)==1);
  elf=limestone_object_emit_elf(object,&error);assert(elf);limestone_object_destroy(object);
  object=limestone_object_load_elf(limestone_object_data_bytes(elf),limestone_object_data_size(elf),"rel32.o",&error);limestone_object_data_destroy(elf);
  assert(object&&limestone_object_elf_class(object)==32);objects[0]=object;
  image=limestone_object_link(target,objects,1,4096,1024,&external,1,&error);assert(image&&limestone_linked_image_bytes(image)[0]==41);
  limestone_linked_image_destroy(image);limestone_object_destroy(object);limestone_object_target_destroy(target);
  assert(!limestone_object_create(NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);assert(!limestone_object_load_elf(NULL,0,NULL,&error)&&error.code==LIMESTONE_PARSE);
  limestone_object_data_destroy(NULL);limestone_object_destroy(NULL);limestone_object_target_destroy(NULL);limestone_linked_image_destroy(NULL);return 0;
}
