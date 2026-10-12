#include "limestone.h"
#include "limestone/object.h"
#include "exolayer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
extern int c_header_other(void);
extern void il_c_api(const char *directory);
extern void runtime_c_api(const char *directory);
static void identity(const exl_value_t *args,size_t count,exl_value_t *result,void *userdata) {
  (void)userdata;assert(count==1);*result=args[0];
}
static long long native_identity(long long value){return value;}
static double native_variadic(long long value,...) {va_list args;double real;void *pointer;va_start(args,value);real=va_arg(args,double);pointer=va_arg(args,void *);va_end(args);return (double)value+real+(pointer?1.0:0.0);}
int main(int argc,char **argv) {
  assert(argc==2);
  il_c_api(argv[1]);
  runtime_c_api(argv[1]);
  limestone_options options;limestone_options_default(&options);assert(options.optimize==1&&options.allocate==0);
  limestone_error error;limestone_module* module=limestone_compile_checked("(add 1 2)",&options,&error);
  assert(module&&error.code==LIMESTONE_OK);assert(strstr(limestone_module_text(module),"#3"));limestone_module_destroy(module);
  assert(limestone_module_text(NULL)==NULL);limestone_module_destroy(NULL);
  {
    char path[4096];limestone_target *target;
    snprintf(path,sizeof(path),"%s/arithmetic-includes.umd",argv[1]);module=limestone_compile_umd_file(path,NULL,&error);assert(module&&strstr(limestone_module_text(module),"ADD"));limestone_module_destroy(module);
    target=limestone_target_load_file(path,&error);assert(target);limestone_target_destroy(target);
    snprintf(path,sizeof(path),"%s/includes/cycle.umd",argv[1]);assert(!limestone_target_load_file(path,&error)&&error.code==LIMESTONE_CONFLICT&&strstr(error.message,"cycle.umd:1:1"));
    assert(!limestone_compile_umd_file(NULL,NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  }
  {
    const char *umd="machine x { regclass G=[$r0,$r1]; operator const(0); instruction C {latency=0;} pattern p:const()->C; default_register_class=G; }";
    limestone_target *target=limestone_target_load(umd,&error);
    limestone_program *program=limestone_program_create();assert(target&&program);
    assert(limestone_program_add_node(program,1,"const","i64",NULL,0,1,42,1,1,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,1,"const","i64",NULL,0,1,42,1,1,&error)==LIMESTONE_CONFLICT);
    assert(limestone_program_add_node(program,1,"const","i64",NULL,0,1,42,1,1,NULL)==LIMESTONE_CONFLICT);
    assert(limestone_program_add_output(NULL,1,NULL)==LIMESTONE_INVALID_ARGUMENT);
    assert(limestone_program_add_output(program,1,&error)==LIMESTONE_OK);
    {uint32_t aliases[]={17};limestone_memory_access memory={1,0,0,0,LIMESTONE_MEMORY_ACQUIRE,"heap",aliases,1,8,8};assert(limestone_program_set_memory(program,1,&memory,&error)==LIMESTONE_OK);memory.alignment=3;assert(limestone_program_set_memory(program,1,&memory,NULL)==LIMESTONE_INVALID_ARGUMENT);assert(limestone_program_set_memory(program,1,NULL,&error)==LIMESTONE_OK);}
    options.allocate=1;module=limestone_compile_program(program,target,&options,&error);assert(module&&strstr(limestone_module_text(module),"#42"));
    limestone_target_destroy(target);limestone_program_destroy(program);
    assert(limestone_module_stage_count(module)==6);assert(strcmp(limestone_module_stage(module,3),"allocate")==0);assert(strcmp(limestone_module_stage(module,4),"allocated-schedule")==0);assert(strcmp(limestone_module_stage(module,5),"machine-ir")==0);limestone_module_destroy(module);
    assert(limestone_compile_umd(NULL,NULL,&error)==NULL&&error.code==LIMESTONE_INVALID_ARGUMENT);
  }
  {
    char path[4096],isa[16384];FILE *file;size_t count;uint32_t physical,id;
    limestone_configuration *config=limestone_configuration_create();limestone_program *program=limestone_program_create();limestone_target *target;
    assert(config&&program);snprintf(path,sizeof(path),"%s/pipeline-machine.isa",argv[1]);file=fopen(path,"rb");assert(file);count=fread(isa,1,sizeof(isa)-1,file);assert(!ferror(file)&&feof(file));fclose(file);isa[count]=0;
    target=limestone_target_load_isa(isa,&error);assert(target&&error.code==LIMESTONE_OK);
    assert(limestone_configuration_set_pipeline(config,1,1,1,1,0,&error)==LIMESTONE_OK);
    assert(limestone_configuration_set_algorithms(config,LIMESTONE_SELECT_BURS,LIMESTONE_ALLOCATE_COLOR,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,10,"const","i64",NULL,0,1,20,1,1,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,20,"const","i64",NULL,0,1,22,1,1,&error)==LIMESTONE_OK);
    {uint32_t inputs[]={10,20};assert(limestone_program_add_node(program,1,"add","i64",inputs,2,0,0,1,1,&error)==LIMESTONE_OK);}
    assert(limestone_program_add_output(program,1,&error)==LIMESTONE_OK);
    assert(limestone_program_set_node_properties(program,1,"G","c-test",0,0,0,0,&error)==LIMESTONE_OK);
    module=limestone_compile_program_configured(program,target,config,&error);assert(module&&limestone_module_byte_count(module)==5&&limestone_module_bytes(module));
    assert(limestone_module_instruction_count(module)==3&&limestone_module_instruction_opcode(module,0));
    assert(limestone_module_exchange(module)&&strstr(limestone_module_exchange(module),"limestone.machineir.region"));
    assert(limestone_module_instruction_id(module,100,&id,NULL)==LIMESTONE_NOT_FOUND);
    assert(limestone_module_value_register(module,1,&physical,NULL)==LIMESTONE_OK);
    {
      limestone_binary_architecture *binary=limestone_binary_architecture_load(isa,&error);limestone_buffer *disassembly,*copy;assert(binary);
      disassembly=limestone_binary_disassemble(binary,limestone_module_bytes(module),5,100,&error);assert(disassembly&&strstr(limestone_buffer_text(disassembly),"ADD")&&limestone_buffer_size(disassembly)>0);limestone_buffer_destroy(disassembly);
      copy=limestone_binary_translate(binary,binary,limestone_module_bytes(module),5,100,500,&error);assert(copy&&limestone_buffer_size(copy)==5&&memcmp(limestone_buffer_data(copy),limestone_module_bytes(module),5)==0);limestone_buffer_destroy(copy);
      assert(!limestone_binary_translate(binary,binary,NULL,1,0,0,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);limestone_binary_architecture_destroy(binary);
    }
    limestone_module_destroy(module);limestone_target_destroy(target);limestone_program_destroy(program);
    assert(limestone_configuration_set_pipeline(config,1,1,0,0,1,&error)==LIMESTONE_OK);
    assert(limestone_configuration_set_algorithms(config,LIMESTONE_SELECT_GLOBAL,LIMESTONE_ALLOCATE_LINEAR,&error)==LIMESTONE_OK);
    module=limestone_compile_configured("(add 20 22)",config,&error);assert(module&&strstr(limestone_module_text(module),"checked.add.i64"));limestone_module_destroy(module);
    assert(limestone_configuration_set_pipeline(config,2,1,0,0,0,NULL)==LIMESTONE_INVALID_ARGUMENT);
    limestone_configuration_destroy(config);
  }
  {
    const char *umd="machine cfg {regclass G=[$r0,$r1];operator const(0);operator branch(1);operator return(1);instruction C {latency=0;}instruction B {latency=0;control_flow=conditional_branch;}instruction R {latency=0;control_flow=return;}pattern c: const():i64 -> C;pattern b: branch(?x:i64) -> B side_effects true;pattern r: return(?x:i64) -> R side_effects true;default_register_class=G;}";
    uint32_t successors[]={3,9},input[]={1},block,id;limestone_target *target=limestone_target_load(umd,&error);limestone_program *program=limestone_program_create();assert(target&&program);
    assert(limestone_program_add_block(program,7,"entry",successors,2,NULL,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_block(program,9,"fallthrough",NULL,0,NULL,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_block(program,3,"taken",NULL,0,NULL,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_block(program,7,"duplicate",NULL,0,NULL,0,NULL)==LIMESTONE_CONFLICT);
    assert(limestone_program_add_node(program,1,"const","i64",NULL,0,1,42,1,1,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,2,"branch","",input,1,0,0,1,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,4,"return","",input,1,0,0,1,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,6,"return","",input,1,0,0,1,0,&error)==LIMESTONE_OK);
    assert(limestone_program_set_node_block(program,1,7,&error)==LIMESTONE_OK);assert(limestone_program_set_node_block(program,2,7,&error)==LIMESTONE_OK);
    assert(limestone_program_set_node_block(program,4,9,&error)==LIMESTONE_OK);assert(limestone_program_set_node_block(program,6,3,&error)==LIMESTONE_OK);
    assert(limestone_program_set_control(program,2,LIMESTONE_FLOW_CONDITIONAL,successors,2,&error)==LIMESTONE_OK);successors[0]=100;
    assert(limestone_program_set_control(program,4,LIMESTONE_FLOW_RETURN,NULL,0,&error)==LIMESTONE_OK);assert(limestone_program_set_control(program,6,LIMESTONE_FLOW_RETURN,NULL,0,&error)==LIMESTONE_OK);
    assert(limestone_program_set_control(program,999,LIMESTONE_FLOW_RETURN,NULL,0,NULL)==LIMESTONE_NOT_FOUND);assert(limestone_program_set_node_block(NULL,1,7,NULL)==LIMESTONE_INVALID_ARGUMENT);
    options.allocate=1;module=limestone_compile_program(program,target,&options,&error);assert(module&&limestone_module_block_count(module)==3&&limestone_module_instruction_count(module)==4);
    assert(limestone_module_instruction_block(module,0,&block,&error)==LIMESTONE_OK&&block==7);assert(limestone_module_instruction_id(module,0,&id,&error)==LIMESTONE_OK&&id==1);
    assert(limestone_module_instruction_block(module,3,&block,&error)==LIMESTONE_OK&&block==3);assert(limestone_module_instruction_block(module,100,&block,NULL)==LIMESTONE_NOT_FOUND);limestone_module_destroy(module);
    assert(limestone_program_set_entry(program,9,&error)==LIMESTONE_OK);assert(!limestone_compile_program(program,target,&options,&error)&&error.code==LIMESTONE_CONFLICT);
    limestone_target_destroy(target);limestone_program_destroy(program);
  }
  {
    char path[4096],isa[16384];FILE *file;size_t count;uint32_t physical;
    limestone_configuration *config=limestone_configuration_create();limestone_target *target;
    snprintf(path,sizeof(path),"%s/native-constant.isa",argv[1]);file=fopen(path,"rb");assert(file);count=fread(isa,1,sizeof(isa)-1,file);assert(!ferror(file)&&feof(file));fclose(file);isa[count]=0;
    target=limestone_target_load_isa(isa,&error);assert(target&&config);assert(limestone_configuration_set_pipeline(config,1,1,1,1,0,&error)==LIMESTONE_OK);
    module=limestone_compile_target("((lambda x (add x 2)) 40)",target,config,&error);assert(module&&limestone_module_byte_count(module)==8);
    assert(limestone_module_bytes(module)[0]==0x48&&limestone_module_bytes(module)[3]==42&&limestone_module_bytes(module)[7]==0xc3);
    assert(limestone_module_value_register(module,1,&physical,&error)==LIMESTONE_OK&&physical==1);
    limestone_target_destroy(target);limestone_configuration_destroy(config);assert(limestone_module_bytes(module)[3]==42);limestone_module_destroy(module);
    assert(!limestone_compile_target("42",NULL,NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  }
  {
    char path[4096],isa[16384];FILE *file;size_t count;uint32_t input[2],physical,spilled;limestone_spill_slot slot;
    limestone_configuration *config=limestone_configuration_create();limestone_program *program=limestone_program_create();limestone_target *target;
    assert(config&&program);snprintf(path,sizeof(path),"%s/backend-machine.isa",argv[1]);file=fopen(path,"rb");assert(file);count=fread(isa,1,sizeof(isa)-1,file);assert(!ferror(file)&&feof(file));fclose(file);isa[count]=0;
    target=limestone_target_load_isa(isa,&error);assert(target);
    assert(limestone_configuration_set_pipeline(config,1,1,1,1,0,&error)==LIMESTONE_OK);
    assert(limestone_configuration_set_algorithms(config,(limestone_selector)100,LIMESTONE_ALLOCATE_LINEAR,NULL)==LIMESTONE_INVALID_ARGUMENT);
    assert(limestone_program_add_node(program,10,"const","i64",NULL,0,1,7,1,1,&error)==LIMESTONE_OK);
    assert(limestone_program_add_node(program,20,"const","i64",NULL,0,1,14,1,1,&error)==LIMESTONE_OK);
    input[0]=10;input[1]=20;assert(limestone_program_add_node(program,30,"add","i64",input,2,0,0,1,1,&error)==LIMESTONE_OK);
    input[0]=input[1]=30;assert(limestone_program_add_node(program,40,"add","i64",input,2,0,0,1,1,&error)==LIMESTONE_OK);
    input[0]=40;assert(limestone_program_add_node(program,50,"return","",input,1,0,0,1,0,&error)==LIMESTONE_OK);
    assert(limestone_program_set_control(program,50,LIMESTONE_FLOW_RETURN,NULL,0,&error)==LIMESTONE_OK);
    assert(limestone_program_add_output(program,40,&error)==LIMESTONE_OK);
    module=limestone_compile_program_configured(program,target,config,&error);assert(module);
    assert(limestone_module_value_register(module,41,&physical,&error)==LIMESTONE_OK&&physical>0);
    assert(limestone_module_spill_count(module)>0&&limestone_module_frame_size(module)>0);
    assert(limestone_module_spill_count(module)==limestone_module_spill_slot_count(module));
    assert(limestone_module_spill_value(module,0,&spilled,&error)==LIMESTONE_OK);
    assert(limestone_module_spill_slot(module,0,&slot,&error)==LIMESTONE_OK&&slot.value==spilled&&slot.size==8&&strcmp(slot.register_class,"G")==0);
    assert(limestone_module_value_register(module,spilled,&physical,NULL)==LIMESTONE_NOT_FOUND);
    assert(limestone_module_spill_slot(module,999,&slot,NULL)==LIMESTONE_NOT_FOUND);
    {
      limestone_object_target *object_target=limestone_object_target_load_isa(isa,&error);
      limestone_object *object;limestone_object_data *elf;limestone_object_section section;
      assert(object_target);object=limestone_module_object(module,object_target,"compiled",&error);assert(object);
      assert(limestone_object_get_section(object,0,&section,&error)==LIMESTONE_OK&&section.byte_count==limestone_module_byte_count(module));
      assert(memcmp(section.bytes,limestone_module_bytes(module),section.byte_count)==0);
      limestone_object_target_destroy(object_target);elf=limestone_object_emit_elf(object,&error);limestone_object_destroy(object);assert(elf&&limestone_object_data_size(elf)>64);limestone_object_data_destroy(elf);
      assert(!limestone_module_object(module,NULL,"compiled",&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
    }
    limestone_module_destroy(module);limestone_target_destroy(target);limestone_program_destroy(program);limestone_configuration_destroy(config);
  }
  exl_context_t* context=exl_context_create();assert(context);
  {
    exl_kind_t kinds[]={EXL_I64,EXL_F64,EXL_PTR};exl_signature_t signature={EXL_F64,kinds,3};exl_value_t arguments[3],result;
    arguments[0].kind=EXL_I64;arguments[0].as.i64=20;arguments[1].kind=EXL_F64;arguments[1].as.f64=21.0;arguments[2].kind=EXL_PTR;arguments[2].as.ptr=context;
    assert(exl_register_native_variadic(context,"invalid_fixed",(exl_native_address_t)native_variadic,&signature,0,EXL_CC_HOST)==-1);
    if(exl_native_available()){assert(exl_register_native_variadic(context,"variadic",(exl_native_address_t)native_variadic,&signature,1,EXL_CC_HOST)==0);assert(exl_call(context,"variadic",arguments,3,&result)==0&&result.kind==EXL_F64&&result.as.f64==42.0);}else assert(exl_register_native_variadic(context,"variadic",(exl_native_address_t)native_variadic,&signature,1,EXL_CC_HOST)==-4);
  }
  {exl_kind_t kind=EXL_I64;exl_signature_t signature={EXL_I64,&kind,1};exl_value_t arg,result;arg.kind=EXL_I64;arg.as.i64=-42;if(exl_native_available()){assert(exl_register_native(context,"native_id",(exl_native_address_t)native_identity,&signature,EXL_CC_HOST)==0);assert(exl_call(context,"native_id",&arg,1,&result)==0&&result.kind==EXL_I64&&result.as.i64==-42);}else assert(exl_register_native(context,"native_id",(exl_native_address_t)native_identity,&signature,EXL_CC_HOST)==-4);}
  assert(exl_register(context,"id",identity,NULL)==0);
  {exl_signature_t signature={EXL_I64,NULL,0};assert(exl_register_native(context,"invalid_cc",(exl_native_address_t)native_identity,&signature,(exl_callconv_t)100)==-1);signature.return_kind=(exl_kind_t)100;assert(exl_register_typed(context,"invalid_type",identity,NULL,&signature)==-1);}
  exl_value_t value;value.kind=EXL_I64;value.as.i64=42;
  assert(exl_call(context,"id",&value,1,&value)==0&&value.as.i64==42);
  assert(exl_call(context,"id",NULL,1,&value)<0&&value.kind==EXL_VOID);
  {
    exl_kind_t kinds[]={EXL_I64};exl_signature_t signature={EXL_I64,kinds,1};size_t size,alignment;
    assert(exl_kind_layout(EXL_I64,&size,&alignment)==0&&size==sizeof(long long)&&alignment>0);
    assert(exl_kind_layout(EXL_VOID,&size,&alignment)==-1);
    assert(exl_register_typed(context,"typed",identity,NULL,&signature)==0);kinds[0]=EXL_F64;
    value.kind=EXL_I64;value.as.i64=42;assert(exl_call(context,"typed",&value,1,&value)==0&&value.as.i64==42);
    value.kind=EXL_F64;value.as.f64=1;assert(exl_call(context,"typed",&value,1,&value)==-1&&value.kind==EXL_VOID);
    assert(exl_call(context,"typed",NULL,0,&value)==-1);
  }
  exl_context_destroy(context);assert(c_header_other()==EXL_I64);return 0;
}
