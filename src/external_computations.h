
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
	//Parameter_Value *val;
	double *val;
	s64 stride;
	s64 count;
	
	double &at(s64 idx) { return val[idx*stride]; }
};

#endif // MOBIUS_SPECIAL_COMPUTATION_H