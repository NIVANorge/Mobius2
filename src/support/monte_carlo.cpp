
#include <algorithm>
#include "monte_carlo.h"

void
MC_Data::allocate(s64 n_walkers, s64 n_pars, s64 n_steps) {
	free_data();
	
	par_data = (double *)malloc(sizeof(double)*n_steps*n_walkers*n_pars);
	ll_data  = (double *)malloc(sizeof(double)*n_steps*n_walkers);
	this->n_steps   = n_steps;
	this->n_walkers = n_walkers;
	this->n_pars    = n_pars;
	
	for(int idx = 0; idx < n_steps*n_walkers*n_pars; ++idx) par_data[idx] = std::numeric_limits<double>::quiet_NaN();
	for(int idx = 0; idx < n_steps*n_walkers; ++idx)        ll_data[idx]  = std::numeric_limits<double>::quiet_NaN();
	
	n_accepted = 0;
}

void
MC_Data::extend_steps(s64 n_steps_new) {
	if(!par_data || n_steps_new <= n_steps) return;
	
	double *par_data_new = (double *)malloc(sizeof(double)*n_steps_new*n_walkers*n_pars);
	double *ll_data_new  = (double *)malloc(sizeof(double)*n_steps_new*n_walkers);
	
	for(int walker = 0; walker < n_walkers; ++walker) {
		for(int par = 0; par < n_pars; ++par) {
			for(int step = 0; step < n_steps_new; ++step) {
				if(step < n_steps)
					par_data_new[step + n_steps_new*(par + n_pars*walker)] = (*this)(walker, par, step);
				else
					par_data_new[step + n_steps_new*(par + n_pars*walker)] = std::numeric_limits<double>::quiet_NaN();
			}
		}
		
		for(int step = 0; step < n_steps_new; ++step) {
			if(step < n_steps)
				ll_data_new[walker*n_steps_new + step] = score_value(walker, step);
			else
				ll_data_new[walker*n_steps_new + step] = std::numeric_limits<double>::quiet_NaN();
		}
	}
	n_steps = n_steps_new;
	free(par_data);
	free(ll_data);
	par_data = par_data_new;
	ll_data  = ll_data_new;
}

void
MC_Data::get_map_index(int burnin, int cur_step, int &best_walker, int &best_step) {
	//NOTE: Doesn't do error handling, all bounds have to be checked externally
	
	double best = -std::numeric_limits<double>::infinity();
	for(int step = burnin; step <= cur_step; ++step) {
		for(int walker = 0; walker < n_walkers; ++walker) {
			double val = score_value(walker, step);
			if(val > best) {
				best = val;
				best_walker = walker;
				best_step = step;
			}
		}
	}
}

s64
MC_Data::flatten(s64 burnin, s64 &up_to_step, std::vector<std::vector<double>> &flattened_out, bool sort) {
	if(up_to_step < 0) up_to_step = n_steps - 1;
	s64 out_steps = up_to_step - burnin;
	if(out_steps <= 0) return 0;
	flattened_out.resize(n_pars);
			
	s64 n_vals = out_steps * n_walkers;
	
	for(int par = 0; par < n_pars; ++par) {
		flattened_out[par].resize(n_vals);
		
		for(int walker = 0; walker < n_walkers; ++walker) {
			for(int step = burnin; step < up_to_step; ++step)
				flattened_out[par][walker*out_steps + step - burnin] = (*this)(walker, par, step);
		}
		
		if(sort)
			std::sort(flattened_out[par].begin(), flattened_out[par].end());
	}
	return n_vals;
}
