#include "limestone/optimization.h"
#include "limestone/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct proof_context {
  limestone_optimizer *source;
  int calls,releases,mode;
} proof_context;
static void release_proof(void *data){++((proof_context *)data)->releases;}
static limestone_status prove(const char *const *terms,size_t count,int *allowed,void *data,limestone_error *error) {
  proof_context *proof=(proof_context *)data;++proof->calls;
  assert(count==2&&terms&&allowed&&error);
  if(proof->mode==1){strcpy(error->message,"proof interrupted");return LIMESTONE_INTERRUPTED;}
  if(proof->mode==2){*allowed=2;return LIMESTONE_OK;}
  if(proof->mode==3)return (limestone_status)12345;
  if(proof->mode==4){memset(error->message,'x',sizeof(error->message));return LIMESTONE_CONFLICT;}
  if(proof->mode==5){limestone_optimizer *source=proof->source;proof->source=NULL;limestone_optimizer_destroy(source);}
  *allowed=!strcmp(terms[0],terms[1]);return LIMESTONE_OK;
}
static limestone_status cancel(void *data,int *cancelled,limestone_error *error) {
  proof_context *proof=(proof_context *)data;(void)error;++proof->calls;*cancelled=1;return LIMESTONE_OK;
}
static const char *rules="(operator add 2) (rule zero (add ?x 0) ?x :where (literal_is ?x 42))";
static limestone_optimizer *optimizer(proof_context *proof) {
  limestone_error error;limestone_optimizer *engine=limestone_optimizer_create(&error);assert(engine&&error.code==LIMESTONE_OK);
  assert(limestone_optimizer_define_predicate(engine,"literal_is",2,prove,proof,release_proof,&error)==LIMESTONE_OK);
  assert(limestone_optimizer_load_rules(engine,rules,"proof.sexpr",&error)==LIMESTONE_OK);return engine;
}
static void generic_terms(void) {
  proof_context proof={0},stop={0};limestone_error error;limestone_optimization_limits limits;
  limestone_optimization_info info;limestone_optimization *result;limestone_optimizer *engine=optimizer(&proof);
  assert(limestone_optimizer_rule_count(engine)==1);
  assert(limestone_optimizer_define_operator(engine,"add",2,&error)==LIMESTONE_OK);
  assert(limestone_optimizer_define_operator(engine,"add",3,&error)==LIMESTONE_CONFLICT);
  assert(limestone_optimizer_define_predicate(engine,"literal_is",2,prove,&proof,release_proof,&error)==LIMESTONE_CONFLICT&&proof.releases==0);
  assert(limestone_optimizer_load_rules(engine,"(operator lost 1)(rule bad (lost ?x) ?missing)","transaction.sexpr",&error)==LIMESTONE_INVALID_ARGUMENT);
  assert(strstr(error.message,"transaction.sexpr")&&limestone_optimizer_rule_count(engine)==1);
  assert(limestone_optimizer_set_operator_cost(engine,"lost",1,&error)==LIMESTONE_NOT_FOUND);
  assert(limestone_optimizer_set_operator_cost(engine,"add",7,&error)==LIMESTONE_OK);
  assert(limestone_optimizer_set_literal_cost(engine,2,&error)==LIMESTONE_OK);
  result=limestone_optimizer_saturate(engine,"(add 42 0)","input.sexpr",&error);assert(result);
  assert(!strcmp(limestone_optimization_expression(result),"42"));
  assert(limestone_optimization_get_info(result,&info,&error)==LIMESTONE_OK&&info.cost==2&&info.rewrites>0&&info.saturated&&!info.limit_reached&&info.nodes>=info.classes);
  assert(limestone_optimization_trace_count(result)>0&&strstr(limestone_optimization_trace(result,0),"zero")&&strstr(limestone_optimization_trace(result,0),"proof.sexpr"));
  assert(!limestone_optimization_trace(result,SIZE_MAX));limestone_optimization_destroy(result);
  result=limestone_optimizer_saturate(engine,"(add 41 0)",NULL,&error);assert(result&&!strcmp(limestone_optimization_expression(result),"(add 41 0)"));limestone_optimization_destroy(result);
  proof.mode=1;assert(!limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error)&&error.code==LIMESTONE_INTERRUPTED&&strstr(error.message,"proof interrupted"));
  proof.mode=2;assert(!limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  proof.mode=3;assert(!limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error)&&error.code==LIMESTONE_INTERNAL);
  proof.mode=4;assert(!limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error)&&error.code==LIMESTONE_CONFLICT&&strlen(error.message)==sizeof(error.message)-1);proof.mode=0;
  assert(!limestone_optimizer_saturate(engine,"(add 42)","arity.sexpr",&error)&&error.code==LIMESTONE_INVALID_ARGUMENT&&strstr(error.message,"arity.sexpr"));
  assert(!limestone_optimizer_saturate(engine,"(","syntax.sexpr",&error)&&error.code==LIMESTONE_PARSE);
  limestone_optimization_limits_default(&limits);limits.nodes=0;assert(limestone_optimizer_set_limits(engine,&limits,&error)==LIMESTONE_INVALID_ARGUMENT);
  limestone_optimization_limits_default(&limits);limits.nodes=1;assert(limestone_optimizer_set_limits(engine,&limits,&error)==LIMESTONE_OK);
  assert(!limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error)&&error.code==LIMESTONE_RESOURCE_LIMIT);
  limestone_optimization_limits_default(&limits);limits.iterations=0;limits.trace=0;assert(limestone_optimizer_set_limits(engine,&limits,&error)==LIMESTONE_OK);
  result=limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error);assert(result&&limestone_optimization_get_info(result,&info,&error)==LIMESTONE_OK&&info.limit_reached&&!info.saturated&&info.iterations==0&&info.cost==11&&limestone_optimization_trace_count(result)==0);limestone_optimization_destroy(result);
  assert(limestone_optimizer_set_limits(engine,NULL,NULL)==LIMESTONE_OK);
  assert(limestone_optimizer_set_cancellation(engine,NULL,&stop,release_proof,&error)==LIMESTONE_INVALID_ARGUMENT&&stop.releases==0);
  assert(limestone_optimizer_set_cancellation(engine,cancel,&stop,release_proof,&error)==LIMESTONE_OK);
  result=limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error);assert(result&&stop.calls>0&&limestone_optimization_get_info(result,&info,&error)==LIMESTONE_OK&&info.limit_reached&&info.rewrites==0);limestone_optimization_destroy(result);
  assert(limestone_optimizer_set_cancellation(engine,NULL,NULL,NULL,&error)==LIMESTONE_OK&&stop.releases==1);
  result=limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error);assert(result);
  limestone_optimizer_destroy(engine);assert(proof.releases==1&&limestone_optimization_get_info(result,&info,NULL)==LIMESTONE_OK&&!strcmp(limestone_optimization_expression(result),"42"));limestone_optimization_destroy(result);
  engine=optimizer(&proof);proof.source=engine;proof.mode=5;
  result=limestone_optimizer_saturate(engine,"(add 42 0)",NULL,&error);assert(result&&proof.source==NULL&&proof.releases==2&&!strcmp(limestone_optimization_expression(result),"42"));limestone_optimization_destroy(result);
  assert(limestone_optimization_get_info(NULL,&info,&error)==LIMESTONE_INVALID_ARGUMENT&&!limestone_optimization_expression(NULL));
  assert(!limestone_optimizer_saturate(NULL,"42",NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  limestone_optimizer_destroy(NULL);limestone_optimization_destroy(NULL);
}
static void pipeline_attachment(void) {
  const char *machine="machine proof {operator const(0);operator add(2);instruction CONST {latency=0;}instruction ADD {latency=1;}pattern 1 c:const():i64 -> CONST;pattern 2 a:add(?x:i64,?y:i64):i64 -> ADD;}";
  proof_context proof={0};limestone_error error;limestone_optimizer *engine=optimizer(&proof);
  limestone_target *target=limestone_target_load(machine,&error);limestone_program *program=limestone_program_create();
  limestone_module *module;limestone_options options={1,1,0};limestone_optimization_limits limits;uint32_t inputs[]={1,2};assert(target&&program);
  assert(limestone_optimizer_define_graph_operator(engine,"sum","missing","i64",0,&error)==LIMESTONE_NOT_FOUND);
  assert(limestone_optimizer_define_graph_operator(engine,"add","add","i64",0,&error)==LIMESTONE_OK);
  assert(limestone_optimizer_define_graph_operator(engine,"add","add","f64",0,&error)==LIMESTONE_CONFLICT);
  assert(limestone_optimizer_define_graph_operator(engine,"sum","add","i64",0,&error)==LIMESTONE_CONFLICT);
  assert(limestone_optimizer_set_constant_opcode(engine,"add",&error)==LIMESTONE_CONFLICT);
  assert(limestone_optimizer_set_constant_opcode(engine,"const",&error)==LIMESTONE_OK);
  assert(limestone_target_set_optimizer(target,engine,&error)==LIMESTONE_OK);
  limestone_optimization_limits_default(&limits);limits.iterations=0;assert(limestone_optimizer_set_limits(engine,&limits,&error)==LIMESTONE_OK);
  limestone_optimizer_destroy(engine);assert(proof.releases==0);
  assert(limestone_program_add_node(program,1,"const","i64",NULL,0,1,42,1,1,&error)==LIMESTONE_OK);
  assert(limestone_program_add_node(program,2,"const","i64",NULL,0,1,0,1,1,&error)==LIMESTONE_OK);
  assert(limestone_program_add_node(program,3,"add","i64",inputs,2,0,0,1,1,&error)==LIMESTONE_OK);
  assert(limestone_program_add_output(program,3,&error)==LIMESTONE_OK);
  module=limestone_compile_program(program,target,&options,&error);assert(module&&limestone_module_instruction_count(module)==1&&!strcmp(limestone_module_instruction_opcode(module,0),"CONST")&&strstr(limestone_module_text(module),"#42"));
  assert(!strcmp(limestone_module_stage(module,1),"optimize"));limestone_module_destroy(module);
  options.optimize=0;module=limestone_compile_program(program,target,&options,&error);assert(module&&limestone_module_instruction_count(module)==3&&!strcmp(limestone_module_stage(module,1),"select"));limestone_module_destroy(module);
  assert(limestone_target_set_optimizer(target,NULL,&error)==LIMESTONE_OK&&proof.releases==1);
  options.optimize=1;module=limestone_compile_program(program,target,&options,&error);assert(module&&limestone_module_instruction_count(module)==3);limestone_module_destroy(module);
  limestone_program_destroy(program);limestone_target_destroy(target);
}
typedef struct binary_host {
  limestone_binary_runtime *runtime;
  limestone_binary_transform *transform;
  limestone_binary_architecture *source,*target;
  int calls,releases,cancel_releases,stop,mode,installs,code_releases;
} binary_host;
static void destruction_checks(binary_host *host) {
  if(host->mode==5&&host->runtime) {
    limestone_error error;size_t count=999;uint8_t byte=1;
    assert(limestone_runtime_resident_count(host->runtime)==0);
    assert(!limestone_runtime_prepare(host->runtime,&byte,1,100,200,&error)&&error.code==LIMESTONE_CONFLICT);
    assert(limestone_runtime_invalidate(host->runtime,100,1,&count,&error)==LIMESTONE_CONFLICT&&count==999);
    assert(limestone_runtime_open_cache(host->runtime,"unused",1048576,&error)==LIMESTONE_CONFLICT);
  }
}
static void binary_release(void *data){binary_host *host=(binary_host *)data;++host->releases;destruction_checks(host);}
static void cancel_release(void *data){binary_host *host=(binary_host *)data;++host->cancel_releases;destruction_checks(host);}
static limestone_status binary_cancel(void *data,int *cancelled,limestone_error *error){(void)error;*cancelled=((binary_host *)data)->stop;return LIMESTONE_OK;}
static limestone_status binary_legal(const limestone_binary_semantic_view *view,const char *candidate,int *proved,void *data,limestone_error *error) {
  binary_host *host=(binary_host *)data;++host->calls;
  assert(view&&candidate&&proved&&error&&error->code==LIMESTONE_OK&&error->message[0]==0);
  assert(view->address>=100&&view->control==LIMESTONE_BINARY_FALLTHROUGH&&!view->has_branch_target);
  assert(!strcmp(view->semantics,"(set counter (add counter 1))"));
  if(host->mode==1) {
    uint8_t byte=1;limestone_error nested;size_t count;host->mode=0;
    assert(!limestone_runtime_prepare(host->runtime,&byte,1,100,200,&nested)&&nested.code==LIMESTONE_CONFLICT);
    assert(limestone_runtime_invalidate(host->runtime,100,1,&count,&nested)==LIMESTONE_OK);
  }else if(host->mode==2) {
    host->mode=0;limestone_binary_transform_destroy(host->transform);host->transform=NULL;
    limestone_binary_architecture_destroy(host->source);host->source=NULL;
    limestone_binary_architecture_destroy(host->target);host->target=NULL;assert(host->releases==0);
  }else if(host->mode==3){*proved=2;return LIMESTONE_OK;}
  else if(host->mode==4){strcpy(error->message,"unsupported legality proof");return LIMESTONE_UNSUPPORTED;}
  else if(host->mode==6){*proved=0;return LIMESTONE_OK;}
  *proved=!strcmp(view->semantics,candidate)||!strcmp(candidate,"(set counter (sub counter -1))");return LIMESTONE_OK;
}
static const char *source_isa=
  "arch byte_source {word_size=64;addr_size=64;endian=little;}"
  "profile {version=\"1\";execution_model=\"wrapping-counter\";}"
  "tooling {bin2bin={schema_version=1;instruction_encoding=fixed8;execution_domain=bytecode;word_size=64;address_size=64;endianness=little;};}"
  "encoding INC {width=8;base=1;}op inc {encoding=INC;semantics=\"(set counter (add counter 1))\";tooling={binary_translation={schema_version=1;equivalence_basis=semantics;};};}";
static const char *target_isa=
  "arch byte_target {word_size=64;addr_size=64;endian=little;}"
  "profile {version=\"1\";execution_model=\"wrapping-counter\";}"
  "tooling {bin2bin={schema_version=1;instruction_encoding=fixed8;execution_domain=bytecode;word_size=64;address_size=64;endianness=little;};}"
  "encoding INC {width=8;base=8;}encoding SUB {width=8;base=9;}"
  "op inc {encoding=INC;semantics=\"(set counter (add counter 1))\";tooling={binary_translation={schema_version=1;equivalence_basis=semantics;};};}"
  "op subtract {encoding=SUB;semantics=\"(set counter (sub counter -1))\";tooling={binary_translation={schema_version=1;equivalence_basis=semantics;};};}";
static limestone_optimizer *binary_optimizer(void) {
  limestone_optimizer *engine=limestone_optimizer_create(NULL);assert(engine);
  assert(limestone_optimizer_load_rules(engine,"(operator set 2)(operator add 2)(operator sub 2)(rule increment (add ?x 1) (sub ?x -1))","binary.rules",NULL)==LIMESTONE_OK);
  assert(limestone_optimizer_set_operator_cost(engine,"add",20,NULL)==LIMESTONE_OK);
  assert(limestone_optimizer_set_operator_cost(engine,"sub",1,NULL)==LIMESTONE_OK);return engine;
}
typedef struct binary_executable {binary_host *host;size_t count;uint8_t bytes[8];} binary_executable;
static limestone_status binary_execute(void *data,int64_t *out,limestone_error *error) {
  binary_executable *code=(binary_executable *)data;size_t k;int64_t counter=39;(void)error;
  for(k=0;k<code->count;++k){if(code->bytes[k]==8)counter+=1;else if(code->bytes[k]==9)counter-= -1;else return LIMESTONE_UNSUPPORTED;}
  *out=counter;return LIMESTONE_OK;
}
static void code_release(void *data){binary_executable *code=(binary_executable *)data;++code->host->code_releases;free(code);}
static limestone_status binary_install(const limestone_runtime_region_view *view,limestone_runtime_executable *out,void *data,limestone_error *error) {
  binary_host *host=(binary_host *)data;binary_executable *code=(binary_executable *)malloc(sizeof(*code));(void)error;
  assert(code&&view->byte_count<=sizeof(code->bytes));code->host=host;code->count=view->byte_count;memcpy(code->bytes,view->bytes,code->count);
  out->userdata=code;out->execute=binary_execute;out->release=code_release;++host->installs;return LIMESTONE_OK;
}
static void binary_optimization(void) {
  limestone_error error;limestone_runtime_options options={1,4};uint8_t bytes[]={1,1,1};char identity[4096];
  limestone_optimizer *engine=binary_optimizer();binary_host host={0},other={0};limestone_binary_transform *changed;
  limestone_buffer *buffer;limestone_translated_region *first,*second;limestone_runtime_region_view view;int64_t value;size_t count;int prior;
  host.source=limestone_binary_architecture_load(source_isa,&error);host.target=limestone_binary_architecture_load(target_isa,&error);assert(host.source&&host.target);
  assert(!limestone_optimizer_binary_transform(engine,"",binary_legal,&host,binary_release,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT&&host.releases==0);
  assert(!limestone_optimizer_binary_transform(engine,"counter:1",NULL,&host,binary_release,&error)&&error.code==LIMESTONE_UNSUPPORTED&&host.releases==0);
  host.transform=limestone_optimizer_binary_transform(engine,"counter:1",binary_legal,&host,binary_release,&error);assert(host.transform&&limestone_binary_transform_is_cacheable(host.transform));
  assert(strstr(limestone_binary_transform_identity(host.transform),"counter:1"));strcpy(identity,limestone_binary_transform_identity(host.transform));
  buffer=limestone_binary_translate(host.source,host.target,bytes,3,100,200,&error);assert(buffer&&limestone_buffer_size(buffer)==3&&limestone_buffer_data(buffer)[0]==8&&host.calls==0);limestone_buffer_destroy(buffer);
  buffer=limestone_binary_translate_with_transform(host.source,host.target,bytes,3,100,200,host.transform,&error);assert(buffer&&limestone_buffer_size(buffer)==3&&limestone_buffer_data(buffer)[0]==9&&host.calls==6);limestone_buffer_destroy(buffer);
  assert(limestone_optimizer_set_operator_cost(engine,"add",1,NULL)==LIMESTONE_OK);
  assert(limestone_optimizer_set_operator_cost(engine,"sub",20,NULL)==LIMESTONE_OK);
  changed=limestone_optimizer_binary_transform(engine,"counter:1",binary_legal,&other,binary_release,&error);assert(changed&&strcmp(identity,limestone_binary_transform_identity(changed))&&strcmp(identity,limestone_binary_transform_identity(host.transform))==0);
  buffer=limestone_binary_translate_with_transform(host.source,host.target,bytes,3,100,200,changed,&error);assert(buffer&&limestone_buffer_data(buffer)[0]==8);limestone_buffer_destroy(buffer);limestone_binary_transform_destroy(changed);assert(other.releases==1);
  host.runtime=limestone_runtime_create_with_transform(host.source,host.target,&options,host.transform,binary_install,&host,&error);assert(host.runtime);
  limestone_optimizer_destroy(engine);limestone_binary_transform_destroy(host.transform);host.transform=NULL;assert(host.releases==0);
  first=limestone_runtime_prepare(host.runtime,bytes,3,100,200,&error);assert(first&&limestone_translated_region_is_compiled(first));prior=host.calls;
  second=limestone_runtime_prepare(host.runtime,bytes,3,100,200,&error);assert(second&&host.calls==prior&&host.installs==1);
  assert(limestone_runtime_invoke(host.runtime,first,&value,&error)==LIMESTONE_OK&&value==42);limestone_translated_region_destroy(second);
  host.mode=3;assert(!limestone_runtime_prepare(host.runtime,bytes,3,101,200,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  host.mode=4;assert(!limestone_runtime_prepare(host.runtime,bytes,3,101,200,&error)&&error.code==LIMESTONE_UNSUPPORTED&&strstr(error.message,"unsupported legality proof"));
  host.mode=6;assert(!limestone_runtime_prepare(host.runtime,bytes,3,101,200,&error)&&error.code==LIMESTONE_CONFLICT);
  host.mode=1;assert(!limestone_runtime_prepare(host.runtime,bytes,3,101,200,&error)&&error.code==LIMESTONE_INTERRUPTED&&!limestone_translated_region_is_valid(first));
  limestone_translated_region_destroy(first);host.mode=5;limestone_runtime_destroy(host.runtime);host.runtime=NULL;assert(host.releases==1&&host.code_releases==1);
  limestone_binary_architecture_destroy(host.source);limestone_binary_architecture_destroy(host.target);host.source=host.target=NULL;
  /* Legality may release its source handles; the translation call is a snapshot. */
  engine=binary_optimizer();host.calls=0;host.source=limestone_binary_architecture_load(source_isa,NULL);host.target=limestone_binary_architecture_load(target_isa,NULL);
  host.transform=limestone_optimizer_binary_transform(engine,"counter:1",binary_legal,&host,binary_release,NULL);assert(host.transform);limestone_optimizer_destroy(engine);
  host.releases=0;host.mode=2;buffer=limestone_binary_translate_with_transform(host.source,host.target,bytes,3,100,200,host.transform,&error);
  assert(buffer&&limestone_buffer_data(buffer)[0]==9&&host.releases==1&&!host.transform&&!host.source&&!host.target);limestone_buffer_destroy(buffer);
  /* Cancellation changes subsequent observations without mutating retained code. */
  memset(&host,0,sizeof(host));engine=binary_optimizer();
  assert(limestone_optimizer_set_cancellation(engine,binary_cancel,&host,cancel_release,NULL)==LIMESTONE_OK);
  host.transform=limestone_optimizer_binary_transform(engine,"counter:dynamic:1",binary_legal,&host,binary_release,NULL);assert(host.transform&&!limestone_binary_transform_is_cacheable(host.transform));
  host.source=limestone_binary_architecture_load(source_isa,NULL);host.target=limestone_binary_architecture_load(target_isa,NULL);
  host.runtime=limestone_runtime_create_with_transform(host.source,host.target,&options,host.transform,binary_install,&host,NULL);assert(host.runtime);
  limestone_binary_architecture_destroy(host.source);limestone_binary_architecture_destroy(host.target);host.source=host.target=NULL;
  limestone_optimizer_destroy(engine);limestone_binary_transform_destroy(host.transform);host.transform=NULL;assert(!host.releases&&!host.cancel_releases);
  first=limestone_runtime_prepare(host.runtime,bytes,3,100,200,&error);assert(first);host.stop=1;
  second=limestone_runtime_prepare(host.runtime,bytes,3,100,200,&error);assert(second&&host.installs==2&&host.calls==12);
  assert(limestone_translated_region_get_view(first,&view,&error)==LIMESTONE_OK&&view.bytes[0]==9);
  assert(limestone_translated_region_get_view(second,&view,&error)==LIMESTONE_OK&&view.bytes[0]==8);
  assert(limestone_runtime_invoke(host.runtime,first,&value,&error)==LIMESTONE_OK&&value==42);
  assert(limestone_runtime_invoke(host.runtime,second,&value,&error)==LIMESTONE_OK&&value==42);
  assert(limestone_runtime_invalidate(host.runtime,100,3,&count,&error)==LIMESTONE_OK&&count==2);
  host.mode=5;limestone_runtime_destroy(host.runtime);host.runtime=NULL;assert(host.releases==1&&host.cancel_releases==1);
  assert(limestone_translated_region_get_view(first,&view,&error)==LIMESTONE_OK&&view.bytes[0]==9);limestone_translated_region_destroy(first);limestone_translated_region_destroy(second);assert(host.code_releases==2);
  assert(!limestone_binary_transform_identity(NULL)&&!limestone_binary_transform_is_cacheable(NULL));limestone_binary_transform_destroy(NULL);
}
int main(void){generic_terms();pipeline_attachment();binary_optimization();return 0;}
