
#ifndef MOBIUS_AGGREGATE_H
#define MOBIUS_AGGREGATE_H

#include "../common_types.h"   // For declaration of Aggregation_Period

enum class Aggregation_Type {
	mean = 0,
	sum,
	min,
	max,
	freq,
};

inline Date_Time
normalize_to_aggregation_period(Date_Time time, Aggregation_Period agg, int pivot_month) {
	if(agg == Aggregation_Period::none) return time;
	Date_Time result = time;
	result.seconds_since_epoch -= result.second_of_day();
	if(agg == Aggregation_Period::weekly) {
		int dow = result.day_of_week()-1;
		result.seconds_since_epoch -= 86400*dow;
	} else {
		s32 y, m, d;
		result.year_month_day(&y, &m, &d);
		if(agg == Aggregation_Period::yearly) {
			if(m < pivot_month) y--;
			m = pivot_month;
		}
		result = Date_Time(y, m, 1);
	}
	return result;
}

inline double
reset_aggregate(Aggregation_Type type) {
	if(type == Aggregation_Type::mean || type == Aggregation_Type::sum || type == Aggregation_Type::freq)
		return 0.0;
	else if(type == Aggregation_Type::min)
		return std::numeric_limits<double>::infinity();
	else if(type == Aggregation_Type::max)
		return -std::numeric_limits<double>::infinity();
	return 0.0;
}

template<typename Source> void
aggregate_data(Date_Time &ref_time, Date_Time &start_time, Source *data,
	Aggregation_Period agg_period, Aggregation_Type agg_type, Time_Step_Size ts_size, int pivot_month, std::vector<double> &x_vals, std::vector<double> &y_vals)
{
	s64 steps = data->GetCount();
	
	double curr_agg = reset_aggregate(agg_type);
		
	int count = 0;
	Expanded_Date_Time time(start_time, ts_size);
	
	for(s64 step = 0; step < steps; ++step) {
		double val = data->y(step);
		
		//TODO: more numerically stable summation method.
		if(std::isfinite(val)) {
			if(agg_type == Aggregation_Type::mean || agg_type == Aggregation_Type::sum)
				curr_agg += val;
			else if(agg_type == Aggregation_Type::min)
				curr_agg = std::min(val, curr_agg);
			else if(agg_type == Aggregation_Type::max)
				curr_agg = std::max(val, curr_agg);
			else if(agg_type == Aggregation_Type::freq)
				curr_agg += 1.0;
			++count;
		}
		
		Date_Time prev = time.date_time;
		s32 y = time.year;
		s32 m = time.month;
		s32 d = time.day_of_month;
		s32 w = time.date_time.week_since_epoch();
		time.advance();
		
		//TODO: Want more aggregation interval types like daily or hourly (for models with smaller time steps).
		bool push = false;
		if(agg_period == Aggregation_Period::yearly) {
			push = (time.month >= pivot_month && (time.year != y || m < pivot_month));
		} else if(agg_period == Aggregation_Period::monthly)
			push = (time.month != m) || (time.year != y);
		else if(agg_period == Aggregation_Period::weekly) {
			auto week = time.date_time.week_since_epoch();
			push = (week != w);
		} else
			fatal_error(Mobius_Error::internal, "Unhandled aggregation type in aggregate_data()");
		
		if(push || (step == steps-1)) {
			double yval = curr_agg;
			if(agg_type == Aggregation_Type::mean) yval /= (double)count;
			if(!std::isfinite(yval) || (count == 0 && agg_type != Aggregation_Type::freq))
				yval = std::numeric_limits<double>::quiet_NaN();
			y_vals.push_back(yval);
			
			// Put the mark down at a round location.
			Date_Time mark = normalize_to_aggregation_period(prev, agg_period, pivot_month);
			double xval = (double)(mark.seconds_since_epoch - ref_time.seconds_since_epoch);
			x_vals.push_back(xval);
			
			curr_agg = reset_aggregate(agg_type);
			
			count = 0;
		}
	}
}

#endif // MOBIUS_AGGREGATE_H