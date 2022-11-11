
#include "optimization.h"


int
set_parameters(Model_Data *data, const std::vector<Indexed_Parameter> &parameters, const double *values, bool use_expr) {
	Parameter_Value val;
	//if(!use_expr) {
		int par_idx = 0;
		for(const auto &par : parameters) {
			val.val_real = values[par_idx++];
			if(!par.virt)
				set_parameter_value(par, data, val);
		}
		return -1;
	//}
	
	//TODO: expressions!
	
	return -1;
}

double
evaluate_target(Model_Data *data, Optimization_Target *target, double *err_param) {
	//TODO: if multiple targets have the same start and end, it could be wasteful to re-compute the stats for each target, so we could cache them.
	//   We should also have some short-circuit to not compute some of the more expensive statistics if we only need a simple one.
	
	Stat_Class typetype = is_stat_class(target->stat_type);
	if(typetype == Stat_Class::stat) {
		Time_Series_Stats stats;
		compute_time_series_stats(&stats, nullptr, &data->results, target->sim_offset, target->sim_stat_offset, target->stat_ts);
		return get_stat(&stats, (Stat_Type)target->stat_type);
	} else if(typetype == Stat_Class::residual) {
		Residual_Stats residual_stats;
		auto obs_data = target->obs_id.type == Var_Id::Type::series ? &data->series : &data->additional_series;
		compute_residual_stats(&residual_stats, &data->results, target->sim_offset, target->sim_stat_offset, obs_data, target->obs_offset,
			target->obs_stat_offset, target->stat_ts, target->stat_type == (int)Residual_Type::srcc);
		return get_stat(&residual_stats, (Residual_Type)target->stat_type);
	} else if(typetype == Stat_Class::log_likelihood) {
		auto obs_data = target->obs_id.type == Var_Id::Type::series ? &data->series : &data->additional_series;
		return compute_ll(&data->results, target->sim_offset, target->sim_stat_offset, obs_data, target->obs_offset, target->obs_stat_offset,
			target->stat_ts, err_param, (LL_Type)target->stat_type);
	}
	return std::numeric_limits<double>::quiet_NaN();
}

Optimization_Model::Optimization_Model(Model_Data *data, std::vector<Indexed_Parameter> &parameters, std::vector<Optimization_Target> &targets, double *initial_pars, const Optim_Callback &callback, s64 ms_timeout)
	: data(data), parameters(&parameters), targets(&targets), ms_timeout(ms_timeout), initial_pars(initial_pars) {
		
	Date_Time input_start = data->series.start_date;
	Date_Time run_start = data->get_start_date_parameter();
	Date_Time run_end   = data->get_end_date_parameter();
	
	Stat_Class typetype = Stat_Class::none;
	int maximize = -1;

	for(auto &target : targets) {
		target.sim_stat_offset = steps_between(run_start, target.start, data->app->time_step_size);
		target.obs_stat_offset = steps_between(input_start, target.start, data->app->time_step_size);
		target.stat_ts         = steps_between(target.start, target.end, data->app->time_step_size) + 1;
		
		if(target.start < run_start || target.end > run_end || target.end < target.start)
			fatal_error(Mobius_Error::api_usage, "The start-end interval for one of the target stats is invalid or does not lie within the model run interval.");
		
		if(target.weight < 0)
			fatal_error(Mobius_Error::api_usage, "One of the optimization targets got a negative weight.");
		
		target.sim_offset = data->results.structure->get_offset(target.sim_id, target.indexes);
		if(is_valid(target.obs_id)) {
			auto obs_data = target.obs_id.type == Var_Id::Type::series ? &data->series : &data->additional_series;
			target.obs_offset = obs_data->structure->get_offset(target.obs_id, target.indexes);
		}
		
		if(typetype == Stat_Class::none) {
			typetype = is_stat_class(target.stat_type);
			maximize = is_positive_good((Residual_Type)target.stat_type);
			if(maximize < 0 && typetype == Stat_Class::residual)
				fatal_error(Mobius_Error::api_usage, "Can only use residual types that should be maximized or minimized in optimization.");
			this->maximize = (bool)maximize;
		} else {
			Stat_Class typetype2 = is_stat_class(target.stat_type);
			if(typetype != typetype2)
				fatal_error(Mobius_Error::api_usage, "Mismatching statistic types between optimization targets."); //TODO better error message
			int maximize2 = is_positive_good((Residual_Type)target.stat_type);
			if(maximize2 != maximize)
				fatal_error(Mobius_Error::api_usage, "Mixing statistic types that should be maximized with types that should be minimized");
		}
		
		if(typetype != Stat_Class::stat && !is_valid(target.obs_id))
			fatal_error(Mobius_Error::api_usage, "A target statistic of this type requires an observed series to compare against");
	}
	
	use_expr = false; //TODO!!
	
	this->callback = nullptr; // To not have it call back in initial score computation.
	if(initial_pars) {
		int err_row = set_parameters(data, parameters, initial_pars, use_expr);
		if(err_row >= 0)
			fatal_error(Mobius_Error::api_usage, "The expression of the parameter at row ", err_row, " could not successfully be evaluated."); //TODO: could maybe have a better message when we actually implement this!
		
		best_score = initial_score = evaluate(initial_pars);
	} else
		best_score = initial_score = maximize ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	
	this->callback = callback;
	n_evals    = 0;
	n_timeouts = 0;
}

double Optimization_Model::evaluate(const double *values) {
	
	set_parameters(data, *parameters, values, use_expr);
	
	bool run_finished = run_model(data, ms_timeout);
	
	if(!run_finished) {
		++n_timeouts;
		return maximize ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	}
	
	double agg = 0.0;
	for(Optimization_Target &target : *targets) {
		double val;
		if(is_stat_class(target.stat_type) == Stat_Class::log_likelihood) {
			std::vector<double> err_param(err_par_count((LL_Type)target.stat_type));
			for(int idx = 0; idx < err_param.size(); ++idx)
				err_param[idx] = values[target.err_par_idx[idx]];
			val = evaluate_target(data, &target, err_param.data());
		} else {
			val = evaluate_target(data, &target);
		}
		agg += target.weight * val;
	}
	
	best_score = maximize ? std::max(best_score, agg) : std::min(best_score, agg);
	
	++n_evals;
	if(callback)
		callback(n_evals, n_timeouts, initial_score, best_score);
	
	return agg;
}
