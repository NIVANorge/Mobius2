

#ifndef MOBIUS_ODE_SOLVERS_H
#define MOBIUS_ODE_SOLVERS_H

#include "run_model.h"

//typedef void ODE_Function(double t, void *run_state);
//typedef bool Solver_Function(double *try_h, double hmin, int n, double *x0, double *wk, ODE_Function ode_fun, void *run_state);


typedef bool Solver_Function(double *try_h, double hmin, int n, double *x0, Model_Run_State *state, batch_function fun);


// Specific solvers:

bool inca_dascru(double *try_h, double hmin, int n, double *x0, Model_Run_State *run_state, batch_function ode_fun);
bool euler_solver(double *try_h, double hmin, int n, double *x0, Model_Run_State *run_state, batch_function ode_fun);


#endif // MOBIUS_ODE_SOLVERS_H