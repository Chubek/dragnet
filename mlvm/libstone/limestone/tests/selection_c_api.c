#include "limestone/il.h"
#include <assert.h>
#include <string.h>

typedef struct state {
  int calls,releases,proved;
  limestone_status status;
  limestone_unisel_document *document;
  limestone_selection_model *model;
  limestone_burs_document *burs;
  limestone_program *program;
  limestone_target *target;
} state;
static limestone_status prove(const char *context,const char *parameters,int *proved,void *userdata,limestone_error *error) {
  state *s=(state *)userdata;++s->calls;
  assert(strstr(context,"\"root\"")&&strstr(context,"\"bindings\"")&&strstr(context,"\"covered\""));
  assert(strstr(context,"symbol")&&strstr(context,"exact")&&strstr(parameters,"required"));
  if(s->document){limestone_unisel_document_destroy(s->document);s->document=NULL;}
  if(s->model){limestone_selection_model_destroy(s->model);s->model=NULL;}
  if(s->burs){limestone_burs_document_destroy(s->burs);s->burs=NULL;}
  if(s->program){limestone_program_destroy(s->program);s->program=NULL;}
  if(s->target){limestone_target_destroy(s->target);s->target=NULL;}
  *proved=s->proved;error->code=s->status;strcpy(error->message,"host proof failed");return s->status;
}
static void release(void *userdata){++((state *)userdata)->releases;}
static const char *machine="machine m {operator const(0);instruction C {latency=0;}"
  "pattern p:const():i64 -> C where {predicates=[{name=exact;parameters={required=true;};}];};}";
static const char *umd="machine m {operator const(0);instruction C {latency=0;}"
  "pattern p:const():i64 -> C where {predicates=[{name=exact;parameters={required=true;};}];};}"
  "program p {node %1=const(\"symbol\",42):i64 properties {metadata={mode=exact;};};output %1;}";
static const char *burs="ruleset r {nonterminal r;terminal C(0);rule r:C():i64 -> I "
  "where {predicates=[{name=exact;parameters={required=true;};}];};}"
  "tree t {node %1=C():i64 immediate 42 properties {strings=[{index=0;value=\"symbol\";}];metadata={mode=exact;};};root %1:r;}";

int main(void) {
  limestone_error error;state s={0};limestone_selection_predicate *predicate;
  limestone_selection *selected;limestone_burs_selection *tree;limestone_module *module;limestone_configuration *config;
  assert(!limestone_selection_predicate_create("exact",NULL,&s,release,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT&&!s.releases);
  predicate=limestone_selection_predicate_create("exact",prove,&s,release,&error);assert(predicate);
  s.document=limestone_unisel_load(umd,NULL,&error);assert(s.document);
  assert(!limestone_unisel_analyze(s.document,&error)&&error.code==LIMESTONE_UNSUPPORTED&&strstr(error.message,"exact"));
  assert(limestone_unisel_set_selection_predicate(s.document,"missing",predicate,&error)==LIMESTONE_INVALID_ARGUMENT);
  assert(limestone_unisel_set_selection_predicate(s.document,"absent",NULL,&error)==LIMESTONE_NOT_FOUND);
  assert(limestone_unisel_set_selection_predicate(s.document,"exact",predicate,&error)==LIMESTONE_OK);
  s.proved=1;s.model=limestone_unisel_analyze(s.document,&error);assert(s.model&&!s.document&&s.calls&&!s.releases);
  selected=limestone_selection_run(s.model,LIMESTONE_SELECT_GLOBAL,&error);assert(selected&&!s.model);
  assert(strstr(limestone_selection_text(selected),"source_metadata")&&limestone_selection_count(selected)==1);limestone_selection_destroy(selected);
  assert(!s.releases); /* The owning predicate still retains the callback. */
  s.burs=limestone_burs_load(burs,NULL,&error);assert(s.burs);
  assert(limestone_burs_set_selection_predicate(s.burs,"exact",predicate,&error)==LIMESTONE_OK);
  tree=limestone_burs_select(s.burs,0,&error);assert(tree&&!s.burs&&strstr(limestone_burs_selection_text(tree),"symbol"));limestone_burs_selection_destroy(tree);

  config=limestone_configuration_create();assert(config);
  assert(limestone_configuration_set_pipeline(config,0,0,0,0,0,&error)==LIMESTONE_OK);
  {
    limestone_selector selector;
    for(selector=LIMESTONE_SELECT_GLOBAL;selector<=LIMESTONE_SELECT_BURS;selector=(limestone_selector)(selector+1)) {
      s.target=limestone_target_load(machine,&error);s.program=limestone_program_create();assert(s.target&&s.program);
      assert(limestone_target_set_selection_predicate(s.target,"exact",predicate,&error)==LIMESTONE_OK);
      assert(limestone_program_add_node(s.program,1,"const","i64",NULL,0,1,42,1,1,&error)==LIMESTONE_OK);
      assert(limestone_program_add_output(s.program,1,&error)==LIMESTONE_OK);
      assert(limestone_program_set_metadata(s.program,1,"{\"strings\":[{\"index\":0,\"value\":\"symbol\"}],\"properties\":{\"mode\":\"exact\"}}",&error)==LIMESTONE_OK);
      assert(limestone_program_set_metadata(s.program,1,"{\"strings\":[],\"properties\":1}",&error)==LIMESTONE_INVALID_ARGUMENT);
      assert(limestone_configuration_set_algorithms(config,selector,LIMESTONE_ALLOCATE_LINEAR,&error)==LIMESTONE_OK);
      module=limestone_compile_program_configured(s.program,s.target,config,&error);assert(module&&!s.target&&!s.program);
      assert(strstr(limestone_module_exchange(module),"symbol"));limestone_module_destroy(module);
    }
  }
  limestone_configuration_destroy(config);
  s.document=limestone_unisel_load(umd,NULL,&error);assert(s.document);assert(limestone_unisel_set_selection_predicate(s.document,"exact",predicate,&error)==LIMESTONE_OK);
  s.proved=0;s.model=limestone_unisel_analyze(s.document,&error);assert(s.model&&!limestone_selection_model_candidate_count(s.model));
  assert(!limestone_selection_run(s.model,LIMESTONE_SELECT_GLOBAL,&error)&&error.code==LIMESTONE_UNSATISFIABLE);limestone_selection_model_destroy(s.model);s.model=NULL;
  s.burs=limestone_burs_load(burs,NULL,&error);assert(s.burs);assert(limestone_burs_set_selection_predicate(s.burs,"exact",predicate,&error)==LIMESTONE_OK);
  assert(!limestone_burs_select(s.burs,0,&error)&&error.code==LIMESTONE_UNSATISFIABLE&&!s.burs);
  s.document=limestone_unisel_load(umd,NULL,&error);assert(limestone_unisel_set_selection_predicate(s.document,"exact",predicate,&error)==LIMESTONE_OK);
  s.proved=2;assert(!limestone_unisel_analyze(s.document,&error)&&error.code==LIMESTONE_INVALID_ARGUMENT&&!s.document);
  s.document=limestone_unisel_load(umd,NULL,&error);assert(limestone_unisel_set_selection_predicate(s.document,"exact",predicate,&error)==LIMESTONE_OK);
  s.status=LIMESTONE_CONFLICT;assert(!limestone_unisel_analyze(s.document,&error)&&error.code==LIMESTONE_CONFLICT&&strstr(error.message,"host proof failed"));
  assert(!s.releases);limestone_selection_predicate_destroy(predicate);assert(s.releases==1);limestone_selection_predicate_destroy(NULL);
  return 0;
}
