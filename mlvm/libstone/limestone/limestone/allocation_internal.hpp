#pragma once
#include "c_api_internal.hpp"
#include "regtl/regtl.hpp"
#include <cmath>

namespace limestone::c_api_internal {
inline regtl::PbqpOptions pbqp_options(const limestone_pbqp_options* source) {
  regtl::PbqpOptions result;if(!source)return result;
  if((source->value_count&&!source->values)||(source->coalescing_count&&!source->coalescing)||source->value_count>1048576||source->coalescing_count>1048576)
    throw Error{Error::Code::InvalidArgument,"invalid PBQP cost arrays"};
  auto cost=[](double value){if(!std::isfinite(value)||value<0)throw Error{Error::Code::InvalidArgument,"PBQP costs must be finite and nonnegative"};return value;};
  result.search_limit=source->search_limit;result.cell_limit=source->cell_limit;result.work_limit=source->work_limit;
  result.costs.default_spill_cost=cost(source->default_spill_cost);
  for(size_t k=0;k<source->value_count;++k) {
    auto& input=source->values[k];if((input.register_count&&!input.registers)||input.register_count>1048576)throw Error{Error::Code::InvalidArgument,"invalid PBQP register costs"};
    regtl::UnaryCost policy{input.value,cost(input.spill_cost),{}};
    for(size_t j=0;j<input.register_count;++j)policy.registers.emplace_back(input.registers[j].physical,cost(input.registers[j].cost));
    result.costs.values.push_back(std::move(policy));
  }
  for(size_t k=0;k<source->coalescing_count;++k){auto& move=source->coalescing[k];result.costs.coalescing.push_back({move.first,move.second,cost(move.cost)});}
  return result;
}
}
