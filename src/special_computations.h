
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
Special_Indexed_Value {
	//Parameter_Value *val;
	double *val;
	s64 stride;
	
	//Parameter_Value &operator[](s64 idx) { return val[idx*stride]; }
	double &operator[](s64 idx) { return val[idx*stride]; }
};

#endif // MOBIUS_SPECIAL_COMPUTATION_H