
#include <boost/numeric/odeint.hpp>

#include "ode_solvers.h"

// TODO: See if we can improve on this later so that we don't need to use temprorarily allocated vectors and matrices but instead reuse the same workspace?

typedef boost::numeric::ublas::vector<double> vec_boost;
typedef boost::numeric::ublas::matrix<double> mat_boost;

struct
Boost_ODE_System {
	Model_Run_State *run_state;
	batch_function *fun;
	
	// TODO: More constructor arguments
	Boost_ODE_System(Model_Run_State *run_state, batch_function *fun) : run_state(run_state), fun(fun) {}
	
	// TODO: N, ode_offset must be arguments to constructor
	void operator()(const vec_boost &x, vec_boost &dxdt, double t) {
		// TODO: Could we avoid having to do this?
		for(s64 i = 0; i < N; ++i)
			run_state->state_vars[i + ode_offset] = x[i];
		
		// TODO: We could instead just call the function directly with dxdt.data().begin() replacing solver workspace
		call_fun(fun, run_state, t);
		
		for(s64 i = 0; i < N; ++i)
			dxdt[i] = run_state->solver_workspace[i];
	}
};

struct
Boost_ODE_System_Jacobi {
	Model_Run_State *run_state;
	batch_function *fun;
	
	// TODO: More constructor arguments
	Boost_ODE_System_Jacobi(Model_Run_State *run_state, batch_function *fun) : run_state(run_state), fun(fun) {}
	
	void operator()(const vec_boost &x, mat_boost &J, const double &t, vec_boost &dfdt) {
		//J.clear(); // Only necessary if we don't compute all the values inside estimate_jacobian.
		
		// TODO: Need to pass J obviously.
		estimate_jacobian(fun, run_state, t, x.data().begin(), N, Nstate, Ntemp, ode_offset, jacobi_temp_storage);
	}
};

bool
rosenbrock4_solver(double *try_h, double hmin, int n, double *x0, Model_Run_State *run_state, batch_function ode_fun) {
	
	using namespace boost::numeric::odeint;
	
	vec_boost x(n);
	for(int i = 0; i < n; ++i)
	x[i] = x0[i];
	
	//TODO: Could we make it use a different error report method??
	try {
		size_t nsteps = integrate_adaptive(
			make_controlled<rosenbrock4<double>>(abs_err, rel_err),
			std::make_pair(
				Boost_ODE_System(/**/), 
				Boost_ODE_System_Jacobi(/**/)
			),
			x, 0.0, 1.0, *try_h
		);
		
	} catch(const no_progress_error &err) {
		//TODO
	} catch(const step_adjustment_error &err) {
		//TODO
	} catch(const odeint_error &err) {
		//TODO
	}
	
	for(int i = 0; i < n; ++i)
		x0[i] = x[i];
	
	return true;
}