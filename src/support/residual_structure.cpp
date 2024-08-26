
#include "statistics.h"


inline double
log_pdf_normal(double x, double mu, double sigma_squared) {
	constexpr double log_2_pi = 1.83787706641;
	double factor = (x - mu);
	return 0.5*(-std::log(sigma_squared) - log_2_pi - factor*factor/sigma_squared);
}

double
compute_ll(Data_Storage<double, Var_Id> *data_sim, s64 offset_sim, s64 ts_begin_sim, Data_Storage<double, Var_Id> *data_obs, s64 offset_obs, s64 ts_begin_obs, s64 len, double *err_param, LL_Type ll_type) {
	double result = 0.0;
	double prev_eta = std::numeric_limits<double>::infinity();
	
	for(s64 idx = 0; idx < len; ++idx) {
		double obs = *data_obs->get_value(offset_obs, ts_begin_obs + idx);
		double sim = *data_sim->get_value(offset_sim, ts_begin_sim + idx);
		
		if(!std::isfinite(sim))
			return -std::numeric_limits<double>::infinity();
		
		if(!std::isfinite(obs))
			continue;
		
		// TODO: numerically sound summation
		switch(ll_type) {
			case LL_Type::normal : {
				double std_dev = err_param[0];
				result += log_pdf_normal(obs, sim, std_dev*std_dev);
			} break;
			
			case LL_Type::normal_h : {
				double std_dev = err_param[0] * sim;
				result += log_pdf_normal(obs, sim, std_dev*std_dev);
			} break;
			
			case LL_Type::wls : {
				double std_dev = err_param[0] + err_param[1]*sim;
				result += log_pdf_normal(obs, sim, std_dev*std_dev);
			} break;
			
			case LL_Type::wlsar1 : {
				double std_dev = err_param[0] + err_param[1]*sim;
				double eta = (obs - sim) / std_dev;
				if(!std::isfinite(prev_eta))
					result += -std::log(std_dev) + log_pdf_normal(eta, 0.0, 1.0 - err_param[2]*err_param[2]);
				else {
					double y = eta - prev_eta*err_param[2];
					result += -std::log(std_dev) + log_pdf_normal(y, 0.0, 1.0);
				}
				prev_eta = eta;
			} break;
			
			default :
				fatal_error(Mobius_Error::internal, "Error structure not implemented");
		}
	}
	return result;
}

void
add_random_error(double* series, s64 time_steps, double *err_param, LL_Type ll_type, std::mt19937_64 &gen) {
	double prev_eta = std::numeric_limits<double>::infinity();
	
	// TODO: could also just keep around a std::normal_distribution<double> distr(0.0, 1.0) and then scale the results of it...
	
	for(int ts = 0; ts < time_steps; ++ts) {
		switch(ll_type) {
			case LL_Type::normal : {
				double std_dev = err_param[0];
				std::normal_distribution<double> distr(series[ts], std_dev);
				series[ts] = distr(gen);
			} break;
			
			case LL_Type::normal_h : {
				double std_dev = err_param[0]*series[ts];
				std::normal_distribution<double> distr(series[ts], std_dev);
				series[ts] = distr(gen);
			} break;
			
			case LL_Type::wls : {
				double std_dev = err_param[0] + err_param[1]*series[ts];
				std::normal_distribution<double> distr(series[ts], std_dev);
				series[ts] = distr(gen);
			} break;
			
			case LL_Type::wlsar1 : {
				std::normal_distribution<double> distr(0.0, 1.0);
				double y = distr(gen);
				double std_dev = err_param[0] + err_param[1]*series[ts];
				double eta;
				if(!std::isfinite(prev_eta))
					eta = y;
				else
					eta = err_param[2]*prev_eta + y;
			
				series[ts] += std_dev*eta;
				prev_eta = eta;
			} break;
			
			default :
				fatal_error(Mobius_Error::internal, "Error structure not implemented");
		}
	}
}

void
compute_standard_residuals(double *obs, double *sim, s64 time_steps, double *err_param, LL_Type ll_type, std::vector<double> &resid_out) {
	double prev_eta = std::numeric_limits<double>::infinity();
	
	for(s64 ts = 0; ts < time_steps; ++ts) {
		if(!std::isfinite(obs[ts]) || !std::isfinite(sim[ts])) continue;
		
		double resid;
		switch(ll_type) {
			case LL_Type::normal : {
				double std_dev = err_param[0];
				resid = (obs[ts] - sim[ts])/std_dev;
			} break;
			
			case LL_Type::normal_h : {
				double std_dev = err_param[0]*sim[ts];
				resid = (obs[ts] - sim[ts])/std_dev;
			} break;
			
			case LL_Type::wls : {
				double std_dev = err_param[0] + err_param[1]*sim[ts];
				resid = (obs[ts] - sim[ts]) / std_dev;
			} break;
			
			case LL_Type::wlsar1 : {
				double std_dev = err_param[0] + err_param[1]*sim[ts];
				double eta    = (obs[ts] - sim[ts]) / std_dev;
				if(!std::isfinite(prev_eta))
					resid = eta;
				else
					resid = eta - err_param[2]*prev_eta;   //NOTE: The standardized residual is then Y, which is supposed to be normally (independently) distributed.
				prev_eta = eta;
			} break;
			
			default :
				fatal_error(Mobius_Error::internal, "Error structure not implemented");
		}
		resid_out.push_back(resid);
	}
}
