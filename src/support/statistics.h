
#ifndef MOBIUS_STATISTICS_H
#define MOBIUS_STATISTICS_H

#include "../model_application.h"
#include "../common_types.h"

#include <random>

struct
Time_Series_Stats {
	#define SET_STATISTIC(handle, name) double handle;
	#include "stat_types.incl"
	#undef SET_STATISTIC
	
	std::vector<double> percentiles;
	double initial_value;
	s64 data_points;
	bool initialized = false;
};

struct
Residual_Stats {
	#define SET_RESIDUAL(handle, name, type) double handle;
	#include "residual_types.incl"
	#undef SET_RESIDUAL
	
	double min_error;
	double max_error;
	s64 data_points;
	bool initialized = false;
};

enum class
Stat_Type {
	offset = 0,
	#define SET_STATISTIC(handle, name) handle,
	#include "stat_types.incl"
	#undef SET_STATISTIC
	end,
};

enum class
Residual_Type {
	offset = (int)Stat_Type::end + 1,
	#define SET_RESIDUAL(handle, name, type) handle,
	#include "residual_types.incl"
	#undef SET_RESIDUAL
	end,
};

enum class
LL_Type {
	offset = (int)Residual_Type::end + 1,
	#define SET_LOG_LIKELIHOOD(handle, name, type) handle,
	#include "log_likelihood_types.incl"
	#undef SET_LOG_LIKELIHOOD
	end,
};

enum class
Stat_Class {
	stat = 0,
	residual = 1,
	log_likelihood = 2,  //Not yet used..
	none,
};

inline double
get_stat(Time_Series_Stats *stats, Stat_Type type) {
	// NOTE: would like this to work, but it doesn't:
	//return *(((double *)stats) + (int)type - (int)Stat_Type::offset - 1);
	switch(type) {
		#define SET_STATISTIC(handle, name) case Stat_Type::handle : { return stats->handle; } break;
		#include "stat_types.incl"
		#undef SET_STATISTIC
	}
	return std::numeric_limits<double>::quiet_NaN();
}

inline double
get_stat(Residual_Stats *stats, Residual_Type type) {
	switch(type) {
		#define SET_RESIDUAL(handle, name, type) case Residual_Type::handle : { return stats->handle; } break;
		#include "residual_types.incl"
		#undef SET_RESIDUAL
	}
	return std::numeric_limits<double>::quiet_NaN();
}

inline Stat_Class
is_stat_class(int type) {
	if(type > (int)Stat_Type::offset && type < (int)Stat_Type::end)              return Stat_Class::stat;
	else if(type > (int)Residual_Type::offset && type < (int)Residual_Type::end) return Stat_Class::residual;
	else if(type > (int)LL_Type::offset && type < (int)LL_Type::end)             return Stat_Class::log_likelihood;
	fatal_error(Mobius_Error::api_usage, "Unidentified stat type in is_type() (statistics.h)");
	return Stat_Class::none;
}

inline String_View
stat_name(int type) {
	#define SET_STATISTIC(handle, name) if(type == (int)Stat_Type::handle) return name;
	#include "stat_types.incl"
	#undef SET_STATISTIC
	#define SET_RESIDUAL(handle, name, type) if(type == (int)Residual_Type::handle) return name;
	return "";
}

inline int
is_positive_good(Residual_Type res_type) {
	switch(res_type) {
		#define SET_RESIDUAL(handle, name, type) case Residual_Type::handle : { return type; } break;
		#include "residual_types.incl"
		#undef SET_RESIDUAL
	}
	return -1;
}

inline int
err_par_count(LL_Type ll_type) {
	switch(ll_type) {
		#define SET_LOG_LIKELIHOOD(handle, name, count) case LL_Type::handle : { return count; } break;
		#include "log_likelihood_types.incl"
		#undef SET_LOG_LIKELIHOOD
	}
	return -1;
}

inline double
get_stat(Time_Series_Stats *stats, Residual_Stats *residual_stats, int type) {
	Stat_Class typetype = is_stat_class(type);
	if(typetype == Stat_Class::stat)
		return get_stat(stats, (Stat_Type)type);
	else if(typetype == Stat_Class::residual)
		return get_stat(residual_stats, (Residual_Type)type);
	return std::numeric_limits<double>::quiet_NaN();
}

struct
Statistics_Settings {
	std::vector<double> percentiles = {2.5, 5.0, 15.0, 25.0, 50.0, 75.0, 85.0, 95.0, 97.5};
	double eckhardt_filter_param = 0.925;
};

inline double
median_of_sorted(double *data, s64 size) {
	size_t idx = size / 2;
	if(size % 2 == 0)
		return 0.5*(data[idx] + data[idx+1]);
	else
		return data[idx];
}

inline double
quantile_of_sorted(double *data, s64 size, double q) {
	//TODO: Should we make this interpolate to be more in line with median?
	s64 idx = std::ceil(q * (double)(size - 1));
	return data[idx];
}


void
compute_time_series_stats(Time_Series_Stats *stats, Statistics_Settings *settings, Data_Storage<double, Var_Id> *data, s64 offset, s64 ts_begin, s64 len);

void
compute_residual_stats(Residual_Stats *stats, Data_Storage<double, Var_Id> *data_sim, s64 offset_sim, s64 ts_begin_sim, Data_Storage<double, Var_Id> *data_obs, s64 offset_obs, s64 ts_begin_obs, s64 len, bool compute_rcc);

// MCMC stuff:

double
compute_ll(Data_Storage<double, Var_Id> *data_sim, s64 offset_sim, s64 ts_begin_sim, Data_Storage<double, Var_Id> *data_obs, s64 offset_obs, s64 ts_begin_obs, s64 len, double *err_param, LL_Type ll_type);

void
add_random_error(double* series, s64 time_steps, double *err_param, LL_Type ll_type, std::mt19937_64 &gen);

void
compute_standard_residuals(double *obs, double *sim, s64 time_steps, double *err_param, LL_Type ll_type, std::vector<double> &resid_out);

#endif // MOBIUS_STATISTICS_H