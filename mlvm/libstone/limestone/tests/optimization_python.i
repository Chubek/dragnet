%module optimization_fixture
%{
#include "limestone/optimization.h"
#include <cstring>
// The fixture proves a concrete wrapping-counter vocabulary. It exercises an
// independently compiled host extension rather than trusting arbitrary terms.
static limestone_status counter_legal(const limestone_binary_semantic_view *view,
    const char *candidate,int *proved,void *,limestone_error *) {
  *proved=view->control==LIMESTONE_BINARY_FALLTHROUGH&&!view->has_branch_target
    &&!std::strcmp(view->semantics,"(set counter (add counter 1))")
    &&(!std::strcmp(candidate,view->semantics)
       ||!std::strcmp(candidate,"(set counter (sub counter -1))"));
  return LIMESTONE_OK;
}
%}
%import "limestone/optimization.h"
%inline %{
limestone_binary_legality counter_legality() { return counter_legal; }
%}
