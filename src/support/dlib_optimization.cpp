
#include "dlib_optimization.h"

column_vector
to_dlib_vec(double *data, int len) {
	column_vector result(len);
	for(int idx = 0; idx < len; ++idx) result(idx) = data[idx];
	return std::move(result);
}

bool
run_optimization(Dlib_Optimization_Model &opt_model, double *min_vals, double *max_vals, double epsilon, int max_function_calls) {
	
	//TODO: make multithreading work somehow.
	
	int len = opt_model.get_n_active();//parameters->parameters.size();
	
	std::vector<dlib::function_evaluation> initial_evals;
	if(!opt_model.initial_pars.empty()) {
		dlib::function_evaluation initial_eval;
		initial_eval.x = to_dlib_vec(opt_model.initial_pars.data(), len);
		initial_eval.y = opt_model.maximize ? opt_model.initial_score : -opt_model.initial_score;
		initial_evals.push_back(std::move(initial_eval));
	}
	
	column_vector min_bound = to_dlib_vec(min_vals, len);
	column_vector max_bound = to_dlib_vec(max_vals, len);
	
	dlib::function_evaluation result =
		dlib::find_max_global(opt_model, min_bound, max_bound, dlib::max_function_calls(max_function_calls), dlib::FOREVER, epsilon, initial_evals);
	
	double new_score = opt_model.maximize ? result.y : -result.y;
	opt_model.best_score = new_score; //NOTE: we have to do this since find_max_global works with a copy of the opt_model, thus the best_score is not stored in the original instance.
	
	if( (opt_model.maximize && (new_score <= opt_model.initial_score)) || (!opt_model.maximize && (new_score >= opt_model.initial_score)) || !std::isfinite(new_score) )
		return false;
	
	std::vector<double> pars(len);
	for(int idx = 0; idx < len; ++idx)
		pars[idx] = result.x(idx);
	
	// NOTE: this ensures that the right values are set in the opt_model.data when we finish the function.
	set_parameters(opt_model.data, *opt_model.parameters, pars);
	run_model(opt_model.data, opt_model.ms_timeout);
	
	return true;
}
