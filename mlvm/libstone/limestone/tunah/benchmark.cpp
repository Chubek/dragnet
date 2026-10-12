#include "tunah/tunah.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
template<class T> T checked(limestone::Result<T> result) { if(!result)throw std::runtime_error(result.error().message);return std::move(result.value()); }
std::string read(const std::string& path) { std::ifstream input(path);if(!input)throw std::runtime_error("cannot read "+path);return {(std::istreambuf_iterator<char>(input)),{}}; }
}
int main(int argc,char** argv) {
  try {
    if(argc!=3)throw std::runtime_error("usage: limestone-tunah-benchmark TUNER_DIRECTORY TRACE(0|1)");
    limestone::tunah::Limits limits;limits.trace=std::string_view(argv[2])=="1";
    limestone::tunah::Session small,corpus;
    checked(small.load_rules("(operator add 2) (rule zero (add ?x 0) ?x)"));
    checked(corpus.load_rules(read(std::string(argv[1])+"/vocabulary.tuner")));
    for(auto file:{"alg-sim","const-folding","codesize-reduce","strength-reduct","uarch-opt"})checked(corpus.load_rules(read(std::string(argv[1])+"/"+file+".tuner")));
    auto run=[&](const auto& session,std::string_view term,size_t calls,std::string_view expected,size_t cost) {
      size_t total=0;auto start=std::chrono::steady_clock::now();
      for(size_t i=0;i<calls;++i){auto result=checked(session.saturate(term,limits));if(result.expression!=expected||result.cost!=cost||!result.saturated||result.limit_reached)throw std::runtime_error("benchmark semantic/quality regression");total+=result.cost;}
      auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<elapsed<<" ms cost="<<total<<"\n";
    };
    run(small,"(add (add x 0) 0)",10000,"x",1);
    run(corpus,"(iadd (imul (iadd x 0) 4) (iadd (imul (iadd x 0) 12) (imul y 0)))",200,"(ishl x 4)",5);
    return 0;
  }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
