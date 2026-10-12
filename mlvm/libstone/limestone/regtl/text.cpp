#include "text.hpp"
#include "parsers/regtl_ast.hpp"
#include "parsers/semantic.hpp"
#include <set>
#include <cmath>
#include <limits>

namespace limestone::regtl {
namespace {
namespace ast=syntax::regtl;
namespace sem=syntax::semantic;
using Object=sem::Object;
TransferOperand operand(const ast::Operand& syntax,const sem::Names& values,const sem::Names& physical,const std::map<std::string,uint32_t>& slots) {
  return std::visit([&](const auto& p)->TransferOperand{using T=std::remove_cvref_t<decltype(*p)>;using Kind=TransferOperand::Kind;
    if constexpr(std::is_same_v<T,ast::Virtual>)return {Kind::Virtual,values.at(p->value)};
    else if constexpr(std::is_same_v<T,ast::Physical>)return {Kind::Physical,physical.at(p->value)};
    else if constexpr(std::is_same_v<T,ast::Immediate>)return {Kind::Immediate,0,sem::integer(p->literal->value)};
    else if constexpr(std::is_same_v<T,ast::Memory>)return {Kind::Memory,0,0,{operand(*p->address,values,physical,slots)}};
    else {auto name=sem::spelling(*p->name);if(!slots.contains(name))sem::fail(*p,"unknown spill storage: "+name,Error::Code::NotFound);return {Kind::Spill,slots.at(name)};}
  },syntax.value);
}
void reads(Instruction& i,const TransferOperand& o) {
  if(o.kind==TransferOperand::Kind::Virtual)i.uses.push_back(o.id);
  else if(o.kind==TransferOperand::Kind::Physical)i.physical_uses.push_back(o.id);
  else if(o.kind==TransferOperand::Kind::Memory)for(auto& a:o.address)reads(i,a);
}
void writes(Instruction& i,const TransferOperand& o) {
  if(o.kind==TransferOperand::Kind::Virtual)i.defs.push_back(o.id);
  else if(o.kind==TransferOperand::Kind::Physical)i.physical_defs.push_back(o.id);
  else if(o.kind==TransferOperand::Kind::Memory)for(auto& a:o.address)reads(i,a);
  else if(o.kind==TransferOperand::Kind::Immediate)sem::fail("an immediate is not writable storage");
}
std::vector<uint32_t> references(const sem::Value& value,const sem::Names& names) {
  std::vector<uint32_t> out;for(auto& v:sem::array(value))out.push_back(names.at(v.text()));return out;
}
std::string refs(std::span<const uint32_t> ids,char prefix) {
  std::string out="[";for(auto id:ids){if(out.size()>1)out+=", ";out+=prefix+std::to_string(id);}return out+"]";
}
std::string print_operand(const TransferOperand& o,const AllocationUnit& unit) {
  switch(o.kind){case TransferOperand::Kind::Virtual:return "%"+std::to_string(o.id);case TransferOperand::Kind::Physical:return "$"+std::to_string(o.id);case TransferOperand::Kind::Immediate:return "#"+std::to_string(o.immediate);case TransferOperand::Kind::Spill:{auto slot=std::find_if(unit.slots.begin(),unit.slots.end(),[&](auto& s){return s.id==o.id;});if(slot==unit.slots.end())sem::fail("unknown transfer spill storage");return "slot("+sem::quote(slot->name)+")";}case TransferOperand::Kind::Memory:if(o.address.size()!=1)sem::fail("memory transfer needs one address expression");return "["+print_operand(o.address[0],unit)+"]";}sem::fail("unknown transfer operand");
}
double cost(const sem::Value& value) {
  auto text=value.text();double number=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),number);
  if(error!=std::errc{}||end!=text.data()+text.size()||!std::isfinite(number)||number<0)sem::fail("allocation cost must be finite and nonnegative");return number;
}
sem::Value cost_value(double cost) {
  char text[64];auto [end,error]=std::to_chars(text,text+sizeof(text),cost,std::chars_format::general,std::numeric_limits<double>::max_digits10);
  if(error!=std::errc{})sem::fail("allocation cost cannot be printed");return sem::Value(std::string(text,end));
}
void fields(const Object& object,std::initializer_list<std::string_view> allowed) {
  for(auto& [key,value]:object)if(std::find(allowed.begin(),allowed.end(),key)==allowed.end())sem::fail("unsupported PBQP field: "+key,Error::Code::Unsupported);
}
}
Result<std::vector<AllocationUnit>> load_regtl(std::string_view source,std::string_view file) {
  auto parsed=ast::parse(source,file);if(!parsed)return Result<std::vector<AllocationUnit>>::err(parsed.error());
  try {
    std::vector<AllocationUnit> units;std::set<std::string> unit_names;
    for(auto& syntax:parsed.value()->units) {
      AllocationUnit unit;unit.name=sem::spelling(*syntax->name);if(!unit_names.insert(unit.name).second)sem::fail(*syntax,"duplicate allocation unit",Error::Code::Conflict);
      std::set<std::string> preg,vreg;std::vector<const ast::LiveRange*> ranges;std::vector<const ast::Function*> functions;std::vector<const ast::RegisterAlias*> aliases;std::vector<const ast::Clobber*> clobbers;std::map<std::string,uint32_t> slots;
      for(auto& declaration:syntax->declarations)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;
        if constexpr(std::is_same_v<T,ast::RegisterClass>){for(auto& r:p->members->registers)preg.insert(r->value);}
        else if constexpr(std::is_same_v<T,ast::LiveRange>){if(!vreg.insert(p->reference->value).second)sem::fail(*p,"duplicate virtual register",Error::Code::Conflict);ranges.push_back(p.get());}
        else if constexpr(std::is_same_v<T,ast::RegisterAlias>)aliases.push_back(p.get());
        else if constexpr(std::is_same_v<T,ast::Clobber>)clobbers.push_back(p.get());
        else if constexpr(std::is_same_v<T,ast::Function>)functions.push_back(p.get());
        else if constexpr(std::is_same_v<T,ast::SpillSlot>){auto name=sem::spelling(*p->name);if(!slots.emplace(name,uint32_t(unit.slots.size())).second)sem::fail(*p,"duplicate spill storage",Error::Code::Conflict);auto fields=sem::attributes(p->attributes);auto size=sem::number(sem::required(fields,"size").text()),alignment=sem::number(sem::required(fields,"alignment").text());if(!size||!alignment||(alignment&(alignment-1)))sem::fail(*p,"invalid spill size/alignment");unit.slots.push_back({name,uint32_t(unit.slots.size()),size,alignment,std::move(fields)});}
        else if(!unit.metadata.emplace(sem::spelling(*p->name),sem::attribute(*p)).second)sem::fail(*p,"duplicate allocation metadata",Error::Code::Conflict);
      },declaration->value);
      sem::Names physical,values;physical.assign(preg);values.assign(vreg);unit.physical_names=physical.ids;unit.virtual_names=values.ids;
      for(auto& declaration:syntax->declarations)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;if constexpr(std::is_same_v<T,ast::RegisterClass>){RegClass c;c.name=sem::spelling(*p->name);for(auto& r:p->members->registers)c.members.push_back(physical.at(r->value));unit.problem.classes.push_back(std::move(c));}},declaration->value);
      for(auto p:aliases)unit.problem.aliases.emplace_back(physical.at(p->first->value),physical.at(p->second->value));
      for(auto p:clobbers){Clobber c;c.position=sem::number(p->position->value);for(auto& r:p->registers->registers)c.registers.push_back(physical.at(r->value));unit.problem.clobbers.push_back(std::move(c));}
      if(unit.metadata.contains("storage"))for(auto& v:sem::array(unit.metadata.at("storage"))){auto& fields=sem::object(v);RegisterStorage storage;storage.id=physical.at(sem::required(fields,"register").text());storage.bank=sem::text(fields,"bank");if(fields.contains("slices"))for(auto& part:sem::array(fields.at("slices"))){auto& s=sem::object(part);storage.slices.push_back({sem::number(sem::required(s,"unit").text()),sem::number(sem::required(s,"begin").text()),sem::number(sem::required(s,"width").text())});}unit.problem.storage.push_back(std::move(storage));}
      if(unit.metadata.contains("tuples"))for(auto& v:sem::array(unit.metadata.at("tuples"))){auto& fields=sem::object(v);RegisterTuple tuple;tuple.values=references(sem::required(fields,"values"),values);for(auto& a:sem::array(sem::required(fields,"alternatives")))tuple.alternatives.push_back(references(a,physical));unit.problem.tuples.push_back(std::move(tuple));}
      auto constraints=[&](const auto& list,Constraint& constraint,bool& spillable,Object& metadata){std::set<std::string> seen;
        for(auto& entry:list)std::visit([&](const auto& p){using T=std::remove_cvref_t<decltype(*p)>;std::string key;
          if constexpr(std::is_same_v<T,ast::Allowed>||std::is_same_v<T,ast::Forbidden>){std::vector<uint32_t> regs;for(auto& r:p->registers->registers)regs.push_back(physical.at(r->value));if constexpr(std::is_same_v<T,ast::Allowed>){key="allowed";if(regs.empty())sem::fail(*p,"empty allowed-register set",Error::Code::Unsatisfiable);constraint.allowed=std::move(regs);}else {key="forbidden";constraint.forbidden=std::move(regs);}}
          else if constexpr(std::is_same_v<T,ast::Fixed>){key="fixed";constraint.fixed=physical.at(p->reference->value);}
          else if constexpr(std::is_same_v<T,ast::Spillable>){key="spillable";spillable=sem::boolean(p->enabled->value);}
          else {key=sem::spelling(*p->name);auto value=sem::attribute(*p);if(key=="allowed"||key=="forbidden"){auto regs=references(value,physical);if(key=="allowed"){if(regs.empty())sem::fail(*p,"empty allowed-register set",Error::Code::Unsatisfiable);constraint.allowed=std::move(regs);}else constraint.forbidden=std::move(regs);}else if(key=="fixed")constraint.fixed=physical.at(value.text());else if(key=="spillable")spillable=sem::boolean(value.text());else if(key=="bank")constraint.bank=value.text();else {for(auto unsupported:{"register_pair","register_tuple","subregister","width","alignment","rematerialize"})if(key==unsupported)sem::fail(*p,"allocation constraint requires a target adapter: "+key,Error::Code::Unsupported);metadata[key]=std::move(value);}}
          if(!seen.insert(key).second)sem::fail(*p,"duplicate allocation constraint: "+key,Error::Code::Conflict);
        },entry->value);
      };
      for(auto p:ranges){LiveRange r{values.at(p->reference->value),sem::number(p->begin->value),sem::number(p->end->value),sem::spelling(*p->register_class)};constraints(p->constraints,r.constraint,r.spillable,unit.value_metadata[r.value]);unit.problem.ranges.push_back(std::move(r));}
      if(unit.metadata.contains("reserved"))unit.problem.reserved=references(unit.metadata.at("reserved"),physical);
      for(auto key:{"ties","interference"})if(unit.metadata.contains(key))for(auto& pair:sem::array(unit.metadata.at(key))){auto& a=sem::array(pair);if(a.size()!=2)sem::fail("allocation edge needs two virtual values");auto edge=std::pair{values.at(a[0].text()),values.at(a[1].text())};if(std::string_view(key)=="ties")unit.problem.ties.push_back(edge);else {unit.problem.explicit_interference=true;unit.problem.interference.push_back(edge);}}
      if(unit.metadata.contains("explicit_interference"))unit.problem.explicit_interference=sem::boolean(unit.metadata.at("explicit_interference").text());
      if(unit.metadata.contains("pbqp")) {
        auto& policy=unit.pbqp;auto& config=sem::object(unit.metadata.at("pbqp"));fields(config,{"default_spill_cost","values","coalescing","search_limit","cell_limit","work_limit"});
        if(config.contains("default_spill_cost"))policy.costs.default_spill_cost=cost(config.at("default_spill_cost"));
        for(auto [key,target]:{std::pair{"search_limit",&policy.search_limit},{"cell_limit",&policy.cell_limit},{"work_limit",&policy.work_limit}})if(config.contains(key)){auto value=sem::wide(config.at(key).text());if(value>SIZE_MAX)sem::fail("PBQP limit exceeds host size");*target=size_t(value);}
        if(config.contains("values"))for(auto& entry:sem::array(config.at("values"))) {
          auto& config=sem::object(entry);fields(config,{"value","spill_cost","registers"});UnaryCost unary{values.at(sem::required(config,"value").text()),policy.costs.default_spill_cost,{}};
          if(config.contains("spill_cost"))unary.spill_cost=cost(config.at("spill_cost"));
          if(config.contains("registers"))for(auto& entry:sem::array(config.at("registers"))){auto& config=sem::object(entry);fields(config,{"register","cost"});unary.registers.emplace_back(physical.at(sem::required(config,"register").text()),cost(sem::required(config,"cost")));}
          policy.costs.values.push_back(std::move(unary));
        }
        if(config.contains("coalescing"))for(auto& entry:sem::array(config.at("coalescing"))){auto& config=sem::object(entry);fields(config,{"first","second","cost"});policy.costs.coalescing.push_back({values.at(sem::required(config,"first").text()),values.at(sem::required(config,"second").text()),cost(sem::required(config,"cost"))});}
        auto valid=validate_costs(unit.problem,policy.costs);if(!valid)return Result<std::vector<AllocationUnit>>::err(valid.error());
      }
      auto valid=validate(unit.problem);if(!valid)return Result<std::vector<AllocationUnit>>::err(valid.error());std::set<std::string> function_names;
      for(auto p:functions){TextFunction function;function.name=sem::spelling(*p->name);if(!function_names.insert(function.name).second)sem::fail(*p,"duplicate allocation function",Error::Code::Conflict);auto& f=function.function;f.classes=unit.problem.classes;f.aliases=unit.problem.aliases;f.reserved=unit.problem.reserved;for(auto& r:unit.problem.ranges)f.values.push_back({r.value,r.klass,r.constraint,r.spillable});std::set<std::string> block_names;for(auto& b:p->blocks)if(!block_names.insert(sem::spelling(*b->name)).second)sem::fail(*b,"duplicate allocation block",Error::Code::Conflict);sem::Names blocks;blocks.assign(block_names);uint32_t next_instruction=0;
        f.storage=unit.problem.storage;f.tuples=unit.problem.tuples;
        for(auto& b:p->blocks){Block block{blocks.at(sem::spelling(*b->name))};auto& metadata=function.block_metadata[block.id];
          for(auto& transfer:b->transfers)std::visit([&](const auto& t){using T=std::remove_cvref_t<decltype(*t)>;
            if constexpr(std::is_same_v<T,ast::Successor>)block.successors.push_back(blocks.at(sem::spelling(*t->target)));
            else if constexpr(std::is_same_v<T,ast::Attribute>){if(!metadata.emplace(sem::spelling(*t->name),sem::attribute(*t)).second)sem::fail(*t,"duplicate block attribute",Error::Code::Conflict);}
            else {Instruction i{};i.id=next_instruction++;i.origin=std::string(file)+":"+std::to_string(t->source.begin.line);
              auto move=[&](const ast::Move& m){Transfer copy{operand(*m.destination,values,physical,slots),operand(*m.source_operand,values,physical,slots)};writes(i,copy.destination);reads(i,copy.source);i.transfers.push_back(std::move(copy));};
              if constexpr(std::is_same_v<T,ast::Move>){i.opcode="move";move(*t);}
              else if constexpr(std::is_same_v<T,ast::ParallelCopy>){i.opcode="parallel";i.parallel=true;for(auto& m:t->moves){move(*m);for(size_t k=0;k+1<i.transfers.size();++k)if(i.transfers[k].destination==i.transfers.back().destination)sem::fail(*m,"duplicate parallel destination",Error::Code::Conflict);}}
              else {i.opcode=sem::spelling(*t->name);std::vector<const ast::Constraint*> requirements;
                for(auto& item:t->items)std::visit([&](const auto& n){using N=std::remove_cvref_t<decltype(*n)>;if constexpr(std::is_same_v<N,ast::RegisterOperand>){auto o=operand(*n->operand,values,physical,slots);auto role=n->role->value;if(role=="def")writes(i,o);else if(role=="use")reads(i,o);else {if(o.kind!=TransferOperand::Kind::Physical)sem::fail(*n,"implicit operands must identify physical storage");if(role=="implicit_def")i.physical_defs.push_back(o.id);else i.physical_uses.push_back(o.id);}}else requirements.push_back(n.get());},item->value);
                Constraint requirement;bool spillable=true;Object fields;constraints(requirements,requirement,spillable,fields);
                if(!requirement.bank.empty()){std::set<VReg> involved(i.defs.begin(),i.defs.end());involved.insert(i.uses.begin(),i.uses.end());if(involved.size()!=1)sem::fail(*t,"instruction bank constraint needs one virtual value",Error::Code::Unsupported);auto value=std::find_if(f.values.begin(),f.values.end(),[&](auto& r){return r.value==*involved.begin();});if(!value->constraint.bank.empty()&&value->constraint.bank!=requirement.bank)sem::fail(*t,"conflicting register banks",Error::Code::Unsatisfiable);value->constraint.bank=requirement.bank;fields["bank"]=sem::Value(requirement.bank);}
                if(!requirement.allowed.empty()||!requirement.forbidden.empty()||requirement.fixed||!spillable){std::set<VReg> involved(i.defs.begin(),i.defs.end());involved.insert(i.uses.begin(),i.uses.end());if(involved.size()!=1)sem::fail(*t,"instruction-wide constraints need exactly one virtual value",Error::Code::Unsupported);auto v=*involved.begin();auto value=std::find_if(f.values.begin(),f.values.end(),[&](auto& r){return r.value==v;});auto& c=value->constraint;if(!requirement.allowed.empty()){if(c.allowed.empty())c.allowed=requirement.allowed;else {std::vector<uint32_t> intersection;for(auto r:c.allowed)if(std::find(requirement.allowed.begin(),requirement.allowed.end(),r)!=requirement.allowed.end())intersection.push_back(r);if(intersection.empty())sem::fail(*t,"disjoint allowed registers",Error::Code::Unsatisfiable);c.allowed=std::move(intersection);}}c.forbidden.insert(c.forbidden.end(),requirement.forbidden.begin(),requirement.forbidden.end());if(requirement.fixed){if(c.fixed&&c.fixed!=requirement.fixed)sem::fail(*t,"conflicting fixed registers",Error::Code::Unsatisfiable);c.fixed=requirement.fixed;}value->spillable&=spillable;}
                if(fields.contains("early_defs"))i.early_defs=references(fields.at("early_defs"),values);
                if(fields.contains("clobbers"))i.clobbers=references(fields.at("clobbers"),physical);
                if(fields.contains("ties"))for(auto& tie:sem::array(fields.at("ties"))){auto& pair=sem::array(tie);if(pair.size()!=2)sem::fail(*t,"tie needs a definition and input");i.ties.emplace_back(values.at(pair[0].text()),values.at(pair[1].text()));}
                auto register_values=[](std::span<const uint32_t> ids){sem::Value::Array list;for(auto id:ids)list.emplace_back("$"+std::to_string(id));return sem::Value(std::move(list));};
                if(!requirement.allowed.empty())fields["allowed"]=register_values(requirement.allowed);if(!requirement.forbidden.empty())fields["forbidden"]=register_values(requirement.forbidden);if(requirement.fixed)fields["fixed"]=sem::Value("$"+std::to_string(*requirement.fixed));if(!spillable)fields["spillable"]=sem::Value(false);function.instruction_metadata[i.id]=std::move(fields);
              }
              block.instructions.push_back(std::move(i));
            }
          },transfer->value);
          if(metadata.contains("live_out"))block.live_out=references(metadata.at("live_out"),values);
          if(metadata.contains("physical_live_out"))block.physical_live_out=references(metadata.at("physical_live_out"),physical);
          f.blocks.push_back(std::move(block));
        }
        auto analysis=analyze(f);if(!analysis)return Result<std::vector<AllocationUnit>>::err(analysis.error());unit.functions.push_back(std::move(function));
      }
      units.push_back(std::move(unit));
    }
    return Result<std::vector<AllocationUnit>>::ok(std::move(units));
  }catch(const Error& e){return Result<std::vector<AllocationUnit>>::err(e);}
}
Result<std::string> print_regtl(std::span<const AllocationUnit> units) {
  try {
    std::string out;for(auto& unit:units){auto valid=validate(unit.problem);if(!valid)return Result<std::string>::err(valid.error());out+="regtl "+sem::quote(unit.name)+" {\n";
      for(auto& c:unit.problem.classes)out+="  regclass "+sem::quote(c.name)+" = "+refs(c.members,'$')+";\n";
      for(auto [a,b]:unit.problem.aliases)out+="  alias $"+std::to_string(a)+" = $"+std::to_string(b)+";\n";
      for(auto& r:unit.problem.ranges){out+="  live %"+std::to_string(r.value)+":"+sem::quote(r.klass)+" ["+std::to_string(r.begin)+", "+std::to_string(r.end)+"] {\n";if(!r.constraint.allowed.empty())out+="    allowed = "+refs(r.constraint.allowed,'$')+";\n";if(!r.constraint.forbidden.empty())out+="    forbidden = "+refs(r.constraint.forbidden,'$')+";\n";if(r.constraint.fixed)out+="    fixed = $"+std::to_string(*r.constraint.fixed)+";\n";if(!r.constraint.bank.empty())out+="    bank = "+sem::quote(r.constraint.bank)+";\n";out+="    spillable = "+std::string(r.spillable?"true":"false")+";\n";if(unit.value_metadata.contains(r.value))for(auto& [k,v]:sem::ordered(unit.value_metadata.at(r.value)))out+="    "+sem::quote(k)+" = "+sem::print(v)+";\n";out+="  }\n";}
      for(auto& c:unit.problem.clobbers)out+="  clobber "+std::to_string(c.position)+" = "+refs(c.registers,'$')+";\n";
      if(!unit.problem.storage.empty()){sem::Value::Array storage;for(auto& r:unit.problem.storage){sem::Value::Array slices;for(auto& s:r.slices)slices.emplace_back(Object{{"unit",sem::Value(uint64_t(s.unit))},{"begin",sem::Value(uint64_t(s.begin))},{"width",sem::Value(uint64_t(s.width))}});storage.emplace_back(Object{{"register",sem::Value("$"+std::to_string(r.id))},{"bank",sem::Value(r.bank)},{"slices",sem::Value(std::move(slices))}});}out+="  storage = "+sem::print(sem::Value(std::move(storage)))+";\n";}
      if(!unit.problem.tuples.empty()){sem::Value::Array tuples;for(auto& t:unit.problem.tuples){sem::Value::Array values,alternatives;for(auto value:t.values)values.emplace_back("%"+std::to_string(value));for(auto& a:t.alternatives){sem::Value::Array registers;for(auto reg:a)registers.emplace_back("$"+std::to_string(reg));alternatives.emplace_back(std::move(registers));}tuples.emplace_back(Object{{"values",sem::Value(std::move(values))},{"alternatives",sem::Value(std::move(alternatives))}});}out+="  tuples = "+sem::print(sem::Value(std::move(tuples)))+";\n";}
      if(unit.metadata.contains("pbqp")||unit.pbqp.costs.default_spill_cost!=1||!unit.pbqp.costs.values.empty()||!unit.pbqp.costs.coalescing.empty()||unit.pbqp.search_limit!=1000000||unit.pbqp.cell_limit!=8*1024*1024||unit.pbqp.work_limit!=50000000) {
        auto valid=validate_costs(unit.problem,unit.pbqp.costs);if(!valid)return Result<std::string>::err(valid.error());
        sem::Value::Array values,moves;auto policies=unit.pbqp.costs.values;std::sort(policies.begin(),policies.end(),[](auto& a,auto& b){return a.value<b.value;});
        for(auto& unary:policies){sem::Value::Array registers;auto costs=unary.registers;std::sort(costs.begin(),costs.end());for(auto [reg,cost]:costs)registers.emplace_back(Object{{"register",sem::Value("$"+std::to_string(reg))},{"cost",cost_value(cost)}});values.emplace_back(Object{{"value",sem::Value("%"+std::to_string(unary.value))},{"spill_cost",cost_value(unary.spill_cost)},{"registers",sem::Value(std::move(registers))}});}
        auto coalescing=unit.pbqp.costs.coalescing;std::sort(coalescing.begin(),coalescing.end(),[](auto& a,auto& b){return std::tie(a.first,a.second)<std::tie(b.first,b.second);});for(auto& move:coalescing)moves.emplace_back(Object{{"first",sem::Value("%"+std::to_string(move.first))},{"second",sem::Value("%"+std::to_string(move.second))},{"cost",cost_value(move.cost)}});
        Object config{{"default_spill_cost",cost_value(unit.pbqp.costs.default_spill_cost)},{"values",sem::Value(std::move(values))},{"coalescing",sem::Value(std::move(moves))},{"search_limit",sem::Value(uint64_t(unit.pbqp.search_limit))},{"cell_limit",sem::Value(uint64_t(unit.pbqp.cell_limit))},{"work_limit",sem::Value(uint64_t(unit.pbqp.work_limit))}};
        out+="  pbqp = "+sem::print(sem::Value(std::move(config)))+";\n";
      }
      for(auto& s:unit.slots){auto fields=s.metadata;fields["size"]=sem::Value(uint64_t(s.size));fields["alignment"]=sem::Value(uint64_t(s.alignment));out+="  spill_slot "+sem::quote(s.name)+" "+sem::print(sem::Value(std::move(fields)))+"\n";}
      for(auto& f:unit.functions){out+="  function "+sem::quote(f.name)+" {\n";for(auto& b:f.function.blocks){out+="    block "+sem::quote(std::to_string(b.id))+" {\n";
        for(auto& i:b.instructions){if(!i.transfers.empty()){if(i.parallel)out+="      parallel {\n";for(auto& t:i.transfers)out+="      move "+print_operand(t.destination,unit)+" <- "+print_operand(t.source,unit)+";\n";if(i.parallel)out+="      }\n";}else {out+="      instruction "+sem::quote(i.opcode)+" {\n";for(auto v:i.defs)out+="        def %"+std::to_string(v)+";\n";for(auto v:i.uses)out+="        use %"+std::to_string(v)+";\n";for(auto v:i.physical_defs)out+="        implicit_def $"+std::to_string(v)+";\n";for(auto v:i.physical_uses)out+="        implicit_use $"+std::to_string(v)+";\n";if(!i.early_defs.empty())out+="        early_defs = "+refs(i.early_defs,'%')+";\n";if(!i.clobbers.empty())out+="        clobbers = "+refs(i.clobbers,'$')+";\n";if(!i.ties.empty()){out+="        ties = [";for(size_t k=0;k<i.ties.size();++k){if(k)out+=", ";out+="[%"+std::to_string(i.ties[k].first)+", %"+std::to_string(i.ties[k].second)+"]";}out+="];\n";}if(f.instruction_metadata.contains(i.id))for(auto& [k,v]:sem::ordered(f.instruction_metadata.at(i.id)))if(k!="early_defs"&&k!="ties"&&k!="clobbers"){if(k=="fixed")out+="        fixed = "+v.text()+";\n";else if(k=="allowed"||k=="forbidden"){std::string list="[";for(auto& r:sem::array(v)){if(list.size()>1)list+=", ";list+=r.text();}out+="        "+k+" = "+list+"];\n";}else if(k=="spillable")out+="        spillable = "+v.text()+";\n";else out+="        "+sem::quote(k)+" = "+sem::print(v)+";\n";}out+="      }\n";}}
        for(auto next:b.successors)out+="      successor "+sem::quote(std::to_string(next))+";\n";
        if(!b.live_out.empty())out+="      live_out = "+refs(b.live_out,'%')+";\n";if(!b.physical_live_out.empty())out+="      physical_live_out = "+refs(b.physical_live_out,'$')+";\n";
        if(f.block_metadata.contains(b.id))for(auto& [k,v]:sem::ordered(f.block_metadata.at(b.id)))if(k!="live_out"&&k!="physical_live_out")out+="      "+sem::quote(k)+" = "+sem::print(v)+";\n";out+="    }\n";
      }out+="  }\n";}
      if(!unit.problem.reserved.empty())out+="  reserved = "+refs(unit.problem.reserved,'$')+";\n";
      for(auto& [k,v]:sem::ordered(unit.metadata))if(k!="reserved"&&k!="ties"&&k!="interference"&&k!="explicit_interference"&&k!="storage"&&k!="tuples"&&k!="pbqp")out+="  "+sem::quote(k)+" = "+sem::print(v)+";\n";
      for(auto key:{"ties","interference"}){auto& edges=std::string_view(key)=="ties"?unit.problem.ties:unit.problem.interference;if(!edges.empty()){out+="  "+std::string(key)+" = [";for(size_t k=0;k<edges.size();++k){if(k)out+=", ";out+="[%"+std::to_string(edges[k].first)+", %"+std::to_string(edges[k].second)+"]";}out+="];\n";}}
      if(unit.problem.explicit_interference)out+="  explicit_interference = true;\n";out+="}\n";
    }
    return Result<std::string>::ok(std::move(out));
  }catch(const Error& e){return Result<std::string>::err(e);}
}
}
