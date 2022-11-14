
#include "mcmc.h"

#include <thread>
#include <random>

typedef void (*sampler_move)(double *, double *, int, int, int, int, int, MC_Data &, Random_State *rand_state, double (*log_likelihood)(void *, int, int), void *);

void
affine_stretch_move(double *sampler_params, double *scale, int step, int walker, int first_ensemble_walker, int ensemble_step, int n_ensemble, MC_Data &data,
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
		ensemble_walker = disti(rand_state->gen) + first_ensemble_walker; //NOTE: Only works for the particular way the Ensembles are ordered right now.
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
differential_evolution_move(double *sampler_params, double *scale, int step, int walker, int first_ensemble_walker, int ensemble_step, int n_ensemble, MC_Data &data,
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
		
		int ens_w1 = first_ensemble_walker + disti(rand_state->gen);
		int ens_w2;
		do
			ens_w2 = first_ensemble_walker + disti(rand_state->gen);
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
metropolis_move(double *sampler_params, double *scale, int step, int walker, int first_ensemble_walker, int ensemble_step, int n_ensemble, MC_Data &data,
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
		/*
		case MCMCMethod_AffineWalk :
			Move = AffineWalkMove;
			break;
		*/
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
	// explicitly here.
	if(initial_step == 0) //NOTE: If the initial step is not 0, this is a continuation of an earlier run, and so the LL value will already have been computed.
		for(int walker = 0; walker < data.n_walkers; ++walker)
			data.score_value(walker, 0) = log_likelihood(ll_state, walker, 0);
	
	Random_State rand_state; // TODO: maybe seed the state randomly.

	// TODO: We should instead have some kind of thread pool here.
	std::vector<std::thread> workers;
	workers.reserve(data.n_walkers);
	
	for(int step = initial_step + 1; step < data.n_steps; ++step) {
		int first_ensemble_walker = n_ens1;
		int ensemble_step         = step-1;
		
		for(int walker = 0; walker < n_ens1; ++walker) {
			workers.push_back(std::thread([&, walker]() {
				move(sampler_params, scales, step, walker, first_ensemble_walker, ensemble_step, n_ens2, data, &rand_state, log_likelihood, ll_state);
			}));
		}
		for(auto &worker : workers)
			if(worker.joinable()) worker.join();
		workers.clear();
		
		first_ensemble_walker = 0;
		ensemble_step         = step;
		
		for(int walker = n_ens1; walker < data.n_walkers; ++walker) {
			workers.push_back(std::thread([&, walker]() {
				move(sampler_params, scales, step, walker, first_ensemble_walker, ensemble_step, n_ens1, data, &rand_state, log_likelihood, ll_state);
			}));
		}
		for(auto &worker : workers)
			if(worker.joinable()) worker.join();
		workers.clear();
		
		bool halt = false;
		if(((step-initial_step) % callback_interval == 0) || (step == data.n_steps-1))
			halt = !callback(callback_state, step);
		
		if(halt) return false;
	}
	
	return true;
}

/*

bool AffineWalkMove(double *SamplerParams, double *Scale, int Step, int Walker, int FirstEnsembleWalker, int EnsembleStep, size_t NEnsemble, mcmc_data *Data,
	double (*LogLikelyhood)(void *, int, int), void *LLFunState)
{
	int S0 = (int)SamplerParams[0];
	
	bool Accepted = true;
	
	double PrevLL = Data->LLValue(Walker, Step-1);
	
	double *Z = (double *)malloc(S0*sizeof(double));
	int    *Ens = (int *)malloc(S0*sizeof(int));
	
	DrawIndependentStandardNormals(Z, S0);
	
	double Z2;
	for(int S = 0; S < S0; ++S)
		Ens[S] = (int)Random(S0); // NOTE: Unlike in Differential Evolution, the members of the sub-ensemble could be repeating (or at least I can't see it mentioned in the paper).
	
	for(int Par = 0; Par < Data->NPars; ++Par)
	{
		double Xk = (*Data)(Walker, Par, Step-1);
		double Xsmean = 0.0;
		for(int S = 0; S < S0; ++S)
		{
			int EnsembleWalker = Ens[S] + FirstEnsembleWalker;
			Xsmean += (*Data)(EnsembleWalker, Par, EnsembleStep);
		}
		Xsmean /= (double)S0;
		double W = 0.0;
		for(int S = 0; S < S0; ++S)
		{
			int EnsembleWalker = Ens[S] + FirstEnsembleWalker;
			double Xj = (*Data)(EnsembleWalker, Par, EnsembleStep);
			W += Z[S]*(Xj - Xsmean);
		}
		(*Data)(Walker, Par, Step) = Xk + W;
	}
	
	free(Z);
	free(Ens);
	
	double LL = LogLikelyhood(LLFunState, Walker, Step);
	double R = Randomf();
	
	double Q = LL-PrevLL;
	
	if(!std::isfinite(LL) || std::log(R) > Q)  // Reject the proposal
	{
		for(int Par = 0; Par < Data->NPars; ++Par)
			(*Data)(Walker, Par, Step) = (*Data)(Walker, Par, Step-1);
			
		LL = PrevLL;
		Accepted = false;
	}
	
	Data->LLValue(Walker, Step) = LL;
	return Accepted;
}


void DrawLatinHyperCubeSamples(mcmc_data *Data, double *MinBound, double *MaxBound)
{
	for(int Par = 0; Par < Data->NPars; ++Par)
	{
		double Span = (MaxBound[Par] - MinBound[Par]);
		for(int Sample = 0; Sample < Data->NSteps; ++Sample)
			(*Data)(0, Par, Sample) = MinBound[Par] + Span * (((double)Sample + Randomf()) / ((double)Data->NSteps));
		
		// Shuffle
		for(int Sample = 0; Sample < Data->NSteps; ++Sample)
		{
			int Swap = Random(Data->NSteps);
			double Temp = (*Data)(0, Par, Sample);
			(*Data)(0, Par, Sample) = (*Data)(0, Par, Swap);
			(*Data)(0, Par, Swap) = Temp;
		}
	}
}

void DrawUniformSamples(mcmc_data *Data, double *MinBound, double *MaxBound)
{
	for(int Par = 0; Par < Data->NPars; ++Par)
	{
		double Span = (MaxBound[Par] - MinBound[Par]);
		for(int Sample = 0; Sample < Data->NSteps; ++Sample)
			(*Data)(0, Par, Sample) = MinBound[Par] + Randomf()*Span;
	}
}

*/