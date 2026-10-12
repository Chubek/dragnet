#include "il.h"
#include "c_api_internal.hpp"
#include "allocation_internal.hpp"
#include "selection_internal.hpp"
#include "limeburg/text.hpp"
#include "schedrow/text.hpp"
#include "regtl/text.hpp"
#include "unisel/umd.hpp"
#include <map>

struct limestone_burs_document { limestone::limeburg::RuleDocument document;std::string text; };
struct limestone_burs_selection { limestone::schedrow::Region region;uint64_t cost;std::string text; };
struct limestone_burs_analysis {
  struct State { uint32_t node,nonterminal,rule;uint64_t cost;std::string name; };
  std::vector<State> states;
  std::vector<limestone::limeburg::RuleAttempt> attempts;
};
struct limestone_scheduling_document { limestone::schedrow::SchedulingDocument document;std::string text; };
struct limestone_schedule { std::vector<limestone::schedrow::Scheduled> issues;std::string text;std::vector<limestone::schedrow::InstructionGroup> groups; };
struct limestone_allocation_document { std::vector<limestone::regtl::AllocationUnit> units;std::string text; };
struct limestone_assignment { limestone::regtl::Allocation allocation;std::vector<std::pair<uint32_t,uint32_t>> assignments;std::string text;double cost=0; };
struct limestone_unisel_document { limestone::unisel::Document document; };
struct limestone_selection_model {
  limestone::unisel::Program program;
  std::vector<limestone::unisel::Pattern> patterns;
  limestone::unisel::ConstraintModel model;
  std::string text;
};
struct limestone_selection {
  std::vector<limestone::unisel::Pattern> patterns;
  std::vector<limestone::unisel::Candidate> candidates;
  uint64_t cost;
  std::string text;
};

namespace {
using namespace limestone;
using namespace limestone::c_api_internal;
[[noreturn]] void invalid(){throw Error{Error::Code::InvalidArgument,"null IL API argument"};}
[[noreturn]] void missing(std::string_view what){throw Error{Error::Code::NotFound,std::string(what)+" index out of range"};}
template<class T> const T& at(const std::vector<T>& values,size_t index,std::string_view what){if(index>=values.size())missing(what);return values[index];}
void match_info(const unisel::Candidate& match,const std::vector<unisel::Pattern>& patterns,limestone_match_info& result) {
  auto found=std::find_if(patterns.begin(),patterns.end(),[&](auto& pattern){return pattern.id==match.pattern;});
  if(found==patterns.end())throw Error{Error::Code::Internal,"selection candidate has no pattern"};
  result={match.pattern,match.root,uint64_t(match.cost),found->name.c_str(),found->instruction.c_str(),found->origin.c_str(),match.reason.c_str(),match.covered.size(),match.inputs.size(),match.outputs.size()};
}
uint32_t match_value(const unisel::Candidate& match,limestone_match_values kind,size_t index) {
  switch(kind){case LIMESTONE_MATCH_COVERED:return at(match.covered,index,"covered value");case LIMESTONE_MATCH_INPUT:return at(match.inputs,index,"input value");case LIMESTONE_MATCH_OUTPUT:return at(match.outputs,index,"output value");}
  throw Error{Error::Code::InvalidArgument,"unknown selection value role"};
}
}

extern "C" limestone_unisel_document* limestone_unisel_load(const char* source,const char* file,limestone_error* error) {
  return boundary(error,[&]()->limestone_unisel_document*{if(!source)invalid();return new limestone_unisel_document{checked(unisel::load_umd(source,file?file:"<unisel>"))};});
}
extern "C" limestone_unisel_document* limestone_unisel_load_file(const char* path,limestone_error* error) {
  return boundary(error,[&]()->limestone_unisel_document*{if(!path||!*path)invalid();return new limestone_unisel_document{checked(unisel::load_umd_file(path))};});
}
extern "C" void limestone_unisel_document_destroy(limestone_unisel_document* document){delete document;}
extern "C" limestone_status limestone_unisel_set_selection_predicate(limestone_unisel_document* document,const char* name,const limestone_selection_predicate* predicate,limestone_error* error) {
  return boundary(error,[&](){if(!document)invalid();auto patterns=document->document.machine.patterns;if(!bind_selection_predicate(patterns,name,predicate))throw Error{Error::Code::NotFound,"selection predicate is not declared"};document->document.machine.patterns=std::move(patterns);return LIMESTONE_OK;});
}
extern "C" limestone_selection_model* limestone_unisel_analyze(const limestone_unisel_document* document,limestone_error* error) {
  return boundary(error,[&]()->limestone_selection_model* {
    if(!document)invalid();if(!document->document.program)throw Error{Error::Code::InvalidArgument,"Unisel analysis needs a source graph"};
    auto program=checked(unisel::prepare(*document->document.program));auto patterns=document->document.machine.patterns;auto model=checked(unisel::build_model(program,patterns));
    std::string text="Unisel selection model\n";
    for(size_t k=0;k<model.candidates.size();++k){auto& match=model.candidates[k];limestone_match_info info;match_info(match,patterns,info);text+="candidate "+std::to_string(k+1)+" pattern "+std::to_string(info.pattern)+" "+info.name+" -> "+info.opcode+" root %"+std::to_string(info.root)+" cost "+std::to_string(info.cost)+"\n";for(auto value:match.covered)text+="  covers %"+std::to_string(value)+"\n";for(auto value:match.inputs)text+="  input %"+std::to_string(value)+"\n";for(auto value:match.outputs)text+="  output %"+std::to_string(value)+"\n";if(*info.origin)text+="  origin "+std::string(info.origin)+"\n";}
    for(auto& clause:model.clauses){text+="clause";for(auto literal:clause)text+=" "+std::to_string(literal);text+='\n';}
    return new limestone_selection_model{std::move(program),std::move(patterns),std::move(model),std::move(text)};
  });
}
extern "C" void limestone_selection_model_destroy(limestone_selection_model* model){delete model;}
extern "C" size_t limestone_selection_model_candidate_count(const limestone_selection_model* model){return model?model->model.candidates.size():0;}
extern "C" limestone_status limestone_selection_model_candidate(const limestone_selection_model* model,size_t index,limestone_match_info* result,limestone_error* error) {
  if(result)*result={};return boundary(error,[&](){if(!model||!result)invalid();match_info(at(model->model.candidates,index,"candidate"),model->patterns,*result);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_selection_model_value(const limestone_selection_model* model,size_t candidate,limestone_match_values kind,size_t index,uint32_t* value,limestone_error* error) {
  return boundary(error,[&](){if(!model||!value)invalid();*value=match_value(at(model->model.candidates,candidate,"candidate"),kind,index);return LIMESTONE_OK;});
}
extern "C" size_t limestone_selection_model_clause_count(const limestone_selection_model* model){return model?model->model.clauses.size():0;}
extern "C" size_t limestone_selection_model_literal_count(const limestone_selection_model* model,size_t clause){return model&&clause<model->model.clauses.size()?model->model.clauses[clause].size():0;}
extern "C" limestone_status limestone_selection_model_literal(const limestone_selection_model* model,size_t clause,size_t index,int32_t* literal,limestone_error* error) {
  return boundary(error,[&](){if(!model||!literal)invalid();*literal=at(at(model->model.clauses,clause,"clause"),index,"literal");return LIMESTONE_OK;});
}
extern "C" const char* limestone_selection_model_text(const limestone_selection_model* model){return model?model->text.c_str():nullptr;}
extern "C" limestone_selection* limestone_selection_run(const limestone_selection_model* model,limestone_selector algorithm,limestone_error* error) {
  return boundary(error,[&]()->limestone_selection* {
    if(!model)invalid();if(algorithm==LIMESTONE_SELECT_BURS)throw Error{Error::Code::Unsupported,"BURS selection uses the separate Limeburg adapter"};
    if(algorithm!=LIMESTONE_SELECT_GLOBAL&&algorithm!=LIMESTONE_SELECT_GREEDY)throw Error{Error::Code::InvalidArgument,"unknown graph selection algorithm"};
    auto snapshot=*model;model=&snapshot;
    auto selected=checked(algorithm==LIMESTONE_SELECT_GLOBAL?unisel::solve(model->program,model->patterns):unisel::solve_greedy(model->program,model->patterns));
    auto region=checked(unisel::emit_scheduler(model->program,model->patterns,selected));std::vector<unisel::Candidate> ordered;
    for(auto& instruction:region.instructions){auto found=std::find_if(selected.selected.begin(),selected.selected.end(),[&](auto& match){return match.root==instruction.id;});if(found==selected.selected.end())throw Error{Error::Code::Internal,"selected operation has no candidate"};ordered.push_back(*found);}
    schedrow::SchedulingDocument handoff;handoff.regions.push_back({std::move(region)});auto text=checked(schedrow::print_schedrow(handoff));
    return new limestone_selection{model->patterns,std::move(ordered),uint64_t(selected.cost),std::move(text)};
  });
}
extern "C" void limestone_selection_destroy(limestone_selection* selection){delete selection;}
extern "C" uint64_t limestone_selection_cost(const limestone_selection* selection){return selection?selection->cost:0;}
extern "C" size_t limestone_selection_count(const limestone_selection* selection){return selection?selection->candidates.size():0;}
extern "C" limestone_status limestone_selection_candidate(const limestone_selection* selection,size_t index,limestone_match_info* result,limestone_error* error) {
  if(result)*result={};return boundary(error,[&](){if(!selection||!result)invalid();match_info(at(selection->candidates,index,"selected candidate"),selection->patterns,*result);return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_selection_value(const limestone_selection* selection,size_t candidate,limestone_match_values kind,size_t index,uint32_t* value,limestone_error* error) {
  return boundary(error,[&](){if(!selection||!value)invalid();*value=match_value(at(selection->candidates,candidate,"selected candidate"),kind,index);return LIMESTONE_OK;});
}
extern "C" const char* limestone_selection_text(const limestone_selection* selection){return selection?selection->text.c_str():nullptr;}

extern "C" limestone_burs_document* limestone_burs_load(const char* source,const char* file,limestone_error* error) {
  return boundary(error,[&]()->limestone_burs_document*{if(!source)invalid();auto document=checked(limeburg::load_rules(source,file?file:"<limeburg>"));auto text=checked(limeburg::print_rules(document));return new limestone_burs_document{std::move(document),std::move(text)};});
}
extern "C" limestone_burs_document* limestone_burs_load_file(const char* path,limestone_error* error) {
  return boundary(error,[&]()->limestone_burs_document*{if(!path||!*path)invalid();auto document=checked(limeburg::load_rules_file(path));auto text=checked(limeburg::print_rules(document));return new limestone_burs_document{std::move(document),std::move(text)};});
}
extern "C" void limestone_burs_document_destroy(limestone_burs_document* document){delete document;}
extern "C" limestone_status limestone_burs_set_selection_predicate(limestone_burs_document* document,const char* name,const limestone_selection_predicate* predicate,limestone_error* error) {
  return boundary(error,[&](){if(!document)invalid();auto rules=document->document.rules.rules;if(!bind_selection_predicate(rules,name,predicate))throw Error{Error::Code::NotFound,"selection predicate is not declared"};document->document.rules.rules=std::move(rules);return LIMESTONE_OK;});
}
extern "C" limestone_burs_analysis* limestone_burs_analyze(const limestone_burs_document* document,size_t tree,limestone_error* error) {
  return boundary(error,[&]()->limestone_burs_analysis*{if(!document)invalid();auto snapshot=*document;document=&snapshot;auto& input=at(document->document.trees,tree,"tree");auto table=checked(limeburg::analyze(input.nodes,input.root,document->document.rules,true));auto analysis=std::make_unique<limestone_burs_analysis>();std::map<uint32_t,std::string> names;for(auto& [name,id]:document->document.rules.nonterminals)names[id]=name;for(auto& [node,states]:table.states)for(auto& [nt,derivation]:states)analysis->states.push_back({node,nt,derivation.rule,uint64_t(derivation.cost),names.at(nt)});analysis->attempts=std::move(table.attempts);return analysis.release();});
}
extern "C" void limestone_burs_analysis_destroy(limestone_burs_analysis* analysis){delete analysis;}
extern "C" size_t limestone_burs_state_count(const limestone_burs_analysis* analysis){return analysis?analysis->states.size():0;}
extern "C" limestone_status limestone_burs_state_at(const limestone_burs_analysis* analysis,size_t index,limestone_burs_state* output,limestone_error* error) {
  if(output)*output={};return boundary(error,[&]()->limestone_status{if(!analysis||!output)invalid();auto& state=at(analysis->states,index,"BURS state");*output={state.node,state.nonterminal,state.rule,state.cost,state.name.c_str()};return LIMESTONE_OK;});
}
extern "C" size_t limestone_burs_attempt_count(const limestone_burs_analysis* analysis){return analysis?analysis->attempts.size():0;}
extern "C" limestone_status limestone_burs_attempt_at(const limestone_burs_analysis* analysis,size_t index,limestone_burs_attempt* output,limestone_error* error) {
  if(output)*output={};return boundary(error,[&]()->limestone_status{if(!analysis||!output)invalid();auto& attempt=at(analysis->attempts,index,"BURS attempt");*output={attempt.node,attempt.rule,bool(attempt.cost),attempt.improves_state,attempt.cost?uint64_t(*attempt.cost):0,attempt.reason.c_str()};return LIMESTONE_OK;});
}
extern "C" size_t limestone_burs_tree_count(const limestone_burs_document* document){return document?document->document.trees.size():0;}
extern "C" const char* limestone_burs_tree_name(const limestone_burs_document* document,size_t tree){return document&&tree<document->document.trees.size()?document->document.trees[tree].name.c_str():nullptr;}
extern "C" const char* limestone_burs_document_text(const limestone_burs_document* document){return document?document->text.c_str():nullptr;}
extern "C" limestone_burs_selection* limestone_burs_select(const limestone_burs_document* document,size_t index,limestone_error* error) {
  return boundary(error,[&]()->limestone_burs_selection*{if(!document)invalid();auto snapshot=*document;document=&snapshot;auto& tree=at(document->document.trees,index,"tree");auto selected=checked(limeburg::select(tree.nodes,tree.root,document->document.rules,tree.nonterminal));auto region=checked(limeburg::emit_scheduler(tree.nodes,document->document.rules,selected));region.name=tree.name;schedrow::SchedulingDocument handoff;handoff.regions.push_back({region});auto text=checked(schedrow::print_schedrow(handoff));return new limestone_burs_selection{std::move(region),uint64_t(selected.cost),std::move(text)};});
}
extern "C" void limestone_burs_selection_destroy(limestone_burs_selection* selection){delete selection;}
extern "C" uint64_t limestone_burs_selection_cost(const limestone_burs_selection* selection){return selection?selection->cost:0;}
extern "C" size_t limestone_burs_selection_count(const limestone_burs_selection* selection){return selection?selection->region.instructions.size():0;}
extern "C" const char* limestone_burs_selection_opcode(const limestone_burs_selection* selection,size_t index){return selection&&index<selection->region.instructions.size()?selection->region.instructions[index].opcode.c_str():nullptr;}
extern "C" const char* limestone_burs_selection_text(const limestone_burs_selection* selection){return selection?selection->text.c_str():nullptr;}

extern "C" limestone_scheduling_document* limestone_schedrow_load(const char* source,const char* file,limestone_error* error) {
  return boundary(error,[&]()->limestone_scheduling_document*{if(!source)invalid();auto document=checked(schedrow::load_schedrow(source,file?file:"<schedrow>"));auto text=checked(schedrow::print_schedrow(document));return new limestone_scheduling_document{std::move(document),std::move(text)};});
}
extern "C" void limestone_scheduling_document_destroy(limestone_scheduling_document* document){delete document;}
extern "C" size_t limestone_scheduling_region_count(const limestone_scheduling_document* document){return document?document->document.regions.size():0;}
extern "C" const char* limestone_scheduling_region_name(const limestone_scheduling_document* document,size_t index){return document&&index<document->document.regions.size()?document->document.regions[index].region.name.c_str():nullptr;}
extern "C" const char* limestone_scheduling_document_text(const limestone_scheduling_document* document){return document?document->text.c_str():nullptr;}
extern "C" limestone_schedule* limestone_schedule_run(const limestone_scheduling_document* document,size_t index,uint32_t interval,limestone_error* error) {
   return boundary(error,[&]()->limestone_schedule*{if(!document)invalid();auto& region=at(document->document.regions,index,"region").region;auto& machine=document->document.machine;auto issues=interval?checked(schedrow::schedule_modulo(region,machine,{interval})):checked(schedrow::schedule(region,machine));if(interval)checked(schedrow::verify_modulo(region,machine,issues,interval));else checked(schedrow::verify(region,machine,issues));auto hazards=checked(schedrow::dependencies(region,machine.register_aliases));std::string text=schedrow::print(hazards);for(auto& i:issues){text+="instruction "+std::to_string(i.id)+" cycle "+std::to_string(i.cycle);if(i.slot)text+=" slot "+std::to_string(*i.slot);for(auto& r:i.resources)text+=" resource "+r;text+='\n';}return new limestone_schedule{std::move(issues),std::move(text),region.groups};});
}
extern "C" void limestone_schedule_destroy(limestone_schedule* schedule){delete schedule;}
extern "C" size_t limestone_schedule_count(const limestone_schedule* schedule){return schedule?schedule->issues.size():0;}
extern "C" limestone_status limestone_schedule_issue(const limestone_schedule* schedule,size_t index,limestone_issue* result,limestone_error* error) {
  return boundary(error,[&](){if(!schedule||!result)invalid();auto& issue=at(schedule->issues,index,"issue");*result={issue.id,issue.cycle,int(issue.slot.has_value()),issue.slot.value_or(0)};return LIMESTONE_OK;});
}
extern "C" size_t limestone_schedule_resource_count(const limestone_schedule* schedule,size_t index){return schedule&&index<schedule->issues.size()?schedule->issues[index].resources.size():0;}
extern "C" size_t limestone_schedule_slot_count(const limestone_schedule* schedule,size_t index){return schedule&&index<schedule->issues.size()?(schedule->issues[index].slot?1:0)+schedule->issues[index].additional_slots.size():0;}
extern "C" limestone_status limestone_schedule_slot(const limestone_schedule* schedule,size_t index,size_t slot,uint32_t* value,limestone_error* error) {
  return boundary(error,[&](){if(!schedule||!value)invalid();auto& issue=at(schedule->issues,index,"issue");if(!issue.slot||slot>issue.additional_slots.size())missing("issue slot");*value=slot?issue.additional_slots[slot-1]:*issue.slot;return LIMESTONE_OK;});
}
extern "C" const char* limestone_schedule_resource(const limestone_schedule* schedule,size_t index,size_t resource){return schedule&&index<schedule->issues.size()&&resource<schedule->issues[index].resources.size()?schedule->issues[index].resources[resource].c_str():nullptr;}
extern "C" const char* limestone_schedule_text(const limestone_schedule* schedule){return schedule?schedule->text.c_str():nullptr;}
extern "C" size_t limestone_schedule_group_count(const limestone_schedule* schedule){return schedule?schedule->groups.size():0;}
extern "C" limestone_status limestone_schedule_group(const limestone_schedule* schedule,size_t index,limestone_group_info* result,limestone_error* error) {
  return boundary(error,[&](){if(!schedule||!result)invalid();auto& group=at(schedule->groups,index,"group");*result={group.id,static_cast<limestone_group_kind>(group.kind),group.name.c_str(),group.origin.c_str(),group.pattern.c_str(),group.benefit,group.issue_width,group.members.size()};return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_schedule_group_member(const limestone_schedule* schedule,size_t index,size_t member,uint32_t* id,limestone_error* error) {
  return boundary(error,[&](){if(!schedule||!id)invalid();*id=at(at(schedule->groups,index,"group").members,member,"group member");return LIMESTONE_OK;});
}

extern "C" limestone_allocation_document* limestone_regtl_load(const char* source,const char* file,limestone_error* error) {
  return boundary(error,[&]()->limestone_allocation_document*{if(!source)invalid();auto units=checked(regtl::load_regtl(source,file?file:"<regtl>"));auto text=checked(regtl::print_regtl(units));return new limestone_allocation_document{std::move(units),std::move(text)};});
}
extern "C" void limestone_allocation_document_destroy(limestone_allocation_document* document){delete document;}
extern "C" size_t limestone_allocation_unit_count(const limestone_allocation_document* document){return document?document->units.size():0;}
extern "C" const char* limestone_allocation_unit_name(const limestone_allocation_document* document,size_t index){return document&&index<document->units.size()?document->units[index].name.c_str():nullptr;}
extern "C" size_t limestone_allocation_function_count(const limestone_allocation_document* document,size_t unit){return document&&unit<document->units.size()?document->units[unit].functions.size():0;}
extern "C" const char* limestone_allocation_function_name(const limestone_allocation_document* document,size_t unit,size_t function){return document&&unit<document->units.size()&&function<document->units[unit].functions.size()?document->units[unit].functions[function].name.c_str():nullptr;}
extern "C" const char* limestone_allocation_document_text(const limestone_allocation_document* document){return document?document->text.c_str():nullptr;}
namespace {
limestone_assignment* assignment_run(const limestone_allocation_document* document,size_t unit,size_t function,limestone_allocator algorithm,const limestone_pbqp_options* options,limestone_error* error) {
  return boundary(error,[&]()->limestone_assignment*{
    if(!document)invalid();auto& source=at(document->units,unit,"unit");auto problem=function==LIMESTONE_ALLOCATION_RANGES?source.problem:checked(regtl::analyze(at(source.functions,function,"function").function)).problem;
    auto policy=options?pbqp_options(options):source.pbqp;
    auto allocation=[&](){switch(algorithm){case LIMESTONE_ALLOCATE_LINEAR:return regtl::linear_scan(problem);case LIMESTONE_ALLOCATE_GREEDY:return regtl::greedy(problem);case LIMESTONE_ALLOCATE_COLOR:return regtl::graph_color(problem);case LIMESTONE_ALLOCATE_CONSTRAINT:return regtl::constraint_allocate(problem);case LIMESTONE_ALLOCATE_PBQP:return regtl::pbqp_allocate(problem,policy);}throw Error{Error::Code::InvalidArgument,"unknown allocation algorithm"};}();auto result=checked(std::move(allocation));checked(regtl::verify(problem,result));auto cost=checked(regtl::allocation_cost(problem,result,policy.costs));
    std::map<uint32_t,uint32_t> sorted(result.regs.begin(),result.regs.end());std::vector<std::pair<uint32_t,uint32_t>> assignments(sorted.begin(),sorted.end());std::string text=regtl::print(problem);for(auto [v,r]:assignments)text+="v"+std::to_string(v)+" -> physical "+std::to_string(r)+"\n";for(auto v:result.spilled)text+="v"+std::to_string(v)+" -> spill\n";return new limestone_assignment{std::move(result),std::move(assignments),std::move(text),cost};
  });
}
}
extern "C" limestone_assignment* limestone_assignment_run(const limestone_allocation_document* document,size_t unit,size_t function,limestone_allocator algorithm,limestone_error* error) {
  return assignment_run(document,unit,function,algorithm,nullptr,error);
}
extern "C" limestone_assignment* limestone_assignment_run_pbqp(const limestone_allocation_document* document,size_t unit,size_t function,const limestone_pbqp_options* options,limestone_error* error) {
  return assignment_run(document,unit,function,LIMESTONE_ALLOCATE_PBQP,options,error);
}
extern "C" limestone_status limestone_assignment_cost(const limestone_assignment* assignment,double* cost,limestone_error* error) {
  return boundary(error,[&](){if(!assignment||!cost)invalid();*cost=assignment->cost;return LIMESTONE_OK;});
}
extern "C" void limestone_assignment_destroy(limestone_assignment* assignment){delete assignment;}
extern "C" size_t limestone_assignment_count(const limestone_assignment* assignment){return assignment?assignment->assignments.size():0;}
extern "C" limestone_status limestone_assignment_at(const limestone_assignment* assignment,size_t index,uint32_t* value,uint32_t* physical,limestone_error* error) {
  return boundary(error,[&](){if(!assignment||!value||!physical)invalid();auto [v,r]=at(assignment->assignments,index,"assignment");*value=v;*physical=r;return LIMESTONE_OK;});
}
extern "C" limestone_status limestone_assignment_register(const limestone_assignment* assignment,uint32_t value,uint32_t* physical,limestone_error* error) {
  return boundary(error,[&](){if(!assignment||!physical)invalid();auto r=assignment->allocation.regs.find(value);if(r==assignment->allocation.regs.end())throw Error{Error::Code::NotFound,"value has no physical assignment"};*physical=r->second;return LIMESTONE_OK;});
}
extern "C" size_t limestone_assignment_spill_count(const limestone_assignment* assignment){return assignment?assignment->allocation.spilled.size():0;}
extern "C" limestone_status limestone_assignment_spill(const limestone_assignment* assignment,size_t index,uint32_t* value,limestone_error* error) {
  return boundary(error,[&](){if(!assignment||!value)invalid();*value=at(assignment->allocation.spilled,index,"spill");return LIMESTONE_OK;});
}
extern "C" const char* limestone_assignment_text(const limestone_assignment* assignment){return assignment?assignment->text.c_str():nullptr;}
