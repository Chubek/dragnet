#pragma once
#include "schedrow.hpp"
namespace limestone::schedrow::detail {
Result<int> validate_model(const Region&,const MachineModel&);
inline bool same_cycle(GroupKind kind) {return kind==GroupKind::SameCycle||kind==GroupKind::Bundle;}
inline bool adjacent(GroupKind kind) {return kind!=GroupKind::Ordered&&kind!=GroupKind::SameCycle;}
}
