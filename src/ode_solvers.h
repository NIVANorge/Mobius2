

#ifndef MOBIUS_ODE_SOLVERS_H
#define MOBIUS_ODE_SOLVERS_H

typedef void ODE_Function(double *x0, double *wk, double t, void *run_state);
typedef bool Solver_Function(double *try_h, double hmin, int n, double *x0, double *wk, ODE_Function ode_fun, void *run_state);


// Specific solvers:

bool inca_dascru(double *try_h, double hmin, int n, double *x0, double *wk, ODE_Function ode_fun, void *run_state);
bool euler_solver(double *try_h, double hmin, int n, double *x0, double *wk, ODE_Function ode_fun, void *run_state);


#endif // MOBIUS_ODE_SOLVERS_H