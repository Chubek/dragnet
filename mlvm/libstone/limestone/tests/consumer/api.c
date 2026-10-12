#include <limestone/limestone.h>
#include <exolayer/exolayer.h>
#include <limestone/il.h>
#include <limestone/runtime.h>
#include <limestone/optimization.h>
#include <string.h>
static void identity(const exl_value_t *args,size_t count,exl_value_t *result,void *user) {
  (void)user;if(count==1)*result=args[0];
}
int consumer_c_api(void) {
  exl_context_t *context=exl_context_create();exl_value_t argument,result;int status;
  limestone_burs_document *document=limestone_burs_load("ruleset x {nonterminal reg;terminal A(0);rule reg:A():i64 -> I;}tree t {node %7=A():i64;root %7:reg;}",NULL,NULL);
  limestone_burs_analysis *analysis;limestone_burs_state state;limestone_runtime_options runtime_options;
  if(!document)return 0;analysis=limestone_burs_analyze(document,0,NULL);limestone_burs_document_destroy(document);
  if(!analysis)return 0;status=limestone_burs_state_at(analysis,0,&state,NULL);
  if(status||state.node!=7||strcmp(state.nonterminal_name,"reg")){limestone_burs_analysis_destroy(analysis);return 0;}limestone_burs_analysis_destroy(analysis);
  limestone_runtime_options_default(&runtime_options);if(!runtime_options.hot_threshold||limestone_runtime_resident_count(NULL)!=0)return 0;
  {
    limestone_optimizer *optimizer=limestone_optimizer_create(NULL);limestone_optimization *optimized;
    if(!optimizer)return 0;
    status=limestone_optimizer_load_rules(optimizer,"(operator add 2)(rule identity (add ?x 0) ?x)",NULL,NULL);
    if(status){limestone_optimizer_destroy(optimizer);return 0;}
    optimized=limestone_optimizer_saturate(optimizer,"(add 42 0)",NULL,NULL);limestone_optimizer_destroy(optimizer);
    if(!optimized)return 0;status=strcmp(limestone_optimization_expression(optimized),"42");limestone_optimization_destroy(optimized);if(status)return 0;
  }
  if(!context)return 0;argument.kind=EXL_I64;argument.as.i64=42;
  status=exl_register(context,"identity",identity,NULL);
  if(!status)status=exl_call(context,"identity",&argument,1,&result);
  exl_context_destroy(context);return status?0:(int)result.as.i64;
}
