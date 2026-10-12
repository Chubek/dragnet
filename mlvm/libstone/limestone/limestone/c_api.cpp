#include "limestone.h"
#include "limestone.hpp"
#include "c_api_internal.hpp"
#include "allocation_internal.hpp"
#include "bin2bin/bin2bin.hpp"
#include "bin2bin/runtime.hpp"
#include "runtime.h"
#include "optimization_internal.hpp"
#include "object_internal.hpp"
#include "selection_internal.hpp"
#include "metacode/json.hpp"
#include <cstring>
#include <exception>

struct limestone_module { limestone::Module module; };
struct limestone_target { limestone::PipelineTarget target; };
struct limestone_program { limestone::unisel::Program program; };
struct limestone_configuration { limestone::PipelineOptions options; };
struct limestone_binary_architecture { limestone::bin2bin::Architecture architecture; };
struct limestone_buffer { std::vector<uint8_t> bytes;std::optional<std::string> text; };
struct limestone_binary_runtime { limestone::bin2bin::Runtime runtime; };
struct limestone_translated_region { std::shared_ptr<const limestone::bin2bin::TranslatedRegion> region; };
namespace {
using namespace limestone::c_api_internal;
limestone::PipelineOptions configuration(const limestone_options* options) {
  limestone::PipelineOptions result;if(!options)return result;
  for(auto value:{options->optimize,options->schedule,options->allocate})if(value!=0&&value!=1)throw limestone::Error{limestone::Error::Code::InvalidArgument,"pipeline options must be Boolean"};
  result.optimize=options->optimize;result.schedule=options->schedule;result.allocate=options->allocate;return result;
}
}
extern "C" void limestone_options_default(limestone_options* options) {
  if(options)*options={1,1,0};
}
extern "C" limestone_module* limestone_compile_checked(const char* input,const limestone_options* options,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!input)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null source input"};return new limestone_module{checked(limestone::run_pipeline(input,configuration(options)))};});
}
extern "C" limestone_module* limestone_compile(const char* input) {
  return limestone_compile_checked(input,nullptr,nullptr);
}
extern "C" const char* limestone_module_text(const limestone_module* module) {
  return module?module->module.machine_ir.c_str():nullptr;
}
extern "C" const char* limestone_module_exchange(const limestone_module* module){return module?module->module.machine_ir_exchange.c_str():nullptr;}
extern "C" void limestone_module_destroy(limestone_module* module) { delete module; }
extern "C" limestone_object* limestone_module_object(const limestone_module* module,const limestone_object_target* target,const char* symbol,limestone_error* error) {
  return boundary(error,[&]()->limestone_object*{if(!module||!target||!symbol)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null module/object target/symbol"};return new limestone_object(checked(limestone::make_object(module->module,target->target,symbol)));});
}
extern "C" limestone_target* limestone_target_load(const char* source,limestone_error* error) {
  return boundary(error,[&]()->limestone_target*{if(!source)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null UMD input"};auto document=checked(limestone::unisel::load_umd(source));auto target=checked(limestone::make_target(document.machine));return new limestone_target{std::move(target)};});
}
extern "C" limestone_target* limestone_target_load_file(const char* path,limestone_error* error) {
  return boundary(error,[&]()->limestone_target*{if(!path||!*path)throw limestone::Error{limestone::Error::Code::InvalidArgument,"empty UMD path"};auto document=checked(limestone::unisel::load_umd_file(path));return new limestone_target{checked(limestone::make_target(document.machine))};});
}
extern "C" void limestone_target_destroy(limestone_target* target){delete target;}
extern "C" limestone_status limestone_target_set_selection_predicate(limestone_target* target,const char* name,const limestone_selection_predicate* predicate,limestone_error* error) {
  return boundary(error,[&](){if(!target)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null selection target"};auto copy=target->target;auto count=bind_selection_predicate(copy.patterns,name,predicate);if(copy.burs_rules)count+=bind_selection_predicate(copy.burs_rules->rules,name,predicate);if(!count)throw limestone::Error{limestone::Error::Code::NotFound,"selection predicate is not declared"};target->target=std::move(copy);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_target_set_optimizer(limestone_target* target,const limestone_optimizer* optimizer,limestone_error* error) {
  return boundary(error,[&](){if(!target)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null optimizer target"};
    std::function<limestone::Result<limestone::unisel::Program>(const limestone::unisel::Program&)> replacement;
    if(optimizer)replacement=[snapshot=*optimizer](const limestone::unisel::Program& source)->limestone::Result<limestone::unisel::Program> {
      auto optimized=limestone::tunah::optimize_graph(source,snapshot.session,snapshot.options);if(!optimized)return limestone::Result<limestone::unisel::Program>::err(optimized.error());return limestone::Result<limestone::unisel::Program>::ok(std::move(optimized.value().program));
    };
    replacement.swap(target->target.optimizer);return LIMESTONE_OK;
  });
}
extern "C" limestone_program* limestone_program_create(){try{return new limestone_program;}catch(...){return nullptr;}}
extern "C" void limestone_program_destroy(limestone_program* program){delete program;}
extern "C" limestone_status limestone_program_add_node(limestone_program* program,uint32_t id,const char* opcode,const char* type,const uint32_t* inputs,size_t count,int has_constant,int64_t constant,int required,int produces,limestone_error* error) {
  return boundary(error,[&](){
    if(!program||!opcode||!*opcode||!type||(count&&!inputs)||(has_constant!=0&&has_constant!=1)||(required!=0&&required!=1)||(produces!=0&&produces!=1))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid graph node argument"};
    if(std::any_of(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& node){return node.id==id;}))throw limestone::Error{limestone::Error::Code::Conflict,"duplicate graph node"};
    limestone::unisel::Node node{id,opcode,{},has_constant?std::optional<int64_t>{constant}:std::nullopt,type,0,bool(required),false,bool(produces)};
    if(count)node.inputs.assign(inputs,inputs+count);program->program.nodes.push_back(std::move(node));return LIMESTONE_OK;
  });
}
extern "C" limestone_status limestone_program_add_output(limestone_program* program,uint32_t id,limestone_error* error) {
  return boundary(error,[&](){if(!program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null graph"};if(std::find(program->program.outputs.begin(),program->program.outputs.end(),id)!=program->program.outputs.end())throw limestone::Error{limestone::Error::Code::Conflict,"duplicate graph output"};program->program.outputs.push_back(id);return LIMESTONE_OK;});
}
extern "C" limestone_module* limestone_compile_program(const limestone_program* program,const limestone_target* target,const limestone_options* options,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!program||!target)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null graph or target"};auto source=program->program;auto machine=target->target;auto config=configuration(options);return new limestone_module{checked(limestone::run_pipeline(source,machine,config))};});
}
extern "C" limestone_module* limestone_compile_umd(const char* source,const limestone_options* options,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!source)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null UMD input"};auto document=checked(limestone::unisel::load_umd(source));if(!document.program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"UMD has no source program"};auto target=checked(limestone::make_target(document.machine));return new limestone_module{checked(limestone::run_pipeline(*document.program,target,configuration(options)))};});
}
extern "C" limestone_module* limestone_compile_umd_file(const char* path,const limestone_options* options,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!path||!*path)throw limestone::Error{limestone::Error::Code::InvalidArgument,"empty UMD path"};auto document=checked(limestone::unisel::load_umd_file(path));if(!document.program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"UMD has no source program"};auto target=checked(limestone::make_target(document.machine));return new limestone_module{checked(limestone::run_pipeline(*document.program,target,configuration(options)))};});
}
extern "C" size_t limestone_module_stage_count(const limestone_module* module){return module?module->module.stages.size():0;}
extern "C" const char* limestone_module_stage(const limestone_module* module,size_t index){return module&&index<module->module.stages.size()?module->module.stages[index].c_str():nullptr;}
extern "C" limestone_target* limestone_target_load_isa(const char* source,limestone_error* error) {
  return boundary(error,[&]()->limestone_target*{if(!source)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null ISA input"};return new limestone_target{checked(limestone::make_target(checked(limestone::metacode::parse_isa(source))))};});
}
extern "C" limestone_status limestone_program_set_node_properties(limestone_program* program,uint32_t id,const char* klass,const char* origin,int effect,int call,int terminator,int trap,limestone_error* error) {
  return boundary(error,[&](){if(!program||!klass||!origin)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null graph property argument"};for(auto value:{effect,call,terminator,trap})if(value!=0&&value!=1)throw limestone::Error{limestone::Error::Code::InvalidArgument,"graph effects must be Boolean"};auto node=std::find_if(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& n){return n.id==id;});if(node==program->program.nodes.end())throw limestone::Error{limestone::Error::Code::NotFound,"unknown graph node"};auto changed=*node;changed.register_class=klass;changed.origin=origin;changed.side_effect=effect;changed.call=call;changed.terminator=terminator;changed.may_trap=trap;*node=std::move(changed);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_add_dependency(limestone_program* program,uint32_t producer,uint32_t consumer,limestone_dependency_kind kind,uint32_t latency,int scheduling,limestone_error* error) {
  return boundary(error,[&](){if(!program||kind<LIMESTONE_DEP_TRUE||kind>LIMESTONE_DEP_ORDERING||(scheduling!=0&&scheduling!=1))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid graph dependency"};program->program.dependencies.push_back({producer,consumer,static_cast<limestone::schedrow::DepKind>(kind),latency,0,bool(scheduling)});return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_set_memory(limestone_program* program,uint32_t id,const limestone_memory_access* access,limestone_error* error) {
  return boundary(error,[&](){if(!program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null program"};auto node=std::find_if(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& n){return n.id==id;});if(node==program->program.nodes.end())throw limestone::Error{limestone::Error::Code::NotFound,"unknown graph node"};if(!access){node->access.reset();return LIMESTONE_OK;}
    if(!access->address_space||(access->alias_count&&!access->alias_sets)||access->alias_count>65536||access->ordering<LIMESTONE_MEMORY_RELAXED||access->ordering>LIMESTONE_MEMORY_SEQ_CST||(access->alignment&&(access->alignment&(access->alignment-1))))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid memory descriptor"};
    for(auto value:{access->read,access->write,access->volatile_access,access->atomic})if(value!=0&&value!=1)throw limestone::Error{limestone::Error::Code::InvalidArgument,"memory flags must be Boolean"};
    limestone::schedrow::MemoryAccess memory{bool(access->read),bool(access->write),bool(access->volatile_access),bool(access->atomic),static_cast<limestone::schedrow::MemoryOrdering>(access->ordering),access->address_space,{},access->size,access->alignment};for(size_t k=0;k<access->alias_count;++k)memory.alias_sets.push_back(access->alias_sets[k]);node->access=std::move(memory);return LIMESTONE_OK;});
}
extern "C" limestone_configuration* limestone_configuration_create(){try{return new limestone_configuration;}catch(...){return nullptr;}}
extern "C" limestone_status limestone_program_set_metadata(limestone_program* program,uint32_t id,const char* json,limestone_error* error) {
  return boundary(error,[&](){if(!program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null graph"};auto node=std::find_if(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& n){return n.id==id;});if(node==program->program.nodes.end())throw limestone::Error{limestone::Error::Code::NotFound,"unknown graph node"};limestone::metacode::OperandMetadata metadata;if(json){if(std::strlen(json)>4*1024*1024)throw limestone::Error{limestone::Error::Code::ResourceLimit,"source payload size limit"};metadata=checked(limestone::metacode::load_operand_metadata(checked(limestone::metacode::parse_json(json))));}node->metadata=std::move(metadata);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_add_block(limestone_program* program,uint32_t id,const char* name,const uint32_t* successors,size_t count,const uint32_t* live_out,size_t live_count,limestone_error* error) {
  return boundary(error,[&](){if(!program||!name||(count&&!successors)||(live_count&&!live_out))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid CFG block argument"};if(std::any_of(program->program.blocks.begin(),program->program.blocks.end(),[&](auto& b){return b.id==id;}))throw limestone::Error{limestone::Error::Code::Conflict,"duplicate CFG block"};limestone::schedrow::BasicBlock block{id,name};if(count)block.successors.assign(successors,successors+count);if(live_count)block.live_out.assign(live_out,live_out+live_count);program->program.blocks.push_back(std::move(block));if(program->program.blocks.size()==1)program->program.entry=id;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_set_entry(limestone_program* program,uint32_t block,limestone_error* error) {
  return boundary(error,[&](){if(!program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null program"};program->program.entry=block;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_set_node_block(limestone_program* program,uint32_t id,uint32_t block,limestone_error* error) {
  return boundary(error,[&](){if(!program)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null program"};auto node=std::find_if(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& n){return n.id==id;});if(node==program->program.nodes.end())throw limestone::Error{limestone::Error::Code::NotFound,"unknown CFG node"};node->block=block;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_program_set_control(limestone_program* program,uint32_t id,limestone_control_flow flow,const uint32_t* targets,size_t count,limestone_error* error) {
  return boundary(error,[&](){if(!program||(count&&!targets)||flow<LIMESTONE_FLOW_NONE||flow>LIMESTONE_FLOW_CALL)throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid control-flow argument"};auto node=std::find_if(program->program.nodes.begin(),program->program.nodes.end(),[&](auto& n){return n.id==id;});if(node==program->program.nodes.end())throw limestone::Error{limestone::Error::Code::NotFound,"unknown CFG node"};std::vector<uint32_t> copied;if(count)copied.assign(targets,targets+count);node->control=static_cast<limestone::schedrow::ControlFlow>(flow);node->block_targets=std::move(copied);return LIMESTONE_OK;});
}
extern "C" void limestone_configuration_destroy(limestone_configuration* config){delete config;}
extern "C" limestone_status limestone_configuration_set_pipeline(limestone_configuration* config,int optimize,int schedule,int allocate,int encode,int trace,limestone_error* error) {
  return boundary(error,[&](){if(!config)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null configuration"};for(auto value:{optimize,schedule,allocate,encode,trace})if(value!=0&&value!=1)throw limestone::Error{limestone::Error::Code::InvalidArgument,"pipeline flags must be Boolean"};config->options.optimize=optimize;config->options.schedule=schedule;config->options.allocate=allocate;config->options.encode=encode;config->options.trace_execution=trace;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_configuration_set_algorithms(limestone_configuration* config,limestone_selector selector,limestone_allocator allocator,limestone_error* error) {
  return boundary(error,[&](){if(!config||selector<LIMESTONE_SELECT_GLOBAL||selector>LIMESTONE_SELECT_BURS||allocator<LIMESTONE_ALLOCATE_LINEAR||allocator>LIMESTONE_ALLOCATE_PBQP)throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid pipeline algorithm"};config->options.selector=static_cast<limestone::SelectionStrategy>(selector);config->options.allocator=static_cast<limestone::AllocationStrategy>(allocator);return LIMESTONE_OK;});
}
extern "C" void limestone_pbqp_options_default(limestone_pbqp_options* options) {
  if(options)*options={1000000,8*1024*1024,50000000,1,nullptr,0,nullptr,0};
}
extern "C" limestone_status limestone_configuration_set_pbqp(limestone_configuration* config,const limestone_pbqp_options* options,limestone_error* error) {
  return boundary(error,[&](){if(!config)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null configuration"};config->options.pbqp=pbqp_options(options);return LIMESTONE_OK;});
}
extern "C" limestone_module* limestone_compile_configured(const char* source,const limestone_configuration* config,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!source)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null source"};return new limestone_module{checked(limestone::run_pipeline(source,config?config->options:limestone::PipelineOptions{}))};});
}
extern "C" limestone_module* limestone_compile_program_configured(const limestone_program* program,const limestone_target* target,const limestone_configuration* config,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!program||!target)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null graph or target"};auto source=program->program;auto machine=target->target;auto options=config?config->options:limestone::PipelineOptions{};return new limestone_module{checked(limestone::run_pipeline(source,machine,options))};});
}
extern "C" limestone_module* limestone_compile_target(const char* source,const limestone_target* target,const limestone_configuration* config,limestone_error* error) {
  return boundary(error,[&]()->limestone_module*{if(!source||!target)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null source or target"};std::string input=source;auto machine=target->target;auto options=config?config->options:limestone::PipelineOptions{};return new limestone_module{checked(limestone::run_pipeline(input,machine,options))};});
}
extern "C" const uint8_t* limestone_module_bytes(const limestone_module* module){return module&&module->module.encoded&&!module->module.encoded->bytes.empty()?module->module.encoded->bytes.data():nullptr;}
extern "C" size_t limestone_module_byte_count(const limestone_module* module){return module&&module->module.encoded?module->module.encoded->bytes.size():0;}
extern "C" size_t limestone_module_instruction_count(const limestone_module* module){return module?module->module.order.size():0;}
extern "C" const char* limestone_module_instruction_opcode(const limestone_module* module,size_t index){if(!module||index>=module->module.order.size())return nullptr;auto id=module->module.order[index];auto& region=module->module.materialized?module->module.materialized->region:module->module.selected;auto instruction=std::find_if(region.instructions.begin(),region.instructions.end(),[&](auto& i){return i.id==id;});return instruction==region.instructions.end()?nullptr:instruction->opcode.c_str();}
extern "C" limestone_status limestone_module_instruction_id(const limestone_module* module,size_t index,uint32_t* id,limestone_error* error) {
  return boundary(error,[&](){if(!module||!id)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null module inspection argument"};if(index>=module->module.order.size())throw limestone::Error{limestone::Error::Code::NotFound,"instruction index out of range"};*id=module->module.order[index];return LIMESTONE_OK;});
}
extern "C" size_t limestone_module_block_count(const limestone_module* module){return module?module->module.selected.blocks.empty()?1:module->module.selected.blocks.size():0;}
extern "C" limestone_status limestone_module_instruction_block(const limestone_module* module,size_t index,uint32_t* block,limestone_error* error) {
  return boundary(error,[&](){if(!module||!block)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null block inspection argument"};if(index>=module->module.order.size())throw limestone::Error{limestone::Error::Code::NotFound,"instruction index out of range"};auto id=module->module.order[index];auto& region=module->module.materialized?module->module.materialized->region:module->module.selected;auto instruction=std::find_if(region.instructions.begin(),region.instructions.end(),[&](auto& i){return i.id==id;});if(instruction==region.instructions.end())throw limestone::Error{limestone::Error::Code::Conflict,"missing materialized instruction"};*block=instruction->block;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_module_value_register(const limestone_module* module,uint32_t value,uint32_t* physical,limestone_error* error) {
  return boundary(error,[&](){if(!module||!physical)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null allocation inspection argument"};auto* allocation=module->module.materialized?&module->module.materialized->allocation:module->module.allocation?&*module->module.allocation:nullptr;if(!allocation||!allocation->regs.contains(value))throw limestone::Error{limestone::Error::Code::NotFound,"value has no physical register assignment"};*physical=allocation->regs.at(value);return LIMESTONE_OK;});
}
extern "C" size_t limestone_module_spill_count(const limestone_module* module){return module&&module->module.allocation?module->module.allocation->spilled.size():0;}
extern "C" limestone_status limestone_module_spill_value(const limestone_module* module,size_t index,uint32_t* value,limestone_error* error) {
  return boundary(error,[&](){if(!module||!value)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null spill inspection argument"};if(!module->module.allocation||index>=module->module.allocation->spilled.size())throw limestone::Error{limestone::Error::Code::NotFound,"spill index out of range"};*value=module->module.allocation->spilled[index];return LIMESTONE_OK;});
}
extern "C" uint64_t limestone_module_frame_size(const limestone_module* module){return module&&module->module.materialized?module->module.materialized->frame_size:0;}
extern "C" size_t limestone_module_spill_slot_count(const limestone_module* module){return module&&module->module.materialized?module->module.materialized->slots.size():0;}
extern "C" limestone_status limestone_module_spill_slot(const limestone_module* module,size_t index,limestone_spill_slot* slot,limestone_error* error) {
  return boundary(error,[&](){if(!module||!slot)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null spill-slot inspection argument"};if(!module->module.materialized||index>=module->module.materialized->slots.size())throw limestone::Error{limestone::Error::Code::NotFound,"spill-slot index out of range"};auto& s=module->module.materialized->slots[index];*slot={s.value,s.klass.c_str(),s.offset,s.size,s.alignment};return LIMESTONE_OK;});
}
extern "C" limestone_binary_architecture* limestone_binary_architecture_load(const char* source,limestone_error* error) {
  return boundary(error,[&]()->limestone_binary_architecture*{if(!source)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null ISA input"};return new limestone_binary_architecture{checked(limestone::bin2bin::from_metacode(checked(limestone::metacode::parse_isa(source))))};});
}
extern "C" void limestone_binary_architecture_destroy(limestone_binary_architecture* architecture){delete architecture;}
extern "C" limestone_buffer* limestone_binary_translate(const limestone_binary_architecture* src,const limestone_binary_architecture* dst,const uint8_t* bytes,size_t count,uint64_t source_address,uint64_t target_address,limestone_error* error) {
  return limestone_binary_translate_with_transform(src,dst,bytes,count,source_address,target_address,nullptr,error);
}
extern "C" limestone_buffer* limestone_binary_translate_with_transform(const limestone_binary_architecture* src,const limestone_binary_architecture* dst,const uint8_t* bytes,size_t count,uint64_t source_address,uint64_t target_address,const limestone_binary_transform* transform,limestone_error* error) {
  return boundary(error,[&]()->limestone_buffer*{if(!src||!dst||(count&&!bytes))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid binary translation argument"};limestone::bin2bin::TranslationOptions options;options.source_address=source_address;options.target_address=target_address;if(transform)options.semantic_transform=transform->transform;
    // A legality callback may destroy its source handles during the run.
    auto source=src->architecture,target=dst->architecture;return new limestone_buffer{checked(limestone::bin2bin::translate(source,target,{bytes,count},nullptr,options)),{}};
  });
}
extern "C" limestone_buffer* limestone_binary_disassemble(const limestone_binary_architecture* architecture,const uint8_t* bytes,size_t count,uint64_t address,limestone_error* error) {
  return boundary(error,[&]()->limestone_buffer*{if(!architecture||(count&&!bytes))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid binary disassembly argument"};auto text=limestone::bin2bin::disassemble(checked(limestone::bin2bin::decode(architecture->architecture,{bytes,count},address)));return new limestone_buffer{{},std::move(text)};});
}
extern "C" const uint8_t* limestone_buffer_data(const limestone_buffer* buffer){if(!buffer)return nullptr;return buffer->text?reinterpret_cast<const uint8_t*>(buffer->text->data()):buffer->bytes.data();}
extern "C" const char* limestone_buffer_text(const limestone_buffer* buffer){return buffer&&buffer->text?buffer->text->c_str():nullptr;}
extern "C" size_t limestone_buffer_size(const limestone_buffer* buffer){return buffer?buffer->text?buffer->text->size():buffer->bytes.size():0;}
extern "C" void limestone_buffer_destroy(limestone_buffer* buffer){delete buffer;}

namespace {
limestone_runtime_region_view runtime_view(const limestone::bin2bin::TranslatedRegion& region) {
  return {region.guest_address,region.target_address,region.guest_bytes.data(),region.guest_bytes.size(),region.bytes.data(),region.bytes.size()};
}
void callback_status(limestone_status status,const limestone_error& error) {
  if(status==LIMESTONE_OK)return;
  if(status<LIMESTONE_INVALID_ARGUMENT||status>LIMESTONE_RESOURCE_LIMIT)throw limestone::Error{limestone::Error::Code::Internal,"runtime callback returned an invalid status"};
  auto end=std::find(std::begin(error.message),std::end(error.message),'\0');
  auto message=end==std::begin(error.message)?std::string("runtime callback failed"):std::string(std::begin(error.message),end);
  throw limestone::Error{static_cast<limestone::Error::Code>(static_cast<int>(status)-1),std::move(message)};
}
struct RuntimeExecutable {
  limestone_runtime_executable code{};
  ~RuntimeExecutable()noexcept {if(code.release)try{code.release(code.userdata);}catch(...){}}
};
}
extern "C" void limestone_runtime_options_default(limestone_runtime_options* options){if(options)*options={10,1024};}
extern "C" limestone_binary_runtime* limestone_runtime_create(const limestone_binary_architecture* source,const limestone_binary_architecture* target,const limestone_runtime_options* options,limestone_runtime_installer install,void* userdata,limestone_error* error) {
  return limestone_runtime_create_with_transform(source,target,options,nullptr,install,userdata,error);
}
extern "C" limestone_binary_runtime* limestone_runtime_create_with_transform(const limestone_binary_architecture* source,const limestone_binary_architecture* target,const limestone_runtime_options* options,const limestone_binary_transform* transform,limestone_runtime_installer install,void* userdata,limestone_error* error) {
  return boundary(error,[&]()->limestone_binary_runtime* {
    if(!source||!target||(options&&(!options->hot_threshold||!options->max_regions)))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid runtime architecture or options"};
    limestone::bin2bin::RuntimeOptions configuration;if(options){configuration.hot_threshold=options->hot_threshold;configuration.max_regions=options->max_regions;}
    if(transform)configuration.translation.semantic_transform=transform->transform;
    limestone::bin2bin::CodeInstaller installer;
    if(install)installer=[install,userdata](const limestone::bin2bin::TranslatedRegion& region)->limestone::Result<std::function<limestone::Result<int64_t>()>> {
      auto executable=std::make_shared<RuntimeExecutable>();auto view=runtime_view(region);limestone_error diagnostic{};
      callback_status(install(&view,&executable->code,userdata,&diagnostic),diagnostic);
      if(!executable->code.execute)throw limestone::Error{limestone::Error::Code::Conflict,"installer returned no execution callback"};
      return limestone::Result<std::function<limestone::Result<int64_t>()>>::ok([executable] {
        int64_t result=0;limestone_error diagnostic{};
        callback_status(executable->code.execute(executable->code.userdata,&result,&diagnostic),diagnostic);
        return limestone::Result<int64_t>::ok(result);
      });
    };
    return new limestone_binary_runtime{limestone::bin2bin::Runtime(source->architecture,target->architecture,std::move(configuration),std::move(installer))};
  });
}
extern "C" void limestone_runtime_destroy(limestone_binary_runtime* runtime){delete runtime;}
extern "C" limestone_translated_region* limestone_runtime_prepare(limestone_binary_runtime* runtime,const uint8_t* bytes,size_t count,uint64_t guest_address,uint64_t target_address,limestone_error* error) {
  return boundary(error,[&]()->limestone_translated_region* {if(!runtime||(count&&!bytes))throw limestone::Error{limestone::Error::Code::InvalidArgument,"invalid runtime preparation argument"};return new limestone_translated_region{checked(runtime->runtime.prepare({bytes,count},guest_address,target_address))};});
}
extern "C" void limestone_translated_region_destroy(limestone_translated_region* region){delete region;}
extern "C" int limestone_translated_region_is_valid(const limestone_translated_region* region){return region&&region->region->valid();}
extern "C" int limestone_translated_region_is_compiled(const limestone_translated_region* region){return region&&region->region->compiled();}
extern "C" limestone_status limestone_translated_region_get_view(const limestone_translated_region* region,limestone_runtime_region_view* view,limestone_error* error) {
  return boundary(error,[&](){if(!region||!view)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null runtime region inspection argument"};*view=runtime_view(*region->region);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_runtime_invoke(limestone_binary_runtime* runtime,const limestone_translated_region* region,int64_t* result,limestone_error* error) {
  return boundary(error,[&](){if(!runtime||!region||!result)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null runtime invocation argument"};auto value=checked(runtime->runtime.invoke(region->region));*result=value;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_runtime_invalidate(limestone_binary_runtime* runtime,uint64_t address,uint64_t count,size_t* invalidated,limestone_error* error) {
  return boundary(error,[&](){if(!runtime||!invalidated)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null runtime invalidation argument"};auto removed=checked(runtime->runtime.invalidate(address,count));*invalidated=removed;return LIMESTONE_OK;});
}
extern "C" size_t limestone_runtime_resident_count(const limestone_binary_runtime* runtime){return runtime?runtime->runtime.resident_regions():0;}
extern "C" limestone_status limestone_runtime_open_cache(limestone_binary_runtime* runtime,const char* path,size_t size,limestone_error* error) {
  return boundary(error,[&](){if(!runtime||!path)throw limestone::Error{limestone::Error::Code::InvalidArgument,"null runtime cache argument"};checked(runtime->runtime.open_persistent_cache(path,size));return LIMESTONE_OK;});
}
