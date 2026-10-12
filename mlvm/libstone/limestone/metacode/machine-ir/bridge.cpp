#include "bridge.hpp"
#include "schedrow/slots.hpp"
#include "schedrow/timing_metadata.hpp"
#include <charconv>
#include <cmath>
#include <map>
#include <set>

namespace limestone::machineir_bridge {
namespace {
using Value=metacode::Value;using Object=Value::Object;using Array=Value::Array;
[[noreturn]] void fail(std::string message,Error::Code code=Error::Code::InvalidArgument){throw Error{code,"MachineIR exchange: "+message};}
Value num(uint32_t n){return Value(uint64_t(n));}
Value str(const std::string& s){return Value(s);}
Value real(double n){if(!std::isfinite(n))fail("non-finite scheduling quantity");char buf[64];auto [end,error]=std::to_chars(buf,buf+sizeof(buf),n);if(error!=std::errc{})fail("cannot serialize scheduling quantity");return Value(std::string(buf,end));}
Value ids(const std::vector<uint32_t>& values){Array out;for(auto n:values)out.push_back(num(n));return Value(std::move(out));}
const Object& object(const Value& v){auto p=std::get_if<Object>(&v.data);if(!p)fail("expected object");return *p;}
const Value& get(const Object& o,const std::string& key){auto it=o.find(key);if(it==o.end())fail("missing field: "+key);return it->second;}
const Array& array(const Value& v){auto p=std::get_if<Array>(&v.data);if(!p)fail("expected array");return *p;}
std::string text(const Value& v){auto p=std::get_if<std::string>(&v.data);if(!p)fail("expected string");return *p;}
bool flag(const Value& v){auto p=std::get_if<bool>(&v.data);if(!p)fail("expected Boolean");return *p;}
uint32_t number(const Value& v){uint64_t n=0;if(auto p=std::get_if<uint64_t>(&v.data))n=*p;else if(auto p=std::get_if<int64_t>(&v.data);p&&*p>=0)n=*p;else fail("expected unsigned integer");if(n>UINT32_MAX)fail("integer exceeds 32 bits");return uint32_t(n);}
uint64_t wide_number(const Value& v){if(auto p=std::get_if<uint64_t>(&v.data))return *p;if(auto p=std::get_if<int64_t>(&v.data);p&&*p>=0)return *p;fail("expected unsigned 64-bit integer");}
int64_t signed_number(const Value& v){if(auto p=std::get_if<int64_t>(&v.data))return *p;if(auto p=std::get_if<uint64_t>(&v.data);p&&*p<=INT64_MAX)return int64_t(*p);fail("expected signed 64-bit integer");}
int small_signed(const Value& v){auto n=signed_number(v);if(n<INT32_MIN||n>INT32_MAX)fail("integer exceeds signed 32 bits");return int(n);}
double floating(const Value& v){auto s=text(v);double n;auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);if(error!=std::errc{}||end!=s.data()+s.size()||!std::isfinite(n))fail("invalid scheduling quantity");return n;}
std::vector<uint32_t> numbers(const Value& v){std::vector<uint32_t> out;for(auto& n:array(v))out.push_back(number(n));return out;}
schedrow::LatencyRange range(const Value& v){auto bounds=numbers(v);if(bounds.size()!=2||bounds[0]>bounds[1])fail("invalid latency range");return {bounds[0],bounds[1]};}
const std::vector<std::string> kinds{"true","anti","output","memory","control","ordering"};
const std::vector<std::string> orderings{"relaxed","acquire","release","acq_rel","seq_cst"};
const std::vector<std::string> flows{"none","branch","conditional_branch","return","indirect_branch","trap","call"};
const std::vector<std::string> group_kinds{"ordered","adjacent","same_cycle","bundle","atomic","fusion","pair"};
template<class E> std::string name(E e,const std::vector<std::string>& names){auto n=static_cast<size_t>(e);if(n>=names.size())fail("invalid enum");return names[n];}
template<class E> E enumeration(const Value& v,const std::vector<std::string>& names){auto s=text(v);auto it=std::find(names.begin(),names.end(),s);if(it==names.end())fail("unknown enum: "+s);return static_cast<E>(it-names.begin());}
void known(const Object& o,std::initializer_list<std::string_view> fields){for(auto& [key,v]:o)if(std::find(fields.begin(),fields.end(),key)==fields.end())fail("unknown exchange field: "+key,Error::Code::Unsupported);}
uint32_t identity_key(const std::string& key){uint32_t id;auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),id);if(error!=std::errc{}||end!=key.data()+key.size()||std::to_string(id)!=key)fail("noncanonical identity key: "+key);return id;}
Result<int> validate(const RegionExchange& x) {
  if(x.module.empty()||x.function.empty()||x.target.empty())return Result<int>::err({Error::Code::InvalidArgument,"MachineIR exchange has no identity"});
  auto region=schedrow::validate_region(x.region);if(!region)return region;
  std::set<uint32_t> values,instructions,ordered,scheduled;
  for(auto& v:x.values)if(!values.insert(v.id).second)return Result<int>::err({Error::Code::Conflict,"duplicate exchange value"});
  for(auto& i:x.region.instructions){if(i.opcode.empty()||!instructions.insert(i.id).second)return Result<int>::err({Error::Code::Conflict,"invalid exchange instruction"});for(auto v:i.defs)if(!values.contains(v))return Result<int>::err({Error::Code::NotFound,"unknown exchange definition"});for(auto v:i.uses)if(!values.contains(v))return Result<int>::err({Error::Code::NotFound,"unknown exchange use"});}
  if(x.region.blocks.empty()&&std::any_of(x.region.instructions.begin(),x.region.instructions.end(),[](auto& i){return !i.block_targets.empty();}))return Result<int>::err({Error::Code::Conflict,"exchange branch targets need an explicit CFG"});
  if(!x.region.blocks.empty())for(auto& b:x.region.blocks){auto checked=schedrow::block_region(x.region,b.id);if(!checked)return Result<int>::err(checked.error());for(auto v:b.live_out)if(!values.contains(v))return Result<int>::err({Error::Code::NotFound,"unknown CFG live-out value"});}
  for(auto id:x.order)if(!instructions.contains(id)||!ordered.insert(id).second)return Result<int>::err({Error::Code::Conflict,"invalid exchange order"});
  if(ordered!=instructions)return Result<int>::err({Error::Code::Conflict,"incomplete exchange order"});
  for(auto v:x.outputs)if(!values.contains(v))return Result<int>::err({Error::Code::NotFound,"unknown exchange output"});
  for(auto& d:x.region.deps)if(!instructions.contains(d.producer)||!instructions.contains(d.consumer))return Result<int>::err({Error::Code::NotFound,"unknown exchange dependency"});
  for(auto& s:x.schedule)if(!instructions.contains(s.id)||!scheduled.insert(s.id).second)return Result<int>::err({Error::Code::Conflict,"invalid exchange schedule"});
  if(!scheduled.empty()&&scheduled!=instructions)return Result<int>::err({Error::Code::Conflict,"partial exchange schedule"});
  if(!x.schedule.empty()){std::vector<schedrow::InstrId> issues;for(auto& s:x.schedule)issues.push_back(s.id);auto order=schedrow::verify_order(x.region,issues);if(!order)return order;auto groups=schedrow::verify_groups(x.region,x.schedule);if(!groups)return groups;}
  // Exchange values are the SSA identities exported by the selection pipeline.
  // Values without definitions are explicit region inputs or spill identities.
  using Set=std::set<uint32_t>;std::map<uint32_t,const schedrow::Instruction*> operations;std::map<uint32_t,size_t> positions;
  for(auto& i:x.region.instructions)operations[i.id]=&i;for(size_t k=0;k<x.order.size();++k)positions[x.order[k]]=k;
  auto layout=x.region;layout.instructions.clear();for(auto id:x.order)layout.instructions.push_back(*operations.at(id));region=schedrow::validate_region(layout);if(!region)return region;
  region=schedrow::verify_order(x.region,x.order);if(!region)return region;
  std::map<uint32_t,const schedrow::BasicBlock*> blocks;std::map<uint32_t,Set> predecessors,dominators;Set reachable,all;
  if(x.region.blocks.empty()){all.insert(0);reachable.insert(0);dominators[0]={0};}
  else {
    for(auto& b:x.region.blocks){blocks[b.id]=&b;all.insert(b.id);for(auto s:b.successors)predecessors[s].insert(b.id);}
    std::vector<uint32_t> work{x.region.entry};while(!work.empty()){auto id=work.back();work.pop_back();if(reachable.insert(id).second)for(auto next:blocks.at(id)->successors)work.push_back(next);}
    for(auto id:all)dominators[id]=id==x.region.entry||!reachable.contains(id)?Set{id}:reachable;
    bool changed=true;while(changed){changed=false;for(auto id:reachable)if(id!=x.region.entry){Set next=reachable;for(auto p:predecessors[id])if(reachable.contains(p)){Set intersection;std::set_intersection(next.begin(),next.end(),dominators[p].begin(),dominators[p].end(),std::inserter(intersection,intersection.end()));next=std::move(intersection);}next.insert(id);if(next!=dominators[id]){dominators[id]=std::move(next);changed=true;}}}
  }
  auto block=[&](const schedrow::Instruction& i){return x.region.blocks.empty()?0:i.block;};std::map<uint32_t,const schedrow::Instruction*> definitions;
  for(auto& i:x.region.instructions)for(auto v:i.defs)if(!definitions.emplace(v,&i).second)return Result<int>::err({Error::Code::Conflict,"multiple exchange definitions of v"+std::to_string(v)});
  auto available=[&](uint32_t value,uint32_t target,size_t position){if(!definitions.contains(value))return true;auto& definition=*definitions.at(value);return block(definition)==target?positions.at(definition.id)<position:dominators[target].contains(block(definition));};
  for(auto& i:x.region.instructions)for(auto value:i.uses)if(!available(value,block(i),positions.at(i.id)))return Result<int>::err({Error::Code::Conflict,"exchange definition does not dominate its use"});
  for(auto id:all){if(!blocks.empty())for(auto v:blocks.at(id)->live_out)if(!available(v,id,x.order.size()))return Result<int>::err({Error::Code::Conflict,"exchange value does not dominate a live-out"});if(blocks.empty()||blocks.at(id)->successors.empty())for(auto v:x.outputs)if(!available(v,id,x.order.size()))return Result<int>::err({Error::Code::Conflict,"exchange output does not dominate every exit"});}
  auto hazards=schedrow::dependencies(layout);if(!hazards)return Result<int>::err(hazards.error());std::map<uint32_t,uint32_t> cycles;std::map<uint32_t,size_t> issue_positions;for(size_t k=0;k<x.schedule.size();++k){auto& s=x.schedule[k];cycles[s.id]=s.cycle;issue_positions[s.id]=k;}
  std::set<std::tuple<uint32_t,uint32_t,uint32_t>> slots;
  for(auto& s:x.schedule){auto& i=*operations.at(s.id);auto assigned=schedrow::detail::assigned_slots(s);if((!s.slot&&!s.additional_slots.empty())||(!assigned.empty()&&assigned.size()!=i.issue_width))return Result<int>::err({Error::Code::Conflict,"incomplete exchange issue-slot assignment"});for(auto slot:assigned)if((!i.issue_slots.empty()&&std::find(i.issue_slots.begin(),i.issue_slots.end(),slot)==i.issue_slots.end())||!slots.emplace(block(i),s.cycle,slot).second)return Result<int>::err({Error::Code::Conflict,"invalid exchange issue-slot assignment"});if(!s.slot&&(!i.issue_slots.empty()||i.issue_width>1))return Result<int>::err({Error::Code::Conflict,"missing exchange issue-slot assignment"});if(!s.resources.empty()){if(s.resources.size()!=i.resources.size())return Result<int>::err({Error::Code::Conflict,"incomplete exchange resource assignment"});for(size_t k=0;k<s.resources.size();++k){auto& r=i.resources[k];if(r.alternatives.empty()?s.resources[k]!=r.resource:std::find(r.alternatives.begin(),r.alternatives.end(),s.resources[k])==r.alternatives.end())return Result<int>::err({Error::Code::Conflict,"invalid exchange resource assignment"});}}}
    for(auto& d:hazards.value().deps)if(!d.distance){auto& a=*operations.at(d.producer);auto& b=*operations.at(d.consumer);if(block(a)==block(b)){if(positions.at(a.id)>=positions.at(b.id))return Result<int>::err({Error::Code::Conflict,"exchange order violates a dependency"});if(!cycles.empty()){if(uint64_t(cycles.at(a.id))+d.latency>cycles.at(b.id))return Result<int>::err({Error::Code::Conflict,"exchange schedule violates dependency latency"});if(issue_positions.at(a.id)>=issue_positions.at(b.id))return Result<int>::err({Error::Code::Conflict,"exchange issue order violates a dependency"});}}else if(!dominators[block(b)].contains(block(a)))return Result<int>::err({Error::Code::Conflict,"exchange dependency crosses unrelated CFG paths"});}
  if(x.allocation){std::set<uint32_t> assigned;for(auto [v,r]:x.allocation->regs){if(!values.contains(v))return Result<int>::err({Error::Code::NotFound,"unknown allocated exchange value"});assigned.insert(v);}for(auto v:x.allocation->spilled)if(!values.contains(v)||!assigned.insert(v).second)return Result<int>::err({Error::Code::Conflict,"invalid exchange spill"});}
  std::set<uint32_t> slotted;for(size_t k=0;k<x.spill_slots.size();++k){auto& s=x.spill_slots[k];if(!values.contains(s.value)||!slotted.insert(s.value).second||s.klass.empty()||!s.size||!s.alignment||(s.alignment&(s.alignment-1))||s.offset%s.alignment||s.offset>x.frame_size||s.size>x.frame_size-s.offset)return Result<int>::err({Error::Code::Conflict,"invalid exchange spill slot"});for(size_t j=0;j<k;++j){auto& t=x.spill_slots[j];if(s.offset<t.offset+t.size&&t.offset<s.offset+s.size)return Result<int>::err({Error::Code::Conflict,"overlapping exchange spill slots"});}}
  return Result<int>::ok(0);
}
}
Result<std::string> serialize(const RegionExchange& x) {
  auto valid=validate(x);if(!valid)return Result<std::string>::err(valid.error());
  try {
    bool sourced=std::any_of(x.region.instructions.begin(),x.region.instructions.end(),[](auto& i){return !i.source_metadata.empty();});bool cfg=!x.region.blocks.empty();bool grouped=!x.region.groups.empty();bool multi_slot=std::any_of(x.region.instructions.begin(),x.region.instructions.end(),[](auto& i){return i.issue_width!=1;});bool timed=std::any_of(x.region.instructions.begin(),x.region.instructions.end(),[](auto& i){return i.latency_range||i.memory_latency||!i.result_latency_ranges.empty()||!i.implicit_result_latency_ranges.empty()||!i.operand_latencies.empty();})||std::any_of(x.region.deps.begin(),x.region.deps.end(),[](auto& d){return bool(d.latency_range);});bool extended=sourced||timed||multi_slot||grouped||cfg||x.frame_size||!x.spill_slots.empty()||std::any_of(x.region.instructions.begin(),x.region.instructions.end(),[](auto& i){return i.control!=schedrow::ControlFlow::None||!i.block_targets.empty()||!i.implicit_result_latency.empty();});
    Array values,operations,edges,schedule;auto sorted=x.values;std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.id<b.id;});
    for(auto& v:sorted)values.emplace_back(Object{{"id",num(v.id)},{"type",str(v.type)},{"class",str(v.register_class)}});
    for(auto id:x.order) {
      auto& i=*std::find_if(x.region.instructions.begin(),x.region.instructions.end(),[&](auto& n){return n.id==id;});Array immediates,ties,resources;Object result_latency,pressure;
      for(auto [v,n]:i.immediates)immediates.emplace_back(Object{{"value",num(v)},{"integer",Value(n)}});
      for(auto [a,b]:i.ties)ties.emplace_back(Array{num(a),num(b)});
      for(auto& r:i.resources){Array alternatives;for(auto& a:r.alternatives)alternatives.push_back(str(a));resources.emplace_back(Object{{"resource",str(r.resource)},{"duration",num(r.duration)},{"quantity",real(r.quantity)},{"offset",num(r.offset)},{"alternatives",Value(std::move(alternatives))}});}
      for(auto [v,n]:i.result_latency)result_latency[std::to_string(v)]=num(n);for(auto& [klass,n]:i.pressure_delta)pressure[klass]=Value(int64_t(n));
      Object o{{"id",num(i.id)},{"opcode",str(i.opcode)},{"defs",ids(i.defs)},{"uses",ids(i.uses)},{"immediates",Value(std::move(immediates))},{"origin",str(i.origin)},
        {"implicit_defs",ids(i.implicit_defs)},{"implicit_uses",ids(i.implicit_uses)},{"barrier",Value(i.barrier)},{"call",Value(i.call)},{"terminator",Value(i.terminator)},{"may_trap",Value(i.may_trap)},
        {"speculative",Value(i.speculative)},{"memory",Value(i.memory)},{"latency",num(i.latency)},{"semantic_class",str(i.semantic_class)},{"opcode_class",str(i.opcode_class)},{"early_defs",ids(i.early_defs)},{"ties",Value(std::move(ties))},
        {"throughput",real(i.throughput)},{"resources",Value(std::move(resources))},{"issue_slots",ids(i.issue_slots)},{"priority",Value(int64_t(i.priority))},{"pressure_delta",Value(std::move(pressure))},{"result_latency",Value(std::move(result_latency))}};
      if(extended){o["block"]=num(i.block);o["block_targets"]=ids(i.block_targets);o["control"]=str(name(i.control,flows));}
      if(multi_slot)o["issue_width"]=num(i.issue_width);
      if(i.latency_range)o["latency_range"]=schedrow::metadata::value(*i.latency_range);
      if(i.memory_latency)o["memory_latency"]=schedrow::metadata::value(*i.memory_latency);
      for(auto field:{std::pair{"result_latency_ranges",&i.result_latency_ranges},std::pair{"implicit_result_latency_ranges",&i.implicit_result_latency_ranges}})if(!field.second->empty()){Object ranges;for(auto [value,range]:*field.second)ranges[std::to_string(value)]=schedrow::metadata::value(range);o[field.first]=Value(std::move(ranges));}
      if(!i.operand_latencies.empty()){Array timings;for(auto& t:i.operand_latencies)timings.emplace_back(Object{{"result",num(t.result)},{"consumer",str(t.consumer_opcode)},{"use",num(t.use_index)},{"cycles",schedrow::metadata::value(t.cycles)},{"implicit",Value(t.implicit)}});o["operand_latencies"]=Value(std::move(timings));}
      if(!i.implicit_result_latency.empty()){Object latencies;for(auto [v,n]:i.implicit_result_latency)latencies[std::to_string(v)]=num(n);o["implicit_result_latency"]=Value(std::move(latencies));}
      Object classes;for(auto& [value,klass]:i.register_classes)classes[std::to_string(value)]=str(klass);if(!classes.empty())o["register_classes"]=Value(std::move(classes));
      if(!i.source_metadata.empty()){Object sources;for(auto& [id,metadata]:i.source_metadata)sources[std::to_string(id)]=metacode::operand_metadata(metadata);o["source_metadata"]=Value(std::move(sources));}
      if(i.access){auto& m=*i.access;o["access"]=Value(Object{{"read",Value(m.read)},{"write",Value(m.write)},{"volatile",Value(m.volatile_access)},{"atomic",Value(m.atomic)},{"ordering",str(name(m.ordering,orderings))},{"address_space",str(m.address_space)},{"alias_sets",ids(m.alias_sets)},{"size",num(m.size)},{"alignment",num(m.alignment)}});}
      operations.emplace_back(std::move(o));
    }
    for(auto& e:x.region.deps){Object edge{{"producer",num(e.producer)},{"consumer",num(e.consumer)},{"kind",str(name(e.kind,kinds))},{"latency",num(e.latency)},{"distance",num(e.distance)},{"scheduler_only",Value(e.scheduler_only)}};if(e.latency_range)edge["latency_range"]=schedrow::metadata::value(*e.latency_range);edges.emplace_back(std::move(edge));}
    for(auto& s:x.schedule){Object o{{"id",num(s.id)},{"cycle",num(s.cycle)}};if(s.slot)o["slot"]=num(*s.slot);if(multi_slot)o["additional_slots"]=ids(s.additional_slots);Array resources;for(auto& r:s.resources)resources.push_back(str(r));o["resources"]=Value(std::move(resources));schedule.emplace_back(std::move(o));}
    Object root{{"schema",Value(std::string("limestone.machineir.region"))},{"version",num(sourced?6:timed?5:multi_slot?4:grouped?3:extended?2:1)},{"module",str(x.module)},{"target",str(x.target)},{"function",str(x.function)},{"region",str(x.region.name)},
      {"values",Value(std::move(values))},{"instructions",Value(std::move(operations))},{"dependencies",Value(std::move(edges))},{"schedule",Value(std::move(schedule))},{"outputs",ids(x.outputs)}};
    if(cfg){Array blocks;for(auto& b:x.region.blocks)blocks.emplace_back(Object{{"id",num(b.id)},{"name",str(b.name)},{"successors",ids(b.successors)},{"live_out",ids(b.live_out)}});root["blocks"]=Value(std::move(blocks));root["entry"]=num(x.region.entry);}
    if(grouped){Array groups;for(auto& g:x.region.groups)groups.emplace_back(Object{{"id",num(g.id)},{"kind",str(name(g.kind,group_kinds))},{"members",ids(g.members)},{"name",str(g.name)},{"origin",str(g.origin)},{"pattern",str(g.pattern)},{"benefit",Value(int64_t(g.benefit))},{"issue_width",num(g.issue_width)},{"issue_slots",ids(g.issue_slots)}});root["groups"]=Value(std::move(groups));}
    if(x.allocation){Array regs;std::map<uint32_t,uint32_t> sorted_regs(x.allocation->regs.begin(),x.allocation->regs.end());for(auto [v,r]:sorted_regs)regs.emplace_back(Array{num(v),num(r)});root["allocation"]=Value(Object{{"registers",Value(std::move(regs))},{"spilled",ids(x.allocation->spilled)}});}
    if(x.frame_size||!x.spill_slots.empty()){Array slots;for(auto& s:x.spill_slots)slots.emplace_back(Object{{"value",num(s.value)},{"class",str(s.klass)},{"offset",Value(s.offset)},{"size",num(s.size)},{"alignment",num(s.alignment)}});root["spill_frame"]=Value(Object{{"size",Value(x.frame_size)},{"slots",Value(std::move(slots))}});}
    auto output=metacode::print_json(Value(std::move(root)));if(output&&output.value().size()>4*1024*1024)return Result<std::string>::err({Error::Code::ResourceLimit,"MachineIR exchange: JSON output size limit"});return output;
  }catch(const Error& e){return Result<std::string>::err(e);}
}
Result<RegionExchange> deserialize(std::string_view source,std::string_view file) {
  if(source.size()>4*1024*1024)return Result<RegionExchange>::err({Error::Code::ResourceLimit,"MachineIR exchange: JSON input size limit"});
  auto parsed=metacode::parse_json(source,file);if(!parsed)return Result<RegionExchange>::err(parsed.error());
  try {
    auto& root=object(parsed.value());known(root,{"schema","version","module","target","function","region","values","instructions","dependencies","schedule","outputs","allocation","blocks","entry","spill_frame","groups"});auto version=number(get(root,"version"));if(text(get(root,"schema"))!="limestone.machineir.region"||(version<1||version>6))fail("unsupported schema version",Error::Code::Unsupported);
    if(root.contains("entry")&&!root.contains("blocks"))fail("entry needs a CFG");
    RegionExchange x;x.module=text(get(root,"module"));x.target=text(get(root,"target"));x.function=text(get(root,"function"));x.region.name=text(get(root,"region"));x.outputs=numbers(get(root,"outputs"));
    if(root.contains("blocks")){if(version<2)fail("CFG needs exchange version 2");x.region.entry=number(get(root,"entry"));for(auto& v:array(root.at("blocks"))){auto& b=object(v);known(b,{"id","name","successors","live_out"});x.region.blocks.push_back({number(get(b,"id")),text(get(b,"name")),numbers(get(b,"successors")),numbers(get(b,"live_out"))});}}
    for(auto& v:array(get(root,"values"))){auto& o=object(v);known(o,{"id","type","class"});x.values.push_back({number(get(o,"id")),text(get(o,"type")),text(get(o,"class"))});}
    for(auto& v:array(get(root,"instructions"))) {
      auto& o=object(v);known(o,{"id","opcode","defs","uses","immediates","origin","latency","implicit_defs","implicit_uses","barrier","call","terminator","may_trap","memory","speculative","semantic_class","opcode_class","early_defs","ties","throughput","resources","issue_slots","priority","pressure_delta","result_latency","access","block","block_targets","control","register_classes","implicit_result_latency","issue_width","latency_range","memory_latency","result_latency_ranges","implicit_result_latency_ranges","operand_latencies","source_metadata"});
      schedrow::Instruction i{};i.id=number(get(o,"id"));i.opcode=text(get(o,"opcode"));i.defs=numbers(get(o,"defs"));i.uses=numbers(get(o,"uses"));i.origin=text(get(o,"origin"));i.latency=number(get(o,"latency"));
      i.implicit_defs=numbers(get(o,"implicit_defs"));i.implicit_uses=numbers(get(o,"implicit_uses"));i.barrier=flag(get(o,"barrier"));i.call=flag(get(o,"call"));i.terminator=flag(get(o,"terminator"));i.may_trap=flag(get(o,"may_trap"));i.memory=flag(get(o,"memory"));i.speculative=flag(get(o,"speculative"));i.semantic_class=text(get(o,"semantic_class"));i.opcode_class=text(get(o,"opcode_class"));i.early_defs=numbers(get(o,"early_defs"));i.throughput=floating(get(o,"throughput"));i.issue_slots=numbers(get(o,"issue_slots"));i.priority=small_signed(get(o,"priority"));
      if(version>=2){i.block=number(get(o,"block"));i.block_targets=numbers(get(o,"block_targets"));i.control=enumeration<schedrow::ControlFlow>(get(o,"control"),flows);}else if(o.contains("block")||o.contains("block_targets")||o.contains("control"))fail("control-flow fields need exchange version 2");
      if(o.contains("register_classes"))for(auto& [key,klass]:object(o.at("register_classes")))i.register_classes[identity_key(key)]=text(klass);
      if(o.contains("source_metadata")){if(version<6)fail("source metadata needs exchange version 6");for(auto& [key,value]:object(o.at("source_metadata"))){auto metadata=metacode::load_operand_metadata(value);if(!metadata)return Result<RegionExchange>::err(metadata.error());i.source_metadata[identity_key(key)]=std::move(metadata.value());}}
      if(o.contains("issue_width")){if(version<4)fail("instruction issue width needs exchange version 4");i.issue_width=number(o.at("issue_width"));}
      for(auto& [key,n]:object(get(o,"result_latency")))i.result_latency[identity_key(key)]=number(n);for(auto& [key,n]:object(get(o,"pressure_delta")))i.pressure_delta[key]=small_signed(n);
      if(o.contains("implicit_result_latency")){if(version<2)fail("implicit result latency needs exchange version 2");for(auto& [key,n]:object(o.at("implicit_result_latency")))i.implicit_result_latency[identity_key(key)]=number(n);}
      for(auto field:{std::pair{"latency_range",&i.latency_range},std::pair{"memory_latency",&i.memory_latency}})if(o.contains(field.first)){if(version<5)fail("timing ranges need exchange version 5");*field.second=range(o.at(field.first));}
      for(auto field:{std::pair{"result_latency_ranges",&i.result_latency_ranges},std::pair{"implicit_result_latency_ranges",&i.implicit_result_latency_ranges}})if(o.contains(field.first)){if(version<5)fail("result timing ranges need exchange version 5");for(auto& [key,value]:object(o.at(field.first)))(*field.second)[identity_key(key)]=range(value);}
      if(o.contains("operand_latencies")){if(version<5)fail("operand timing needs exchange version 5");for(auto& value:array(o.at("operand_latencies"))){auto& t=object(value);known(t,{"result","consumer","use","cycles","implicit"});i.operand_latencies.push_back({number(get(t,"result")),text(get(t,"consumer")),number(get(t,"use")),range(get(t,"cycles")),flag(get(t,"implicit"))});}}
      for(auto& n:array(get(o,"resources"))){auto& r=object(n);known(r,{"resource","duration","quantity","offset","alternatives"});schedrow::ResourceUse use{text(get(r,"resource")),number(get(r,"duration")),floating(get(r,"quantity")),number(get(r,"offset"))};for(auto& a:array(get(r,"alternatives")))use.alternatives.push_back(text(a));i.resources.push_back(std::move(use));}
      for(auto& n:array(get(o,"immediates"))){auto& imm=object(n);known(imm,{"value","integer"});i.immediates.emplace_back(number(get(imm,"value")),signed_number(get(imm,"integer")));}
      for(auto& n:array(get(o,"ties"))){auto& tie=array(n);if(tie.size()!=2)fail("tie must have two values");i.ties.emplace_back(number(tie[0]),number(tie[1]));}
      if(o.contains("access")){auto& a=object(o.at("access"));known(a,{"read","write","volatile","atomic","ordering","address_space","alias_sets","size","alignment"});i.access=schedrow::MemoryAccess{flag(get(a,"read")),flag(get(a,"write")),flag(get(a,"volatile")),flag(get(a,"atomic")),enumeration<schedrow::MemoryOrdering>(get(a,"ordering"),orderings),text(get(a,"address_space")),numbers(get(a,"alias_sets")),number(get(a,"size")),number(get(a,"alignment"))};}
      x.order.push_back(i.id);x.region.instructions.push_back(std::move(i));
    }
    for(auto& v:array(get(root,"dependencies"))){auto& d=object(v);known(d,{"producer","consumer","kind","latency","distance","scheduler_only","latency_range"});schedrow::Dependency edge{number(get(d,"producer")),number(get(d,"consumer")),enumeration<schedrow::DepKind>(get(d,"kind"),kinds),number(get(d,"latency")),number(get(d,"distance")),flag(get(d,"scheduler_only"))};if(d.contains("latency_range")){if(version<5)fail("dependency ranges need exchange version 5");edge.latency_range=range(d.at("latency_range"));}x.region.deps.push_back(edge);}
    for(auto& v:array(get(root,"schedule"))){auto& s=object(v);known(s,{"id","cycle","slot","resources","additional_slots"});schedrow::Scheduled entry{number(get(s,"id")),number(get(s,"cycle"))};if(s.contains("slot"))entry.slot=number(s.at("slot"));if(s.contains("additional_slots")){if(version<4)fail("additional issue slots need exchange version 4");entry.additional_slots=numbers(s.at("additional_slots"));}for(auto& r:array(get(s,"resources")))entry.resources.push_back(text(r));x.schedule.push_back(std::move(entry));}
    if(root.contains("allocation")){auto& a=object(root.at("allocation"));known(a,{"registers","spilled"});x.allocation.emplace();for(auto& v:array(get(a,"registers"))){auto& r=array(v);if(r.size()!=2||!x.allocation->regs.emplace(number(r[0]),number(r[1])).second)fail("invalid register assignment");}x.allocation->spilled=numbers(get(a,"spilled"));}
    if(root.contains("spill_frame")){if(version<2)fail("spill frames need exchange version 2");auto& f=object(root.at("spill_frame"));known(f,{"size","slots"});x.frame_size=wide_number(get(f,"size"));for(auto& v:array(get(f,"slots"))){auto& s=object(v);known(s,{"value","class","offset","size","alignment"});x.spill_slots.push_back({number(get(s,"value")),text(get(s,"class")),wide_number(get(s,"offset")),number(get(s,"size")),number(get(s,"alignment"))});}}
    if(root.contains("groups")){if(version<3)fail("groups need exchange version 3");for(auto& v:array(root.at("groups"))){auto& g=object(v);known(g,{"id","kind","members","name","origin","pattern","benefit","issue_width","issue_slots"});x.region.groups.push_back({number(get(g,"id")),enumeration<schedrow::GroupKind>(get(g,"kind"),group_kinds),numbers(get(g,"members")),text(get(g,"name")),text(get(g,"origin")),text(get(g,"pattern")),small_signed(get(g,"benefit")),number(get(g,"issue_width")),numbers(get(g,"issue_slots"))});}}
    auto valid=validate(x);if(!valid)return Result<RegionExchange>::err(valid.error());return Result<RegionExchange>::ok(std::move(x));
  }catch(const Error& e){return Result<RegionExchange>::err(e);}
}
}
