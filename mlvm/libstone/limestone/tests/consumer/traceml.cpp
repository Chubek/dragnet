#include <limestone/traceml.h>
#include <traceml/traceml.hpp>

// Compile the standalone public C header and C++ runtime in the same consumer.
static_assert(sizeof(limestone_traceml_options) > 0);
static_assert(sizeof(limestone::traceml::RuntimeValue) > 0);
