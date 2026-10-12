#include "ObjectVM.c"
#include "vmweave/adapters/kalloc.h"
VMWEAVE_KALLOC_ADAPTER(ObjectVM)
int main(void) {
  ObjectVM_vm vm; void *arena=km_init(); ObjectVM_object *object;
  if(!arena) return 1;
  ObjectVM_init(&vm);
  if(ObjectVM_memory_kalloc(&vm,arena)) return 2;
  object=ObjectVM_object_new(&vm,1,32); if(!object) return 3;
  if(ObjectVM_object_clear(&vm) || vm.vw_objects) return 4;
  km_destroy(arena); return 0;
}
