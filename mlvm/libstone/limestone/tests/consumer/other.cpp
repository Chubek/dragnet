#include <exolayer/exolayer.h>
#include <limestone/limestone.h>
int consumer_cpp_other() {
  auto* module=limestone_compile("42");if(!module)return 0;
  limestone_module_destroy(module);return 42;
}
