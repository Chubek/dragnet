#include "test.hpp"
#include "limeburg/text.hpp"
#include "schedrow/text.hpp"
#include "regtl/text.hpp"
#include <fstream>

static std::string read(const std::string& path){std::ifstream f(path);CHECK(f.good());return {(std::istreambuf_iterator<char>(f)),{}};}
int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;CHECK(argc==2);const std::string fixtures=argv[1];
  auto burs=take(limeburg::load_rules(read(fixtures+"/selection.limeburg")));auto& tree=burs.trees[0];auto selected=take(limeburg::select(tree.nodes,tree.root,burs.rules,tree.nonterminal));auto emitted=take(limeburg::emit_scheduler(tree.nodes,burs.rules,selected));CHECK(selected.cost==3&&emitted.instructions.size()==2&&emitted.instructions[1].opcode=="ADDI"&&emitted.instructions[1].immediates[0].second==7);
  auto rules_text=take(limeburg::print_rules(burs));auto copied=take(limeburg::load_rules(rules_text));CHECK(take(limeburg::print_rules(copied))==rules_text);CHECK(take(limeburg::select(copied.trees[0].nodes,copied.trees[0].root,copied.rules,"reg")).cost==3);
  fails(limeburg::load_rules("ruleset x {nonterminal reg;terminal A(1);rule reg: A() -> X;}"),Error::Code::InvalidArgument);
  fails(limeburg::load_rules("ruleset x {nonterminal reg;rule reg: UNKNOWN() -> X;}"),Error::Code::NotFound);
  fails(limeburg::load_rules("include \"missing\";"),Error::Code::Unsupported);
  auto included=take(limeburg::load_rules_file(fixtures+"/selection-includes.limeburg"));CHECK(included.rules.rules.size()==3&&included.trees[0].nodes[0].origin.ends_with("/includes/sum.tree:2"));auto& included_tree=included.trees[0];auto included_selection=take(limeburg::select(included_tree.nodes,included_tree.root,included.rules,"reg"));CHECK(included_selection.cost==3&&take(limeburg::emit_scheduler(included_tree.nodes,included.rules,included_selection)).instructions.back().opcode=="ADDI");auto included_text=take(limeburg::print_rules(included));CHECK(take(limeburg::print_rules(take(limeburg::load_rules(included_text))))==included_text);
  CHECK(take(limeburg::emit_scheduler(included_tree.nodes,included.rules,included_selection)).instructions.back().origin.find("/includes/scalar.rules:4:1")!=std::string::npos);
  syntax::IncludeOptions options;options.resolver=[](auto,auto){return Result<syntax::ResolvedSource>::ok({"virtual.rules","terminal A(0);\nterminal A(0);"});};auto duplicate=limeburg::load_rules("ruleset x {include \"rules\";}","root.rules",options);fails(duplicate,Error::Code::Conflict);CHECK(duplicate.error().message.find("virtual.rules:2:1")!=std::string::npos);
  options.resolver=[](auto,auto){return Result<syntax::ResolvedSource>::ok({"virtual.rules","include \"rules\";"});};fails(limeburg::load_rules("include \"rules\";","root.rules",options),Error::Code::Conflict);
  options={};options.depth=1;fails(limeburg::load_rules_file(fixtures+"/selection-includes.limeburg",options),Error::Code::ResourceLimit);
  options.resolver=[](auto,auto){return Result<syntax::ResolvedSource>::ok({"virtual.tree","tree t {node %1=A();root %1:reg;}"});};fails(limeburg::load_rules("ruleset x {include \"tree\";}","root.rules",options),Error::Code::Conflict);
  options={};options.bytes=8;fails(limeburg::load_rules_file(fixtures+"/selection-includes.limeburg",options),Error::Code::ResourceLimit);
  const char* repeated=R"(ruleset x {nonterminal reg;terminal INPUT(0);terminal PAIR(2);rule reg: INPUT() -> "" cost 0 external_only true;rule reg: PAIR(reg binding x register_class G,reg binding x register_class G) -> P side_effects true;}tree t {node %1=INPUT() register_class G required false;node %2=PAIR(%1,%1) side_effect true;root %2:reg;})";
  auto constrained=take(limeburg::load_rules(repeated));auto constraint_text=take(limeburg::print_rules(constrained));auto constraint_copy=take(limeburg::load_rules(constraint_text));CHECK(take(limeburg::print_rules(constraint_copy))==constraint_text);auto& constraint_tree=constraint_copy.trees[0];auto constraint_selection=take(limeburg::select(constraint_tree.nodes,constraint_tree.root,constraint_copy.rules,"reg"));CHECK(take(limeburg::emit_scheduler(constraint_tree.nodes,constraint_copy.rules,constraint_selection)).instructions[0].uses==std::vector<uint32_t>({1,1}));constraint_tree.nodes[0].register_class="F";fails(limeburg::select(constraint_tree.nodes,constraint_tree.root,constraint_copy.rules,"reg"),Error::Code::Unsatisfiable);
  fails(limeburg::load_rules("nonterminal reg;terminal C(0);tree t {node %1=C();node %2=C();root %1:reg;}"),Error::Code::Conflict);
  auto schedule_document=take(schedrow::load_schedrow(read(fixtures+"/scheduling.schedrow")));auto& text=schedule_document.regions[0];auto schedule=take(schedrow::schedule(text.region,schedule_document.machine));take(schedrow::verify(text.region,schedule_document.machine,schedule));CHECK(schedule[0].id==9&&schedule[1].id==2&&schedule[1].cycle==2&&schedule[2].id==5&&schedule[2].cycle==3);CHECK(text.region.instructions[0].throughput==0.5&&text.region.instructions[1].access->alias_sets==std::vector<uint32_t>{17});
  auto schedule_text=take(schedrow::print_schedrow(schedule_document));auto schedule_copy=take(schedrow::load_schedrow(schedule_text));CHECK(take(schedrow::print_schedrow(schedule_copy))==schedule_text);CHECK(schedule_copy.machine_metadata.at("custom").text()==schedule_document.machine_metadata.at("custom").text());take(schedrow::verify(schedule_copy.regions[0].region,schedule_copy.machine,schedule));
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=unknown;}}"),Error::Code::Unsupported);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=0;}dependency {producer=%1;consumer=%missing;kind=true;}}"),Error::Code::NotFound);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=0;group={kind=adjacent;};}}"),Error::Code::InvalidArgument);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=0;early_defs=[7];}}"),Error::Code::Conflict);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=0;resources=[{resource=ALU;duration=0;}];}}"),Error::Code::InvalidArgument);
  auto named=take(schedrow::load_schedrow("region x {instruction %name {opcode=A;latency=0;def %value;result_latency={value=3;};}}"));CHECK(named.regions[0].region.instructions[0].result_latency.at(0)==3);
  auto physical_latency=take(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=4;def %1;implicit_def %1;result_latency={\"1\"=0;};implicit_result_latency={\"1\"=2;};}instruction %2 {opcode=B;latency=0;use %1;implicit_use %1;}}"));auto physical_text=take(schedrow::print_schedrow(physical_latency));auto physical_copy=take(schedrow::load_schedrow(physical_text));CHECK(take(schedrow::schedule(physical_copy.regions[0].region,{}))[1].cycle==2&&take(schedrow::print_schedrow(physical_copy))==physical_text);
  auto ranges=take(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency={result=2..8;memory=3..9;};memory=true;def %value;implicit_def %flag;result_latency={value=2..6;};implicit_result_latency={flag=[1,4];};operand_latencies=[{result=%value;consumer=B;use=0;cycles=[0,2];},{result=%flag;consumer=B;use=0;cycles={min=1;max=3;};implicit=true;}];}instruction %2 {opcode=B;latency=0;use %value;implicit_use %flag;}}"));auto range_text=take(schedrow::print_schedrow(ranges));auto range_copy=take(schedrow::load_schedrow(range_text));CHECK(take(schedrow::print_schedrow(range_copy))==range_text);CHECK(take(schedrow::schedule(range_copy.regions[0].region,{}))[1].cycle==3);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=8..2;}}"),Error::Code::InvalidArgument);
  fails(schedrow::load_schedrow("region x {instruction %1 {opcode=A;latency=0;def %v;operand_latencies=[{result=%v;consumer=B;use=0;cycles=[0,2];unknown=true;}];}}"),Error::Code::Unsupported);
  auto grouped=take(schedrow::load_schedrow(read(fixtures+"/grouping.schedrow")));CHECK(grouped.regions.size()==2&&grouped.regions[0].region.groups[0].kind==schedrow::GroupKind::Bundle);auto group_text=take(schedrow::print_schedrow(grouped));auto group_copy=take(schedrow::load_schedrow(group_text));CHECK(take(schedrow::print_schedrow(group_copy))==group_text);for(auto& r:group_copy.regions)take(schedrow::verify(r.region,group_copy.machine,take(schedrow::schedule(r.region,group_copy.machine))));auto bundle_issues=take(schedrow::schedule(group_copy.regions[0].region,group_copy.machine));CHECK(bundle_issues[0].slot==1&&bundle_issues[0].resources[0]=="B");
  auto annotated=take(schedrow::load_schedrow("region x {instruction %9 {opcode=A;latency=0;group={id=5;kind=pair;};}instruction %1 {opcode=B;latency=0;group={id=5;kind=pair;};}}"));CHECK(annotated.regions[0].region.groups[0].members==std::vector<uint32_t>({9,1}));auto annotation_text=take(schedrow::print_schedrow(annotated));CHECK(take(schedrow::print_schedrow(take(schedrow::load_schedrow(annotation_text))))==annotation_text);
  auto standalone_bundle=take(schedrow::load_schedrow("bundle named {instruction %1 {opcode=A;latency=0;}}"));CHECK(standalone_bundle.regions[0].region.groups[0].name=="named");
  auto overlapping=take(schedrow::load_schedrow("machine_model shared {issue_width=2;group_search_limit=1234;} region x {groups=[{id=1;kind=adjacent;members=[%1,%2];},{id=2;kind=bundle;members=[%1,%2];}];instruction %1 {opcode=A;latency=0;}instruction %2 {opcode=B;latency=0;}}"));
  auto overlap_copy=take(schedrow::load_schedrow(take(schedrow::print_schedrow(overlapping))));CHECK(overlap_copy.machine.group_search_limit==1234&&overlap_copy.regions[0].region.groups.size()==2);take(schedrow::verify(overlap_copy.regions[0].region,overlap_copy.machine,take(schedrow::schedule(overlap_copy.regions[0].region,overlap_copy.machine))));
  auto nested=take(schedrow::load_schedrow("region x {bundle a {bundle b {instruction %1 {opcode=A;latency=0;}}}}"));CHECK(nested.regions[0].region.groups.size()==2);take(schedrow::verify(nested.regions[0].region,{},take(schedrow::schedule(nested.regions[0].region,{}))));
  fails(schedrow::load_schedrow("region x {instruction %9 {opcode=A;latency=0;group={id=5;kind=pair;};}instruction %1 {opcode=B;latency=0;group={id=5;kind=atomic;};}}"),Error::Code::Conflict);
  auto wide=take(schedrow::load_schedrow("machine_model dual {issue_width=2;} region x {instruction %1 {opcode=A;latency=0;issue={width=2;slots=[0,1];};}}"));
  CHECK(wide.regions[0].region.instructions[0].issue_width==2);auto wide_roundtrip=take(schedrow::load_schedrow(take(schedrow::print_schedrow(wide))));CHECK(wide_roundtrip.regions[0].region.instructions[0].issue_width==2);take(schedrow::verify(wide_roundtrip.regions[0].region,wide_roundtrip.machine,take(schedrow::schedule(wide_roundtrip.regions[0].region,wide_roundtrip.machine))));
  auto units=take(regtl::load_regtl(read(fixtures+"/allocation.regtl")));CHECK(units.size()==1&&units[0].functions.size()==1&&units[0].slots[0].alignment==8);auto& function=units[0].functions[0].function;auto live=take(regtl::analyze(function));CHECK(function.blocks[0].instructions[3].parallel&&function.blocks[0].instructions[3].transfers.size()==2);CHECK(live.live_in.at(function.blocks[1].id)==std::vector<uint32_t>{7});for(auto allocator:{regtl::linear_scan,regtl::greedy,regtl::graph_color}){auto assigned=take(allocator(live.problem));CHECK(assigned.regs.at(8)==2);take(regtl::verify(live.problem,assigned));}
  auto allocation_text=take(regtl::print_regtl(units));auto allocation_copy=take(regtl::load_regtl(allocation_text));CHECK(take(regtl::print_regtl(allocation_copy))==allocation_text);auto copy_live=take(regtl::analyze(allocation_copy[0].functions[0].function));CHECK(take(regtl::graph_color(copy_live.problem)).regs.at(8)==2);CHECK(allocation_copy[0].metadata.at("notes").text()==units[0].metadata.at("notes").text());
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {} function f {block b {instruction I {use %missing;}}}}"),Error::Code::NotFound);
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {} function f {block b {parallel {move $0 <- #1;move $0 <- #2;}}}}"),Error::Code::Conflict);
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {fixed=$2;}}"),Error::Code::NotFound);
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {allowed=[];}}"),Error::Code::Unsatisfiable);
  fails(regtl::load_regtl("regtl x {regclass G=[$0,$1];alias $0=$1;function f {block b {parallel {move $0 <- #1;move $1 <- #2;}}}}"),Error::Code::Conflict);
  auto quoted_constraints=take(regtl::load_regtl("regtl x {regclass G=[$0,$1];live %1:G [0,1] {\"fixed\"=\"$1\";}}"));CHECK(take(regtl::linear_scan(quoted_constraints[0].problem)).regs.at(1)==1);
  auto lanes=take(regtl::load_regtl(R"(regtl x {
    regclass G=[$low,$high,$full];
    live %left:G [0,2] {bank=integer;fixed=$low;}
    live %right:G [0,2] {bank=integer;fixed=$high;}
    storage=[{register=$low;bank=integer;slices=[{unit=0;begin=0;width=8;}];},
             {register=$high;bank=integer;slices=[{unit=0;begin=8;width=8;}];},
             {register=$full;bank=integer;slices=[{unit=0;begin=0;width=16;}];}];
    tuples=[{values=[%left,%right];alternatives=[[$low,$high]];}];
    function f {block b {instruction I {def %left;def %right;} live_out=[%left,%right];}}
  })"));
  auto lanes_text=take(regtl::print_regtl(lanes));auto lanes_copy=take(regtl::load_regtl(lanes_text));CHECK(take(regtl::print_regtl(lanes_copy))==lanes_text);
  CHECK(lanes_copy[0].problem.ranges[0].constraint.bank=="integer");take(regtl::verify(lanes_copy[0].problem,take(regtl::graph_color(lanes_copy[0].problem))));
  auto lane_live=take(regtl::analyze(lanes_copy[0].functions[0].function));take(regtl::verify(lane_live.problem,take(regtl::constraint_allocate(lane_live.problem))));
  lanes[0].problem.storage[0].bank="edited";lanes[0].problem.ranges[0].constraint.bank="edited";CHECK(take(regtl::print_regtl(lanes)).find("edited")!=std::string::npos);
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {bank=missing;}}"),Error::Code::InvalidArgument);
  auto weighted=take(regtl::load_regtl(R"(regtl costed {regclass G=[$r];live %a:G [0,3] {} live %b:G [0,3] {}
    pbqp={values=[{value=%a;spill_cost=10;registers=[{register=$r;cost=0.25;}];},{value=%b;spill_cost=2;}];};})"));
  auto weighted_text=take(regtl::print_regtl(weighted));auto weighted_copy=take(regtl::load_regtl(weighted_text));CHECK(take(regtl::print_regtl(weighted_copy))==weighted_text);
  auto optimal=take(regtl::solve_pbqp(weighted_copy[0].problem,weighted_copy[0].pbqp));CHECK(optimal.cost==2.25&&optimal.allocation.spilled==std::vector<uint32_t>{weighted[0].virtual_names.at("%b")});
  fails(regtl::load_regtl("regtl x {regclass G=[$0];live %1:G [0,1] {} pbqp={default_spill_cost=-1;};}"),Error::Code::InvalidArgument);
  fails(regtl::load_regtl("regtl x {pbqp={approximate=true;};}"),Error::Code::Unsupported);
});}
