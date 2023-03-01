
#include "special_computations.h"

extern "C" DLLEXPORT
void
_special_test_(Special_Indexed_Value result, Special_Indexed_Value par) {
	*result.val = 5.0*(*par.val);
}