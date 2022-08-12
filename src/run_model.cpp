

#include "model_application.h"
#include "emulate.h"
#include "run_model.h"



void run_model(Model_Application *model_app) {
	
	warning_print("begin emulate model run.\n");
	
	if(!model_app->is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to run model before it was compiled.");
	Mobius_Model *model = model_app->model;
	
	Date_Time start_date = model_app->get_start_date_parameter();
	Date_Time end_date   = model_app->get_end_date_parameter();
	
	if(start_date > end_date)
		fatal_error(Mobius_Error::api_usage, "The end date of the model run was set to be later than the start date.\n");
	
	s64 time_steps = steps_between(start_date, end_date, model_app->timestep_size) + 1; // +1 since end date is inclusive.
	
	s64 input_offset = 0;
	if(model_app->series_data.data) {
		if(start_date < model_app->series_data.start_date)
			fatal_error(Mobius_Error::api_usage, "Tried to start the model run at an earlier time than there exists time series input data.\n");
		input_offset = steps_between(model_app->series_data.start_date, start_date, model_app->timestep_size);
		
		if(input_offset + time_steps > model_app->series_data.time_steps)
			fatal_error(Mobius_Error::api_usage, "Tried to run the model for longer than there exists time series input data.\n");
	} else {
		// TODO: This is not that good, because what then if somebody change the run dates and run again?
		model_app->series_data.allocate(time_steps);
		model_app->series_data.start_date = start_date;
	}
	
	model_app->result_data.allocate(time_steps);
	model_app->result_data.start_date = start_date;
	
	warning_print("got past allocation\n");
	
	int var_count    = model_app->result_data.total_count;
	int series_count = model_app->series_data.total_count;
	
	//TODO: better encapsulate run_state functionality
	Model_Run_State run_state;
	//run_state.model_app  = model_app;
	run_state.parameters = model_app->parameter_data.data;
	run_state.state_vars = model_app->result_data.data;
	run_state.series     = model_app->series_data.data + series_count*input_offset;
	run_state.neighbor_info = model_app->neighbor_data.data;
	run_state.solver_workspace = nullptr;
	run_state.date_time  = Expanded_Date_Time(start_date, model_app->timestep_size);
	run_state.solver_t   = 0.0;
	
	int solver_workspace_size = 0;
	for(auto &batch : model_app->batches) {
		if(batch.solver_fun)
			solver_workspace_size = std::max(solver_workspace_size, 4*batch.n_ode); // TODO:    the 4*  is INCA-Dascru specific. Make it general somehow.
	}
	if(solver_workspace_size > 0)
		run_state.solver_workspace = (double *)malloc(sizeof(double)*solver_workspace_size);
	
	warning_print("begin run\n");
	

#if MOBIUS_EMULATE
	#define BATCH_FUNCTION(batch) reinterpret_cast<batch_function *>(batch.run_code)
#else
	#define BATCH_FUNCTION(batch) batch.compiled_code
#endif

	Timer run_timer;
	run_state.date_time.step = -1;

	// Initial values:
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
}
