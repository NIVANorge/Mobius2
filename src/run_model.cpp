

#include "model_application.h"
#include "emulate.h"
#include "run_model.h"



bool
run_model(Model_Data *data, s64 ms_timeout) {
	
	//warning_print("begin model run.\n");
	
	Model_Application *app = data->app;
	Mobius_Model *model    = app->model;
	if(!app->is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to run model before it was compiled.");
	
	Date_Time start_date = data->get_start_date_parameter();
	Date_Time end_date   = data->get_end_date_parameter();
	
	if(start_date > end_date)
		fatal_error(Mobius_Error::api_usage, "The end date of the model run was set to be later than the start date.\n");
	
	s64 time_steps = steps_between(start_date, end_date, app->time_step_size) + 1; // +1 since end date is inclusive.
	
	s64 input_offset = 0;
	if(data->series.data) {
		if(start_date < data->series.start_date)
			fatal_error(Mobius_Error::api_usage, "Tried to start the model run at an earlier time than there exists time series input data.\n");
		input_offset = steps_between(data->series.start_date, start_date, app->time_step_size);
		
		if(input_offset + time_steps > data->series.time_steps)
			fatal_error(Mobius_Error::api_usage, "Tried to run the model for longer than there exists time series input data.\n");
	} else if (app->series_structure.total_count > 0) {
		// TODO: This is not that good, because what then if somebody change the run dates and run again?
		// note: it is a bit of a fringe case though. Will only happen if somebody run without any input data when some was expected.
		app->allocate_series_data(time_steps, start_date);
	}
	
	app->data.results.allocate(time_steps, start_date);
	
	//warning_print("got past allocation\n");
	
	int var_count    = app->result_structure.total_count;
	int series_count = app->series_structure.total_count;
	
	Model_Run_State run_state;
	
	run_state.parameters       = data->parameters.data;
	run_state.state_vars       = data->results.data;
	run_state.series           = data->series.data + series_count*input_offset;
	run_state.neighbor_info    = data->neighbors.data;
	run_state.solver_workspace = nullptr;
	run_state.date_time        = Expanded_Date_Time(start_date, app->time_step_size);
	run_state.solver_t         = 0.0;
	
	int solver_workspace_size = 0;
	for(auto &batch : app->batches) {
		if(batch.solver_fun)
			solver_workspace_size = std::max(solver_workspace_size, 4*batch.n_ode); // TODO:    the 4*  is INCA-Dascru specific. Make it general somehow.
	}
	if(solver_workspace_size > 0)
		run_state.solver_workspace = (double *)malloc(sizeof(double)*solver_workspace_size);
	
	//warning_print("begin run\n");
	

#if MOBIUS_EMULATE
	#define BATCH_FUNCTION(batch) reinterpret_cast<batch_function *>(batch.run_code)
#else
	#define BATCH_FUNCTION(batch) batch.compiled_code
#endif

	Timer run_timer;
	run_state.date_time.step = -1;

	// Initial values:
	call_fun(BATCH_FUNCTION(app->initial_batch), &run_state);

	for(run_state.date_time.step = 0; run_state.date_time.step < time_steps; run_state.date_time.advance()) {
		memcpy(run_state.state_vars+var_count, run_state.state_vars, sizeof(double)*var_count); // Copy in the last step's values as the initial state of the current step
		run_state.state_vars+=var_count;
		
		//TODO: we *could* also generate code for this for loop to avoid the ifs (but branch prediction should work well since the branches don't change)
		for(auto &batch : app->batches) {
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
		
		if(ms_timeout > 0) {
			s64 ms = run_timer.get_milliseconds();
			if(ms > ms_timeout)
				return false;
		}
	}
	
	//s64 cycles = run_timer.get_cycles();
	//s64 ms     = run_timer.get_milliseconds();
	
	if(run_state.solver_workspace) free(run_state.solver_workspace);
	
	//warning_print("Run time: ", ms, " milliseconds, ", cycles, " cycles.\n");
	
	return true;
}

bool
run_model(Model_Application *app, s64 ms_timeout) {
	return run_model(&app->data, ms_timeout);
}
