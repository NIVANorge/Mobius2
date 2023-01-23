
#include "mcmc.h"

#include <thread>
#include <random>
//#include <execution>

typedef void (*sampler_move)(double *, double *, int, int, int, int*, int, MC_Data &, Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *);

void
affine_stretch_move(double *sampler_params, double *scale, int step, int walker, int ensemble_step, int *ensemble, int n_ensemble, MC_Data &data,
	Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *ll_state) {
	/*
	This is a simple C++ implementation of the Affine-invariant ensemble sampler from https://github.com/dfm/emcee
	
	( Foreman-Mackey, Hogg, Lang & Goodman (2012) Emcee: the MCMC Hammer )

	*/
	
	// Draw needed random values
	double u, r;
	int ensemble_walker;
	{
		std::uniform_real_distribution<double> distu(0.0, 1.0);
		std::uniform_int_distribution<>        disti(0, n_ensemble-1);
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		u = distu(rand_state->gen); //uniform between 0,1
		r = distu(rand_state->gen);
		ensemble_walker = ensemble[disti(rand_state->gen)];
	}
	
	double a = sampler_params[0];
	bool accepted = true;
	double prev_ll = data.score_value(walker, step-1);
	
	double zz = (a - 1.0)*u + 1.0;
	zz = zz*zz/a;
		
	for(int par = 0; par < data.n_pars; ++par) {
		double x_j = data(ensemble_walker, par, ensemble_step);
		double x_k = data(walker, par, step-1);
		data(walker, par, step) = x_j + zz*(x_k - x_j);
	}
	
	double ll = log_likelihood(ll_state, walker, step);
	double q = std::pow(zz, (double)data.n_pars-1.0)*std::exp(ll-prev_ll);
	
	if(!std::isfinite(ll) || r > q) { // Reject the proposed step
		// reset parameter values
		for(int par = 0; par < data.n_pars; ++par)
			data(walker, par, step) = data(walker, par, step-1);
		ll = prev_ll;
		accepted = false;
	}
	
	data.score_value(walker, step) = ll;
	
	if(accepted) {
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		data.n_accepted++;
	}
}

void
affine_walk_move(double *sampler_params, double *scale, int step, int walker, int ensemble_step, int *ensemble, int n_ensemble, MC_Data &data,
	Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *ll_state) {
		
	int s0 = (int)sampler_params[0];
	bool accepted = true;
	double prev_ll = data.score_value(walker, step-1);
	
	double r;
	// Could this be rewritten without the vectors?
	std::vector<double> z(s0);
	std::vector<int> ens(s0);
	{
		std::uniform_real_distribution<double> distu;
		std::normal_distribution<double>       distn;
		std::uniform_int_distribution<>        disti(0, n_ensemble-1);
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		
		r = distu(rand_state->gen);
		for(int s = 0; s < s0; ++s) {
			// NOTE: Unlike in Differential Evolution, the members of the sub-ensemble could be repeating (or at least it is not otherwise mentioned in the paper).
			ens[s] = ensemble[disti(rand_state->gen)];
			z[s]   = distn(rand_state->gen);
		}
	}
		
	for(int par = 0; par < data.n_pars; ++par) {
		double x_k = data(walker, par, step-1);
		double x_s_mean = 0.0;
		for(int s = 0; s < s0; ++s)
			x_s_mean += data(ens[s], par, ensemble_step);
		x_s_mean /= (double)s0;
		double w = 0.0;
		for(int s = 0; s < s0; ++s) {
			double x_j = data(ens[s], par, ensemble_step);
			w += z[s]*(x_j - x_s_mean);
		}
		w /= std::sqrt((double)s0); // NOTE: This is not specified in the paper, but without it the algorithm gives very poor results, and it seems to be more mathematically sound.
		data(walker, par, step) = x_k + w;
	}

	double ll = log_likelihood(ll_state, walker, step);
	double q = ll - prev_ll;
	
	if(!std::isfinite(ll) || std::log(r) > q) { // Reject the proposal
		for(int par = 0; par < data.n_pars; ++par)
			data(walker, par, step) = data(walker, par, step-1);
		ll = prev_ll;
		accepted = false;
	}
	
	data.score_value(walker, step) = ll;
	
	if(accepted) {
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		data.n_accepted++;
	}
}

void
differential_evolution_move(double *sampler_params, double *scale, int step, int walker, int ensemble_step, int *ensemble, int n_ensemble, MC_Data &data,
	Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *ll_state)
{
	//Based on
	
	//Braak, C.J.F.T.
	//A Markov Chain Monte Carlo version of the genetic algorithm Differential Evolution: easy Bayesian computing for real parameter spaces.
	//Stat Comput 16, 239–249 (2006). https://doi.org/10.1007/s11222-006-8769-1

	
	double c  = sampler_params[0];
	double b  = sampler_params[1];
	double cr = sampler_params[2];
	
	if(c < 0.0) c = 2.38 / std::sqrt(2.0 * (double)data.n_pars); // Default.
	
	bool accepted = true;
	double prev_ll = data.score_value(walker, step-1);
	
	double r;
	{
		std::uniform_real_distribution<double> distu(0.0, 1.0);
		std::uniform_int_distribution<>        disti(0, n_ensemble-1);
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		
		r = distu(rand_state->gen);
		
		int ens_w1 = ensemble[disti(rand_state->gen)];
		int ens_w2;
		do
			ens_w2 = ensemble[disti(rand_state->gen)];
		while(ens_w2 == ens_w1);
	
		for(int par = 0; par < data.n_pars; ++par) {
			double x_k  = data(walker, par, step-1);
			
			double cross = distu(rand_state->gen);
			if(cross <= cr) {
				double bs = b*scale[par];  // scale relative to |max - min| for the parameter.
				double bb = -bs + distu(rand_state->gen)*2.0*bs; // Uniform [-BS, BS]
				
				double x_r1 = data(ens_w1, par, ensemble_step);
				double x_r2 = data(ens_w2, par, ensemble_step);
				
				data(walker, par, step) = x_k + c*(x_r1 - x_r2) + bb;
			}
			else
				data(walker, par, step) = x_k;
		}
	}
	
	double ll = log_likelihood(ll_state, walker, step);
	double q = ll - prev_ll;
	
	if(q < std::log(r)) {  // Reject
		for(int par = 0; par < data.n_pars; ++par)
			data(walker, par, step) = data(walker, par, step-1);
		ll = prev_ll;
		accepted = false;
	}
	data.score_value(walker, step) = ll;
	
	if(accepted) {
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		data.n_accepted++;
	}
}

void
metropolis_move(double *sampler_params, double *scale, int step, int walker, int ensemble_step, int *ensemble, int n_ensemble, MC_Data &data,
	Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *ll_state)
{
	// Metropolis-Hastings (parallel chains with no crossover).
	
	double b = sampler_params[0];   // Scale of normal perturbation.
	
	bool accepted = true;
	double prev_ll = data.score_value(walker, step-1);
	
	double r;
	{
		std::uniform_real_distribution<double> distu(0.0, 1.0);
		std::normal_distribution<> distn(0.0, 1.0);
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		
		r = distu(rand_state->gen);
		for(int par = 0; par < data.n_pars; ++par) {
			double x_k = data(walker, par, step-1);
			double sigma = b*scale[par]; // scale[par] is a scaling relative to the par |max - min|
			data(walker, par, step) = x_k + sigma*distn(rand_state->gen);
		}
	}
	
	double ll = log_likelihood(ll_state, walker, step);
	double q = ll - prev_ll;
	if(q < std::log(r)) { // Reject
		for(int par = 0; par < data.n_pars; ++par)
			data(walker, par, step) = data(walker, par, step-1);
			
		ll = prev_ll;
		accepted = false;
	}
	data.score_value(walker, step) = ll;
	
	if(accepted) {
		std::lock_guard<std::mutex> lock(rand_state->gen_mutex);
		data.n_accepted++;
	}
}

bool
run_mcmc(MCMC_Sampler method, double *sampler_params, double *scales, double (*log_likelihood)(void *, int, int), void *ll_state, MC_Data &data, bool (*callback)(void *, int), void *callback_state, int callback_interval, int initial_step) {
	sampler_move move;
	switch(method) {
		case MCMC_Sampler::affine_stretch :
			move = affine_stretch_move;
			break;
		
		case MCMC_Sampler::differential_evolution :
			move = differential_evolution_move;
			break;
			
		case MCMC_Sampler::metropolis_hastings :
			move = metropolis_move;
			break;
	
		case MCMC_Sampler::affine_walk :
			move = affine_walk_move;
			break;
	
		default:
			fatal_error(Mobius_Error::api_usage, "MCMC method not implemented!");
	}
	
	int n_ens1 = data.n_walkers / 2;
	int n_ens2 = data.n_walkers - n_ens1;
	
	// Compute the LLs of the initial walkers (Can be paralellized too, but probably unnecessary)
	// This assumes that data(walker, par, 0) has been filled with an initial ensemble.
	
	// NOTE: current implementation relies on this initialization not being threaded because the first run needs to
	// allocate space for the results in the main thread so that it can be freed in the main
	// thread ( threads can't delete stuff that was allocated in other threads ).
	// This is a bit messy though. Could alternatively allocate the needed result data
	// explicitly first and then do step 0.
	if(initial_step == 0) //NOTE: If the initial step is not 0, this is a continuation of an earlier run, and so the LL value of the initial step will already have been computed.
		for(int walker = 0; walker < data.n_walkers; ++walker)
			data.score_value(walker, 0) = log_likelihood(ll_state, walker, 0);
	
	Random_State rand_state; // TODO: maybe seed the state randomly.

	// TODO: We should instead have some kind of thread pool here.
	std::vector<std::thread> workers;
	workers.reserve(data.n_walkers);
	
	std::uniform_int_distribution<> disti(0, data.n_walkers-1);
	std::vector<int> walkers(data.n_walkers);
	for(int idx = 0; idx < data.n_walkers; ++idx) walkers[idx] = idx;
	
	for(int step = initial_step + 1; step < data.n_steps; ++step) {
		
		// Shuffle the walkers so that they get into different sub-ensembles for each step.
		for(int idx = 0; idx < data.n_walkers; ++idx) {
			int swp = disti(rand_state.gen);
			std::swap(walkers[idx], walkers[swp]);
		}
		
		int ensemble_step         = step-1;
		
		for(int wk = 0; wk < n_ens1; ++wk) {
			int walker = walkers[wk];
			workers.push_back(std::thread([&, walker]() {
				move(sampler_params, scales, step, walker, ensemble_step, walkers.data()+n_ens1, n_ens2, data, &rand_state, log_likelihood, ll_state);
			}));
		}
		for(auto &worker : workers)
			if(worker.joinable()) worker.join();
		workers.clear();
		
		// NOTE: Can't use any of the parallel for_each stuff before upp supports C++20
		/*
		std::for_each(std::execution::par, walkers.begin(), walkers.begin()+n_ens1, [&](int walker) {
			move(sampler_params, scales, step, walker, ensemble_step, walkers.data()+n_ens1, n_ens2, data, &rand_state, log_likelihood, ll_state);
		});
		*/
		
		ensemble_step         = step;
		
		for(int wk = n_ens1; wk < data.n_walkers; ++wk) {
			int walker = walkers[wk];
			workers.push_back(std::thread([&, walker]() {
				move(sampler_params, scales, step, walker, ensemble_step, walkers.data(), n_ens1, data, &rand_state, log_likelihood, ll_state);
			}));
		}
		for(auto &worker : workers)
			if(worker.joinable()) worker.join();
		workers.clear();
		
		/*
		std::for_each(std::execution::par, walkers.begin()+n_ens1, walkers.end(), [&](int walker) {
			move(sampler_params, scales, step, walker, ensemble_step, walkers.data(), n_ens1, data, &rand_state, log_likelihood, ll_state);
		});
		*/
		
		bool halt = false;
		if(((step-initial_step) % callback_interval == 0) || (step == data.n_steps-1))
			halt = !callback(callback_state, step);
		
		if(halt) return false;
	}
	
	return true;
}
