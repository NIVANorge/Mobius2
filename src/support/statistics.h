
#ifndef MOBIUS_STATISTICS_H
#define MOBIUS_STATISTICS_H

#include "../model_application.h"
#include "../common_types.h"

struct
Time_Series_Stats {
	std::vector<double> percentiles;
	
	#define SET_STATISTIC(handle, name) double handle;
	#include "stat_types.incl"
	#undef SET_STATISTIC
	
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

struct
Statistics_Settings {
	std::vector<double> percentiles = {2.5, 5.0, 15.0, 25.0, 50.0, 75.0, 85.0, 95.0, 97.5};
	double eckhardt_filter_param = 0.925;
};

inline double median_of_sorted(double *data, s64 size) {
	size_t idx = size / 2;
	if(size % 2 == 0)
		return 0.5*(data[idx] + data[idx+1]);
	else
		return data[idx];
}

inline double quantile_of_sorted(double *data, s64 size, double q) {
	//TODO: Should we make this interpolate to be more in line with median?
	s64 idx = std::ceil(q * (double)(size - 1));
	return data[idx];
}


void
compute_time_series_stats(Time_Series_Stats *stats, Statistics_Settings *settings, Data_Storage<double, Var_Id> *data, s64 offset, s64 ts_begin, s64 len);

void
compute_residual_stats(Residual_Stats *stats, Data_Storage<double, Var_Id> *data_sim, s64 offset_sim, s64 ts_begin_sim, Data_Storage<double, Var_Id> *data_obs, s64 offset_obs, s64 ts_begin_obs, s64 len, bool compute_rcc);


#endif // MOBIUS_STATISTICS_H