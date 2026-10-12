#include "CellState.c"
int main(void) { CellState_vm vm; CellState_init(&vm); CellState_inc(&vm); return vm.cell==1 ? 0 : 1; }
