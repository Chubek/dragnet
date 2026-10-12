#include "native.hpp"
#include "internal.hpp"
#include "metacode/machine-ir/native_c.hpp"
#include <bit>
#include <charconv>
#include <limits>
#include <set>

namespace limestone::vmweave {
namespace {
template<class T,class F> Result<T> checked(F&& f) {
  try {return Result<T>::ok(f());}
  catch(const Error& e) {return Result<T>::err(e);}
  catch(const std::bad_alloc&) {return Result<T>::err({Error::Code::ResourceLimit,"VMWeave native: allocation failure"});}
  catch(const std::exception& e) {return Result<T>::err({Error::Code::Internal,e.what()});}
}
[[noreturn]] void fail(std::string message,Error::Code code=Error::Code::InvalidArgument) {throw Error{code,"VMWeave native: "+message};}
template<class T> T take(Result<T> result) {if(!result) throw result.error(); return std::move(result.value());}
machineir_native::Options options(const NativeOptions& o) {return {o.compiler,o.compile_arguments,o.link_arguments,o.timeout_seconds,o.image_limit};}
std::map<uint32_t,const Word*> word_map(const Module& m) {std::map<uint32_t,const Word*> out; for(const auto& w:m.words) out[w.opcode]=&w; return out;}
void validate_code(const Module& m,std::span<const NativeInstruction> code,const NativeOptions& o) {
  if(!o.instruction_limit || code.size()>o.instruction_limit || code.size()>4096) fail("native tape instruction limit",Error::Code::ResourceLimit);
  auto words=word_map(m);
  for(size_t i=0;i<code.size();++i) {
    const auto& ins=code[i];
    if(!words.contains(ins.opcode)) fail("instruction "+std::to_string(i)+": unknown opcode");
    const auto& word=*words.at(ins.opcode);
    if(ins.operands.size()!=word.operands.size()) fail("instruction '"+word.name+"': operand count mismatch");
    for(size_t j=0;j<ins.operands.size();++j) {
      auto bits=ins.operands[j];
      if(word.operands[j]=="label" && bits>=code.size()) fail("instruction '"+word.name+"': label out of range");
      if(m.configuration.cell=="intptr_t") {
        auto value=std::bit_cast<int64_t>(bits);
        if(value<std::numeric_limits<intptr_t>::min() || value>std::numeric_limits<intptr_t>::max()) fail("instruction '"+word.name+"': operand exceeds intptr_t");
      }
    }
  }
}
struct Builder {
  machineir_native::Unit unit;
  uint32_t next_value=4, next_instruction=0;
  uint32_t block() {
    auto id=static_cast<uint32_t>(unit.entry.region.blocks.size());
    unit.entry.region.blocks.push_back({id,"b"+std::to_string(id),{}, {}}); return id;
  }
  uint32_t value() {auto id=next_value++; unit.entry.values.push_back({id,"i32",""}); return id;}
  void append(schedrow::Instruction i) {i.id=next_instruction++; i.latency=0; unit.entry.order.push_back(i.id); unit.entry.region.instructions.push_back(std::move(i));}
  uint32_t call(uint32_t block,const std::string& name,std::vector<uint32_t> args) {
    auto result=value(); schedrow::Instruction i{};
    i.block=block; i.opcode="foreign.call"; i.opcode_class="runtime_call"; i.semantic_class="call";
    i.defs={result}; i.uses=std::move(args); i.call=true; i.control=schedrow::ControlFlow::Call;
    i.barrier=true; i.memory=true; i.may_trap=true; i.speculative=false;
    schedrow::MemoryAccess access; access.read=true; access.write=true; access.address_space="runtime"; i.access=access;
    const auto& f=unit.functions.at(name); i.origin=f.file+":"+std::to_string(f.line);
    unit.callees[next_instruction]=name; append(std::move(i)); return result;
  }
  uint32_t compare(uint32_t block,const std::string& opcode,std::vector<uint32_t> args) {
    auto result=value(); schedrow::Instruction i{}; i.block=block; i.opcode=opcode; i.defs={result}; i.uses=std::move(args); append(std::move(i)); return result;
  }
  uint32_t constant(uint32_t block,int32_t n) {
    auto result=value(); schedrow::Instruction i{}; i.block=block; i.opcode="const.i32"; i.defs={result}; i.immediates={{result,n}}; append(std::move(i)); return result;
  }
  void branch(uint32_t block,uint32_t condition,uint32_t yes,uint32_t no) {
    schedrow::Instruction i{}; i.block=block; i.opcode="br.nonzero"; i.uses={condition};
    i.terminator=true; i.control=schedrow::ControlFlow::ConditionalBranch; i.block_targets={yes,no};
    unit.entry.region.blocks[block].successors={yes,no}; append(std::move(i));
  }
  void ret(uint32_t block,uint32_t result) {
    schedrow::Instruction i{}; i.block=block; i.opcode="ret.i32"; i.uses={result}; i.terminator=true; i.control=schedrow::ControlFlow::Return; append(std::move(i));
  }
  void function(std::string name,std::vector<std::string> args,std::string body,Location source={}) {
    machineir_native::ForeignFunction f; f.name=name; f.body=std::move(body); f.argument_types=std::move(args); f.file=std::move(source.file); f.line=source.line;
    unit.functions.emplace(std::move(name),std::move(f));
  }
};
machineir_native::Unit unit(const Module& input,std::span<const NativeInstruction> code,const NativeOptions& opt) {
  auto m=take(checked_module(input)); validate_code(m,code,opt);
  Builder b; auto& u=b.unit; auto n=m.configuration.name;
  u.entry.module=n; u.entry.function=n+"_vw_native_entry"; u.entry.target="host-c-abi"; u.entry.region.name=u.entry.function;
  u.parameters={1,2,3}; u.entry.values={{1,"ptr",""},{2,"ptr",""},{3,"u64",""}};
  // Native materialization needs handlers even for table-only generation. It
  // does not change the user's component selection or emitted public ABI.
  if(std::find(m.configuration.components.begin(),m.configuration.components.end(),"insncode")==m.configuration.components.end()) m.configuration.components.push_back("insncode");
  u.support_c=take(vmweave::emit_c(m));
  u.support_c+="\nsize_t "+n+"_vw_native_size(void) { return sizeof("+n+"_vm); }\nuint64_t "+n+"_vw_native_abi(void) { return "+n+"_vw_abi(); }\n";
  u.support_c+="static const "+n+"_instruction "+n+"_vw_native_tape[] = {\n";
  for(const auto& ins:code) {
    u.support_c+=" {"+std::to_string(ins.opcode)+"u,"+std::to_string(ins.operands.size())+",{";
    if(ins.operands.empty()) u.support_c+="0";
    for(size_t j=0;j<ins.operands.size();++j) {
      if(j) u.support_c+=",";
      auto bits=ins.operands[j]; std::string literal;
      if(m.configuration.cell=="uint64_t") literal="UINT64_C("+std::to_string(bits)+")";
      else {
        auto value=std::bit_cast<int64_t>(bits);
        if(value==INT64_MIN) literal="(-INT64_C(9223372036854775807)-1)";
        else literal=value<0?"(-INT64_C("+std::to_string(-value)+"))":"INT64_C("+std::to_string(value)+")";
      }
      u.support_c+="("+n+"_cell)"+literal;
    }
    u.support_c+="}},\n";
  }
  if(code.empty()) u.support_c+=" {0,0,{0}}\n";
  u.support_c+="};\n";
  auto count=std::to_string(code.size());
  auto begin=n+"_vw_native_begin",select=n+"_vw_native_select",finish=n+"_vw_native_finish",success=n+"_vw_native_success";
  b.function(begin,{"ptr","u64"},"if(!a0) return -1; if(a1!="+n+"_vw_abi()) return -11; "+n+"_vm *vm=("+n+"_vm *)a0; if(vm->vw_pc>"+count+") return -4; vm->vw_code_size="+count+"; vm->vw_halted=0; return 0;");
  b.function(select,{"ptr","ptr"},n+"_vm *vm=("+n+"_vm *)a0; size_t *budget=(size_t *)a1; if(vm->vw_halted || vm->vw_pc>="+count+") return "+count+"; if(!*budget) return -7; --*budget; return (int32_t)vm->vw_pc;");
  b.function(finish,{"ptr","i32"},"(("+n+"_vm *)a0)->vw_current=NULL; return a1;");
  b.function(success,{"ptr"},"(("+n+"_vm *)a0)->vw_current=NULL; return 0;");
  auto words=word_map(m);
  for(size_t index=0;index<code.size();++index) {
    const auto& word=*words.at(code[index].opcode); auto i=std::to_string(index);
    std::string body=n+"_vm *vm=("+n+"_vm *)a0; size_t base=0; const "+n+"_metadata *info=NULL; int status; vm->vw_current=&"+n+"_vw_native_tape["+i+"];\n";
    bool direct=m.configuration.execution=="direct";
    if(!direct) body+="++vm->vw_pc;\n";
    auto early_error=direct?"return status;":"vm->vw_pc="+i+"; return status;";
    body+="status="+n+"_vw_check(vm,"+std::to_string(word.opcode)+"u,&base,&info); if(status) {"+early_error+"}\n";
    if(m.configuration.hooks) body+="if(vm->vmweave_before) { status=vm->vmweave_before(vm,"+std::to_string(word.opcode)+"u,vm->vmweave_userdata); if(status) {"+early_error+"} }\n";
    if(direct) body+="++vm->vw_pc;\n";
    body+=n+"_"+word.name+"(vm);\n";
    if(m.configuration.hooks) body+="if(vm->vmweave_after) vm->vmweave_after(vm,"+std::to_string(word.opcode)+"u,vm->vmweave_userdata);\n";
    body+="status="+n+"_vw_finish(vm,base,info); if(status) vm->vw_pc="+i+"; return status;";
    b.function(n+"_vw_native_op"+i,{"ptr"},std::move(body),word.source);
  }
  auto entry=b.block(),dispatch=b.block(),begin_error=b.block(),select_error=b.block(),done=b.block();
  std::vector<uint32_t> checks,invokes,errors;
  for(size_t i=0;i<code.size();++i) {checks.push_back(b.block()); invokes.push_back(b.block()); errors.push_back(b.block());}
  auto initial=b.call(entry,begin,{1,3}); b.branch(entry,initial,begin_error,dispatch);
  auto selector=b.call(dispatch,select,{1,2}); auto negative=b.compare(dispatch,"lt_zero.i32",{selector}); b.branch(dispatch,negative,select_error,checks.empty()?done:checks[0]);
  b.ret(begin_error,initial); b.ret(select_error,b.call(select_error,finish,{1,selector})); b.ret(done,b.call(done,success,{1}));
  for(size_t i=0;i<code.size();++i) {
    auto c=b.constant(checks[i],static_cast<int32_t>(i)); auto match=b.compare(checks[i],"eq.i32",{selector,c});
    b.branch(checks[i],match,invokes[i],i+1<code.size()?checks[i+1]:done);
    auto result=b.call(invokes[i],n+"_vw_native_op"+std::to_string(i),{1}); b.branch(invokes[i],result,errors[i],dispatch);
    b.ret(errors[i],b.call(errors[i],finish,{1,result}));
  }
  // Basic-block declaration order defines the exchange layout. Stable IDs keep
  // loop-carried execution separate from the static SSA definition identities.
  std::stable_sort(u.entry.region.instructions.begin(),u.entry.region.instructions.end(),[](const auto& a,const auto& c){return a.block<c.block;});
  u.entry.order.clear(); for(const auto& ins:u.entry.region.instructions) u.entry.order.push_back(ins.id);
  u.entry.region=take(schedrow::dependencies(u.entry.region));
  take(machineir_native::verify(u)); return std::move(u);
}
}
Result<std::vector<NativeInstruction>> assemble(const Module& input,std::string_view text) {
  return checked<std::vector<NativeInstruction>>([&] {
    if(text.size()>4*1024*1024) fail("assembly input exceeds 4 MiB",Error::Code::ResourceLimit);
    auto m=take(checked_module(input));
    std::map<std::string,const Word*> words; for(const auto& w:m.words) words[w.name]=&w;
    std::vector<std::string> tokens; std::string token;
    for(size_t i=0;i<text.size();) {
      auto c=static_cast<unsigned char>(text[i]);
      if(std::isspace(c)) {++i; continue;}
      if(c=='#') {while(i<text.size() && text[i]!='\n') ++i; continue;}
      size_t start=i; while(i<text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i]!='#') ++i;
      if(i-start>127 || text.substr(start,i-start).find('\0')!=std::string_view::npos) fail("invalid/oversized assembly token",Error::Code::Parse);
      tokens.emplace_back(text.substr(start,i-start));
    }
    std::map<std::string,uint64_t> labels; std::vector<std::pair<const Word*,std::vector<std::string>>> parsed;
    for(size_t i=0;i<tokens.size();) {
      token=tokens[i++];
      if(token.ends_with(':')) {
        token.pop_back(); if(token.empty() || labels.size()>=256 || !labels.emplace(token,parsed.size()).second) fail("invalid/duplicate label",Error::Code::Parse);
      } else {
        if(parsed.size()>=4096) fail("native tape instruction limit",Error::Code::ResourceLimit);
        if(!words.contains(token)) fail("unknown mnemonic '"+token+"'",Error::Code::Parse);
        const auto* w=words.at(token); if(w->operands.size()>tokens.size()-i) fail("missing operand for '"+token+"'",Error::Code::Parse);
        std::vector<std::string> operands; for(size_t j=0;j<w->operands.size();++j) operands.push_back(tokens[i++]);
        parsed.emplace_back(w,std::move(operands));
      }
    }
    std::vector<NativeInstruction> out;
    for(const auto& [w,args]:parsed) {
      NativeInstruction ins; ins.opcode=w->opcode;
      for(const auto& argument:args) {
        if(argument.starts_with('&')) {auto label=argument.substr(1); if(!labels.contains(label)) fail("unknown label '"+label+"'",Error::Code::Parse); ins.operands.push_back(labels.at(label)); continue;}
        std::string_view number=argument; bool negative=false;
        if(number.starts_with('-') || number.starts_with('+')) {negative=number.front()=='-'; number.remove_prefix(1);}
        int base=10;
        if(number.starts_with("0x") || number.starts_with("0X")) {base=16; number.remove_prefix(2);}
        else if(number.size()>1 && number.front()=='0') base=8;
        uint64_t magnitude=0; auto [end,error]=std::from_chars(number.data(),number.data()+number.size(),magnitude,base);
        bool unsigned_cell=m.configuration.cell=="uint64_t";
        if(number.empty() || error!=std::errc{} || end!=number.data()+number.size() || (unsigned_cell && negative) || (!unsigned_cell && magnitude>(negative?uint64_t(INT64_MAX)+1:uint64_t(INT64_MAX)))) fail("invalid cell literal '"+argument+"'",Error::Code::Parse);
        ins.operands.push_back(negative?uint64_t(0)-magnitude:magnitude);
      }
      out.push_back(std::move(ins));
    }
    NativeOptions limits; limits.instruction_limit=4096; validate_code(m,out,limits); return out;
  });
}
Result<std::string> lower_native(const Module& m,std::span<const NativeInstruction> code,NativeOptions opt) {
  return checked<std::string>([&]{return take(machineir_native::serialize(unit(m,code,opt)));});
}
Result<std::string> emit_native_assembly(const Module& m,std::span<const NativeInstruction> code,NativeOptions opt) {
  return checked<std::string>([&]{return take(machineir_native::emit_assembly(unit(m,code,opt),options(opt)));});
}
struct NativeStorage {
  machineir_native::Library library;
  std::string ir;
  size_t size=0;
  uint64_t abi=0;
  int32_t (*entry)(void*,void*,uint64_t)=nullptr;
};
NativeProgram::NativeProgram(std::shared_ptr<NativeStorage> s):storage_(std::move(s)) {}
Result<NativeProgram> compile_native(const Module& m,std::span<const NativeInstruction> code,NativeOptions opt) {
  return checked<NativeProgram>([&] {
    auto lowered=unit(m,code,opt); auto storage=std::make_shared<NativeStorage>();
    storage->ir=take(machineir_native::serialize(lowered)); storage->library=take(machineir_native::compile(lowered,options(opt)));
    auto n=m.configuration.name;
    storage->size=reinterpret_cast<size_t(*)()>(take(storage->library.symbol(n+"_vw_native_size")))();
    storage->abi=reinterpret_cast<uint64_t(*)()>(take(storage->library.symbol(n+"_vw_native_abi")))();
    storage->entry=reinterpret_cast<int32_t(*)(void*,void*,uint64_t)>(take(storage->library.symbol(lowered.entry.function)));
    return NativeProgram(std::move(storage));
  });
}
Result<int> NativeProgram::execute(void* state,size_t size,uint64_t abi,size_t budget) const {
  auto owner=storage_;
  if(!owner || !state) return Result<int>::err({Error::Code::InvalidArgument,"VMWeave native: empty handle/state"});
  if(size!=owner->size || abi!=owner->abi) return Result<int>::err({Error::Code::Conflict,"VMWeave native: state ABI/layout mismatch"});
  return Result<int>::ok(owner->entry(state,&budget,abi));
}
size_t NativeProgram::state_size() const {return storage_?storage_->size:0;}
uint64_t NativeProgram::state_abi() const {return storage_?storage_->abi:0;}
std::string_view NativeProgram::machine_ir() const {return storage_?std::string_view(storage_->ir):std::string_view{};}
std::string_view NativeProgram::target() const {return storage_?storage_->library.target():std::string_view{};}
std::span<const uint8_t> NativeProgram::image() const {return storage_?storage_->library.image():std::span<const uint8_t>{};}
} // namespace limestone::vmweave
