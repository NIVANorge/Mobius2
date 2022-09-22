
#include "optimization.h"

int
set_parameters(Model_Data *data, const std::vector<Indexed_Parameter> &parameters, double *values, bool use_expr) {
	Parameter_Value val;
	if(!use_expr) {
		int par_idx = 0;
		for(const auto &par : parameters) {
			val.val_real = values[par_idx++];
			if(!par.virt)
				set_parameter_value(par, data, val);
		}
		return -1;
	}
	
	//TODO: expressions!
	
	return -1;
}

double
evaluate_target(Model_Data *data, Optimization_Target *target) {
	//TODO: if multiple targets have the same start and end, it could be wasteful to re-compute the stats for each target, so we could cache them.
	
	int typetype = is_stat_type(target->stat_type);
	if(typetype == 0) {
		Time_Series_Stats stats;
		compute_time_series_stats(&stats, nullptr, &data->results, target->sim_offset, target->sim_stat_offset, target->sim_ts);
		return get_stat(&stats, (Stat_Type)target->stat_type);
	} else if(typetype == 1) {
		Residual_Stats residual_stats;
		auto obs_data = target->obs_series_type == 1 ? &data->series : &data->additional_series;
		compute_residual_stats(&residual_stats, &data->results, target->sim_offset, target->sim_stat_offset, obs_data, target->obs_offset, 
			target->obs_stat_offset, target->stat_ts, target->stat_type == (int)Residual_Type::srcc);
		return get_stat(&residual_stats, (Residual_Type)target->stat_type);
	}
	return std::numeric_limits<double>::quiet_NaN();
}

Optimization_Model::Optimization_Model(Model_Data *data, std::vector<Indexed_Parameter> &parameters, std::vector<Optimization_Target> &targets, double *initial_pars, const Optim_Callback &callback, s64 ms_timeout)
	: data(data), parameters(&parameters), targets(&targets), ms_timeout(ms_timeout) {
		
	Date_Time input_start = data->series.start_date;
	Date_Time run_start = data->get_start_date_parameter();
	Date_Time run_end   = data->get_end_date_parameter();
	
	int typetype = -1;
	int maximize = -1;
	for(auto &target : *targets) {
		target->sim_stat_offset = steps_between(run_start, target->start, data->app->time_step_size);
		target->obs_stat_offset = steps_between(input_start, target->start, data->app->time_step_size);
		target->stat_ts         = steps_between(target->start, target->end, data->app->time_step_size) + 1;
		if(typetype < 0) {
			typetype = is_stat_type(target->stat_type);
			maximize = is_positive_good((Residual_Type)target->stat_type);
			if(maximize < 0 && typetype == 1) 
				fatal_error(Mobius_Error::api_usage, "Can only use residual types that should be maximized or minimized in optimization.");
			this->maximize = (bool)maximize;
		} else {
			int typetype2 = is_stat_type(target->stat_type);
			if(typetype != typetype2)
				fatal_error(Mobius_Error::api_usage, "Mismatching statistic types between optimization targets."); //TODO better error message
			int maximize2 = is_positive_good((Residual_Type)target->stat_type);
			if(maximize2 != maximize)
				fatal_error(Mobius_Error::api_usage, "Mixing statistic types that should be maximized with types that should be minimized");
		}
	}
	
	this->callback = nullptr; // To not have it call back in initial score computation.
	if(initial_pars)
		best_score = initial_score = (*this)(initial_pars);
	else
		best_score = initial_score = maximize ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	
	this->callback = callback;
	n_evals    = 0;
	n_timeouts = 0;
	use_expr = false; //TODO!
}

double Optimization_Model::operator(double *values) {
	set_parameters(data, *parameters, values, use_expr);
	
	bool run_finished = run_model(data, ms_timeout);
	
	if(!run_finished) {
		++n_timeouts;
		return maximize ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	}
	
	double agg = 0.0;
	for(Optimization_Target &target : *targets)
		agg += target.weight * evaluate_target(data, &target);
	
	best_score = maximize ? std::max(best_score, agg) : std::min(best_score, agg);
	
	++n_evals;
	if(callback)
		callback(n_evals, n_timeouts, initial_score, best_score);
	
	return agg;
}
