
#ifndef MOBIUS_OPTIMIZATION_H
#define MOBIUS_OPTIMIZATION_H

#include "../model_application.h"
#include "parameter_editing.h"
#include "statistics.h"

struct
Optimization_Target {
	Var_Id sim_id, obs_id;
	std::vector<Index_T> indexes;
	std::vector<int>     err_par_idx;
	
	int stat_type;
	double weight;
	Date_Time start, end;
	
	// NOTE: These should be set up based on begin, end and whatever the start and end date of the model run is.
	s64 sim_offset = -1, obs_offset = -1;
	s64 sim_stat_offset = -1, obs_stat_offset = -1, stat_ts = -1;
};

int
set_parameters(Model_Data *data, const std::vector<Indexed_Parameter> &parameters, const double *values, bool use_expr);

double
evaluate_target(Model_Data *data, Optimization_Target *target, double *err_param = nullptr);

typedef std::function<void(int, int, double, double)> Optim_Callback;

struct
Optimization_Model {
	
	Optimization_Model(Model_Data *data, std::vector<Indexed_Parameter> &parameters, std::vector<Optimization_Target> &targets, double *initial_pars = nullptr, const Optim_Callback &callback = nullptr, s64 ms_timeout = -1);
	
	double evaluate(const double *values);
	
	bool                              maximize, use_expr;
	s64                               ms_timeout, n_timeouts, n_evals;
	double                            best_score, initial_score;
	double                           *initial_pars;
	Model_Data                       *data;
	std::vector<Indexed_Parameter>   *parameters;
	std::vector<Optimization_Target> *targets;
	Optim_Callback callback;
};


#endif // MOBIUS_OPTIMIZATION_H