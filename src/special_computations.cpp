
#include "special_computations.h"

extern "C" DLLEXPORT void
_special_test_(Special_Indexed_Value *result, Special_Indexed_Value *par) {
	for(int idx = 0; idx < result->count; ++idx)
		result->at(idx) = 5.0*par->at(idx);
}
