
#ifndef MOBIUS_DLIB_OPTIMIZATION_H
#define MOBIUS_DLIB_OPTIMIZATION_H

#include "optimization.h"

typedef dlib::matrix<double,0,1> column_vector;

struct Dlib_Optimization_Model : public Optimization_Model {
	
	double operator(const column_vector &par_values) {
		std::vector<double> par_vals(par_values.size());
		for(int idx = 0; idx < par_values.size(); ++idx) par_vals[idx] = par_values[idx];
		return (*this)(par_vals.data());
	}
};

#endif // MOBIUS_DLIB_OPTIMIZATION_H