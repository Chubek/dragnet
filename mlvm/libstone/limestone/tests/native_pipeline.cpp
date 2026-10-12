#include "test.hpp"
#include "limestone/limestone.hpp"
#include "bin2bin/codegen.hpp"
#include <cstring>
#include <fstream>
#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#endif
using namespace limestone;
int main(int argc,char** argv){return test_main([&]{
  CHECK(argc==3);auto metadata=take(metacode::load_isa_file(std::string(argv[1])+"/native-constant.isa"));auto target=take(make_target(metadata));auto object_target=take(bin2bin::object_target(metadata));auto codec=take(bin2bin::from_metacode(metadata));auto bindings=take(bin2bin::encoding_bindings(metadata));
  CHECK(target.instructions.at("CONST").fixed_definitions.at(0)==target.instructions.at("RETURN").fixed_uses.at(0));
  auto physical=target.instructions.at("CONST").fixed_definitions.at(0);std::map<uint32_t,std::string> names;for(auto& r:target.register_classes[0].members)names[r]=r==physical?"rax":"r10";
  auto other=std::find_if(names.begin(),names.end(),[&](auto& item){return item.first!=physical;})->first;
  PipelineOptions options;options.allocate=options.encode=true;
  for(auto selector:{SelectionStrategy::Global,SelectionStrategy::Greedy,SelectionStrategy::BURS})for(auto allocator:{AllocationStrategy::LinearScan,AllocationStrategy::Greedy,AllocationStrategy::GraphColoring,AllocationStrategy::Constraint,AllocationStrategy::PBQP}) {
    options.selector=selector;options.allocator=allocator;
    for(auto expression:{"((lambda x (add x 2)) 40)","(if 0 (add 9223372036854775807 1) -42)","-2147483648","2147483647"}) {
      auto module=take(run_pipeline(expression,target,options));auto expected=take(traceml::evaluate(take(traceml::compile(expression))));
      CHECK(module.encoded&&module.encoded->bytes.size()==8&&module.encoded->bytes[0]==0x48&&module.encoded->bytes[1]==0xc7&&module.encoded->bytes[2]==0xc0&&module.encoded->bytes.back()==0xc3);
      CHECK(module.allocation->regs.at(1)==physical&&module.allocation_problem.ranges[0].constraint.fixed==physical&&module.stages.front()=="frontend");
      auto object=take(make_object(module,object_target,"native_entry"));auto elf=take(bin2bin::emit_elf(object));auto loaded=take(bin2bin::load_elf(elf));
      auto wrong=*module.allocation;wrong.regs[1]=other;fails(bin2bin::encode_region(codec,bindings,names,module.selected,module.order,wrong),Error::Code::Conflict);
      auto fixed_field=bindings;fixed_field["CONST"]["imm"]={bin2bin::OperandBinding::Kind::FixedDefinition,0,"rax"};fails(bin2bin::encode_region(codec,fixed_field,names,module.selected,module.order,module.allocation),Error::Code::Unsupported);
#if defined(__linux__) && defined(__x86_64__)
      void* memory=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(memory!=MAP_FAILED);auto linked=take(bin2bin::link_objects(std::span{&loaded,1},object_target,{uint64_t(reinterpret_cast<uintptr_t>(memory))+16,4080}));
      std::memcpy(static_cast<uint8_t*>(memory)+16,linked.bytes.data(),linked.bytes.size());CHECK(mprotect(memory,4096,PROT_READ|PROT_EXEC)==0);auto entry=reinterpret_cast<int64_t(*)()>(static_cast<uintptr_t>(linked.symbols.at("native_entry")));CHECK(entry()==expected);CHECK(munmap(memory,4096)==0);
#else
      (void)expected;
#endif
    }
  }
  options.selector=SelectionStrategy::Global;options.allocator=AllocationStrategy::LinearScan;
  for(auto expression:{"2147483648","-2147483649"})fails(run_pipeline(expression,target,options),Error::Code::Unsatisfiable);
  auto no_allocation=options;no_allocation.allocate=false;fails(run_pipeline("42",target,no_allocation),Error::Code::Unsupported);
  auto tracing=options;tracing.trace_execution=true;fails(run_pipeline("(add 20 22)",target,tracing),Error::Code::Unsatisfiable);
  auto incompatible=target;incompatible.instructions["RETURN"].fixed_uses[0]=other;fails(run_pipeline("42",incompatible,options),Error::Code::Conflict);
  incompatible=target;incompatible.instructions["RETURN"].fixed_uses[1]=physical;fails(run_pipeline("42",incompatible,options),Error::Code::Conflict);
  incompatible=target;incompatible.constraints[1].fixed=other;fails(run_pipeline("42",incompatible,options),Error::Code::Conflict);
  incompatible=target;incompatible.constraints[1].forbidden={physical};fails(run_pipeline("42",incompatible,options),Error::Code::Unsatisfiable);
  incompatible=target;incompatible.allocation_adapter=[](auto&,auto&,auto){return Result<regtl::Program>::ok({});};fails(run_pipeline("42",incompatible,options),Error::Code::Conflict);
  regtl::Program allocation{{{1,0,1,"G"}},{{"G",{1,2}}}};take(regtl::with_fixed_registers(allocation,std::vector<std::pair<uint32_t,uint32_t>>{{1,1},{1,1}}));CHECK(!allocation.ranges[0].constraint.fixed);
  fails(regtl::with_fixed_registers(allocation,std::vector<std::pair<uint32_t,uint32_t>>{{1,1},{1,2}}),Error::Code::Conflict);
  fails(regtl::with_fixed_registers(allocation,std::vector<std::pair<uint32_t,uint32_t>>{{99,1}}),Error::Code::Conflict);
  fails(regtl::with_fixed_registers(allocation,std::vector<std::pair<uint32_t,uint32_t>>{{1,3}}),Error::Code::Unsatisfiable);
  auto isa=metadata;auto& fields=std::get<metacode::Value::Object>(isa.operations[0].fields.at("encoding_operands").data);auto& fixed=std::get<metacode::Value::Object>(fields.at("result").data);fixed["register"]=metacode::Value(std::string("missing"));fails(make_target(isa),Error::Code::InvalidArgument);
  fixed.erase("register");fails(bin2bin::encoding_bindings(isa),Error::Code::InvalidArgument);
  auto module=take(run_pipeline("((lambda x (add x 2)) 40)",target,options));auto object=take(make_object(module,object_target,"native_entry"));auto elf=take(bin2bin::emit_elf(object));std::ofstream output(std::string(argv[2])+"/native-pipeline.o",std::ios::binary);output.write(reinterpret_cast<const char*>(elf.data()),elf.size());CHECK(output.good());
});}
