
#include "run_model.h"

// Attempt at very simple estimate of jacobian matrix.
// However, it is very inefficient as it computes very many terms that will always be 0...

void
estimate_jacobian(batch_function *fun, Model_Run_state *run_state, double t, double *x, s64 N, s64 Nstate, s64 Ntemp, s64 ode_offset, double *temp_storage) {
	
	double *fbasevec = temp_storage;
	double *state = fbasevec + N;
	double *tempstate = state + Nstate;
	
	// Create a copy of the state to work with
	for(s64 i = 0; i < Nstate; ++i) {
		state[i] = run_state->state_vars[i];
	}
	for(s64 i = 0; i < Ntemp; ++i) {
		tempstate[i] = run_state->temp_vars[i];
	}
	
	
	// Set the state of the ODE variables to be at the base point
	// (note, ODE variables are never in temp_vars).
	for(s64 i = 0; i < N; ++i) {
		state[i + ode_offset] = x[i];
	}		
	
	fun(
		reinterpret_cast<double *>(run_state->parameters), 
		run_state->series, 
		state, 
		tempstate,
		run_state->asserts,
		run_state->solver_workspace, 
		&run_state->date_time,
		&run_state->rand_state,
		t
	);
	
	// Store the derivatives at the base point
	for(s64 i = 0; i < N; ++i) {
		fbasevec[i] = run_state->solver_workspace[i];
	}
	
	double h0 = std::numeric_limits<double>::infinity();
	for(s64 i = 0; i < N; ++i)
		h0 = std::min(std::max(1e-6*x[i], 1e-9), h0);
	
	for(s64 i = 0; i < N; ++i) {
		
		// Reset the state (TODO: Maybe unnecessary??)
		for(s64 j = 0; j < Nstate; ++j) {
			state[j] = run_state->state_vars[j];
		}
		for(s64 j = 0; j < Ntemp; ++j) {
			tempstate[j] = run_state->temp_vars[j];
		}
		
		// Set the ODE variables to be at the base point, but permute variable i to estimate derivatives in that direction.
		for(s64 j = 0; j < N; ++j) {
			state[j + ode_offset] = x[j];
		}
		
		// Note: attempt to improve numerical accuracy of adding h0 to xi
		volatile double temp = x[i] + h0;
		double h = temp - x[i];
		state[i + ode_offset] = x[i] + h;
			
		fun(
			reinterpret_cast<double *>(run_state->parameters), 
			run_state->series, 
			state, 
			tempstate,
			run_state->asserts,
			run_state->solver_workspace, 
			&run_state->date_time,
			&run_state->rand_state,
			t
		);
		
		for(s64 j = 0; j < N; ++j) {
			double fbase = fbasevec[j];
			double fpermute = run_state->solver_workspace[j];
			double derivative = (fpermute - fbase) / h;
			
			// TODO; insert into jacobi matrix the value "derivative" at row j and column i.
		}
	}

}