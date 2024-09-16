

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
	double             *temp_vars;
	double             *series;
	s64                *asserts = nullptr;
	double             *solver_workspace = nullptr;
	s32                *connection_info;    //NOTE: this is only used if we are in MOBIUS_EMULATE mode... For llvm we bake these in as constants
	s32                *index_counts;       //NOTE: same as above.
	Expanded_Date_Time  date_time;
	double              fractional_step;
	
	void set_solver_workspace_size(int size) {
		if(size <= 0) return;
		solver_workspace = (double *)malloc(sizeof(double)*size);
	}
	
	~Model_Run_State() { if(solver_workspace) { free(solver_workspace); solver_workspace = nullptr; } }
};

#define BATCH_FUN_ARG(name, llvm_ty, cpp_ty) cpp_ty name,
#define BATCH_FUN_ARG_LAST(name, llvm_ty, cpp_ty) cpp_ty name
typedef void batch_function(
	#include "batch_fun_args.incl"
);
#undef BATCH_FUN_ARG
//double *parameters, double *series, double *state_vars, double *temp_vars, double *solver_workspace, Expanded_Date_Time *date_time, double fractional_step);

inline void
call_fun(batch_function *fun, Model_Run_State *run_state, double t = 0.0) {
	run_state->fractional_step = t;
#if MOBIUS_EMULATE
	emulate_expression(reinterpret_cast<Math_Expr_FT *>(fun), run_state, nullptr);
#else
	// Would be nice to use BATCH_FUN_ARG here too, but it is a bit tricky
	fun(
		reinterpret_cast<double *>(run_state->parameters), 
		run_state->series, 
		run_state->state_vars, 
		run_state->temp_vars,
		run_state->asserts,
		run_state->solver_workspace, 
		&run_state->date_time, 
		run_state->fractional_step
	);
#endif
}

struct Model_Application;
struct Model_Data;

typedef void (run_callback_type)(void *, double);

bool
run_model(Model_Data *model_data, s64 ms_timeout = -1, bool check_for_nan = false, run_callback_type callback = nullptr, void *callback_data = nullptr);

bool
run_model(Model_Application *app, s64 ms_timeout = -1, bool check_for_nan = false, run_callback_type callback = nullptr, void *callback_data = nullptr);

#endif // MOBIUS_RUN_MODEL_H