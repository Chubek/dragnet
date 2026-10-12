#include "metacode.hpp"
#include "schedrow.hpp"
#include "regtl.hpp"
#include "limeburg.hpp"
#include "unisel.hpp"
#include "bin2bin.hpp"
#include "tunah.hpp"
#include "traceml.hpp"
#include "exolayer.h"
#include "limestone.hpp"
#include <cassert>
#include <iostream>
int main(){
 using namespace limestone;
 auto isa=metacode::parse_isa("arch test { family = \"x\"; } regclass GPR { r0(64)=0, r1(64)=1, } op ADD { semantics = (add r0 r1); }"); assert(isa);
 schedrow::Region rg{"r",{{1,"add","alu","integer",{1},{2,3},{{"ALU",1,1}},1,1,false,true,false},{2,"ret","branch","return",{}, {1},{},1,1,true,false,false}},{{1,2,schedrow::DepKind::True,1,0}}}; auto sch=schedrow::schedule(rg,{{{"ALU",1}}});assert(sch);
 auto bad_sched=rg; bad_sched.deps.push_back({99,2,schedrow::DepKind::True,0,0}); auto bad_result=schedrow::schedule(bad_sched,{{{"ALU",1}}}); assert(!bad_result && bad_result.error().code==Error::Code::InvalidArgument);
 regtl::Program rp{{{1,0,2,"GPR",{},true},{2,1,3,"GPR",{},true}},{{"GPR",{0,1}}}};auto al=regtl::linear_scan(rp);assert(al&&regtl::verify(rp,al.value()));
 regtl::Program bad_rp{{{1,3,2,"GPR",{},true}},{{"GPR",{0}}}}; auto bad_alloc=regtl::linear_scan(bad_rp); assert(!bad_alloc && bad_alloc.error().code==Error::Code::InvalidArgument);
 limeburg::RuleSet rs{{{1,"reg","add", "reg",{"reg","reg"},1,"ADD",0}},{{"reg",1}}};std::vector<limeburg::Node> ns; ns.push_back(limeburg::Node{1,"x","",{},0,false}); // just exercise failure path
 unisel::Program up{{{1,"add",{2,3},{}},{2,"const",{},1},{3,"const",{},2}}};std::vector<unisel::Pattern> ps; ps.push_back(unisel::Pattern{1,"ADD","add","ADD",{"v","v"},1}); ps.push_back(unisel::Pattern{2,"CONST","const","CONST",{},1});auto us=unisel::solve_greedy(up,ps);assert(us); 
 bin2bin::Architecture ba{"toy",{{1,"nop"}}};uint8_t b[]={1};auto dec=bin2bin::decode(ba,b);assert(dec);
  tunah::Session tunah_session; tunah_session.add_rule({"identity",{"x",{},{}},{"x",{},{}}}); auto sat=tunah_session.saturate("x"); assert(sat && sat.value().saturated && sat.value().expression=="x");
 auto tr=traceml::compile("(add 1 2)");assert(tr);
 auto*ctx=exl_context_create();exl_context_destroy(ctx);
  auto lm=run_pipeline("(add 1 2)");assert(lm && lm.value().machine_ir.find("#3")!=std::string::npos);
 std::cout<<"Limestone smoke tests passed\n";
}
