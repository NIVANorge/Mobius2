
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
	
	s64 sim_offset = -1, obs_offset = -1;
	
	// NOTE: These should be set up based on begin, end and whatever the start and end date of the model run is.
	s64 sim_stat_offset = -1, obs_stat_offset = -1, stat_ts = -1;
	
	void set_offsets(Model_Data *data); // This one sets the sim_offset and obs_offset.
};

struct
Expr_Parameters {
	std::vector<Indexed_Parameter>             parameters;
	std::vector<std::unique_ptr<Math_Expr_FT>> exprs;
	std::vector<int>                           order;
	
	void copy(const Expr_Parameters &other);
	void set(Model_Application *app, const std::vector<Indexed_Parameter> &parameters);
};

void
set_parameters(Model_Data *data, Expr_Parameters &parameters, const std::vector<double> &values);//const double *values);

double
evaluate_target(Model_Data *data, Optimization_Target *target, double *err_param = nullptr);

typedef std::function<void(int, int, double, double)> Optim_Callback;

struct
Optimization_Model {
	
	Optimization_Model(Model_Data *data, Expr_Parameters &parameters, std::vector<Optimization_Target> &targets, const std::vector<double> *initial_pars = nullptr, const Optim_Callback &callback = nullptr, s64 ms_timeout = -1);
	
	double evaluate(const std::vector<double> &values);//const double *values);
	
	bool                              maximize;
	s64                               ms_timeout, n_timeouts, n_evals;
	double                            best_score, initial_score;
	std::vector<double>               initial_pars;
	Model_Data                       *data;
	Expr_Parameters                  *parameters;
	std::vector<Optimization_Target> *targets;
	Optim_Callback                    callback;
};


#endif // MOBIUS_OPTIMIZATION_H