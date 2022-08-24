

#ifndef MOBIUS_RUN_MODEL_H
#define MOBIUS_RUN_MODEL_H

#ifndef MOBIUS_EMULATE
#define MOBIUS_EMULATE 0
#endif

#include "datetime.h"

//#if MOBIUS_EMULATE          // Hmm, why doesn't this work? Not that nice to require it as a dependency of this file unless wanted.
#include "emulate.h"
//#endif

struct
Model_Run_State {
	Parameter_Value    *parameters;
	double             *state_vars;
	double             *series;
	double             *solver_workspace;
	s64                *neighbor_info;    //NOTE: this is only used if we are in emulate... For llvm we bake these in as constants
	Expanded_Date_Time  date_time;
	double              solver_t;      //NOTE: not currently used, but we could expose it to the model code later or interpolate the date_time using it.
	
	Model_Run_State() : solver_workspace(nullptr) {}
};

typedef void batch_function(double *parameters, double *series, double *state_vars, double *solver_workspace, Expanded_Date_Time *date_time);

inline void
call_fun(batch_function *fun, Model_Run_State *run_state, double t = 0.0) {
	run_state->solver_t = t;
#if MOBIUS_EMULATE
	emulate_expression(reinterpret_cast<Math_Expr_FT *>(fun), run_state, nullptr);
#else
	fun(reinterpret_cast<double *>(run_state->parameters), run_state->series, run_state->state_vars, run_state->solver_workspace, &run_state->date_time);
#endif
}

struct Model_Application;

void
run_model(Model_Application *model_app);

#endif // MOBIUS_RUN_MODEL_H