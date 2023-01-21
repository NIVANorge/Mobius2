
#ifndef MOBIUS_DLIB_OPTIMIZATION_H
#define MOBIUS_DLIB_OPTIMIZATION_H

#include "optimization.h"

//#define DLIB_NO_GUI_SUPPORT  //NOTE: Turns off dlib's own GUI since we are using upp.

#include "../../third_party/dlib/optimization.h"
#include "../../third_party/dlib/global_optimization.h"

typedef dlib::matrix<double,0,1> column_vector;

struct Dlib_Optimization_Model : public Optimization_Model {
	
	Dlib_Optimization_Model(Model_Data *data, Expr_Parameters &parameters, std::vector<Optimization_Target> &targets, double *initial_pars = nullptr, const Optim_Callback &callback = nullptr, s64 ms_timeout = -1)
		: Optimization_Model(data, parameters, targets, initial_pars, callback, ms_timeout) {};
	
	double operator()(const column_vector &par_values) {
		std::vector<double> par_vals(par_values.size());
		for(int idx = 0; idx < par_values.size(); ++idx) par_vals[idx] = par_values(idx);
		double result = evaluate(par_vals.data());
		return maximize ? result : -result;
	}
};


bool
run_optimization(Dlib_Optimization_Model &opt_model, double *min_vals, double *max_vals, double epsilon, int max_function_calls);


#endif // MOBIUS_DLIB_OPTIMIZATION_H