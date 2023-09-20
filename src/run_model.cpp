

#include "model_application.h"
#include "emulate.h"
#include "run_model.h"


struct
Batch_Data {
#if MOBIUS_EMULATE
	Math_Expr_FT    *run_code;
#else
	batch_function  *compiled_code;
#endif
	Solver_Function *solver_fun = nullptr;
	double           h;
	double           hmin;
	s64              first_ode_offset;
	int              n_ode;
};


bool
check_for_nans(Model_Data *data, Model_Run_State *run_state) {
	// TODO: This is awfully inefficient. Could we just scan the results vector for NaN first, and then do this if a NaN occurs at all?
	// TODO: Only works for stored results, not temp_results.
	auto &structure = *data->results.structure;
	for(auto &array : structure.structure) {
		for(Var_Id var_id : array.handles) {
			bool error = false;
			structure.for_each(var_id, [var_id, data, run_state, &error](Indexes &idxs, s64 offset) {
				double val = run_state->state_vars[offset];
				if(!std::isfinite(val)) {
					// TODO: Also print indexes.
					begin_error(Mobius_Error::numerical);
					error_print("Got a non-finite value for \"", data->app->vars[var_id]->name, "\" at time step ", run_state->date_time.step, ". Indexes: [");
					int i = 0;
					for(auto idx : idxs.indexes) {
						bool quote;
						auto index_name = data->app->index_data.get_index_name(idxs, idx, &quote);
						maybe_quote(index_name, quote);
						error_print(index_name);
						if(i < idxs.indexes.size()-1) error_print(" ");
						++i;
					}
					error_print("]\n");
					error = true;
					return;
				}
			});
			if(error) return false;
		}
	}
	return true;
}

bool
run_model(Model_Data *data, s64 ms_timeout, bool check_for_nan) {
	
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
	
	data->results.allocate(time_steps, start_date);
	data->temp_results.allocate();
	
	int var_count    = app->result_structure.total_count;
	int series_count = app->series_structure.total_count;
	
	Model_Run_State run_state;
	
	run_state.parameters       = data->parameters.data;
	run_state.state_vars       = data->results.data;
	run_state.temp_vars        = data->temp_results.data;
	run_state.series           = data->series.data + series_count*input_offset;
	run_state.connection_info  = data->connections.data;
	run_state.index_counts     = data->index_counts.data;
	run_state.solver_workspace = nullptr;
	run_state.date_time        = Expanded_Date_Time(start_date, app->time_step_size);
	run_state.fractional_step  = 0.0;
	
	std::vector<Batch_Data>   batch_data(app->batches.size());
	
	int solver_workspace_size = 0;
	int idx = 0;
	for(auto &batch : app->batches) {
		auto &b_data = batch_data[idx];

#if MOBIUS_EMULATE
		b_data.run_code      = batch.run_code;
#else
		b_data.compiled_code = batch.compiled_code;
#endif
		
		if(is_valid(batch.solver_id)) {
			solver_workspace_size = std::max(solver_workspace_size, 4*batch.n_ode); // TODO:    the 4*  is INCA-Dascru specific. Make it general somehow.
			
			auto solver             = model->solvers[batch.solver_id];
			b_data.solver_fun       = model->solver_functions[solver->solver_fun]->solver_fun;
			b_data.first_ode_offset = batch.first_ode_offset;
			b_data.n_ode            = batch.n_ode;
			
			Standardized_Unit *h_unit = nullptr;
			
			if(is_valid(solver->h_par)) {
				// TODO: Should probably check somewhere that this parameter is not distributed over index sets, but we could do that in the model_composition stage.
				s64 offset   = data->parameters.structure->get_offset_base(solver->h_par);
				b_data.h     = data->parameters.get_value(offset)->val_real;
				h_unit       = &model->units[model->parameters[solver->h_par]->unit]->data.standard_form;
			} else {
				b_data.h     = 1.0;
				h_unit       = &model->units[solver->h_unit]->data.standard_form;
			}
			if(is_valid(solver->hmin_par)) {
				s64 offset   = data->parameters.structure->get_offset_base(solver->hmin_par);
				b_data.hmin  = data->parameters.get_value(offset)->val_real;
			} else
				b_data.hmin  = solver->hmin;
			
			double conv;
			bool success = match(h_unit, &app->time_step_unit.standard_form, &conv);
			if(!success) {
				solver->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("It is not possible to convert between the model time step unit and the solver step unit.");
			}
			b_data.h *= conv;
			b_data.h = std::max(std::min(b_data.h, 1.0), 0.0);
			b_data.hmin = std::max(std::min(b_data.hmin, 1.0), 1e-10);
			b_data.hmin = b_data.hmin*b_data.h; //NOTE: The given one was relative, but we need to store it as absolute since h can change in the run.
		}
		++idx;
	}
	run_state.set_solver_workspace_size(solver_workspace_size);

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
		run_state.state_vars += var_count;
		
		//TODO: we *could* also generate code for this for loop to avoid the ifs (but branch prediction should work well since the branches don't change)
		for(auto &batch : batch_data) {
			if(!batch.solver_fun)
				call_fun(BATCH_FUNCTION(batch), &run_state);
			else {
				double *x0 = run_state.state_vars + batch.first_ode_offset;
				//NOTE: h is kept around for the next time step (trying an initial h that we ended up with from the previous step)
				batch.solver_fun(&batch.h, batch.hmin, batch.n_ode, x0, &run_state, BATCH_FUNCTION(batch));
			}
		}
		
		run_state.series    += series_count;
		
		if(check_for_nan)
			if(!check_for_nans(data, &run_state)) return false;
		
		if(ms_timeout > 0) {
			s64 ms = run_timer.get_milliseconds();
			// NOTE: We don't want to write a log to the error stream (or log stream) here since we could get a lot of these during an optimizer run.
			if(ms > ms_timeout)
				return false;
		}
	}
	
	return true;
}

bool
run_model(Model_Application *app, s64 ms_timeout, bool check_for_nan) {
	return run_model(&app->data, ms_timeout, check_for_nan);
}
