#pragma once
#include "limestone.h"
#include "metacode/operand_constraints.hpp"

struct limestone_selection_predicate {
  std::string name;
  std::function<limestone::Result<bool>(const limestone::metacode::Value::Object&,
                                      const limestone::metacode::Value::Object&)> prove;
};
namespace limestone::c_api_internal {
template<class Rules> size_t bind_selection_predicate(Rules& rules,const char* name,
                                                     const limestone_selection_predicate* predicate) {
  if(!name||!*name||(predicate&&predicate->name!=name))
    throw Error{Error::Code::InvalidArgument,"invalid selection predicate name"};
  size_t count=0;
  for(auto& rule:rules)for(auto& constraint:rule.host_constraints)if(constraint.name==name){
    constraint.prove=predicate?predicate->prove:decltype(constraint.prove){};++count;
  }
  return count;
}
}
