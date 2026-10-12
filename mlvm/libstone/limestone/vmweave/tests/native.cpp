#include "vmweave/native.hpp"
#include "metacode/machine-ir/native_c.hpp"
#include "Calculator.h"
#include "Hooked.h"
#include "tests/test.hpp"
#include <bit>
#include <cstring>

using namespace limestone;
using namespace limestone::vmweave;
namespace {
struct HookState { std::vector<size_t> before, after; int stop=0; };
int before(Hooked_vm* vm,uint32_t,void* input) {
  auto& context=*static_cast<HookState*>(input); context.before.push_back(vm->vw_pc);
  if(context.stop) {vm->vw_pc=1; return context.stop;}
  return 0;
}
void after(Hooked_vm* vm,uint32_t,void* input) {
  static_cast<HookState*>(input)->after.push_back(vm->vw_pc);
}
struct HookedThreaded { Hooked_handler address; Hooked_instruction instruction; };
}
int main(int argc,char** argv) {return test_main([&] {
  CHECK(argc==2);
  auto module=take(lower(take(load_file(std::string(argv[1])+"/examples/calculator.lua"))));
  auto differential=[&](std::string_view source,size_t budget,int expected) {
    auto code=take(assemble(module,source));
    auto native=take(compile_native(module,code));
    CHECK(native.state_size()==sizeof(Calculator_vm)); CHECK(native.state_abi()==Calculator_vw_abi());
    CHECK(!native.target().empty() && !native.image().empty());
    auto ir=take(machineir_native::deserialize(native.machine_ir()));
    CHECK(ir.entry.region.blocks.size()>1 && !ir.callees.empty());
    CHECK(take(machineir_native::serialize(ir))==native.machine_ir());
    CHECK(machineir_bridge::deserialize(take(machineir_bridge::serialize(ir.entry))));
    Calculator_instruction storage[128]; Calculator_tape tape={storage,0,128}; Calculator_vm reference,state;
    Calculator_init(&reference); Calculator_init(&state);
    CHECK(!Calculator_compile(std::string(source).c_str(),&tape));
    CHECK(Calculator_run(&reference,&tape,budget)==expected);
    CHECK(take(native.execute(&state,sizeof(state),Calculator_vw_abi(),budget))==expected);
    CHECK(state.vw_pc==reference.vw_pc && state.vw_sp==reference.vw_sp && state.vw_error==reference.vw_error && state.vw_halted==reference.vw_halted && !state.vw_current);
    CHECK(!std::memcmp(state.vw_stack,reference.vw_stack,sizeof(state.vw_stack)));
    return native;
  };
  auto held=differential("PUSH 10 PUSH 20 ADD HALT",100,0);
  differential("PUSH 10 PUSH 4 SUB PUSH 7 MUL PUSH 2 DIV DUP POP HALT",100,0);
  differential("PUSH 0 JZ &yes PUSH 999 yes: PUSH 42 JUMP &end PUSH 999 end: HALT",100,0);
  differential("ADD HALT",100,-2);
  differential("PUSH 1 PUSH 0 DIV HALT",100,-10);
  differential("PUSH 1 loop: DUP JUMP &loop",200,-3);
  differential("loop: JUMP &loop",20,-7);
  auto resumable=take(compile_native(module,take(assemble(module,"PUSH 10 PUSH 20 ADD HALT"))));
  Calculator_vm state; Calculator_init(&state);
  CHECK(take(resumable.execute(&state,sizeof(state),Calculator_vw_abi(),2))==-7 && state.vw_pc==2);
  CHECK(take(resumable.execute(&state,sizeof(state),Calculator_vw_abi(),100))==0 && state.vw_stack[0]==30);
  Calculator_init(&state);
  fails(held.execute(&state,sizeof(state)-1,Calculator_vw_abi(),100),Error::Code::Conflict);
  fails(held.execute(&state,sizeof(state),Calculator_vw_abi()^1,100),Error::Code::Conflict);
  CHECK(state.vw_pc==0 && state.vw_sp==0);
  {auto copy=held; held=NativeProgram{}; CHECK(take(copy.execute(&state,sizeof(state),Calculator_vw_abi(),100))==0 && state.vw_stack[0]==30);}
  auto changed=module;
  auto word=std::find_if(changed.words.begin(),changed.words.end(),[](const auto& w){return w.name=="ADD";});
  CHECK(word!=changed.words.end()); word->name="MISLEADING_ADD";
  word->c_body="cell b=vm_pop(); cell a=vm_pop(); cell total=0; for(cell i=0;i<b;++i) total+=a; vm_push(total);";
  auto generic_c=take(compile_native(changed,take(assemble(changed,"PUSH 6 PUSH 7 MISLEADING_ADD HALT"))));
  Calculator_init(&state); CHECK(take(generic_c.execute(&state,sizeof(state),Calculator_vw_abi(),100))==0 && state.vw_stack[0]==42);
  auto invalid=module; invalid.words[0].c_body="this is not valid C;";
  auto bad=compile_native(invalid,take(assemble(invalid,"PUSH 1 HALT")));
  CHECK(!bad && bad.error().code==Error::Code::Parse && bad.error().message.find("calculator.lua")!=std::string::npos);
  NativeOptions unavailable; unavailable.compiler="/bin/false";
  CHECK(!compile_native(module,{},unavailable));
  NativeOptions packed; packed.compile_arguments={"-fpack-struct=1"};
  auto different_abi=take(compile_native(module,take(assemble(module,"HALT")),packed));
  Calculator_init(&state); fails(different_abi.execute(&state,sizeof(state),Calculator_vw_abi(),100),Error::Code::Conflict);
  auto code=take(assemble(module,"PUSH 10 PUSH 20 ADD HALT"));
  auto assembly=take(emit_native_assembly(module,code)); CHECK(assembly.find("Calculator_vw_native_entry")!=std::string::npos);
  auto unit=take(machineir_native::deserialize(take(lower_native(module,code))));
  auto malformed=unit; malformed.callees.begin()->second="missing"; CHECK(!machineir_native::verify(malformed));
  malformed=unit; malformed.entry.region.instructions.front().speculative=true; CHECK(!machineir_native::verify(malformed));
  malformed=unit; malformed.entry.region.instructions.front().access.reset(); CHECK(!machineir_native::verify(malformed));
  malformed=unit; malformed.entry.region.instructions.front().implicit_defs={1}; CHECK(!machineir_native::verify(malformed));
  malformed=unit; malformed.entry.region.instructions.front().uses[0]=99999; CHECK(!machineir_native::verify(malformed));
  malformed=unit; malformed.entry.values.front().type="u128"; CHECK(!machineir_native::verify(malformed));
  CHECK(!assemble(module,"JUMP &missing")); CHECK(!assemble(module,"JUMP -1"));
  CHECK(!assemble(module,"PUSH 999999999999999999999999"));
  CHECK(!assemble(module,"PUSH 1 PUSH"));
  auto hooked=take(load_file(std::string(argv[1])+"/tests/native_hooks.lua"));
  for(const auto& mode:std::vector<std::string>{"switch","subroutine","indirect","direct"}) {
    auto config=hooked; config.execution=mode;
    if(mode!="switch") config.components.push_back(mode=="subroutine"?"srtbl":mode=="indirect"?"addrtbl":"insntbl");
    auto model=take(lower(config));
    auto tape=take(assemble(model,"PUSH 18446744073709551615 HALT"));
    auto native=take(compile_native(model,tape));
    auto library=take(machineir_native::compile(take(machineir_native::deserialize(native.machine_ir()))));
    Hooked_instruction ins[2]{};
    for(size_t k=0;k<2;++k) {ins[k].opcode=tape[k].opcode; ins[k].operand_count=tape[k].operands.size(); for(size_t j=0;j<tape[k].operands.size();++j) ins[k].operands[j]=tape[k].operands[j];}
    Hooked_tape reference_tape{ins,2,2};
    for(int stopped:{0,17}) {
      HookState reference_hooks, native_hooks; reference_hooks.stop=native_hooks.stop=stopped;
      Hooked_vm reference,state; Hooked_init(&reference); Hooked_init(&state);
      reference.vmweave_before=state.vmweave_before=before; reference.vmweave_after=state.vmweave_after=after;
      reference.vmweave_userdata=&reference_hooks; state.vmweave_userdata=&native_hooks;
      int status;
      if(mode=="direct") {
        HookedThreaded threaded[2];
        auto thread=reinterpret_cast<int(*)(const Hooked_tape*,HookedThreaded*,size_t)>(take(library.symbol("Hooked_thread")));
        CHECK(!thread(&reference_tape,threaded,2));
        auto run=reinterpret_cast<int(*)(Hooked_vm*,const HookedThreaded*,size_t,size_t)>(take(library.symbol("Hooked_run_direct")));
        status=run(&reference,threaded,2,100);
      } else {
        auto run=reinterpret_cast<int(*)(Hooked_vm*,const Hooked_tape*,size_t)>(take(library.symbol("Hooked_run")));
        status=run(&reference,&reference_tape,100);
      }
      CHECK(status==stopped);
      CHECK(take(native.execute(&state,sizeof(state),Hooked_vw_abi(),100))==status);
      CHECK(state.vw_pc==reference.vw_pc && state.vw_sp==reference.vw_sp && state.vw_halted==reference.vw_halted);
      CHECK(native_hooks.before==reference_hooks.before && native_hooks.after==reference_hooks.after);
      if(!stopped) CHECK(state.vw_stack[0]==UINT64_MAX && state.vw_stack[0]==reference.vw_stack[0]);
    }
  }
});}
