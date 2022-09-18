
#include <algorithm>
#include <numeric>
#include "statistics.h"

void
compute_time_series_stats(Time_Series_Stats *stats, Statistics_Settings *settings, Structured_Storage<double, Var_Id> *data, s64 offset, s64 ts_begin, s64 len) {
	
	constexpr double nan = std::numeric_limits<double>::quiet_NaN();
	double sum = 0.0;
	double sum_abs_diff = 0.0;
	s64 finite_count = 0;
	double ef = settings->eckhardt_filter_param;
	
	std::vector<double> sorted_data;
	sorted_data.reserve(len);
	
	double prev = nan;
	double prev_bf = 0.0;
	double sum_bf  = 0.0;
	//double bfimax = 0.8;
	
	stats->initial_value = *data->get_value(offset, -1);
	
	// TODO: should use more numerically stable summation methods.
	for(s64 idx = 0; idx < len; ++idx) {
		double bf = 0.0;
		double val = *data->get_value(offset, ts_begin+idx);
		if(std::isfinite(val)) {
			sorted_data.push_back(val);
			sum += val;
			if(std::isfinite(prev)) {
				sum_abs_diff += std::abs(val - prev);
			
				bf = std::min(val, ef*prev_bf + 0.5*(1.0-ef)*(prev + val));
				//NOTE: These don't seem to work...
				//BF = (1.0/(2.0-a))*(PrevBF + (1.0-a)*Val);
				//BF = ((1.0-bfimax)*a*PrevBF + (1.0-a)*bfimax*Val)/(1.0-a*bfimax);
				
				sum_bf += bf;
			}
			++finite_count;
		}
		prev = val;
		prev_bf  = bf;
	}
	
	std::sort(sorted_data.begin(), sorted_data.end());
	
	double variance;
	double mean;
	if(finite_count > 0) {
		mean = sum / (double)finite_count;
		
		// TODO: summation method.
		for(double val : sorted_data) {
			double dev = mean - val;
			variance += dev*dev;
		}
		variance /= (double)finite_count;
	} else {
		mean = nan;
		variance = nan;
	}
	
	stats->percentiles.resize(settings->percentiles.size());
	for(int idx = 0; idx < settings->percentiles.size(); ++idx) {
		if(finite_count > 0)
			stats->percentiles[idx] = quantile_of_sorted(sorted_data.data(), sorted_data.size(), settings->percentiles[idx]*0.01);
		else
			stats->percentiles[idx] = nan;
	}
	
	if(finite_count > 0) {
		stats->min = sorted_data[0];
		stats->max = sorted_data[finite_count - 1];
		stats->median = median_of_sorted(sorted_data.data(), sorted_data.size());
	} else {
		stats->min = nan;
		stats->max = nan;
		stats->median = nan;
	}
	stats->sum = sum;
	stats->mean = mean;
	stats->variance = variance;
	stats->standard_dev = std::sqrt(variance);
	stats->flashiness = sum_abs_diff / sum;
	stats->est_bfi = sum_bf / sum;
	stats->data_points = finite_count;
	stats->initialized = true;
}

void
compute_residual_stats(Residual_Stats *stats, Structured_Storage<double, Var_Id> *data_sim, s64 offset_sim, s64 ts_begin_sim, Structured_Storage<double, Var_Id> *data_obs, s64 offset_obs, s64 ts_begin_obs, s64 len, bool compute_rcc) {
	double sum = 0.0;
	double sum_abs = 0.0;
	double sum_sq = 0.0;
	s64 finite_count = 0;
	
	double sum_sim = 0.0;
	double sum_obs = 0.0;
	
	double sum_log_obs = 0.0;
	double sum_log_sq = 0.0;
	
	double min = std::numeric_limits<double>::max();
	double max = std::numeric_limits<double>::min();
	
	std::vector<double> finite_obs(0);
	std::vector<double> finite_sim(0);
	if(compute_rcc) {
		finite_obs.reserve(len);
		finite_sim.reserve(len);
	}
	
	// TODO: numerically stable summation methods!
	for(s64 idx = 0; idx < len; ++idx) {
		double obs = *data_obs->get_value(offset_obs, ts_begin_obs + idx);
		double sim = *data_sim->get_value(offset_sim, ts_begin_sim + idx);
		if(std::isfinite(obs) && std::isfinite(sim)) {
			double val = obs - sim;
			
			sum += val;
			sum_abs += std::abs(val);
			sum_sq += val*val;
			
			min = std::min(min, val);
			max = std::max(max, val);
			
			sum_obs += obs;
			sum_sim += sim;
			
			sum_log_obs += std::log(obs);
			double log_res = std::log(obs) - std::log(sim);
			sum_log_sq += log_res*log_res;
			
			++finite_count;
			
			if(compute_rcc) {
				finite_obs.push_back(obs);
				finite_sim.push_back(sim);
			}
		}
	}
	
	stats->data_points = finite_count;
	stats->initialized = true;
	
	constexpr double nan = std::numeric_limits<double>::quiet_NaN();
	if(finite_count == 0) {
		stats->min_error   = nan;
		stats->max_error   = nan;
		stats->mean_error  = nan;
		stats->mae         = nan;
		stats->rmse        = nan;
		stats->ns          = nan;
		stats->log_ns      = nan;
		stats->r2          = nan;
		stats->idx_agr     = nan;
		stats->kge         = nan;
		stats->srcc        = nan;
		return;
	}
	
	//NOTE: We can not just reuse these from timeseries_stats computation, because here we can only count values where *both* obs and sim are not NaN!!
	double mean_obs = sum_obs / (double)finite_count;
	double mean_sim = sum_sim / (double)finite_count;
	
	double mean_log_obs = sum_log_obs / (double)finite_count;
	
	double ss_obs = 0.0;
	double ss_sim = 0.0;
	double cov = 0.0;
	double ss_log_obs = 0.0;
	
	double agr_denom = 0.0;
	
	for(s64 idx = 0; idx < len; ++idx) {
		double obs = *data_obs->get_value(offset_obs, ts_begin_obs + idx);
		double sim = *data_sim->get_value(offset_sim, ts_begin_sim + idx);
		if(std::isfinite(obs) && std::isfinite(sim)) {
			double val = obs - sim;
			
			ss_obs += (obs - mean_obs)*(obs - mean_obs);
			ss_sim += (sim - mean_sim)*(sim - mean_sim);
			cov += (obs - mean_obs)*(sim - mean_sim);
			ss_log_obs += (std::log(obs) - mean_log_obs)*(std::log(obs) - mean_log_obs);
			
			double agr = std::abs(sim - mean_sim) + std::abs(obs - mean_obs);
			agr_denom += agr*agr;
		}
	}
	cov /= (double)finite_count;
	
	double std_obs = std::sqrt(ss_obs/(double)finite_count);
	double std_sim = std::sqrt(ss_sim/(double)finite_count);
	double cvar_obs = std_obs/mean_obs;
	double cvar_sim = std_sim/mean_sim;
	double beta = mean_sim / mean_obs;
	double delta = cvar_sim / cvar_obs;
	double rr = cov / (std_obs * std_sim);
	
	stats->min_error  = min;
	stats->max_error  = max;
	stats->mean_error = sum / (double)finite_count;
	stats->mae        = sum_abs / (double)finite_count;
	stats->rmse       = std::sqrt(sum_sq / (double)finite_count);
	stats->ns         = 1.0 - sum_sq / ss_obs;
	stats->log_ns     = 1.0 - sum_log_sq / ss_log_obs;
	stats->r2         = rr*rr;
	stats->idx_agr    = sum_sq / agr_denom;
	stats->kge        = 1.0 - std::sqrt( (rr-1.0)*(rr-1.0) + (beta-1.0)*(beta-1.0) + (delta-1.0)*(delta-1.0));
	
	stats->srcc       = nan;
	if(compute_rcc) {
		//Determining ranks in order to compute Spearman's rank correlation coefficient
		std::vector<s64> order_obs(finite_count);
		std::vector<s64> order_sim(finite_count);
		std::iota(order_obs.begin(), order_obs.end(), 0);
		std::iota(order_sim.begin(), order_sim.end(), 0);
		
		std::sort(order_obs.begin(), order_obs.end(),
			[&finite_obs](s64 i1, s64 i2) {return finite_obs[i1] < finite_obs[i2];});
		
		std::sort(order_sim.begin(), order_sim.end(),
			[&finite_sim](s64 i1, s64 i2) {return finite_sim[i1] < finite_sim[i2];});
		
		std::vector<s64> rank_obs(finite_count);
		std::vector<s64> rank_sim(finite_count);
		
		for(s64 idx = 0; idx < finite_count; ++idx) {
			rank_obs[order_obs[idx]] = idx + 1;
			rank_sim[order_sim[idx]] = idx + 1;
		}
		
		double ss_rank_diff = 0.0;
		for(s64 idx = 0; idx < finite_count; ++idx) {
			s64 rank_diff = rank_obs[idx] - rank_sim[idx];
			ss_rank_diff += (double)(rank_diff*rank_diff);
		}
		
		double fc = (double)finite_count;
		stats->srcc = 1.0 - 6.0 * ss_rank_diff / (fc * (fc*fc - 1.0));
	}
}
