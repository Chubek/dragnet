#include "limestone/il.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *fixture(const char *directory,const char *name) {
  char path[4096];char *source=(char *)malloc(32768);FILE *file;size_t size;
  assert(source);snprintf(path,sizeof(path),"%s/%s",directory,name);file=fopen(path,"rb");assert(file);
  size=fread(source,1,32767,file);assert(!ferror(file)&&feof(file));fclose(file);source[size]=0;return source;
}
static void graph_selection(const char *directory) {
  const char *source="machine scalar {operator const(0);operator add(2);instruction CONST {} instruction ADD {} instruction ADDI {}"
    "pattern 1 constant:const():i64 -> CONST cost 2;pattern 2 sum:add(?x:i64,?y:i64):i64 -> ADD cost 4;"
    "pattern 3 immediate:add(?x:i64,const():i64[-8..7]):i64 -> ADDI cost 3; }"
    "program graph {node %10=const(100):i64;node %20=const(7):i64;node %1=add(%10,%20):i64;output %1;}";
  limestone_error error;limestone_unisel_document *document=limestone_unisel_load(source,"standalone.umd",&error);
  limestone_selection_model *model;limestone_selection *selected,*greedy;limestone_match_info info;uint32_t value;int32_t literal;size_t k,c,index;
  assert(document&&error.code==LIMESTONE_OK);model=limestone_unisel_analyze(document,&error);assert(model);limestone_unisel_document_destroy(document);
  assert(limestone_selection_model_candidate_count(model)==4&&limestone_selection_model_clause_count(model)==5);
  assert(limestone_selection_model_candidate(model,0,&info,&error)==LIMESTONE_OK&&info.pattern==3&&info.root==1&&info.cost==3&&info.covered_count==2&&info.input_count==1&&info.output_count==1);
  assert(!strcmp(info.opcode,"ADDI")&&!strcmp(info.name,"immediate")&&strstr(info.origin,"standalone.umd:")&&info.reason);
  assert(limestone_selection_model_value(model,0,LIMESTONE_MATCH_COVERED,1,&value,&error)==LIMESTONE_OK&&value==20);
  assert(limestone_selection_model_value(model,0,LIMESTONE_MATCH_INPUT,0,&value,&error)==LIMESTONE_OK&&value==10);
  assert(limestone_selection_model_literal(model,0,0,&literal,&error)==LIMESTONE_OK&&literal==3);
  assert(limestone_selection_model_candidate(model,99,&info,&error)==LIMESTONE_NOT_FOUND&&!info.opcode);
  assert(limestone_selection_model_value(model,0,(limestone_match_values)99,0,&value,NULL)==LIMESTONE_INVALID_ARGUMENT);
  assert(limestone_selection_model_literal(model,99,0,&literal,NULL)==LIMESTONE_NOT_FOUND);
  assert(strstr(limestone_selection_model_text(model),"clause -1 -4"));
  selected=limestone_selection_run(model,LIMESTONE_SELECT_GLOBAL,&error);greedy=limestone_selection_run(model,LIMESTONE_SELECT_GREEDY,&error);
  assert(selected&&greedy&&limestone_selection_cost(selected)==5&&limestone_selection_cost(greedy)==8&&limestone_selection_count(selected)==2);
  /* The selected assignment independently satisfies every inspectable clause. */
  for(c=0;c<limestone_selection_model_clause_count(model);++c) {
    int satisfied=0;
    for(index=0;index<limestone_selection_model_literal_count(model,c);++index) {
      int chosen=0;limestone_match_info candidate;
      assert(limestone_selection_model_literal(model,c,index,&literal,NULL)==LIMESTONE_OK);
      assert(limestone_selection_model_candidate(model,(size_t)(literal>0?literal:-literal)-1,&candidate,NULL)==LIMESTONE_OK);
      for(k=0;k<limestone_selection_count(selected);++k){assert(limestone_selection_candidate(selected,k,&info,NULL)==LIMESTONE_OK);chosen|=candidate.root==info.root&&candidate.pattern==info.pattern;}
      satisfied|=(literal>0)==chosen;
    }
    assert(satisfied);
  }
  assert(!limestone_selection_run(model,(limestone_selector)99,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  assert(!limestone_selection_run(model,LIMESTONE_SELECT_BURS,&error)&&error.code==LIMESTONE_UNSUPPORTED);
  limestone_selection_model_destroy(model);limestone_selection_destroy(greedy);
  assert(limestone_selection_candidate(selected,1,&info,NULL)==LIMESTONE_OK&&info.root==1&&info.pattern==3&&strstr(info.origin,"standalone.umd:"));
  assert(limestone_selection_value(selected,1,LIMESTONE_MATCH_INPUT,0,&value,NULL)==LIMESTONE_OK&&value==10);
  assert(limestone_selection_value(selected,1,LIMESTONE_MATCH_INPUT,99,&value,NULL)==LIMESTONE_NOT_FOUND);
  assert(limestone_selection_candidate(selected,99,&info,NULL)==LIMESTONE_NOT_FOUND&&!info.origin);
  document=limestone_unisel_load("machine x {} program uncovered {node %7=unknown():i64;output %7;}",NULL,&error);assert(document);model=limestone_unisel_analyze(document,&error);assert(model);limestone_unisel_document_destroy(document);
  assert(!limestone_selection_model_candidate_count(model)&&limestone_selection_model_clause_count(model)==1&&limestone_selection_model_literal_count(model,0)==0);
  assert(!limestone_selection_run(model,LIMESTONE_SELECT_GLOBAL,&error)&&error.code==LIMESTONE_UNSATISFIABLE);
  assert(!limestone_selection_run(model,LIMESTONE_SELECT_GREEDY,&error)&&error.code==LIMESTONE_UNSATISFIABLE);limestone_selection_model_destroy(model);
  {
    limestone_scheduling_document *handoff=limestone_schedrow_load(limestone_selection_text(selected),NULL,&error);limestone_schedule *schedule;
    assert(handoff);limestone_selection_destroy(selected);schedule=limestone_schedule_run(handoff,0,0,&error);assert(schedule&&limestone_schedule_count(schedule)==2);limestone_scheduling_document_destroy(handoff);limestone_schedule_destroy(schedule);
  }
  {
    char path[4096];snprintf(path,sizeof(path),"%s/arithmetic-includes.umd",directory);document=limestone_unisel_load_file(path,&error);assert(document);model=limestone_unisel_analyze(document,&error);assert(model);limestone_unisel_document_destroy(document);
    assert(strstr(limestone_selection_model_text(model),"/includes/scalar-machine.umd:7:"));limestone_selection_model_destroy(model);
  }
  document=limestone_unisel_load("machine x {}",NULL,NULL);assert(document&&!limestone_unisel_analyze(document,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);limestone_unisel_document_destroy(document);
  assert(!limestone_unisel_load(NULL,NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT&&!limestone_unisel_analyze(NULL,NULL));
  assert(!limestone_unisel_load_file(NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  {
    const char *legal="machine x {operator const(0);instruction C {} pattern p:const():i64 binding imm -> C where {constraints=[{kind=power_of_two;operand=imm;},{kind=unsigned_bits;operand=imm;value=8;}];};}"
      "program graph {node %1=const(8):i64;output %1;}";
    document=limestone_unisel_load(legal,"constraints.umd",&error);assert(document);model=limestone_unisel_analyze(document,&error);limestone_unisel_document_destroy(document);assert(model&&limestone_selection_model_candidate_count(model)==1);
    selected=limestone_selection_run(model,LIMESTONE_SELECT_GLOBAL,&error);limestone_selection_model_destroy(model);assert(selected&&limestone_selection_count(selected)==1);limestone_selection_destroy(selected);
    assert(!limestone_unisel_load("machine x {operator C(0);instruction I {} pattern p:C() -> I where {constraints=[{kind=power_of_two;operand=missing;}];};}",NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  }
  assert(!limestone_selection_model_text(NULL)&&!limestone_selection_text(NULL)&&!limestone_selection_model_candidate_count(NULL)&&!limestone_selection_count(NULL));
  limestone_unisel_document_destroy(NULL);limestone_selection_model_destroy(NULL);limestone_selection_destroy(NULL);
}
void il_c_api(const char *directory) {
  graph_selection(directory);
  limestone_error error;char *source=fixture(directory,"selection.limeburg");
  {
    char path[4096];limestone_burs_document *document;limestone_burs_selection *selection;
    snprintf(path,sizeof(path),"%s/selection-includes.limeburg",directory);document=limestone_burs_load_file(path,&error);assert(document);
    selection=limestone_burs_select(document,0,&error);limestone_burs_document_destroy(document);assert(selection&&limestone_burs_selection_cost(selection)==3);limestone_burs_selection_destroy(selection);
    assert(!limestone_burs_load_file(NULL,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
  }
  {
    limestone_burs_document *document=limestone_burs_load("ruleset x {nonterminal reg;terminal A(0);rule 9 reg:A():i64 -> I;}tree t {node %7=A():f64;root %7:reg;}","types.rules",&error);
    limestone_burs_analysis *analysis;limestone_burs_attempt attempt;limestone_burs_state state;
    assert(document&&!limestone_burs_select(document,0,&error)&&error.code==LIMESTONE_UNSATISFIABLE);
    analysis=limestone_burs_analyze(document,0,&error);limestone_burs_document_destroy(document);assert(analysis&&limestone_burs_state_count(analysis)==0&&limestone_burs_attempt_count(analysis)==1);
    assert(limestone_burs_attempt_at(analysis,0,&attempt,&error)==LIMESTONE_OK&&attempt.node==7&&attempt.rule==9&&!attempt.matched&&strstr(attempt.reason,"expected type i64"));
    assert(limestone_burs_attempt_at(analysis,1,&attempt,&error)==LIMESTONE_NOT_FOUND&&!attempt.reason);
    assert(limestone_burs_state_at(analysis,0,&state,NULL)==LIMESTONE_NOT_FOUND&&!state.nonterminal_name);limestone_burs_analysis_destroy(analysis);limestone_burs_analysis_destroy(NULL);
  }
  limestone_burs_document *burs=limestone_burs_load(source,"selection.limeburg",&error);limestone_burs_selection *selected;free(source);
  assert(burs&&limestone_burs_tree_count(burs)==1&&strcmp(limestone_burs_tree_name(burs,0),"sum")==0);
  assert(!limestone_burs_select(burs,99,NULL));assert(limestone_burs_document_text(burs));
  selected=limestone_burs_select(burs,0,&error);assert(selected&&limestone_burs_selection_cost(selected)==3&&limestone_burs_selection_count(selected)==2);
  {
    limestone_burs_analysis *analysis=limestone_burs_analyze(burs,0,&error);limestone_burs_state state;size_t index;int found=0;assert(analysis&&limestone_burs_state_count(analysis)>0);
    for(index=0;index<limestone_burs_state_count(analysis);++index){assert(limestone_burs_state_at(analysis,index,&state,&error)==LIMESTONE_OK);if(state.node==1&&state.rule==3){assert(state.cost==3&&strcmp(state.nonterminal_name,"reg")==0);found=1;}}assert(found);limestone_burs_analysis_destroy(analysis);
  }
  limestone_burs_document_destroy(burs);
  assert(strcmp(limestone_burs_selection_opcode(selected,1),"ADDI")==0&&strstr(limestone_burs_selection_text(selected),"immediates"));
  {
    limestone_scheduling_document *handoff=limestone_schedrow_load(limestone_burs_selection_text(selected),NULL,&error);
    limestone_schedule *issues;assert(handoff);limestone_burs_selection_destroy(selected);
    issues=limestone_schedule_run(handoff,0,0,&error);assert(issues&&limestone_schedule_count(issues)==2);
    limestone_scheduling_document_destroy(handoff);assert(limestone_schedule_text(issues));limestone_schedule_destroy(issues);
  }
  source=fixture(directory,"scheduling.schedrow");
  {
    limestone_scheduling_document *document=limestone_schedrow_load(source,"scheduling.schedrow",&error);limestone_schedule *issues;limestone_issue issue;
    free(source);assert(document&&limestone_scheduling_region_count(document)==1&&strcmp(limestone_scheduling_region_name(document,0),"test")==0);
    assert(!limestone_schedule_run(document,99,0,&error)&&error.code==LIMESTONE_NOT_FOUND);
    issues=limestone_schedule_run(document,0,0,&error);assert(issues);limestone_scheduling_document_destroy(document);
    assert(limestone_schedule_count(issues)==3&&limestone_schedule_issue(issues,1,&issue,&error)==LIMESTONE_OK&&issue.instruction==2&&issue.cycle==2&&issue.has_slot);
    assert(limestone_schedule_resource_count(issues,1)==1&&strcmp(limestone_schedule_resource(issues,1,0),"LOAD")==0);
    assert(limestone_schedule_issue(issues,99,&issue,NULL)==LIMESTONE_NOT_FOUND&&strstr(limestone_schedule_text(issues),"cycle 3"));limestone_schedule_destroy(issues);
    document=limestone_schedrow_load("region loop {instruction %1 {opcode=x;latency=0;}dependency {producer=%1;consumer=%1;kind=true;latency=2;distance=1;}}",NULL,&error);assert(document);
    assert(!limestone_schedule_run(document,0,0,&error)&&error.code==LIMESTONE_UNSUPPORTED);
    assert(!limestone_schedule_run(document,0,1,&error)&&error.code==LIMESTONE_UNSATISFIABLE);
    issues=limestone_schedule_run(document,0,2,&error);assert(issues&&limestone_schedule_count(issues)==1);limestone_schedule_destroy(issues);limestone_scheduling_document_destroy(document);
  }
  source=fixture(directory,"allocation.regtl");
  {
    limestone_scheduling_document *document=limestone_schedrow_load("machine_model M {issue_width=3;}region R {instruction %1 {opcode=wide;latency=0;issue={width=2;slots=[0,2];};}}",NULL,&error);
    limestone_schedule *issues;uint32_t slot;assert(document);issues=limestone_schedule_run(document,0,0,&error);assert(issues);limestone_scheduling_document_destroy(document);
    assert(limestone_schedule_slot_count(issues,0)==2&&limestone_schedule_slot(issues,0,0,&slot,&error)==LIMESTONE_OK&&slot==0);
    assert(limestone_schedule_slot(issues,0,1,&slot,&error)==LIMESTONE_OK&&slot==2);
    assert(limestone_schedule_slot(issues,0,2,&slot,&error)==LIMESTONE_NOT_FOUND&&!limestone_schedule_slot_count(issues,99));limestone_schedule_destroy(issues);
  }
  {
    char *group_source=fixture(directory,"grouping.schedrow");limestone_scheduling_document *document=limestone_schedrow_load(group_source,NULL,&error);limestone_schedule *schedule;limestone_group_info group;limestone_issue first,second;uint32_t member;free(group_source);assert(document);
    schedule=limestone_schedule_run(document,0,0,&error);assert(schedule);limestone_scheduling_document_destroy(document);
    assert(limestone_schedule_group_count(schedule)==1&&limestone_schedule_group(schedule,0,&group,&error)==LIMESTONE_OK);
    assert(group.id==44&&group.kind==LIMESTONE_GROUP_BUNDLE&&group.member_count==2&&group.issue_width==2&&strcmp(group.name,"%44")==0);
    assert(limestone_schedule_group_member(schedule,0,0,&member,&error)==LIMESTONE_OK&&member==8);
    assert(limestone_schedule_group_member(schedule,0,99,&member,NULL)==LIMESTONE_NOT_FOUND);
    assert(limestone_schedule_issue(schedule,0,&first,&error)==LIMESTONE_OK&&limestone_schedule_issue(schedule,1,&second,&error)==LIMESTONE_OK&&first.cycle==second.cycle&&first.slot==1&&second.slot==0);
    limestone_schedule_destroy(schedule);
  }
  {
    limestone_allocation_document *document=limestone_regtl_load(source,"allocation.regtl",&error);limestone_assignment *allocation;uint32_t a,b,value;limestone_allocator algorithm;
    free(source);assert(document&&limestone_allocation_unit_count(document)==1&&limestone_allocation_function_count(document,0)==1);
    assert(strcmp(limestone_allocation_unit_name(document,0),"fixture")==0&&strcmp(limestone_allocation_function_name(document,0,0),"test")==0);
    assert(limestone_allocation_document_text(document)&&!limestone_assignment_run(document,0,99,LIMESTONE_ALLOCATE_LINEAR,&error)&&error.code==LIMESTONE_NOT_FOUND);
    assert(!limestone_assignment_run(document,0,0,(limestone_allocator)100,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT);
    for(algorithm=LIMESTONE_ALLOCATE_LINEAR;algorithm<=LIMESTONE_ALLOCATE_PBQP;algorithm=(limestone_allocator)(algorithm+1)) {
      allocation=limestone_assignment_run(document,0,0,algorithm,&error);assert(allocation&&limestone_assignment_count(allocation)==2&&!limestone_assignment_spill_count(allocation));
      assert(limestone_assignment_register(allocation,7,&a,&error)==LIMESTONE_OK&&limestone_assignment_register(allocation,8,&b,&error)==LIMESTONE_OK&&a!=b&&b==2);
      assert(limestone_assignment_at(allocation,0,&value,&b,&error)==LIMESTONE_OK&&value==7&&b==a);limestone_assignment_destroy(allocation);
    }
    allocation=limestone_assignment_run(document,0,LIMESTONE_ALLOCATION_RANGES,LIMESTONE_ALLOCATE_LINEAR,&error);assert(allocation);limestone_allocation_document_destroy(document);
    assert(limestone_assignment_text(allocation)&&limestone_assignment_register(allocation,999,&a,NULL)==LIMESTONE_NOT_FOUND);limestone_assignment_destroy(allocation);
    document=limestone_regtl_load("regtl pressure {regclass G=[$0];live %1:G [0,2] {} live %2:G [0,2] {}}",NULL,&error);assert(document);
    allocation=limestone_assignment_run(document,0,LIMESTONE_ALLOCATION_RANGES,LIMESTONE_ALLOCATE_COLOR,&error);assert(allocation&&limestone_assignment_spill_count(allocation)==1);
    assert(limestone_assignment_spill(allocation,0,&value,&error)==LIMESTONE_OK&&limestone_assignment_register(allocation,value,&a,NULL)==LIMESTONE_NOT_FOUND);
    limestone_assignment_destroy(allocation);limestone_allocation_document_destroy(document);
    {
      limestone_pbqp_options policy;limestone_value_cost costs[2]={{1,10,NULL,0},{2,2,NULL,0}};double cost=0;
      limestone_configuration *config=limestone_configuration_create();limestone_pbqp_options_default(&policy);policy.values=costs;policy.value_count=2;
      assert(limestone_configuration_set_algorithms(config,LIMESTONE_SELECT_GLOBAL,LIMESTONE_ALLOCATE_PBQP,&error)==LIMESTONE_OK);
      assert(limestone_configuration_set_pbqp(config,&policy,&error)==LIMESTONE_OK);policy.default_spill_cost=-1;
      assert(limestone_configuration_set_pbqp(config,&policy,&error)==LIMESTONE_INVALID_ARGUMENT);limestone_configuration_destroy(config);policy.default_spill_cost=1;
      document=limestone_regtl_load("regtl p {regclass G=[$0];live %1:G [0,2] {} live %2:G [0,2] {}}",NULL,&error);assert(document);
      allocation=limestone_assignment_run_pbqp(document,0,LIMESTONE_ALLOCATION_RANGES,&policy,&error);assert(allocation);
      limestone_allocation_document_destroy(document);costs[0].spill_cost=0;costs[1].spill_cost=100;
      assert(limestone_assignment_cost(allocation,&cost,&error)==LIMESTONE_OK&&cost==2);
      assert(limestone_assignment_spill(allocation,0,&value,&error)==LIMESTONE_OK&&value==2);limestone_assignment_destroy(allocation);
      assert(limestone_assignment_cost(NULL,&cost,&error)==LIMESTONE_INVALID_ARGUMENT);
    }
  }
  assert(!limestone_schedrow_load("region broken {", "broken.schedrow",&error)&&error.code==LIMESTONE_PARSE&&strstr(error.message,"broken.schedrow"));
  assert(!limestone_burs_load(NULL,NULL,NULL)&&!limestone_regtl_load(NULL,NULL,NULL));
  limestone_burs_document_destroy(NULL);limestone_burs_selection_destroy(NULL);limestone_scheduling_document_destroy(NULL);limestone_schedule_destroy(NULL);limestone_allocation_document_destroy(NULL);limestone_assignment_destroy(NULL);
}
