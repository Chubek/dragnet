#include "limestone.hpp"
#include "schedrow/timing_metadata.hpp"
#include "limeburg/target.hpp"
#include "bin2bin/codegen.hpp"
#include <charconv>
#include <cmath>
#include <limits>

namespace limestone {
namespace {
using Object=metacode::Value::Object;
const Object* object(const Object& o,const std::string& key) {auto it=o.find(key);if(it==o.end())return nullptr;auto p=std::get_if<Object>(&it->second.data);if(!p)throw Error{Error::Code::InvalidArgument,"target field must be an object: "+key};return p;}
std::string text(const Object& o,const std::string& key){auto it=o.find(key);return it==o.end()?"":it->second.text();}
uint32_t number(const std::string& s) {uint32_t n=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size())throw Error{Error::Code::InvalidArgument,"expected target unsigned integer: "+s};return n;}
int signed_number(const std::string& s) {int n=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size())throw Error{Error::Code::InvalidArgument,"expected target signed integer: "+s};return n;}
void known(const Object& fields,std::initializer_list<std::string_view> names,std::string_view contract) {
  std::optional<std::string> unsupported;
  for(auto& [key,value]:fields)if(std::find(names.begin(),names.end(),key)==names.end()&&(!unsupported||key<*unsupported))unsupported=key;
  if(unsupported)throw Error{Error::Code::Unsupported,"unsupported target "+std::string(contract)+" field: "+*unsupported};
}
bool boolean(const Object& o,const std::string& key) {auto s=text(o,key);if(s.empty()||s=="false")return false;if(s=="true")return true;throw Error{Error::Code::InvalidArgument,"expected target Boolean: "+key};}
double real(const std::string& s) {double n=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);if(e!=std::errc{}||p!=s.data()+s.size()||!std::isfinite(n)||n<=0)throw Error{Error::Code::InvalidArgument,"expected positive target rate/quantity: "+s};return n;}
std::vector<uint32_t> numbers(const Object& o,const std::string& key) {
  auto it=o.find(key);if(it==o.end())return {};auto array=std::get_if<metacode::Value::Array>(&it->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"expected target numeric array: "+key};
  std::vector<uint32_t> out;for(auto& v:*array)out.push_back(number(v.text()));return out;
}
}
Result<PipelineTarget> make_target(const unisel::MachineDescription& description) {
  auto valid=unisel::validate(description);if(!valid)return Result<PipelineTarget>::err(valid.error());
  std::string context="target "+description.name;
  if(!description.source.file.empty())context=description.source.file+":"+std::to_string(description.source.line)+":"+std::to_string(description.source.column)+": "+context;
  const auto machine_context=context;
  try {
    PipelineTarget target;target.name=description.name;target.patterns=description.patterns;target.metadata=description.metadata;
    for(auto& [name,registers]:description.register_classes)target.register_classes.push_back({name,registers});
    std::sort(target.register_classes.begin(),target.register_classes.end(),[](auto& a,auto& b){return a.name<b.name;});target.aliases=description.aliases;
    if(auto field=description.metadata.find("register_storage");field!=description.metadata.end()) {
      auto array=std::get_if<metacode::Value::Array>(&field->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"register_storage must be an array"};
      for(auto& value:*array){auto r=std::get_if<Object>(&value.data);if(!r)throw Error{Error::Code::InvalidArgument,"register storage must be an object"};known(*r,{"register","bank","slices"},"register storage");auto name=text(*r,"register");regtl::RegisterStorage storage;storage.id=description.physical_names.contains(name)?description.physical_names.at(name):number(name);storage.bank=text(*r,"bank");
        if(auto slices=r->find("slices");slices!=r->end()){auto parts=std::get_if<metacode::Value::Array>(&slices->second.data);if(!parts)throw Error{Error::Code::InvalidArgument,"register slices must be an array"};for(auto& part:*parts){auto s=std::get_if<Object>(&part.data);if(!s)throw Error{Error::Code::InvalidArgument,"register slice must be an object"};known(*s,{"unit","begin","width"},"register slice");storage.slices.push_back({number(text(*s,"unit")),number(text(*s,"begin")),number(text(*s,"width"))});}}
        target.register_storage.push_back(std::move(storage));
      }
    }
    regtl::Program storage_model;storage_model.classes=target.register_classes;storage_model.aliases=target.aliases;storage_model.storage=target.register_storage;
    auto storage_valid=regtl::validate(storage_model);if(!storage_valid)throw storage_valid.error();target.scheduling.register_aliases=regtl::register_aliases(storage_model);
    target.default_register_class=text(description.metadata,"default_register_class");
    if(!target.default_register_class.empty()&&!description.register_classes.contains(target.default_register_class))throw Error{Error::Code::InvalidArgument,"unknown default register class: "+target.default_register_class};
    if(auto spills=object(description.metadata,"spills"))for(auto& [klass,value]:*spills){auto c=std::get_if<Object>(&value.data);if(!c)throw Error{Error::Code::InvalidArgument,"spill class must be an object"};regtl::SpillClass model;model.klass=klass;model.load_opcode=text(*c,"load");model.store_opcode=text(*c,"store");model.address_space=text(*c,"address_space");model.size=number(text(*c,"size"));model.alignment=number(text(*c,"alignment"));model.scratch=numbers(*c,"scratch");for(auto& [key,v]:*c)if(key!="load"&&key!="store"&&key!="address_space"&&key!="size"&&key!="alignment"&&key!="scratch")throw Error{Error::Code::Unsupported,"unknown spill class property: "+key};target.spill_classes.push_back(std::move(model));}
    std::sort(target.spill_classes.begin(),target.spill_classes.end(),[](auto& a,auto& b){return a.klass<b.klass;});
    if(auto scheduling=object(description.metadata,"scheduling")) {
      known(*scheduling,{"issue_width","resources","critical_path","group_search_limit","verification_limit"},"scheduling");
      if(scheduling->contains("issue_width"))target.scheduling.issue_width=number(text(*scheduling,"issue_width"));
      if(auto resources=object(*scheduling,"resources"))for(auto& [name,capacity]:*resources){auto n=number(capacity.text());if(!n)throw Error{Error::Code::InvalidArgument,"zero target resource capacity"};target.scheduling.resource_capacity[name]=n;}
      target.scheduling.critical_path=boolean(*scheduling,"critical_path");
      for(auto [key,limit]:{std::pair{"group_search_limit",&target.scheduling.group_search_limit},{"verification_limit",&target.scheduling.verification_limit}})if(scheduling->contains(key)){auto value=text(*scheduling,key);auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),*limit);if(error!=std::errc{}||end!=value.data()+value.size())throw Error{Error::Code::InvalidArgument,"invalid scheduling limit: "+std::string(key)};}
    }
    std::map<std::string,Object> ordered(description.instructions.begin(),description.instructions.end());
    for(auto& [name,fields]:ordered) {
      context=machine_context+" instruction "+name;
      auto file=text(fields,"source_file");if(!file.empty())context=file+":"+text(fields,"source_line")+":"+text(fields,"source_column")+": target "+description.name+" instruction "+name;
      const auto* model=object(fields,"scheduling");if(!model)model=&fields;
      else known(*model,{"latency","memory_latency","operand_latencies","throughput","resources","issue_slots","issue_width","result_latencies","implicit_result_latencies","priority","pressure_delta"},"instruction scheduling");
      InstructionModel instruction;instruction.metadata=fields;
      instruction.rematerializable=boolean(fields,"rematerializable");
      if(auto encoded=object(fields,"encoding_operands")) {
        auto bindings=bin2bin::encoding_operands(*encoded,context);if(!bindings)throw bindings.error();
        for(auto& [field,binding]:bindings.value()) {
          auto kind=binding.kind;bool fixed=kind==bin2bin::OperandBinding::Kind::FixedDefinition||kind==bin2bin::OperandBinding::Kind::FixedUse;if(!fixed)continue;
          auto physical=description.physical_names.find(binding.physical_register);if(physical==description.physical_names.end())throw Error{Error::Code::InvalidArgument,"unknown fixed encoding register: "+binding.physical_register};
          if(binding.index>UINT32_MAX)throw Error{Error::Code::InvalidArgument,"fixed encoding operand index exceeds 32 bits"};
          auto& constraints=kind==bin2bin::OperandBinding::Kind::FixedDefinition?instruction.fixed_definitions:instruction.fixed_uses;auto [it,added]=constraints.emplace(uint32_t(binding.index),physical->second);
          if(!added&&it->second!=physical->second)throw Error{Error::Code::Conflict,"conflicting fixed encoding operand registers"};
        }
      }
      auto latency=text(*model,"latency");if(!latency.empty()&&latency!="unknown"&&latency!="unspecified"&&latency!="target-dependent"){
        const auto* value=&model->at("latency");if(auto object=std::get_if<Object>(&value->data);object&&object->contains("result")){known(*object,{"result","memory"},"timing domain");value=&object->at("result");if(object->contains("memory"))instruction.memory_latency=schedrow::metadata::latency(object->at("memory"));}
        auto range=schedrow::metadata::latency(*value);instruction.latency=range.maximum;if(range.minimum!=range.maximum)instruction.latency_range=range;
      }
      if(model->contains("memory_latency")){if(instruction.memory_latency)throw Error{Error::Code::Conflict,"duplicate memory latency"};instruction.memory_latency=schedrow::metadata::latency(model->at("memory_latency"));}
      auto throughput=text(*model,"throughput");if(!throughput.empty())instruction.throughput=real(throughput);
      instruction.opcode_class=text(fields,"opcode_class");instruction.semantic_class=text(fields,"semantic_class");
      instruction.barrier=boolean(fields,"barrier");instruction.memory=boolean(fields,"memory");instruction.call=boolean(fields,"call");instruction.terminator=boolean(fields,"terminator");instruction.may_trap=boolean(fields,"may_trap");
      if(fields.contains("speculative"))instruction.speculative=boolean(fields,"speculative");
      if(model->contains("priority"))instruction.priority=signed_number(text(*model,"priority"));
      if(auto deltas=object(*model,"pressure_delta"))for(auto& [klass,delta]:*deltas){if(klass.empty())throw Error{Error::Code::InvalidArgument,"empty pressure class"};instruction.pressure_delta[klass]=signed_number(delta.text());}
      auto flow=text(fields,"control_flow");std::map<std::string,schedrow::ControlFlow> flows{{"",schedrow::ControlFlow::None},{"fallthrough",schedrow::ControlFlow::None},{"branch",schedrow::ControlFlow::Branch},{"conditional_branch",schedrow::ControlFlow::ConditionalBranch},{"return",schedrow::ControlFlow::Return},{"indirect_branch",schedrow::ControlFlow::IndirectBranch},{"trap",schedrow::ControlFlow::Trap},{"call",schedrow::ControlFlow::Call}};
      if(!flows.contains(flow))throw Error{Error::Code::InvalidArgument,"unknown target control flow"};instruction.control=flows.at(flow);
      instruction.implicit_defs=numbers(fields,"implicit_defs");instruction.implicit_uses=numbers(fields,"implicit_uses");instruction.issue_slots=numbers(*model,"issue_slots");
      if(model->contains("issue_width")){instruction.issue_width=number(text(*model,"issue_width"));if(!instruction.issue_width)throw Error{Error::Code::InvalidArgument,"zero instruction issue width"};}
      instruction.early_definitions=numbers(fields,"early_definitions");
      if(auto latencies=object(*model,"result_latencies"))for(auto& [index,cycles]:*latencies){auto range=schedrow::metadata::latency(cycles);if(range.minimum==range.maximum)instruction.result_latencies[number(index)]=range.maximum;else instruction.result_latency_ranges[number(index)]=range;}
      if(auto latencies=object(*model,"implicit_result_latencies"))for(auto& [reg,cycles]:*latencies){auto range=schedrow::metadata::latency(cycles);auto id=description.physical_names.contains(reg)?description.physical_names.at(reg):number(reg);if(range.minimum==range.maximum)instruction.implicit_result_latencies[id]=range.maximum;else instruction.implicit_result_latency_ranges[id]=range;}
      if(auto field=model->find("operand_latencies");field!=model->end()){auto array=std::get_if<metacode::Value::Array>(&field->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"operand_latencies must be an array"};for(auto& v:*array){auto timing=std::get_if<Object>(&v.data);if(!timing)throw Error{Error::Code::InvalidArgument,"operand timing must be an object"};known(*timing,{"result","consumer","use","cycles","implicit"},"operand timing");bool implicit=boolean(*timing,"implicit");auto result=text(*timing,"result");auto id=implicit&&description.physical_names.contains(result)?description.physical_names.at(result):number(result);if(!timing->contains("cycles")||text(*timing,"consumer").empty())throw Error{Error::Code::InvalidArgument,"operand timing needs cycles and consumer"};instruction.operand_latencies.push_back({id,text(*timing,"consumer"),number(text(*timing,"use")),schedrow::metadata::latency(timing->at("cycles")),implicit});}}
      if(auto access=object(fields,"access")) {
        known(*access,{"read","write","volatile","atomic","address_space","alias_sets","size","alignment","ordering"},"memory access");
        schedrow::MemoryAccess memory;memory.read=boolean(*access,"read");memory.write=boolean(*access,"write");memory.volatile_access=boolean(*access,"volatile");memory.atomic=boolean(*access,"atomic");memory.address_space=text(*access,"address_space");memory.alias_sets=numbers(*access,"alias_sets");
        if(access->contains("size"))memory.size=number(text(*access,"size"));if(access->contains("alignment"))memory.alignment=number(text(*access,"alignment"));
        auto ordering=text(*access,"ordering");std::map<std::string,schedrow::MemoryOrdering> orderings{{"",schedrow::MemoryOrdering::Relaxed},{"relaxed",schedrow::MemoryOrdering::Relaxed},{"acquire",schedrow::MemoryOrdering::Acquire},{"release",schedrow::MemoryOrdering::Release},{"acq_rel",schedrow::MemoryOrdering::AcquireRelease},{"seq_cst",schedrow::MemoryOrdering::Sequential}};
        if(!orderings.contains(ordering))throw Error{Error::Code::InvalidArgument,"unknown target memory ordering"};memory.ordering=orderings.at(ordering);
        if(memory.alignment&&(memory.alignment&(memory.alignment-1)))throw Error{Error::Code::InvalidArgument,"target memory alignment is not a power of two"};instruction.access=std::move(memory);instruction.memory=true;
      }
      if(auto it=fields.find("ties");it!=fields.end()) {
        auto array=std::get_if<metacode::Value::Array>(&it->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"target ties must be an array"};
        for(auto& value:*array){auto tie=std::get_if<Object>(&value.data);if(!tie||tie->size()!=2||!tie->contains("definition")||!tie->contains("use"))throw Error{Error::Code::InvalidArgument,"target tie needs definition and use indices"};instruction.ties.emplace_back(number(text(*tie,"definition")),number(text(*tie,"use")));}
      }
      if(auto it=model->find("resources");it!=model->end()) {
        auto array=std::get_if<metacode::Value::Array>(&it->second.data);if(!array)throw Error{Error::Code::InvalidArgument,"instruction resources must be an array"};
        for(auto& value:*array) {
          auto reservation=std::get_if<Object>(&value.data);if(!reservation)throw Error{Error::Code::InvalidArgument,"resource reservation must be an object"};
          known(*reservation,{"resource","duration","offset","quantity","alternatives"},"resource reservation");
          schedrow::ResourceUse use;use.resource=text(*reservation,"resource");
          if(reservation->contains("duration"))use.duration=number(text(*reservation,"duration"));
          if(reservation->contains("offset"))use.offset=number(text(*reservation,"offset"));
          if(reservation->contains("quantity"))use.quantity=real(text(*reservation,"quantity"));
          if(auto choices=reservation->find("alternatives");choices!=reservation->end()) {
            auto alternatives=std::get_if<metacode::Value::Array>(&choices->second.data);if(!alternatives)throw Error{Error::Code::InvalidArgument,"resource alternatives must be an array"};for(auto& choice:*alternatives)use.alternatives.push_back(choice.text());
          }
          if(!use.duration||(use.resource.empty()&&use.alternatives.empty()))throw Error{Error::Code::InvalidArgument,"empty resource reservation"};instruction.resources.push_back(std::move(use));
        }
      }
      target.instructions[name]=std::move(instruction);
    }
    auto burs=limeburg::from_umd(description);if(burs)target.burs_rules=std::move(burs.value());
    return Result<PipelineTarget>::ok(std::move(target));
  }catch(const Error& error){return Result<PipelineTarget>::err({error.code,context+": "+error.message});}
}
Result<PipelineTarget> make_target(const metacode::Architecture& architecture) {
  auto description=unisel::from_metacode(architecture);if(!description)return Result<PipelineTarget>::err(description.error());
  auto target=make_target(description.value());if(!target)return target;
  auto codec=bin2bin::from_metacode(architecture);
  if(!codec) {
    if(codec.error().code==Error::Code::Unsupported)return target;
    auto tooling=architecture.fields.find("tooling");
    if(tooling==architecture.fields.end())return target;
    auto fields=std::get_if<metacode::Value::Object>(&tooling->second.data);if(!fields||!fields->contains("bin2bin"))return target;
    return Result<PipelineTarget>::err(codec.error());
  }
  auto bindings=bin2bin::encoding_bindings(architecture);if(!bindings)return Result<PipelineTarget>::err(bindings.error());
  std::map<uint32_t,std::string> names;for(auto& [name,id]:description.value().physical_names)names[id]=name;
  target.value().backend=[codec=std::move(codec.value()),bindings=std::move(bindings.value()),names=std::move(names)](const Module& module)->Result<BackendOutput> {
    const auto& region=module.materialized?module.materialized->region:module.selected;auto allocation=module.materialized?std::optional<regtl::Allocation>(module.materialized->allocation):module.allocation;auto bytes=bin2bin::encode_region(codec,bindings,names,region,module.order,allocation);if(!bytes)return Result<BackendOutput>::err(bytes.error());return Result<BackendOutput>::ok({std::move(bytes.value()),{}});
  };
  return target;
}
Result<PipelineTarget> make_target(const metacode::Architecture& architecture,const bin2bin::CodecAdapter& adapter) {
  auto description=unisel::from_metacode(architecture);if(!description)return Result<PipelineTarget>::err(description.error());
  auto target=make_target(description.value());if(!target)return target;
  auto codec=bin2bin::from_metacode(architecture,adapter);if(!codec)return Result<PipelineTarget>::err(codec.error());
  auto bindings=bin2bin::encoding_bindings(architecture);if(!bindings)return Result<PipelineTarget>::err(bindings.error());
  std::map<uint32_t,std::string> names;for(auto& [name,id]:description.value().physical_names)names[id]=name;
  target.value().backend=[codec=std::move(codec.value()),bindings=std::move(bindings.value()),names=std::move(names)](const Module& module)->Result<BackendOutput> {
    const auto& region=module.materialized?module.materialized->region:module.selected;auto allocation=module.materialized?std::optional<regtl::Allocation>(module.materialized->allocation):module.allocation;
    auto bytes=bin2bin::encode_region(codec,bindings,names,region,module.order,allocation);if(!bytes)return Result<BackendOutput>::err(bytes.error());return Result<BackendOutput>::ok({std::move(bytes.value()),{}});
  };
  return target;
}
}
