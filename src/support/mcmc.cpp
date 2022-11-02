
#include "mcmc.h"

#include <thread>
#include <random>
#include <mutex>

/*
//NOTE: Using Box-Muller to draw independent normally distributed variables since u++ doesn't have a random normal distribution, and we don't
	    //want to use <random> since it is not thread safe.
void DrawIndependentStandardNormals(double *NormalsOut, int Size)
{
	double Z2;
	for(int Idx = 0; Idx < Size; ++Idx)
	{
		if(Idx % 2 == 0)
		{
			double U1 = Randomf();
			double U2 = Randomf();
			double A = std::sqrt(-2.0*std::log(U1));
			double Z1 = A*std::cos(2.0*M_PI*U2);
			       Z2 = A*std::sin(2.0*M_PI*U2);
			       
			NormalsOut[Idx] = Z1;
		}
		else
			NormalsOut[Idx] = Z2;
	}
}
*/

struct
Random_State {
	std::mt19937_64 gen;
	std::mutex      gen_mutex;
};

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
		std::uniform_int_distribution<>        disti(0, n_ensemble);
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

#define MCMC_MULTITHREAD 1

bool run_mcmc(MCMC_Sampler method, double *sampler_params, double *scales, double (*log_likelihood)(void *, int, int), void *ll_state, MC_Data &data, bool (*callback)(void *, int), void *callback_state, int callback_interval, int initial_step) {
	sampler_move move;
	switch(method) {
		case MCMC_Sampler::affine_stretch :
			move = affine_stretch_move;
			break;
		/*
		case MCMCMethod_AffineWalk :
			Move = AffineWalkMove;
			break;
		case MCMCMethod_DifferentialEvolution :
			Move = DifferentialEvolutionMove;
			break;
		case MCMCMethod_MetropolisHastings :
			Move = MetropolisMove;
			break;
			*/
		default:
			fatal_error(Mobius_Error::api_usage, "MCMC method not implemented!");
	}
	
	int n_ens1 = data.n_walkers / 2;
	int n_ens2 = data.n_walkers - n_ens1;
	
	// Compute the LLs of the initial walkers (Can be paralellized too, but probably unnecessary)
	// NOTE: This assumes that data(walker, par, 0) has been filled with an initial ensemble.
	
	// NOTE: current implementation relies on this not being threaded because it needs to
	// allocate space for the results in the main thread so that it can be freed in the main
	// thread ( threads can't delete stuff from other threads ).
	if(initial_step == 0) //NOTE: If the initial step is not 0, this is a continuation of an earlier run, and so the LL value will already have been computed.
		for(int walker = 0; walker < data.n_walkers; ++walker)
			data.score_value(walker, 0) = log_likelihood(ll_state, walker, 0);
	
	Random_State rand_state; // TODO: maybe seed the state randomly.

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

bool DifferentialEvolutionMove(double *SamplerParams, double *Scale, int Step, int Walker, int FirstEnsembleWalker, int EnsembleStep, size_t NEnsemble, mcmc_data *Data,
	double (*LogLikelyhood)(void *, int, int), void *LLFunState)
{
	//Based on
	
	//Braak, C.J.F.T.
	//A Markov Chain Monte Carlo version of the genetic algorithm Differential Evolution: easy Bayesian computing for real parameter spaces.
	//Stat Comput 16, 239–249 (2006). https://doi.org/10.1007/s11222-006-8769-1

	
	double C = SamplerParams[0];
	double B = SamplerParams[1];
	double CR = SamplerParams[2];
	
	if(C < 0.0) C = 2.38 / std::sqrt(2.0 * (double)Data->NPars); // Default.
	
	bool Accepted = true;
	
	double PrevLL = Data->LLValue(Walker, Step-1);
	
	int EnsW1 = (int)Random(NEnsemble) + FirstEnsembleWalker;
	int EnsW2;
	do
		EnsW2 = (int)Random(NEnsemble) + FirstEnsembleWalker;
	while(EnsW2 == EnsW1);
	
	for(int Par = 0; Par < Data->NPars; ++Par)
	{
		double Xk  = (*Data)(Walker, Par, Step-1);
		
		double Cross = Randomf();
		if(Cross <= CR)
		{
			double BS = B*Scale[Par];  //Scale relative to |max - min| for the parameter.
			double BB = -BS + Randomf()*2.0*BS; // Uniform [-BS, BS]
			
			double Xr1 = (*Data)(EnsW1, Par, EnsembleStep);
			double Xr2 = (*Data)(EnsW2, Par, EnsembleStep);
			
			(*Data)(Walker, Par, Step) = Xk + C*(Xr1 - Xr2) + BB;
		}
		else
			(*Data)(Walker, Par, Step) = Xk;
	}
	
	double LL = LogLikelyhood(LLFunState, Walker, Step);
	double R = Randomf();
	double Q = LL - PrevLL;
	
	if(Q < std::log(R))  // Reject
	{
		for(int Par = 0; Par < Data->NPars; ++Par)
			(*Data)(Walker, Par, Step) = (*Data)(Walker, Par, Step-1);
			
		LL = PrevLL;
		Accepted = false;
	}
	Data->LLValue(Walker, Step) = LL;
	
	return Accepted;
}

bool MetropolisMove(double *SamplerParams, double *Scale, int Step, int Walker, int FirstEnsembleWalker, int EnsembleStep, size_t NEnsemble, mcmc_data *Data,
	double (*LogLikelyhood)(void *, int, int), void *LLFunState)
{
	// Metropolis-Hastings (single chain).
	
	double B = SamplerParams[0];   // Scale of normal perturbation.
	
	double *Perturb = (double *)malloc(sizeof(double)*Data->NPars);
	DrawIndependentStandardNormals(Perturb, Data->NPars);
	
	bool Accepted = true;
	double PrevLL = Data->LLValue(Walker, Step-1);
	
	for(int Par = 0; Par < Data->NPars; ++Par)
	{
		double Xk  = (*Data)(Walker, Par, Step-1);
		double P = B*Perturb[Par]*Scale[Par];   // Scale[Par] is a scaling relative to the par |max - min|
		(*Data)(Walker, Par, Step) = Xk + P;
	}
	
	double LL = LogLikelyhood(LLFunState, Walker, Step);
	double R = Randomf();
	double Q = LL - PrevLL;
	if(Q < std::log(R))  // Reject
	{
		for(int Par = 0; Par < Data->NPars; ++Par)
			(*Data)(Walker, Par, Step) = (*Data)(Walker, Par, Step-1);
			
		LL = PrevLL;
		Accepted = false;
	}
	Data->LLValue(Walker, Step) = LL;
	
	free(Perturb);
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


#define MCMC_MULTITHREAD 1

bool RunMCMC(mcmc_sampler_method Method, double *SamplerParams, double *Scale, double (*LogLikelyhood)(void *, int, int), void *LLFunState, mcmc_data *Data, bool (*Callback)(void *, int), void *CallbackState, int CallbackInterval, int InitialStep)
{
	SamplerMove Move;
	switch(Method)
	{
		case MCMCMethod_AffineStretch :
			Move = AffineStretchMove;
			break;
		case MCMCMethod_AffineWalk :
			Move = AffineWalkMove;
			break;
		case MCMCMethod_DifferentialEvolution :
			Move = DifferentialEvolutionMove;
			break;
		case MCMCMethod_MetropolisHastings :
			Move = MetropolisMove;
			break;
		default:
			assert(!"MCMC method not implemented!");
	}
	
	size_t NEns1 = Data->NWalkers / 2;
	size_t NEns2 = NEns1;
	if(Data->NWalkers % 2 != 0) NEns2++;
	
	// Compute the LLs of the initial walkers (Can be paralellized too, but probably unnecessary)
	// NOTE: This assumes that Data(Walker, Par, 0) has been filled with an initial ensemble.
	if(InitialStep == 0) //NOTE: If the initial step is not 0, this is a continuation of an earlier run, and so the LL value will already have been computed.
		for(int Walker = 0; Walker < Data->NWalkers; ++Walker)
			Data->LLValue(Walker, 0) = LogLikelyhood(LLFunState, Walker, 0);

	Array<AsyncWork<int>> Workers;
	Workers.InsertN(0, NEns2);
	
	for(int Step = InitialStep+1; Step < Data->NSteps; ++Step)
	{
		//First complementary ensemble is ensemble2 from previous step
		int FirstEnsembleWalker = NEns1;
		int EnsembleStep        = Step-1;
		
#if MCMC_MULTITHREAD
		for(int Walker = 0; Walker < NEns1; ++Walker)
			Workers[Walker].Do([=]()->int {return (int)Move(SamplerParams, Scale, Step, Walker, FirstEnsembleWalker, EnsembleStep, NEns2, Data, LogLikelyhood, LLFunState);});
		
		for(int Walker = 0; Walker < NEns1; ++Walker)
			Data->NAccepted += Workers[Walker].Get();
			
#else
		for(int Walker = 0; Walker < NEns1; ++Walker)
			Data->NAccepted += (int)Move(SamplerParams, Scale, Step, Walker, FirstEnsembleWalker, EnsembleStep, NEns2, Data, LogLikelyhood, LLFunState);
#endif
		
		
		//Second complementary ensemble is ensemble1 from this step
		FirstEnsembleWalker = 0;
		EnsembleStep        = Step;

#if MCMC_MULTITHREAD
		for(int Walker = NEns1; Walker < Data->NWalkers; ++Walker)
			Workers[Walker-NEns1].Do([=]()->int {return (int)Move(SamplerParams, Scale, Step, Walker, FirstEnsembleWalker, EnsembleStep, NEns1, Data, LogLikelyhood, LLFunState); });
			
		for(int Walker = NEns1; Walker < Data->NWalkers; ++Walker)
			Data->NAccepted += Workers[Walker-NEns1].Get();
	
#else
		for(int Walker = NEns1; Walker < Data->NWalkers; ++Walker)
			Data->NAccepted += (int)Move(SamplerParams, Scale, Step, Walker, FirstEnsembleWalker, EnsembleStep, NEns1, Data, LogLikelyhood, LLFunState);
#endif
		
		bool Halt = false;
		if(((Step-InitialStep) % CallbackInterval == 0) || (Step==Data->NSteps-1)) Halt = !Callback(CallbackState, Step);
		
		if(Halt) return false;
	}
	
	return true;
}

*/