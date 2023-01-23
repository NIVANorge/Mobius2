
#include "optimization.h"
#include "emulate.h"

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

Optimization_Model::Optimization_Model(Model_Data *data, Expr_Parameters &parameters, std::vector<Optimization_Target> &targets, double *initial_pars, const Optim_Callback &callback, s64 ms_timeout)
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
	
	this->callback = nullptr; // To not have it call back in initial score computation.
	if(initial_pars) {
		set_parameters(data, parameters, initial_pars);
		best_score = initial_score = evaluate(initial_pars);
	} else
		best_score = initial_score = maximize ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	
	this->callback = callback;
	n_evals    = 0;
	n_timeouts = 0;
}

double Optimization_Model::evaluate(const double *values) {
	
	set_parameters(data, *parameters, values);
	
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

void
get_dependencies(Math_Expr_FT *expr, std::vector<int> &dependencies_out) {
	if(!expr) return;
	
	for(auto sub : expr->exprs)
		get_dependencies(sub, dependencies_out);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto lit = static_cast<Literal_FT *>(expr->exprs[0]);
		dependencies_out.push_back(lit->value.val_integer);
	}
}

void
topological_sort_exprs_visit(int idx, std::vector<std::unique_ptr<Math_Expr_FT>> &exprs, std::vector<std::pair<bool, bool>> &visited, std::vector<int> &sorted_out) {
	if(!exprs[idx].get()) return;
	if(visited[idx].second) return;
	if(visited[idx].first)
		fatal_error(Mobius_Error::api_usage, "There is a circular reference between the expressions involving row ", idx, ".");
	visited[idx].first = true;
	std::vector<int> dependencies;
	get_dependencies(exprs[idx].get(), dependencies);
	for(auto dep : dependencies)
		topological_sort_exprs_visit(dep, exprs, visited, sorted_out);
	visited[idx].second = true;
	sorted_out.push_back(idx);
}

void
Expr_Parameters::set(Model_Application *app, const std::vector<Indexed_Parameter> &parameters) {
	this->parameters = parameters;
	exprs.clear();
	
	Function_Resolve_Data data;
	data.app = app;
	data.scope = &app->model->global_scope;
	data.simplified = true;
	for(auto &par : parameters)
		data.simplified_syms.push_back(par.symbol);
	
	for(auto &par : parameters) {
		if(par.expr.empty()) {
			exprs.push_back(nullptr);
			continue;
		}
		// TODO: Make source locations on streams better for non-file streams! For instance, should let the line number be the par number.
		Token_Stream stream("", par.expr);
		auto peek = stream.peek_token();
		if(peek.type == Token_Type::eof) {
			exprs.push_back(nullptr);
			continue;
		}
		Math_Expr_AST *ast = parse_math_expr(&stream);
		auto fun = make_cast(resolve_function_tree(ast, &data).fun, Value_Type::real);
		fun = prune_tree(fun);
		exprs.emplace_back(fun);
		delete ast;
	}
	
	order.clear();
	std::vector<std::pair<bool, bool>> visited(parameters.size(), { false, false });
	for(int idx = 0; idx < parameters.size(); ++idx) {
		topological_sort_exprs_visit(idx, exprs, visited, order);
	}
}

void
Expr_Parameters::copy(const Expr_Parameters &other) {
	this->parameters = other.parameters;
	this->order      = other.order;
	exprs.clear();
	for(auto &ptr : other.exprs)
		if(ptr.get()) exprs.emplace_back(::copy(ptr.get()));
		else          exprs.push_back(nullptr);
}


void
set_parameters(Model_Data *data, Expr_Parameters &pars, const double *values) {
	Parameter_Value val;
	int active_idx = 0;
	
	std::vector<double> vals(pars.parameters.size(), std::numeric_limits<double>::quiet_NaN());
	
	for(int idx = 0; idx < pars.parameters.size(); ++idx) {
		auto &par = pars.parameters[idx];
		auto ft = pars.exprs[idx].get();
		if(!ft) {
			vals[idx] = values[active_idx];
			++active_idx;
			val.val_real = vals[idx];
			if(!par.virt)
				set_parameter_value(par, data, val);
		}
	}
	
	// TODO: Rewrite this so that the additional complexity is not used if there are no expressions!
	Model_Run_State run_state = {}; // Hmm, it is not that nice that we need this to call emulate_expression. Should maybe just change the API to pass in the various vectors directly?
	run_state.parameters = (Parameter_Value *)vals.data();
	
	for(int idx : pars.order) {
		auto &par = pars.parameters[idx];
		auto ft = pars.exprs[idx].get();
		vals[idx] = emulate_expression(ft, &run_state, nullptr).val_real;
		val.val_real = vals[idx];
		set_parameter_value(par, data, val);
	}
}
