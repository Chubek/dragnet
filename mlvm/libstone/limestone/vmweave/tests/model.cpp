#include "vmweave/vmweave.hpp"
#include "metacode/machine-ir/bridge.hpp"
#include "tests/test.hpp"
using namespace limestone;
using namespace limestone::vmweave;
int main() { return test_main([] {
  auto spec=take(load_lua(R"(local v=require('vmweave'); return v.vm {
    name='Example', components={'insncode'}, execution='none',
    fields={{name='z',type='u8'},{name='a',type='i64'}},
    instructions={{name='Z',opcode=3,semantics='vm->a++;'},{name='A',semantics='/* }c ; */\nvm->z++;'}}
  })","example.lua"));
  CHECK(spec.fields[0].name=="a");
  auto m=take(lower(spec)); CHECK(m.words[0].name=="A" && m.words[0].opcode==0);
  auto stk=take(print_stk(m)); auto parsed=take(parse_stk(stk));
  CHECK(take(print_stk(parsed))==stk);
  CHECK(take(emit_c(parsed))==take(emit_c(m)));
  auto artifacts=take(generate(m)); CHECK(artifacts.files.size()==4);
  CHECK(artifacts.files.contains("Example.insncode")); CHECK(!artifacts.files.contains("Example.object"));
  auto duplicate=spec; duplicate.instructions[0].opcode=3;
  fails(validate(duplicate),Error::Code::Conflict);
  auto bad=spec; bad.instructions[0].stack_effect="( missing )"; CHECK(!lower(bad));
  bad=spec; bad.components.push_back("dispatc"); CHECK(!lower(bad));
  bad=spec; bad.fields.push_back({"vw_sp","size",0}); CHECK(!lower(bad));
  bad=spec; bad.instructions[0].semantics=std::string("a\0b",3); CHECK(!lower(bad));
  CHECK(!parse_stk(stk+"unexpected")); CHECK(!parse_stk("stk-00 999"));
  CHECK(!parse_stk(stk.substr(0,stk.size()/2)));
  CHECK(!load_lua("return {name='Bad',instructions={{name='X',opcode=1.5}}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',instructions={{name='X',opcode=0/0}}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',hooks='yes'}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',instructions={{name='X',semantics=7}}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',components={'insncode'},subsystems={insncode=7}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',instructions={{name='vm'}}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',surprise=1}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',components={'object'}}","bad.lua"));
  CHECK(!load_lua("return {name='Bad',instructions={{name='X',operands={'ptr'}}}}","bad.lua"));
  CHECK(!load_lua("error('specific failure')","bad.lua"));
  auto diagnostic=load_lua("local v=require('vmweave'); return v.vm {name='Bad',instructions={{name='X',opcode=2},{name='Y',opcode=2}}}","duplicate.lua");
  CHECK(!diagnostic && diagnostic.error().message.find("duplicate.lua")!=std::string::npos);
  auto c=take(load_lua("local v=require('vmweave'); local m=v.vm('Counter'); m:instruction('inc',{body='(void)vm;'}); assert(m:emit():find('Counter_inc')); return m"));
  CHECK(c.name=="Counter");
  struct InvalidAdapter : MachineIRAdapter {
    Result<std::string> lower(const Module&) override {return Result<std::string>::ok("{}");}
  } invalid;
  CHECK(!emit_machineir(m,invalid));
  struct EmptyAdapter : MachineIRAdapter {
    Result<std::string> lower(const Module&) override {
      machineir_bridge::RegionExchange x; x.target="generic";
      return machineir_bridge::serialize(x);
    }
  } empty;
  Specification empty_spec; empty_spec.name="Empty"; empty_spec.execution="none";
  CHECK(machineir_bridge::deserialize(take(emit_machineir(take(lower(empty_spec)),empty))));
}); }
