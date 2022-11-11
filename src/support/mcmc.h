
#include <mutex>
#include <random>
#include "monte_carlo.h"

enum class
MCMC_Sampler {
	affine_stretch,
	affine_walk,
	differential_evolution,
	metropolis_hastings,
};

struct
Random_State {
	std::mt19937_64 gen;
	std::mutex      gen_mutex;
};

bool run_mcmc(MCMC_Sampler method, double *sampler_params, double *scales, double (*log_likelihood)(void *, int, int), void *ll_state,
		MC_Data &data, bool (*callback)(void *, int), void *callback_state, int callback_interval, int initial_step);
