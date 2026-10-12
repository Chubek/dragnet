#pragma once
#include "metacode.hpp"
#include <bit>
#include <map>

namespace limestone::metacode {
// Declarative source-operand legality, independent of selection cost, allocation
// and solver state. Bindings identify matched source nodes, never physical regs.
enum class OperandPredicate {
  SameValue, DifferentValue, ImmediateEqual, ImmediateNotEqual,
  ImmediateLess, ImmediateLessEqual, ImmediateGreater, ImmediateGreaterEqual,
  MultipleOf, PowerOfTwo, SignedBits, UnsignedBits
};
struct OperandConstraint {
  OperandPredicate predicate;
  std::string operand, other;
  std::optional<int64_t> value;
  bool operator==(const OperandConstraint&) const = default;
};
struct BoundOperand { uint32_t id; std::optional<int64_t> immediate; };
inline bool relational(OperandPredicate predicate) {
  return predicate==OperandPredicate::SameValue||predicate==OperandPredicate::DifferentValue;
}
inline Result<int> validate_operand_constraints(std::span<const OperandConstraint> constraints,
                                               std::span<const std::string> bindings) {
  auto bound=[&](const std::string& name){return !name.empty()&&std::find(bindings.begin(),bindings.end(),name)!=bindings.end();};
  for(auto& constraint:constraints) {
    auto predicate=constraint.predicate;
    if(predicate<OperandPredicate::SameValue||predicate>OperandPredicate::UnsignedBits)
      return Result<int>::err({Error::Code::InvalidArgument,"unknown operand predicate"});
    if(!bound(constraint.operand)||(relational(predicate)&&!bound(constraint.other)))
      return Result<int>::err({Error::Code::InvalidArgument,"operand constraint references an unbound name: "+constraint.operand+" "+constraint.other});
    bool needs_value=!relational(predicate)&&predicate!=OperandPredicate::PowerOfTwo;
    if(bool(constraint.value)!=needs_value||(!relational(predicate)&&!constraint.other.empty()))
      return Result<int>::err({Error::Code::InvalidArgument,"operand constraint has incompatible arguments"});
    if(predicate==OperandPredicate::MultipleOf&&*constraint.value<=0)
      return Result<int>::err({Error::Code::InvalidArgument,"immediate divisor must be positive"});
    if((predicate==OperandPredicate::SignedBits||predicate==OperandPredicate::UnsignedBits)
       &&(*constraint.value<1||*constraint.value>64))
      return Result<int>::err({Error::Code::InvalidArgument,"immediate bit width must be in [1,64]"});
  }
  return Result<int>::ok(0);
}
// A missing immediate is unknown and cannot prove an immediate precondition.
inline bool satisfies(const OperandConstraint& constraint,BoundOperand operand,
                      std::optional<BoundOperand> other={}) {
  auto predicate=constraint.predicate;
  if(relational(predicate))return other&&((operand.id==other->id)==(predicate==OperandPredicate::SameValue));
  if(!operand.immediate)return false;
  auto n=*operand.immediate;
  if(predicate==OperandPredicate::PowerOfTwo)return n>0&&std::has_single_bit(uint64_t(n));
  if(!constraint.value)return false;auto value=*constraint.value;
  switch(predicate) {
    case OperandPredicate::ImmediateEqual:return n==value;
    case OperandPredicate::ImmediateNotEqual:return n!=value;
    case OperandPredicate::ImmediateLess:return n<value;
    case OperandPredicate::ImmediateLessEqual:return n<=value;
    case OperandPredicate::ImmediateGreater:return n>value;
    case OperandPredicate::ImmediateGreaterEqual:return n>=value;
    case OperandPredicate::MultipleOf:return value>0&&n%value==0;
    case OperandPredicate::SignedBits:
      if(value<1||value>64)return false;if(value==64)return true;
      {auto limit=int64_t{1}<<(value-1);return n>=-limit&&n<limit;}
    case OperandPredicate::UnsignedBits:
      if(value<1||value>64||n<0)return false;return value>=63||uint64_t(n)<(uint64_t{1}<<value);
    default:return false;
  }
}
inline constexpr std::string_view operand_predicate_names[]={"same_value","different_value","immediate_eq","immediate_ne",
  "immediate_lt","immediate_le","immediate_gt","immediate_ge","multiple_of","power_of_two","signed_bits","unsigned_bits"};
inline std::string_view predicate_name(OperandPredicate predicate) {
  if(predicate<OperandPredicate::SameValue||predicate>OperandPredicate::UnsignedBits)return {};
  return operand_predicate_names[static_cast<size_t>(predicate)];
}
// Both target/rule loaders use the same closed, version-independent contract:
// { constraints=[{kind=multiple_of;operand=imm;value=4;}, ...]; }
Result<std::vector<OperandConstraint>> load_operand_constraints(const Value::Object&);
Value::Object operand_constraints_metadata(std::span<const OperandConstraint>);
struct StringArgument { uint32_t index=0;std::string value;bool operator==(const StringArgument&) const = default; };
// Target-defined facts remain separate from the built-in semantic/effect model.
// String indices refer to the original mixed argument list, before SSA filtering.
struct OperandMetadata {
  std::vector<StringArgument> strings;
  Value::Object properties;
  bool empty() const {return strings.empty()&&properties.empty();}
};
struct HostConstraint {
  std::string name;
  Value::Object parameters;
  // Immutable owning proof callback. The context contains root, bindings and
  // covered source facts; callbacks must be deterministic and side-effect-free.
  std::function<Result<bool>(const Value::Object&,const Value::Object&)> prove;
};
Result<std::vector<HostConstraint>> load_host_constraints(const Value::Object&);
Value::Object selection_constraints_metadata(std::span<const OperandConstraint>,std::span<const HostConstraint>);
Result<int> validate_host_constraints(std::span<const HostConstraint>);
Result<bool> prove_host_constraints(std::span<const HostConstraint>,const Value::Object&);
Value operand_metadata(const OperandMetadata&);
Result<OperandMetadata> load_operand_metadata(const Value&);
}
