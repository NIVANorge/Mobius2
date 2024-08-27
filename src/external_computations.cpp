
#include "external_computations.h"

extern "C" DLLEXPORT void
_special_test_(Value_Access *result, Value_Access *par) {
	for(int idx = 0; idx < result->count; ++idx)
		result->at(idx) = 5.0*par->at(idx);
}
