#include "codegen.hpp"
#include <charconv>
#include <set>

namespace limestone::bin2bin {
Result<std::map<std::string,OperandBinding>> encoding_operands(const metacode::Value::Object& fields,std::string_view origin) {
  using Bindings=std::map<std::string,OperandBinding>;Bindings bindings;
  auto error=[&](std::string message,Error::Code code=Error::Code::InvalidArgument){return Result<Bindings>::err({code,std::string(origin)+": "+message});};
  std::map<std::string,metacode::Value> ordered(fields.begin(),fields.end());
    for(auto& [name,value]:ordered) {
       auto entry=std::get_if<metacode::Value::Object>(&value.data);
       if(name.empty()||!entry||!entry->contains("kind")||!entry->contains("index"))return error("encoding binding needs kind and index");
       auto kind=entry->at("kind").text();OperandBinding binding;
       if(kind=="definition")binding.kind=OperandBinding::Kind::Definition;else if(kind=="use")binding.kind=OperandBinding::Kind::Use;else if(kind=="immediate")binding.kind=OperandBinding::Kind::Immediate;else if(kind=="block")binding.kind=OperandBinding::Kind::BlockTarget;else if(kind=="fixed_definition")binding.kind=OperandBinding::Kind::FixedDefinition;else if(kind=="fixed_use")binding.kind=OperandBinding::Kind::FixedUse;else return error("unsupported encoding operand binding: "+kind,Error::Code::Unsupported);
       bool fixed=binding.kind==OperandBinding::Kind::FixedDefinition||binding.kind==OperandBinding::Kind::FixedUse;
       if(entry->size()!=(fixed?3u:2u))return error("encoding binding has unsupported fields");
       if(fixed){auto reg=entry->find("register");auto text=reg==entry->end()?nullptr:std::get_if<std::string>(&reg->second.data);if(!text||text->empty())return error("fixed encoding binding needs a register name");binding.physical_register=*text;}
        auto text=entry->at("index").text();auto [end,code]=std::from_chars(text.data(),text.data()+text.size(),binding.index);
        if(code!=std::errc{}||end!=text.data()+text.size())return error("invalid encoding operand index");bindings[name]=std::move(binding);
    }
  return Result<Bindings>::ok(std::move(bindings));
}
Result<EncodingBindings> encoding_bindings(const metacode::Architecture& source) {
  EncodingBindings bindings;
  for(auto& operation:source.operations) {
    auto it=operation.fields.find("encoding_operands");if(it==operation.fields.end())continue;
    auto fields=std::get_if<metacode::Value::Object>(&it->second.data);if(!fields)return Result<EncodingBindings>::err({Error::Code::InvalidArgument,"encoding_operands must be an object: "+operation.name});
    auto parsed=encoding_operands(*fields,operation.name);if(!parsed)return Result<EncodingBindings>::err(parsed.error());bindings[operation.name]=std::move(parsed.value());
  }
  return Result<EncodingBindings>::ok(std::move(bindings));
}
Result<std::vector<uint8_t>> encode_region(const Architecture& target,const EncodingBindings& bindings,
  const std::map<uint32_t,std::string>& physical_names,const schedrow::Region& region,
  std::span<const uint32_t> order,const std::optional<regtl::Allocation>& allocation,uint64_t address) {
  if(allocation&&!allocation->spilled.empty())return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"encoding requires spill/reload materialization"});
  auto checked=validate(target);if(!checked)return Result<std::vector<uint8_t>>::err(checked.error());
  checked=schedrow::validate_region(region);if(!checked)return Result<std::vector<uint8_t>>::err(checked.error());
  checked=schedrow::verify_order(region,order);if(!checked)return Result<std::vector<uint8_t>>::err(checked.error());
  std::map<uint32_t,const schedrow::Instruction*> instructions;
  for(auto& instruction:region.instructions)if(!instructions.emplace(instruction.id,&instruction).second)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"duplicate encoding instruction"});
  if(order.size()!=instructions.size())return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"incomplete encoding order"});
  std::set<uint32_t> seen;std::map<uint32_t,size_t> block_index;
  for(size_t k=0;k<region.blocks.size();++k)if(!block_index.emplace(region.blocks[k].id,k).second)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"duplicate encoding block"});
  if(!region.blocks.empty()&&region.blocks.front().id!=region.entry)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"entry block must lead encoding layout"});
  struct Alternative{uint32_t id,width;};struct Plan{const schedrow::Instruction* instruction;std::map<std::string,std::string> operands;std::map<std::string,uint32_t> targets;std::vector<Alternative> alternatives;size_t choice=0;uint64_t address=0;};std::vector<Plan> plans;
  std::optional<size_t> last_block;
  for(auto id:order) {
    auto selected=std::find_if(region.instructions.begin(),region.instructions.end(),[&](auto& i){return i.id==id;});if(selected!=region.instructions.end())for(auto& [source,metadata]:selected->source_metadata)if(!metadata.strings.empty())return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"string operands require a target encoding adapter: "+selected->opcode});
    if(!instructions.contains(id)||!seen.insert(id).second)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"invalid encoding order"});
    auto& instruction=*instructions.at(id);auto mapped=bindings.find(instruction.opcode);
    std::optional<ControlFlow> control;
     switch(instruction.control){case schedrow::ControlFlow::None:if(!instruction.call&&!instruction.terminator)control=ControlFlow::Fallthrough;break;case schedrow::ControlFlow::Branch:control=ControlFlow::Branch;break;case schedrow::ControlFlow::ConditionalBranch:control=ControlFlow::ConditionalBranch;break;case schedrow::ControlFlow::Return:control=ControlFlow::Return;break;case schedrow::ControlFlow::IndirectBranch:control=ControlFlow::IndirectBranch;break;case schedrow::ControlFlow::Call:control=ControlFlow::Call;break;case schedrow::ControlFlow::Trap:control=ControlFlow::Trap;break;}
    if(!control)return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"encoding needs an explicit control-flow adapter for "+instruction.opcode});
    if(target.forms.empty())for(auto [opcode,name]:target.opcodes)if(name==instruction.opcode&&(!target.status.contains(opcode)||target.status.at(opcode)==Status::Supported)&&(target.control.contains(opcode)?target.control.at(opcode):ControlFlow::Fallthrough)!=*control)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"fixed encoding control flow disagrees with selected semantics"});
    for(auto& form:target.forms)if(form.mnemonic==instruction.opcode&&form.status==Status::Supported){
      if(form.control!=*control)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"encoding control flow disagrees with selected semantics: "+instruction.opcode});
      bool direct=*control==ControlFlow::Branch||*control==ControlFlow::ConditionalBranch;
      if(direct&&(mapped==bindings.end()||!mapped->second.contains(form.target_operand)||mapped->second.at(form.target_operand).kind!=OperandBinding::Kind::BlockTarget||mapped->second.at(form.target_operand).index!=0))return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"direct encoding target must bind the first CFG target"});
      if(mapped!=bindings.end())for(auto& [name,binding]:mapped->second)if(binding.kind==OperandBinding::Kind::BlockTarget&&(!direct||name!=form.target_operand))return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"encoding block binding is not a direct branch target"});
    }
    if(!region.blocks.empty()){if(!block_index.contains(instruction.block))return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"unknown encoding block"});auto index=block_index.at(instruction.block);if(last_block&&index<*last_block)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"encoding order crosses block layout"});last_block=index;}
    bool has_fields=std::any_of(target.forms.begin(),target.forms.end(),[&](auto& f){return f.mnemonic==instruction.opcode&&!f.fields.empty();});
    if(mapped==bindings.end()&&has_fields)return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"missing encoding operand bindings for "+instruction.opcode});
    std::set<size_t> definitions,uses,immediates;
    if(mapped!=bindings.end())for(auto& [name,binding]:mapped->second){if(binding.kind<OperandBinding::Kind::Definition||binding.kind>OperandBinding::Kind::FixedUse)return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"invalid encoding operand binding kind"});switch(binding.kind){case OperandBinding::Kind::Definition:case OperandBinding::Kind::FixedDefinition:definitions.insert(binding.index);break;case OperandBinding::Kind::Use:case OperandBinding::Kind::FixedUse:uses.insert(binding.index);break;case OperandBinding::Kind::Immediate:immediates.insert(binding.index);break;case OperandBinding::Kind::BlockTarget:break;}}
    for(size_t k=0;k<instruction.defs.size();++k)if(!definitions.contains(k))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"unbound selected definition needs an encoding adapter: "+instruction.opcode});
    for(size_t k=0;k<instruction.uses.size();++k)if(!uses.contains(k))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"unbound selected use needs an encoding adapter: "+instruction.opcode});
    for(size_t k=0;k<instruction.immediates.size();++k)if(!immediates.contains(k))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"unbound selected immediate needs an encoding adapter: "+instruction.opcode});
    Plan plan{&instruction};auto& operands=plan.operands;
    if(mapped!=bindings.end())for(auto& [name,binding]:mapped->second) {
      if(binding.kind==OperandBinding::Kind::Immediate) {
        if(binding.index>=instruction.immediates.size())return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"missing selected immediate for "+name});
        operands[name]=std::to_string(instruction.immediates[binding.index].second);
      }else if(binding.kind==OperandBinding::Kind::BlockTarget){if(binding.index>=instruction.block_targets.size()||!block_index.contains(instruction.block_targets[binding.index]))return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"missing or unknown selected block target"});operands[name]="0";plan.targets[name]=instruction.block_targets[binding.index];
      }else {
        bool fixed=binding.kind==OperandBinding::Kind::FixedDefinition||binding.kind==OperandBinding::Kind::FixedUse;
        if(fixed&&std::any_of(target.forms.begin(),target.forms.end(),[&](auto& form){return form.mnemonic==instruction.opcode&&std::any_of(form.fields.begin(),form.fields.end(),[&](auto& field){return field.name==name;});}))return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"fixed encoding operand must not name an encoded field"});
        const auto& values=(binding.kind==OperandBinding::Kind::Definition||binding.kind==OperandBinding::Kind::FixedDefinition)?instruction.defs:instruction.uses;
        if(binding.index>=values.size())return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"missing selected register operand for "+name});
        if(!allocation||!allocation->regs.contains(values[binding.index]))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"register encoding requires physical allocation"});
        auto physical=allocation->regs.at(values[binding.index]);auto register_name=physical_names.find(physical);
        if(register_name==physical_names.end())return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"allocated physical register has no target name"});
        if(fixed){if(binding.physical_register.empty())return Result<std::vector<uint8_t>>::err({Error::Code::InvalidArgument,"fixed encoding operand has no register name"});if(register_name->second!=binding.physical_register)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"fixed encoding operand disagrees with allocation: "+instruction.opcode+" "+name});}
        else operands[name]=register_name->second;
      }
    }
    if(target.forms.empty()){auto encoded=encode(target,instruction.opcode,operands,0);if(!encoded)return encoded;for(auto [opcode,name]:target.opcodes)if(name==instruction.opcode&&(!target.status.contains(opcode)||target.status.at(opcode)==Status::Supported))plan.alternatives.push_back({opcode,8});}
    else for(auto& form:target.forms)if(form.mnemonic==instruction.opcode&&form.status==Status::Supported){auto probe=operands;for(auto& [field,block]:plan.targets){auto found=std::find_if(form.fields.begin(),form.fields.end(),[&](auto& f){return f.name==field;});if(found!=form.fields.end())probe[field]=std::to_string(found->kind==OperandKind::PCRelative&&found->relative_to_end?form.width/8:0);}auto encoded=encode_form(target,form.id,probe,0);if(encoded)plan.alternatives.push_back({form.id,form.width});}
    if(plan.alternatives.empty())return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"no legal encoding form for "+instruction.opcode});
    std::sort(plan.alternatives.begin(),plan.alternatives.end(),[](auto& a,auto& b){return std::tie(a.width,a.id)<std::tie(b.width,b.id);});plans.push_back(std::move(plan));
  }
  auto layout=region;layout.instructions.clear();for(auto& plan:plans)layout.instructions.push_back(*plan.instruction);checked=schedrow::validate_region(layout);if(!checked)return Result<std::vector<uint8_t>>::err(checked.error());
  for(size_t k=0;k<region.blocks.size();++k){auto& b=region.blocks[k];auto last=std::find_if(plans.rbegin(),plans.rend(),[&](auto& p){return p.instruction->block==b.id;});if(last==plans.rend()||!last->instruction->terminator){if(b.successors.empty()&&k+1<region.blocks.size())return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"CFG exit would fall through into another block"});if(b.successors.size()==1&&(k+1==region.blocks.size()||b.successors[0]!=region.blocks[k+1].id))return Result<std::vector<uint8_t>>::err({Error::Code::Unsupported,"non-adjacent fallthrough needs an explicit branch"});}else if(last->instruction->control==schedrow::ControlFlow::ConditionalBranch){auto& targets=last->instruction->block_targets;if(targets.size()!=2||k+1==region.blocks.size()||targets[1]!=region.blocks[k+1].id)return Result<std::vector<uint8_t>>::err({Error::Code::Conflict,"conditional fallthrough must be the next layout block"});}}
  bool promoted=true;while(promoted){promoted=false;uint64_t current=address;std::map<uint32_t,uint64_t> addresses;
    if(region.blocks.empty())for(auto& p:plans){p.address=current;auto size=p.alternatives[p.choice].width/8;if(size>UINT64_MAX-current)return Result<std::vector<uint8_t>>::err({Error::Code::ResourceLimit,"encoding address overflow"});current+=size;}
    else for(auto& b:region.blocks){addresses[b.id]=current;for(auto& p:plans)if(p.instruction->block==b.id){p.address=current;auto size=p.alternatives[p.choice].width/8;if(size>UINT64_MAX-current)return Result<std::vector<uint8_t>>::err({Error::Code::ResourceLimit,"encoding address overflow"});current+=size;}}
    std::vector<uint8_t> bytes;
    for(auto& p:plans){auto operands=p.operands;for(auto [name,block]:p.targets)operands[name]=std::to_string(addresses.at(block));std::optional<Error> error;bool found=false;for(size_t choice=p.choice;choice<p.alternatives.size();++choice){auto encoded=encode_form(target,p.alternatives[choice].id,operands,p.address);if(!encoded){error=encoded.error();continue;}if(choice!=p.choice){p.choice=choice;promoted=true;}bytes.insert(bytes.end(),encoded.value().begin(),encoded.value().end());found=true;break;}if(!found){if(promoted)break;return Result<std::vector<uint8_t>>::err(error.value_or(Error{Error::Code::Unsupported,"no encodable layout form"}));}}
    if(!promoted)return Result<std::vector<uint8_t>>::ok(std::move(bytes));
  }
  return Result<std::vector<uint8_t>>::err({Error::Code::Internal,"unreachable encoding layout state"});
}
}
