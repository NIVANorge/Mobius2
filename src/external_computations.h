
#ifndef MOBIUS_SPECIAL_COMPUTATION_H
#define MOBIUS_SPECIAL_COMPUTATION_H

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

//#include "common_types.h"
#include "mobius_common.h"

struct
Value_Access {
	double *val;
	s64 stride;
	s64 count;
	
	inline double &at(s64 idx) { return val[idx*stride]; }
	inline u64    &bool_at(s64 idx) { return ((u64*)val)[idx*stride]; }
	inline s64    &int_at(s64 idx) { return ((s64*)val)[idx*stride]; }
};

#endif // MOBIUS_SPECIAL_COMPUTATION_H