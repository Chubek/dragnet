#include "limestone.hpp"
#include "../bin2bin/bin2bin.hpp"
#include "../bin2bin/object.hpp"
#include "../metacode/metacode.hpp"
#include "../traceml/traceml.hpp"
#include "../tunah/tunah.hpp"
#include "limeburg/text.hpp"
#include "schedrow/text.hpp"
#include "regtl/text.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::string read_input(const std::string& path) {
  std::ostringstream contents;
  if(path.empty()||path=="-")contents<<std::cin.rdbuf();
  else {std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("cannot open input: "+path);contents<<file.rdbuf();if(file.bad())throw std::runtime_error("cannot read input: "+path);}
  if(std::cin.bad())throw std::runtime_error("cannot read standard input");
  return contents.str();
}
template<class T> T checked(limestone::Result<T> result) {
  if(!result)throw std::runtime_error(result.error().message);return std::move(result.value());
}
size_t number(const std::string& text) {
  size_t value=0;
  auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
  if(error!=std::errc{}||end!=text.data()+text.size())throw std::runtime_error("expected a non-negative integer: "+text);
  return value;
}
void help() {
  std::cout<<"Usage: limestone-cli [options] [input-file|-]\n"
     "  --evaluate                 Evaluate a closed TraceML integer program\n"
     "  --trace-execution          Preserve MetaKrivine execution and guard IR\n"
     "  --compile-umd              Compile a machine and graph UMD document\n"
      "  --target-isa ISA           Use an explicit target for TraceML or UMD\n"
      "  --encode --allocate        Emit bytes using explicit encoding contracts\n"
      "  --object SYMBOL            Package encoded compilation as ELF64 ET_REL\n"
      "  --inspect-object           Inspect ELF64 sections, symbols and RELA\n"
      "  --wrap-object ISA SYMBOL   Package raw code using object-file metadata\n"
      "  --link-objects ISA         Link objects to a raw addressed image\n"
      "  --input-object FILE        Add an ELF object or ar archive (repeatable)\n"
      "  --base-address N           Set the linked image installation address\n"
      "  --external-symbol NAME N   Resolve an external link symbol (repeatable)\n"
     "  --machineir-json           Emit the C++/D MachineIR region exchange\n"
      "  --emit-umd ISA             Normalize Infobank metadata to UMD\n"
       "  --select-burs              Select trees from a Limeburg rule document\n"
       "  --analyze-burs             Inspect BURS states and rejected rules\n"
      "  --schedule-il              Schedule a textual Schedrow document\n"
      "  --modulo N                 Use modulo scheduling with initiation interval N\n"
      "  --allocate-il              Allocate textual RegTL units/functions\n"
     "  --selector global|greedy|burs  Select instruction-selection algorithm\n"
     "  --allocator linear|greedy|color|constraint|pbqp  Select register allocator\n"
    "  --optimize-term            Optimize an S-expression with Tunah\n"
    "  --rules FILE               Load operator/rule declarations (repeatable)\n"
    "  --op-cost OP COST          Set a local extraction cost for an operator\n"
    "  --literal-cost COST        Set the local extraction cost of literals\n"
    "  --iterations N --node-limit N --class-limit N --time-limit MS\n"
    "                             Bound Tunah saturation\n"
    "  --trace                    Print Tunah matches and statistics to stderr\n"
    "  --no-optimize --no-schedule Configure portable MachineIR lowering\n"
    "  --isa FILE                 Parse and summarize an Infobank description\n"
    "  --disassemble ISA          Decode fixed8 or masked instruction forms\n"
    "  --decompile ISA            Lift binary code to semantic assembly\n"
    "  --translate SOURCE TARGET  Translate semantically equivalent forms\n"
    "  --cache DIRECTORY          Use a persistent LMDB translation cache\n"
    "  -o, --output FILE          Write output to a file (default: stdout)\n"
    "  --help --version\n";
}
}
int main(int argc,char** argv) {
  try {
    enum class Command { Compile, CompileUMD, EmitUMD, SelectBURS, AnalyzeBURS, ScheduleIL, AllocateIL, Evaluate, OptimizeTerm, ISA, Disassemble, Decompile, Translate, InspectObject, WrapObject, LinkObjects } command=Command::Compile;
    limestone::PipelineOptions options;std::string input,output,source,target,cache_path,pipeline_isa;bool mode_set=false;
    limestone::tunah::CostModel costs;limestone::tunah::Limits limits;limits.trace=false;
    std::vector<std::string> rule_files;bool tunah_options=false,machineir_json=false;uint32_t modulo=0;
    std::string object_symbol;std::vector<std::string> object_files;limestone::bin2bin::LinkOptions link_options;bool link_arguments=false;
    auto next=[&](int& i){if(++i>=argc)throw std::runtime_error("missing option argument");return std::string(argv[i]);};
    auto mode=[&](Command value){if(mode_set)throw std::runtime_error("choose one operation");command=value;mode_set=true;};
    for(int i=1;i<argc;++i) {
      std::string arg=argv[i];
      if(arg=="--help"||arg=="-h"){help();return 0;}
      if(arg=="--version"){std::cout<<"Limestone 0.1.0\n";return 0;}
      if(arg=="--evaluate")mode(Command::Evaluate);
      else if(arg=="--trace-execution")options.trace_execution=true;
      else if(arg=="--compile-umd")mode(Command::CompileUMD);
      else if(arg=="--select-burs")mode(Command::SelectBURS);
      else if(arg=="--analyze-burs")mode(Command::AnalyzeBURS);
      else if(arg=="--schedule-il")mode(Command::ScheduleIL);
      else if(arg=="--allocate-il")mode(Command::AllocateIL);
      else if(arg=="--modulo"){auto value=number(next(i));if(!value||value>UINT32_MAX)throw std::runtime_error("modulo interval must be a positive 32-bit integer");modulo=uint32_t(value);}
      else if(arg=="--target-isa")pipeline_isa=next(i);
      else if(arg=="--encode")options.encode=true;
      else if(arg=="--object")object_symbol=next(i);
      else if(arg=="--inspect-object")mode(Command::InspectObject);
      else if(arg=="--wrap-object"){mode(Command::WrapObject);source=next(i);object_symbol=next(i);}
      else if(arg=="--link-objects"){mode(Command::LinkObjects);source=next(i);}
      else if(arg=="--input-object"){object_files.push_back(next(i));link_arguments=true;}
      else if(arg=="--base-address"){link_options.base_address=number(next(i));link_arguments=true;}
      else if(arg=="--external-symbol"){auto name=next(i);auto address=number(next(i));if(name.empty()||!link_options.externals.emplace(name,address).second)throw std::runtime_error("invalid or duplicate external symbol");link_arguments=true;}
      else if(arg=="--machineir-json")machineir_json=true;
      else if(arg=="--emit-umd"){mode(Command::EmitUMD);source=next(i);}
      else if(arg=="--selector"){
        auto value=next(i);if(value=="global")options.selector=limestone::SelectionStrategy::Global;else if(value=="greedy")options.selector=limestone::SelectionStrategy::Greedy;else if(value=="burs")options.selector=limestone::SelectionStrategy::BURS;else throw std::runtime_error("unknown instruction selector: "+value);
      }
      else if(arg=="--allocator"){
        auto value=next(i);if(value=="linear")options.allocator=limestone::AllocationStrategy::LinearScan;else if(value=="greedy")options.allocator=limestone::AllocationStrategy::Greedy;else if(value=="color")options.allocator=limestone::AllocationStrategy::GraphColoring;else if(value=="constraint")options.allocator=limestone::AllocationStrategy::Constraint;else if(value=="pbqp")options.allocator=limestone::AllocationStrategy::PBQP;else throw std::runtime_error("unknown allocator: "+value);
      }
      else if(arg=="--optimize-term")mode(Command::OptimizeTerm);
      else if(arg=="--rules"){rule_files.push_back(next(i));tunah_options=true;}
      else if(arg=="--op-cost"){
        auto op=next(i);auto cost=number(next(i));
        if(!costs.operators.emplace(op,cost).second)throw std::runtime_error("duplicate operator cost: "+op);
        tunah_options=true;
      }
      else if(arg=="--literal-cost"){costs.literal=number(next(i));tunah_options=true;}
      else if(arg=="--iterations"){limits.iterations=number(next(i));tunah_options=true;}
      else if(arg=="--node-limit"){limits.nodes=number(next(i));tunah_options=true;}
      else if(arg=="--class-limit"){limits.classes=number(next(i));tunah_options=true;}
      else if(arg=="--time-limit"){limits.time_ms=number(next(i));tunah_options=true;}
      else if(arg=="--trace"){limits.trace=true;tunah_options=true;}
      else if(arg=="--isa"){mode(Command::ISA);source=next(i);}
      else if(arg=="--disassemble"){mode(Command::Disassemble);source=next(i);}
      else if(arg=="--decompile"){mode(Command::Decompile);source=next(i);}
      else if(arg=="--translate"){mode(Command::Translate);source=next(i);target=next(i);}
      else if(arg=="--no-optimize")options.optimize=false;
      else if(arg=="--no-schedule")options.schedule=false;
      else if(arg=="--allocate")options.allocate=true;
      else if(arg=="-o"||arg=="--output")output=next(i);
      else if(arg=="--cache")cache_path=next(i);
      else if(arg!="-"&&arg.starts_with('-'))throw std::runtime_error("unknown option: "+arg);
      else {if(!input.empty())throw std::runtime_error("too many input files");input=arg;}
    }
    if(command!=Command::Compile&&command!=Command::CompileUMD&&(!options.optimize||!options.schedule||options.allocate||options.encode||options.trace_execution||options.selector!=limestone::SelectionStrategy::Global||(command!=Command::AllocateIL&&options.allocator!=limestone::AllocationStrategy::LinearScan)))throw std::runtime_error("pipeline options require compilation");
    if(modulo&&command!=Command::ScheduleIL)throw std::runtime_error("--modulo requires --schedule-il");
    if(machineir_json&&(options.encode||(command!=Command::Compile&&command!=Command::CompileUMD)))throw std::runtime_error("--machineir-json requires compilation to MachineIR");
    if(!pipeline_isa.empty()&&command!=Command::Compile&&command!=Command::CompileUMD)throw std::runtime_error("--target-isa requires compilation");
    if(command==Command::CompileUMD&&options.trace_execution)throw std::runtime_error("--trace-execution requires TraceML source");
    if(command!=Command::Translate&&!cache_path.empty())throw std::runtime_error("--cache requires --translate");
    if(command!=Command::OptimizeTerm&&tunah_options)throw std::runtime_error("Tunah options require --optimize-term");
    if(link_arguments&&command!=Command::LinkObjects)throw std::runtime_error("link arguments require --link-objects");
    if(!object_symbol.empty()&&command!=Command::WrapObject&&((command!=Command::Compile&&command!=Command::CompileUMD)||!options.encode||pipeline_isa.empty()||machineir_json))throw std::runtime_error("--object requires compilation with --target-isa --encode");
    std::string result;
    if(command==Command::ISA||command==Command::EmitUMD) {
      if(!input.empty())throw std::runtime_error("--isa does not accept a separate input file");
      auto architecture=checked(limestone::metacode::load_isa_file(source));
      result=command==Command::ISA?limestone::metacode::dump(architecture):limestone::unisel::print_umd(checked(limestone::unisel::from_metacode(architecture)));
    }else if(command==Command::LinkObjects) {
      if(!input.empty())object_files.push_back(input);if(object_files.empty())throw std::runtime_error("link needs --input-object inputs");
      auto target=checked(limestone::bin2bin::object_target(checked(limestone::metacode::load_isa_file(source))));std::vector<limestone::bin2bin::ObjectFile> objects;std::vector<limestone::bin2bin::ObjectArchive> archives;
      for(auto& path:object_files){auto data=read_input(path);auto bytes=std::span{reinterpret_cast<const uint8_t*>(data.data()),data.size()};if(data.starts_with("!<arch>\n")||data.starts_with("!<thin>\n"))archives.push_back(checked(limestone::bin2bin::load_archive(bytes,path)));else objects.push_back(checked(limestone::bin2bin::load_elf(bytes,path)));}
      auto image=checked(limestone::bin2bin::link_archives(objects,archives,target,link_options));result.assign(image.bytes.begin(),image.bytes.end());
    }else {
      auto contents=(!input.empty()&&input!="-"&&(command==Command::CompileUMD||command==Command::SelectBURS||command==Command::AnalyzeBURS))?std::string{}:read_input(input);
      if(command==Command::Compile){std::optional<limestone::metacode::Architecture> metadata;if(!pipeline_isa.empty())metadata=checked(limestone::metacode::load_isa_file(pipeline_isa));auto module=metadata?checked(limestone::run_pipeline(contents,checked(limestone::make_target(*metadata)),options)):checked(limestone::run_pipeline(contents,options));if(!object_symbol.empty()){auto object=checked(limestone::make_object(module,checked(limestone::bin2bin::object_target(*metadata)),object_symbol));auto bytes=checked(limestone::bin2bin::emit_elf(object));result.assign(bytes.begin(),bytes.end());}else if(module.encoded)result.assign(module.encoded->bytes.begin(),module.encoded->bytes.end());else result=machineir_json?module.machine_ir_exchange:module.machine_ir;if(module.execution)std::cerr<<limestone::traceml::print_trace(*module.execution);}
      else if(command==Command::CompileUMD){auto document=checked(input.empty()||input=="-"?limestone::unisel::load_umd(contents,"<stdin>"):limestone::unisel::load_umd_file(input));if(!document.program)throw std::runtime_error("UMD has no source program");std::optional<limestone::metacode::Architecture> metadata;if(!pipeline_isa.empty())metadata=checked(limestone::metacode::load_isa_file(pipeline_isa));auto target=metadata?checked(limestone::make_target(*metadata)):checked(limestone::make_target(document.machine));auto module=checked(limestone::run_pipeline(*document.program,target,options));if(!object_symbol.empty()){auto object=checked(limestone::make_object(module,checked(limestone::bin2bin::object_target(*metadata)),object_symbol));auto bytes=checked(limestone::bin2bin::emit_elf(object));result.assign(bytes.begin(),bytes.end());}else if(module.encoded)result.assign(module.encoded->bytes.begin(),module.encoded->bytes.end());else result=machineir_json?module.machine_ir_exchange:module.machine_ir;}
      else if(command==Command::InspectObject){auto bytes=std::span{reinterpret_cast<const uint8_t*>(contents.data()),contents.size()};if(contents.starts_with("!<arch>\n")){auto archive=checked(limestone::bin2bin::load_archive(bytes,input));for(auto& member:archive.members)result+="member "+member.name+"\n"+limestone::bin2bin::print_object(member.object);}else result=limestone::bin2bin::print_object(checked(limestone::bin2bin::load_elf(bytes,input)));}
      else if(command==Command::WrapObject){auto target=checked(limestone::bin2bin::object_target(checked(limestone::metacode::load_isa_file(source))));auto object=checked(limestone::bin2bin::code_object(target,{reinterpret_cast<const uint8_t*>(contents.data()),contents.size()},object_symbol));auto bytes=checked(limestone::bin2bin::emit_elf(object));result.assign(bytes.begin(),bytes.end());}
      else if(command==Command::Evaluate)result=std::to_string(checked(limestone::traceml::evaluate(checked(limestone::traceml::compile(contents)))))+"\n";
      else if(command==Command::SelectBURS||command==Command::AnalyzeBURS){auto document=checked(input.empty()||input=="-"?limestone::limeburg::load_rules(contents,"<stdin>"):limestone::limeburg::load_rules_file(input));if(document.trees.empty())throw std::runtime_error("Limeburg document has no input trees");for(auto& tree:document.trees){if(command==Command::AnalyzeBURS){result+="tree "+tree.name+"\n";result+=checked(limestone::limeburg::print_analysis(checked(limestone::limeburg::analyze(tree.nodes,tree.root,document.rules,true)),document.rules));}else {auto selected=checked(limestone::limeburg::select(tree.nodes,tree.root,document.rules,tree.nonterminal));auto region=checked(limestone::limeburg::emit_scheduler(tree.nodes,document.rules,selected));region.name=tree.name;result+=limestone::schedrow::print(region)+"cost = "+std::to_string(selected.cost)+"\n";}}}
      else if(command==Command::ScheduleIL){auto document=checked(limestone::schedrow::load_schedrow(contents,input.empty()?"<stdin>":input));for(auto& text:document.regions){auto schedule=modulo?checked(limestone::schedrow::schedule_modulo(text.region,document.machine,{modulo})):checked(limestone::schedrow::schedule(text.region,document.machine));if(modulo)checked(limestone::schedrow::verify_modulo(text.region,document.machine,schedule,modulo));else checked(limestone::schedrow::verify(text.region,document.machine,schedule));result+=limestone::schedrow::print(text.region);for(auto& s:schedule){result+="instruction "+std::to_string(s.id)+" cycle "+std::to_string(s.cycle);if(s.slot)result+=" slot "+std::to_string(*s.slot);for(auto& r:s.resources)result+=" resource "+r;result+='\n';}}}
      else if(command==Command::AllocateIL){auto units=checked(limestone::regtl::load_regtl(contents,input.empty()?"<stdin>":input));auto allocate=[&](const limestone::regtl::Program& p,const limestone::regtl::PbqpOptions& policy){using A=limestone::AllocationStrategy;switch(options.allocator){case A::LinearScan:return limestone::regtl::linear_scan(p);case A::Greedy:return limestone::regtl::greedy(p);case A::GraphColoring:return limestone::regtl::graph_color(p);case A::Constraint:return limestone::regtl::constraint_allocate(p);case A::PBQP:return limestone::regtl::pbqp_allocate(p,policy);}throw std::runtime_error("unknown allocator");};for(auto& unit:units){auto emit_allocation=[&](const auto& problem,const auto& allocation){result+=limestone::regtl::print(problem);std::map<uint32_t,uint32_t> assignments(allocation.regs.begin(),allocation.regs.end());for(auto [v,r]:assignments)result+="v"+std::to_string(v)+" -> physical "+std::to_string(r)+"\n";for(auto v:allocation.spilled)result+="v"+std::to_string(v)+" -> spill\n";};if(unit.functions.empty())emit_allocation(unit.problem,checked(allocate(unit.problem,unit.pbqp)));else for(auto& f:unit.functions){auto live=checked(limestone::regtl::analyze(f.function));emit_allocation(live.problem,checked(allocate(live.problem,unit.pbqp)));}}}
      else if(command==Command::OptimizeTerm){
        limestone::tunah::Session session;
        for(const auto& path:rule_files)checked(session.load_rules_file(path));
        auto term=checked(limestone::tunah::parse_term(contents,input.empty()||input=="-"?"<stdin>":input));
        auto optimized=checked(session.saturate(term,limits,costs));
        result=optimized.expression+"\n";
        if(limits.trace){
          for(const auto& match:optimized.trace)std::cerr<<"tunah: "<<match<<'\n';
          std::cerr<<"tunah: cost="<<optimized.cost<<" rewrites="<<optimized.rewrites
            <<" nodes="<<optimized.nodes<<" classes="<<optimized.classes<<" iterations="<<optimized.iterations
            <<" saturated="<<optimized.saturated<<" limit_reached="<<optimized.limit_reached<<'\n';
        }
      }
      else {
        auto src=checked(limestone::bin2bin::from_metacode(checked(limestone::metacode::load_isa_file(source))));
        std::span<const uint8_t> bytes{reinterpret_cast<const uint8_t*>(contents.data()),contents.size()};
        if(command==Command::Disassemble)result=limestone::bin2bin::disassemble(checked(limestone::bin2bin::decode(src,bytes)));
        else if(command==Command::Decompile)result=checked(limestone::bin2bin::decompile(src,bytes));
        else {
          auto dst=checked(limestone::bin2bin::from_metacode(checked(limestone::metacode::load_isa_file(target))));
          limestone::bin2bin::TranslationCache cache;if(!cache_path.empty())checked(limestone::bin2bin::open_cache(cache,cache_path));
          auto translated=checked(limestone::bin2bin::translate(src,dst,bytes,cache_path.empty()?nullptr:&cache));
          result.assign(translated.begin(),translated.end());
        }
      }
    }
    if(output.empty()||output=="-"){std::cout.write(result.data(),static_cast<std::streamsize>(result.size()));if(!std::cout)throw std::runtime_error("cannot write standard output");}
    else {std::ofstream file(output,std::ios::binary);if(!file)throw std::runtime_error("cannot open output: "+output);file.write(result.data(),static_cast<std::streamsize>(result.size()));file.close();if(!file)throw std::runtime_error("cannot write output: "+output);}
    return 0;
  }catch(const std::exception& e){std::cerr<<"limestone: "<<e.what()<<'\n';return 1;}
}
