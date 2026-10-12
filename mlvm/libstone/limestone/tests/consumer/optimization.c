#include <limestone/optimization.h>
#include <string.h>

int main(void) {
  limestone_error error;limestone_optimization_info info;
  limestone_optimizer *optimizer=limestone_optimizer_create(&error);
  limestone_optimization *result;
  if(!optimizer)return 1;
  if(limestone_optimizer_load_rules(optimizer,"(operator add 2)(rule zero (add ?x 0) ?x)","consumer.rules",&error)!=LIMESTONE_OK){limestone_optimizer_destroy(optimizer);return 2;}
  result=limestone_optimizer_saturate(optimizer,"(add 42 0)",NULL,&error);
  limestone_optimizer_destroy(optimizer);
  if(!result)return 3;
  if(limestone_optimization_get_info(result,&info,&error)!=LIMESTONE_OK||strcmp(limestone_optimization_expression(result),"42")||!info.saturated||!info.rewrites){limestone_optimization_destroy(result);return 4;}
  limestone_optimization_destroy(result);return 0;
}
