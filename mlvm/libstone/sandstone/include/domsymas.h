/* Backward-compatibility shim: the SymAS databank library was renamed
 * DomSymAs -> Sandstone. New code should include <sandstone.h>; this
 * header keeps the historical include path working. */
#ifndef DOMSYMAS_H_COMPAT
#define DOMSYMAS_H_COMPAT
#include "sandstone.h"
#endif /* DOMSYMAS_H_COMPAT */
