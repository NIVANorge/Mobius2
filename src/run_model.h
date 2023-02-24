

#ifndef MOBIUS_RUN_MODEL_H
#define MOBIUS_RUN_MODEL_H

#ifndef MOBIUS_EMULATE
#define MOBIUS_EMULATE 0
#endif

#include "datetime.h"
#include "common_types.h"

#if MOBIUS_EMULATE
#include "emulate.h"
#endif

struct
Model_Run_State {
	Parameter_Value    *parameters;
	double             *state_vars;
	double             *series;
	double             *solver_workspace;
	s32                *connection_info;    //NOTE: this is only used if we are in MOBIUS_EMULATE mode... For llvm we bake these in as constants
	s32                *index_counts;       //NOTE: same as above.
	Expanded_Date_Time  date_time;
	double              solver_t;
	
	Model_Run_State() : solver_workspace(nullptr) {}
};

typedef void batch_function(double *parameters, double *series, double *state_vars, double *solver_workspace, Expanded_Date_Time *date_time, double fractional_step);

inline void
call_fun(batch_function *fun, Model_Run_State *run_state, double t = 0.0) {
	run_state->solver_t = t;
#if MOBIUS_EMULATE
	emulate_expression(reinterpret_cast<Math_Expr_FT *>(fun), run_state, nullptr);
#else
	fun(reinterpret_cast<double *>(run_state->parameters), run_state->series, run_state->state_vars, run_state->solver_workspace, &run_state->date_time, run_state->solver_t);
#endif
}

struct Model_Application;
struct Model_Data;

bool
run_model(Model_Data *model_data, s64 ms_timeout = -1);

bool
run_model(Model_Application *app, s64 ms_timeout = -1);

#endif // MOBIUS_RUN_MODEL_H