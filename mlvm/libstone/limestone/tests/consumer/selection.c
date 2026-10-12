#include <limestone/il.h>
#include <string.h>

int main(void) {
  limestone_error error;
  limestone_unisel_document *document=limestone_unisel_load(
    "machine scalar {operator const(0);instruction C {} pattern constant:const():i64 binding imm -> C where {constraints=[{kind=multiple_of;operand=imm;value=2;}];};}"
    "program graph {node %1=const(42):i64;output %1;}","installed.umd",&error);
  limestone_selection_model *model;
  limestone_selection *selection;
  limestone_match_info match;
  int failed;
  if(!document)return 1;
  model=limestone_unisel_analyze(document,&error);limestone_unisel_document_destroy(document);
  if(!model)return 1;
  selection=limestone_selection_run(model,LIMESTONE_SELECT_GLOBAL,&error);limestone_selection_model_destroy(model);
  if(!selection)return 1;
  failed=limestone_selection_count(selection)!=1||limestone_selection_cost(selection)!=1
    ||limestone_selection_candidate(selection,0,&match,&error)!=LIMESTONE_OK;
  if(!failed)failed=strcmp(match.opcode,"C")||!strstr(match.origin,"installed.umd:");
  limestone_selection_destroy(selection);return failed;
}
