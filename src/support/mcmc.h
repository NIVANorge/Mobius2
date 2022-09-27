


enum MCMC_Sampler {
	affine_stretch,
	affine_walk,
	differential_evolution,
	metropolis_hastings,
};

bool run_mcmc(MCMC_Sampler method, double *sampler_params, double *scales, double (*log_likelihood)(void *, int, int), void *ll_state, MC_Data &data, bool (*callback)(void *, int), void *callback_state, int initial_step);
