

#include "model_application.h"
#include "emulate.h"
#include "run_model.h"



void run_model(Model_Application *model_app, s64 time_steps) {
	
	warning_print("begin emulate model run.\n");
	
	if(!model_app->is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to run model before it was compiled.");
	Mobius_Model *model = model_app->model;
	
	model_app->result_data.allocate(time_steps);
	if(!model_app->series_data.data)
		model_app->series_data.allocate(time_steps);
	
	warning_print("got past allocation\n");
	
	//TODO: better encapsulate run_state functionality
	Model_Run_State run_state;
	//run_state.model_app  = model_app;
	run_state.parameters = model_app->parameter_data.data;
	run_state.state_vars = model_app->result_data.data;
	run_state.series     = model_app->series_data.data;       // TODO: properly advance into this to the start date of the model run.
	run_state.neighbor_info = model_app->neighbor_data.data;
	run_state.solver_workspace = nullptr;
	run_state.date_time  = Expanded_Date_Time(); // TODO: set this properly, using the start date parameter and the timestep_size
	run_state.solver_t   = 0.0;
	
	// TODO: we could reuse structured storage?
	//run_state.neighbor_info = (s64 *)malloc(sizeof(s64)*
	
	int solver_workspace_size = 0;
	for(auto &batch : model_app->batches) {
		if(batch.solver_fun)
			solver_workspace_size = std::max(solver_workspace_size, 4*batch.n_ode); // TODO:    the 4*  is INCA-Dascru specific. Make it general somehow.
	}
	if(solver_workspace_size > 0)
		run_state.solver_workspace = (double *)malloc(sizeof(double)*solver_workspace_size);
	
	int var_count    = model_app->result_data.total_count;
	int series_count = model_app->series_data.total_count;
	
	warning_print("begin run\n");
	// Initial values:

#if MOBIUS_EMULATE
	#define BATCH_FUNCTION(batch) reinterpret_cast<batch_function *>(batch.run_code)
#else
	#define BATCH_FUNCTION(batch) batch.compiled_code
#endif

	Timer run_timer;
	run_state.date_time.step = -1;

	call_fun(BATCH_FUNCTION(model_app->initial_batch), &run_state);

	
	for(run_state.date_time.step = 0; run_state.date_time.step < time_steps; run_state.date_time.advance()) {
		memcpy(run_state.state_vars+var_count, run_state.state_vars, sizeof(double)*var_count); // Copy in the last step's values as the initial state of the current step
		run_state.state_vars+=var_count;
		
		//TODO: we *could* also generate code for this for loop to avoid the ifs (but branch prediction should work well since the branches don't change)
		for(auto &batch : model_app->batches) {
			if(!batch.solver_fun)
				call_fun(BATCH_FUNCTION(batch), &run_state);
			else {
				double *x0 = run_state.state_vars + batch.first_ode_offset;
				double h   = batch.h;
				//TODO: keep h around for the next time step (trying an initial h that we ended up with from the previous step)
				batch.solver_fun(&h, batch.hmin, batch.n_ode, x0, &run_state, BATCH_FUNCTION(batch));
			}
		}
		
		run_state.series    += series_count;
	}
	
	s64 cycles = run_timer.get_cycles();
	s64 ms     = run_timer.get_milliseconds();
	
	if(run_state.solver_workspace) free(run_state.solver_workspace);
	
	warning_print("Run time: ", ms, " milliseconds, ", cycles, " cycles.\n");
	
	warning_print("finished run.\n");
	
	warning_print("finished running.\n");
}
