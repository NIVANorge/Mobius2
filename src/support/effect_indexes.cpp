

#include "monte_carlo.h"

#include <thread>

void
compute_effect_indexes(int n_samples, int n_pars, int n_workers, int sample_method, double *min_bound, double *max_bound, double (*target_fun)(void *, int, const std::vector<double> &pars), void *target_state, bool (*callback)(void *, int, int, double, double), void *callback_state, int callback_interval, std::vector<double> &samples_out) {
	
	
	// TODO: look into the following algorithms: I. Azzini, T. Mara, R. Rosati, Comparison of two sets of Monte Carlo estimators of Sobol’ indices. Environ. Model. Software 144, 105167 (2021).
	//   See also https://www.science.org/doi/10.1126/sciadv.abn9450#body-ref-R66
	
	MC_Data mat_A;
	MC_Data mat_B;
	
	// NOTE: we allocate as if there were only one worker. We don't care about storing separate chains.
	mat_A.allocate(1, n_pars, n_samples);
	mat_B.allocate(1, n_pars, n_samples);
	
	std::mt19937 gen;
	
	if(sample_method == 0) {
		mat_A.draw_latin_hypercube(min_bound, max_bound, gen);
		mat_B.draw_latin_hypercube(min_bound, max_bound, gen);
	} else {
		mat_A.draw_uniform(min_bound, max_bound, gen);
		mat_B.draw_uniform(min_bound, max_bound, gen);
	}
	
	std::vector<std::thread> workers;
	
	int total_samples = 0;
	
	for(int sample_group = 0; sample_group < n_samples/n_workers+1; ++sample_group) {
		for(int worker = 0; worker < n_workers; ++worker) {
			int sample = sample_group*n_workers + worker;
			if(sample >= n_samples) break;
			
			workers.push_back(std::thread([=, &mat_A, &mat_B]() {
				std::vector<double> pars(n_pars);
				for(int par = 0; par < n_pars; ++par)
					pars[par] = mat_A(0, par, sample);
				mat_A.score_value(0, sample) = target_fun(target_state, worker, pars);
				
				for(int par = 0; par < n_pars; ++par)
					pars[par] = mat_B(0, par, sample);
				mat_B.score_value(0, sample) = target_fun(target_state, worker, pars);
			}));
		}
		for(auto &worker : workers)
			if(worker.joinable()) worker.join();
		workers.clear();
		
		total_samples += 2*n_workers;
		
		if(total_samples % callback_interval == 0)
			callback(callback_state, 0, total_samples, 0.0, 0.0);
	}
	
	double m_A = 0.0;
	double v_A = 0.0;
	for(int j = 0; j < n_samples*2; ++j) {
		double f = j < n_samples ? mat_A.score_value(0, j) : mat_B.score_value(0, j-n_samples);
		m_A += f;
	}
	m_A /= (double)(n_samples*2);
	for(int j = 0; j < n_samples*2; ++j) {
		double f = j < n_samples ? mat_A.score_value(0, j) : mat_B.score_value(0, j-n_samples);
		v_A += (f-m_A)*(f-m_A);
	}
	v_A /= (double)(n_samples*2);
	
	for(int i = 0; i < n_pars; ++i) {
		std::vector<double> f_ABi(n_samples);
		
		for(int sample_group = 0; sample_group < n_samples/n_workers+1; ++sample_group) {
			for(int worker = 0; worker < n_workers; ++worker) {
				int sample = sample_group*n_workers + worker;
				if(sample >= n_samples) break;
				
				workers.push_back(std::thread([=, &mat_A, &mat_B, &f_ABi]() {
					std::vector<double> pars(n_pars);
					for(int ii = 0; ii < n_pars; ++ii) {
						if(ii == i) pars[ii] = mat_B(0, ii, sample);
						else        pars[ii] = mat_A(0, ii, sample);
					}
					f_ABi[sample] = target_fun(target_state, worker, pars);
				}));
			}
			for(auto &worker : workers)
				if(worker.joinable()) worker.join();
			workers.clear();
			
			total_samples += n_workers;
		
			if(total_samples % callback_interval == 0)
				callback(callback_state, 0, total_samples, 0.0, 0.0);
		}
		double main_ei  = 0.0;
		double total_ei = 0.0;
		for(int j = 0; j < n_samples; ++j) {
			double f_B = mat_B.score_value(0, j);
			double f_A = mat_A.score_value(0, j);
			double f_ABij = f_ABi[j];
			main_ei  += f_B*(f_ABij - f_A);
			total_ei += (f_A - f_ABij)*(f_A - f_ABij);
		}

		main_ei /= (v_A * (double)n_samples);
		total_ei /= (v_A * 2.0 * (double)n_samples);

		callback(callback_state, 1, i, main_ei, total_ei);
	}
	
	samples_out.resize(n_samples*2);
	for(int sample = 0; sample < n_samples; ++sample) {
		samples_out[sample] = mat_A.score_value(0, sample);
		samples_out[sample+n_samples] = mat_B.score_value(0, sample);
	}
	
	mat_A.free_data();
	mat_B.free_data();
}