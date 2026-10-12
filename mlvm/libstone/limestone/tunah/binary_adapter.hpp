#pragma once
#include "tunah.hpp"
#include "bin2bin/bin2bin.hpp"

namespace limestone::tunah {
struct BinaryAdapterOptions {
  Limits limits;
  CostModel costs;
  // Called at ingress and after extraction. Prove types, architectural effects,
  // widths and operand legality in the host's ISA semantic vocabulary.
  std::function<Result<bool>(const bin2bin::LiftedInstruction&,const Term&)> legality;
};
// Snapshot the session and options. The context identity covers host semantics,
// predicates and legality analysis; rules/costs/budgets are encoded automatically.
// Translation owns layout/encoding and preserves instruction/control boundaries.
Result<bin2bin::SemanticTransform> binary_transform(const Session&,std::string context_identity,const BinaryAdapterOptions&);
 struct BinaryRegionAdapterOptions {
   Limits limits; CostModel costs;
  std::function<Result<Term>(const bin2bin::SemanticRegion&)> ingest;
  std::function<Result<bool>(const bin2bin::SemanticRegion&,const Term&)> legality;
   std::function<Result<bin2bin::SemanticRegion>(const bin2bin::SemanticRegion&,const Term&)> reconstruct;
   std::string source_state_model, target_state_model, source_domain, target_domain;
};
// Region terms can justify instruction combining/removal. Reconstruction owns
// source/synthetic boundary mapping; Bin2Bin validates labels and relaxes branches.
Result<bin2bin::RegionTransform> binary_region_transform(const Session&,std::string context_identity,const BinaryRegionAdapterOptions&);
}
