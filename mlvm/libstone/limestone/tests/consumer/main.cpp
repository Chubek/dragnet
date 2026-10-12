#include <limestone/limestone.hpp>
#include <limestone/il.h>
#include <limestone/runtime.h>
#include <parsers/unisel_ast.hpp>
#include <exolayer/exolayer.h>
#include <tunah/unisel_adapter.hpp>
#include <tunah/binary_adapter.hpp>
#include <limeburg/text.hpp>
#include <schedrow/text.hpp>
#include <regtl/text.hpp>
#include <iostream>

extern "C" int consumer_c_api(void);
int consumer_cpp_other(void);
int main() {
  auto module=limestone::run_pipeline("(add 20 22)");
  auto syntax=limestone::syntax::unisel::parse("machine x { operator const(0); }");
  if(!module||!syntax||module.value().optimized.nodes[0].constant!=42||consumer_c_api()!=42||consumer_cpp_other()!=42)return 1;
  if(limestone_schedule_run(nullptr,0,0,nullptr))return 1;
  auto burs=limestone::limeburg::load_rules("ruleset x {nonterminal reg;terminal A(0);rule reg:A():i64 -> I;}tree t {node %7=A():i64;root %7:reg;}");if(!burs)return 1;
  auto states=limestone::limeburg::analyze(burs.value().trees[0].nodes,7,burs.value().rules,true);if(!states)return 1;
  auto dump=limestone::limeburg::print_analysis(states.value(),burs.value().rules);if(!dump||dump.value().find("reg")==std::string::npos)return 1;
  if(limestone_runtime_create(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))return 1;
  limestone::tunah::Session rules;limestone::tunah::BinaryAdapterOptions options;options.legality=[](const auto&,const auto&){return limestone::Result<bool>::ok(true);};auto transformer=limestone::tunah::binary_transform(rules,"consumer:1",options);if(!transformer)return 1;
  auto term=transformer.value().apply({0,"42",limestone::bin2bin::Status::Supported});if(!term||term.value()!="42")return 1;
  std::cout<<module.value().machine_ir;
}
