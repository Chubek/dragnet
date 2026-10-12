#pragma once
#include "optimization.h"
#include "tunah/unisel_adapter.hpp"
#include "tunah/binary_adapter.hpp"

// Private owning configuration shared by the standalone C adapter and the
// pipeline attachment. Public headers expose only opaque C handles.
struct limestone_optimizer {
  limestone::tunah::Session session;
  limestone::tunah::GraphAdapterOptions options;
};
struct limestone_binary_transform { limestone::bin2bin::SemanticTransform transform; };
