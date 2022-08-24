
#include "ode_solvers.h"

#include <cmath>

bool euler_solver(double *try_h, double hmin, int n, double *x0, Model_Run_State *run_state, batch_function ode_fun) {
	double t = 0.0;
	bool run = true;
	double h = *try_h;
	
	double *wk = run_state->solver_workspace;
	
	while(run) {
	
		if(t + h > 1.0) {
			h = 1.0 - t;
			run = false;
		}
		
		call_fun(ode_fun, run_state, t);
		t += h;
		
		for(int var_idx = 0; var_idx < n; ++var_idx)
			x0[var_idx] += h*wk[var_idx];
	}
	
	return true;
}

bool inca_dascru(double *try_h, double hmin, int n, double *x0, Model_Run_State *run_state, batch_function ode_fun)  {
	//NOTE: This is an adaption of the original solver from INCA based on the DASCRU Runge-Kutta 4 solver. See also
	// Rational Runge-Kutta Methods for Solving Systems of Ordinary Differential Equations, Computing 20, 333-342.
	// Source code has been modified for clarity.

	double h = *try_h;

	double t = 0.0;			      // 0 <= t <= 1 is the time progress of the solver.
	
	double *wk = run_state->solver_workspace;
	// Divide up "workspaces" for the solver where it can store intermediate values.
	double *wk0 = wk + n;
	double *wk1 = wk0 + n;
	double *wk2 = wk1 + n;

	bool run = true;
	
	while(run) {
		double t_backup = t;
		bool step_was_reduced = false;
		bool step_can_be_reduced = true;

		// make a backup of the initial state of the step.
		for(int var_idx = 0; var_idx < n; ++var_idx)
			wk0[var_idx] = x0[var_idx];

		bool reset = false;
		bool step_can_be_increased = true;
		do {
			reset = false;
			step_can_be_increased = true;

			if (t + h > 1.0) {
				h = 1.0 - t;
				run = false;
			}
			
			call_fun(ode_fun, run_state, t);
			for(int var_idx = 0; var_idx < n; ++var_idx) {
				double dx = h * wk[var_idx] / 3.0;
				wk1[var_idx] = dx;
				x0[var_idx] = wk0[var_idx] + dx;
			}
			t += h / 3.0;
			
			call_fun(ode_fun, run_state, t);
			for(int var_idx = 0; var_idx < n; ++var_idx) {
				double dx0 = h * wk[var_idx] / 3.0;
				double dx  = 0.5 * (dx0 + wk1[var_idx]);
				x0[var_idx] = wk0[var_idx] + dx;
			}
			
			call_fun(ode_fun, run_state, t);
			for(int var_idx = 0; var_idx < n; ++var_idx) {
				double dx = h * wk[var_idx];
				wk2[var_idx] = dx;
				dx = 0.375 * (dx + wk1[var_idx]);
				x0[var_idx] = wk0[var_idx] + dx;
			}
			t += h / 6.0;
			
			call_fun(ode_fun, run_state, t);
			for(int var_idx = 0; var_idx < n; ++var_idx) {
				double dx0 = h * wk[var_idx] / 3.0;
				double dx = wk1[var_idx] + 4.0 * dx0;
				wk1[var_idx] = dx;
				dx = 1.5*(dx - wk2[var_idx]);
				x0[var_idx] = wk0[var_idx] + dx;
			}
			t += 0.5*h;
			
			call_fun(ode_fun, run_state, t);
			for(int var_idx = 0; var_idx < n; ++var_idx) {
				double dx0 = h * wk[var_idx] / 3.0;
				double dx = 0.5 * (dx0 + wk1[var_idx]);
				x0[var_idx] = wk0[var_idx] + dx;
				
				// error control:
				double tol = 0.0005;
				double abs_x0 = std::abs(x0[var_idx]);
				if (abs_x0 >= 0.001) tol = abs_x0 * 0.0005;
				
				double est = std::abs(dx + dx - 1.5 * (dx0 + wk2[var_idx]));
				
				if (est < tol || !step_can_be_reduced) {
					if (est >= 0.03125 * tol)
						step_can_be_increased = false;
				} else {
					run = true; // If we thought we reached the end of the integration, that may no longer be true since we are reducing the step size.
					step_was_reduced = true;
					
					h = 0.5 * h; // Reduce the step size.

					if(h < hmin) {
						h = hmin;
						step_can_be_reduced = false;
					}
					
					// Reset the state and try again.
					for (int idx = 0; idx < n; ++idx)
						x0[idx] = wk0[idx];

					t = t_backup;
					
					reset = true;
					break;
				}
			}
		} while(reset);

		if(step_can_be_increased && !step_was_reduced && run) {
			h = h + h;
			step_can_be_reduced = true;
		}
	}
	
	*try_h = h; // return it out again so that it can be used for the next time the function is entered, if desirable.
	
	return true;
}