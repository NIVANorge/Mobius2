
#ifndef MOBIUS_OPTIMIZATION_H
#define MOBIUS_OPTIMIZATION_H

#include "../model_application.h"
#include "parameter_editing.h"

struct
Optimization_Target {
	s64 sim_offset, obs_offset;
	// TODO: err pars for MCMC
	double weight;
	Date_Time begin, end;
};


int
set_parameters(Model_Data *data, const std::vector<Indexed_Parameter> &parameters, double *values, bool use_expr);

#endif // MOBIUS_OPTIMIZATION_H