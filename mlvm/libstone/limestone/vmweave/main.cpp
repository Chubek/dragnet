#include "vmweave.hpp"
#include "native.hpp"
#include "metacode/machine-ir/native_c.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace limestone::vmweave;
int main(int argc,char** argv) {
  try {
    std::string mode="--generate",file,out=".",program;
    NativeOptions native_options;
    for(int i=1;i<argc;++i) {
      std::string arg=argv[i];
      if(arg=="--help") {std::cout<<"VMWeave: Lua -> STK-00 -> C / MachineIR -> native code\nUsage: vmweave-cli [--emit-c|--emit-stk|--check|--generate|--emit-machineir|--emit-assembly|--native]\n  [-o DIRECTORY] [--program TAPE.txt] [--cc COMPILER] [--cflag ARG] [--ldflag ARG]\n  SPEC.lua|MODULE.stk00|MODULE.machineir.json\nNative output: shared image, assembly, MachineIR envelope, STK-00 and ABI header.\nWithout --program, native output still contains all VM handlers/runtime components.\n"; return 0;}
      if(arg=="--emit-c" || arg=="--emit-stk" || arg=="--check" || arg=="--generate" || arg=="--emit-machineir" || arg=="--emit-assembly" || arg=="--native") mode=arg;
      else if(arg=="-o" && i+1<argc) out=argv[++i];
      else if(arg=="--program" && i+1<argc) program=argv[++i];
      else if(arg=="--cc" && i+1<argc) native_options.compiler=argv[++i];
      else if(arg=="--cflag" && i+1<argc) native_options.compile_arguments.push_back(argv[++i]);
      else if(arg=="--ldflag" && i+1<argc) native_options.link_arguments.push_back(argv[++i]);
      else if(arg.starts_with('-') || !file.empty()) throw std::runtime_error("invalid command line: "+arg);
      else file=arg;
    }
    if(file.empty()) throw std::runtime_error("a Lua specification or STK-00 module is required (see --help)");
    auto read_file=[](const std::string& path) {
      std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("cannot read "+path);
      std::ostringstream text; text<<in.rdbuf(); return text.str();
    };
    auto native_mode=mode=="--emit-machineir" || mode=="--emit-assembly" || mode=="--native";
    if(file.ends_with(".machineir.json")) {
      if(!native_mode || !program.empty()) throw std::runtime_error("saved native MachineIR accepts only native output modes and owns its tape");
      auto unit=limestone::machineir_native::deserialize(read_file(file),file);
      if(!unit) throw std::runtime_error(unit.error().message);
      limestone::machineir_native::Options opts{native_options.compiler,native_options.compile_arguments,native_options.link_arguments,native_options.timeout_seconds,native_options.image_limit};
      if(mode=="--emit-machineir" || mode=="--emit-assembly") {
        auto text=mode=="--emit-machineir"?limestone::machineir_native::serialize(unit.value()):limestone::machineir_native::emit_assembly(unit.value(),opts);
        if(!text) throw std::runtime_error(text.error().message);
        std::cout<<text.value(); return 0;
      }
      auto library=limestone::machineir_native::compile(unit.value(),opts);
      if(!library) throw std::runtime_error(library.error().message);
      std::filesystem::create_directories(out);
      std::ofstream image(std::filesystem::path(out)/(unit.value().entry.module+".so"),std::ios::binary);
      auto bytes=library.value().image(); image.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
      if(!image) throw std::runtime_error("cannot write native image");
      return 0;
    }
    auto module=[&]() -> limestone::Result<Module> {
      if(std::filesystem::path(file).extension()==".stk00") {
        std::ifstream in(file,std::ios::binary); if(!in) throw std::runtime_error("cannot read "+file);
        std::ostringstream text; text<<in.rdbuf(); return parse_stk(text.str(),file);
      }
      auto spec=load_file(file); if(!spec) return limestone::Result<Module>::err(spec.error()); return lower(spec.value());
    }();
    if(!module) throw std::runtime_error(module.error().message);
    if(!native_mode && (!program.empty() || !native_options.compiler.empty() || !native_options.compile_arguments.empty() || !native_options.link_arguments.empty())) throw std::runtime_error("native options require a native output mode");
    if(mode=="--check") return 0;
    if(native_mode) {
      std::vector<NativeInstruction> code;
      if(!program.empty()) {auto assembled=assemble(module.value(),read_file(program)); if(!assembled) throw std::runtime_error(assembled.error().message); code=std::move(assembled.value());}
      auto ir=lower_native(module.value(),code,native_options); if(!ir) throw std::runtime_error(ir.error().message);
      if(mode=="--emit-machineir") {std::cout<<ir.value(); return 0;}
      auto assembly=emit_native_assembly(module.value(),code,native_options); if(!assembly) throw std::runtime_error(assembly.error().message);
      if(mode=="--emit-assembly") {std::cout<<assembly.value(); return 0;}
      auto native=compile_native(module.value(),code,native_options); if(!native) throw std::runtime_error(native.error().message);
      auto artifacts=generate(module.value()); if(!artifacts) throw std::runtime_error(artifacts.error().message);
      std::filesystem::create_directories(out); auto name=module.value().configuration.name;
      auto write=[&](const std::string& suffix,std::string_view text) {
        std::ofstream stream(std::filesystem::path(out)/(name+suffix),std::ios::binary);
        stream.write(text.data(),static_cast<std::streamsize>(text.size()));
        if(!stream) throw std::runtime_error("cannot write native artifact "+name+suffix);
      };
      auto header=artifacts.value().files.at(name+".h");
      header+="\n#ifndef "+name+"_VMWEAVE_NATIVE_H\n#define "+name+"_VMWEAVE_NATIVE_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nsize_t "+name+"_vw_native_size(void);\nuint64_t "+name+"_vw_native_abi(void);\nint32_t "+name+"_vw_native_entry(void *, void *, uint64_t);\n#ifdef __cplusplus\n}\n#endif\n";
      header+="static inline int "+name+"_run_native("+name+"_vm *vm, size_t budget) {\n  if(!vm) return -1;\n  if(sizeof(*vm)!="+name+"_vw_native_size() || "+name+"_vw_abi()!="+name+"_vw_native_abi()) return -11;\n  return "+name+"_vw_native_entry(vm,&budget,"+name+"_vw_abi());\n}\n#endif\n";
      write(".h",header); write(".stk00",artifacts.value().files.at(name+".stk00"));
      write(".machineir.json",ir.value()); write(".s",assembly.value());
      auto image=native.value().image(); write(".so",std::string_view(reinterpret_cast<const char*>(image.data()),image.size()));
      return 0;
    }
    if(mode=="--emit-c" || mode=="--emit-stk") {
      auto text=mode=="--emit-c"?emit_c(module.value()):print_stk(module.value());
      if(!text) throw std::runtime_error(text.error().message);
      std::cout<<text.value();
    } else {
      auto files=generate(module.value()); if(!files) throw std::runtime_error(files.error().message);
      std::filesystem::create_directories(out);
      for(const auto& [name,text]:files.value().files) {
        std::ofstream stream(std::filesystem::path(out)/name,std::ios::binary);
        if(!stream || !(stream<<text)) throw std::runtime_error("cannot write artifact '"+name+"'");
      }
    }
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
