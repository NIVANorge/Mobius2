

#ifndef MOBIUS_RUN_MODEL_H
#define MOBIUS_RUN_MODEL_H

#include "datetime.h"
#include "emulate.h"

#ifndef MOBIUS_EMULATE
#define MOBIUS_EMULATE 0
#endif

struct
Model_Run_State {
	Parameter_Value *parameters;
	double *state_vars;
	double *series;
	double *solver_workspace;
	Expanded_Date_Time date_time;
	double solver_t;
	
	Model_Run_State() : solver_workspace(nullptr) {}
};

typedef void batch_function(double *parameters, double *series, double *state_vars, double *solver_workspace, Expanded_Date_Time *date_time);

inline void
call_fun(batch_function *fun, Model_Run_State *run_state) {
#if MOBIUS_EMULATE
	emulate_expression(reinterpret_cast<Math_Expr_FT *>(fun), run_state, nullptr);
#else
	fun(reinterpret_cast<double *>(run_state->parameters), run_state->series, run_state->state_vars, run_state->solver_workspace, &run_state->date_time);
#endif
}

void
run_model(Model_Application *model_app, s64 time_steps);

#endif // MOBIUS_RUN_MODEL_H