
//NOTE: Just a very simple datetime library that has what we need for our purposes.

#ifndef DATETIME_H
#define DATETIME_H

#include "linear_memory.h"

inline bool
is_leap_year(s32 year) {
	if(year % 4 != 0)   return false;
	if(year % 100 != 0) return true;
	if(year % 400 != 0) return false;
	return true;
}

inline s32
year_length(s32 year) {
	return 365 + (s32)is_leap_year(year);
}

inline s32
month_length(s32 year, s32 month) {
	//NOTE: Returns the number of days in a month. The months are indexed from 1 in this context.
	static s32 length[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	s32 days = length[month-1];
	if(month == 2 && is_leap_year(year)) ++days;
	return days;
}

inline s32
month_offset(s32 year, s32 month) {
	//NOTE: Returns the number of the days in this year before this month starts. The months are indexed from 1 in this context.
	static s32 offset[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
	s32 days = offset[month-1];
	if(month > 2 && is_leap_year(year)) ++days;
	return days;
}

struct Date_Time {

public:
	//NOTE: It is very important that SecondsSinceEpoch is the only data member of Date_Time and that it is 8 bytes. This is because a Date_Time is a member of the parameter_value union. Changing this may change the size of the parameter_value, which could break things.
	s64 seconds_since_epoch;
	
	Date_Time() : seconds_since_epoch(0) {}
	
	Date_Time(s32 year, s32 month, s32 day, bool *success) {
		*success = (day >= 1 && day <= month_length(year, month) && month >= 1 && month <= 12);
		if(!*success) return;
		
		s64 result = 0;
		if(year > 1970) {
			for(s32 y = 1970; y < year; ++y)
				result += year_length(y)*24*60*60;
		} else if(year < 1970) {
			for(s32 y = 1969; y >= year; --y)
				result -= year_length(y)*24*60*60;
		}
		
		result += month_offset(year, month)*24*60*60;
		result += (day-1)*24*60*60;
		
		seconds_since_epoch = result;
	}
	
	inline bool
	add_timestamp(s32 hour, s32 minute, s32 second) {
		bool success = hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
		seconds_since_epoch += 3600*hour + 60*minute + second;
		return success;
	}
	
	inline void
	day_of_year(s32 *day_out, s32 *year_out) const {
		//Computes the day of year (Starting at january 1st = day 1)
		s32 year = 1970;
		s32 doy = 0;
		s64 seconds_left = seconds_since_epoch;
		if(seconds_left > 0) {
			while(true) {
				s64 seconds_this_year = year_length(year)*24*60*60;
				if(seconds_left >= seconds_this_year) {
					year++;
					seconds_left -= seconds_this_year;
				}
				else break;
			}
			doy = seconds_left / (24*60*60);
		} else if(seconds_left < 0) {
			seconds_left = -seconds_left;
			year = 1969;
			s64 seconds_this_year;
			while(true) {
				seconds_this_year = year_length(year)*24*60*60;
				if(seconds_left > seconds_this_year) {
					year--;
					seconds_left -= seconds_this_year;
				}
				else break;
			}
			//s64 seconds_this_year = year_length(year)*24*60*60;
			doy = (seconds_this_year - seconds_left) / (24*60*60);
		}
		
		*year_out = year;
		*day_out  = doy + 1;
	}
	
	inline void
	year_month_day(s32 *year_out, s32 *month_out, s32 *day_out) const {
		//Computes the year, month and day (of month) for a seconds since epoch timestamp.
		s32 day;
		day_of_year(&day, year_out);
		
		for(s32 month = 1; month <= 12; ++month) {
			if(day <= month_offset(*year_out, month+1)) {
				*month_out = month;
				*day_out = day - month_offset(*year_out, month);
				break;
			}
		}
	}
	
	inline s64
	second_of_day() const {
		if(seconds_since_epoch >= 0)
			return seconds_since_epoch % 86400;
		else
			return (seconds_since_epoch % 86400 + 86400) % 86400;
		return 0;
	}
	
	inline String_View
	to_string() const {
		//Important: note that this one is overwritten whenever you call it. So you should make a copy of the string if you want to keep it.
		s32 year, month, day, hour, minute, second;
		year_month_day(&year, &month, &day);
		static char buf[64];
		if(seconds_since_epoch % 86400 == 0)
			sprintf(buf, "%04d-%02d-%02d", year, month, day);
		else {
			s64 sod = second_of_day();
			s32 hour = (sod / 3600);
			s32 minute = (sod / 60) % 60;
			s32 second = sod % 60;
			sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
		}
		String_View result(buf);
		
		return result;
	}
	
	bool
	operator<(const Date_Time& other) {
		return seconds_since_epoch < other.seconds_since_epoch;
	}
	
	bool
	operator<=(const Date_Time& other) {
		return seconds_since_epoch <= other.seconds_since_epoch;
	}
	
	bool
	operator>(const Date_Time &other) {
		return seconds_since_epoch > other.seconds_since_epoch;
	}
	
	Date_Time &
	operator+=(const Date_Time &other) {
		seconds_since_epoch += other.seconds_since_epoch;
		return *this;
	}
};


enum class
Time_Step_Unit {
	second = 0,
	month  = 1,
	//year   = 2,   // could have some optimizations in the code below if we know the step size is in whole years
};

struct Time_Step_Size {
	Time_Step_Unit unit;
	s32            magnitude;
	
	Time_Step_Size(Time_Step_Unit unit, s32 magnitude) : unit(unit), magnitude(magnitude) {}
	Time_Step_Size() : unit(Time_Step_Unit::second), magnitude(86400) {}
};

struct Expanded_Date_Time {
	// Note: IMPORTANT! The members coming from time_values.incl have to be at the top of this struct!
	
	#define TIME_VALUE(name, bits) s##bits name;
	#include "time_values.incl"
	#undef TIME_VALUE
	
	Date_Time date_time;
	Time_Step_Size time_step;
	
	Expanded_Date_Time(Date_Time base, Time_Step_Size time_step) : date_time(base), time_step(time_step), step(0) {
		//NOTE: This does double work, but it should not matter that much.
		date_time.year_month_day(&year, &month, &day_of_month);
		date_time.day_of_year(&day_of_year, &year);
		
		days_this_year  = year_length(year);
		days_this_month = month_length(year, month);
		
		second_of_day = date_time.second_of_day();
		
		compute_next_step_size();
	}
	
	Expanded_Date_Time() : Expanded_Date_Time(Date_Time(), Time_Step_Size {Time_Step_Unit::second, 86400}) {}
	
	void
	advance() {
		++step;
		date_time.seconds_since_epoch += step_length_in_seconds;
		if(time_step.unit == Time_Step_Unit::second) {
			second_of_day                 += step_length_in_seconds;
			
			s32 days       = second_of_day / 86400;
			second_of_day -= 86400*days;
			day_of_year   += days;
			day_of_month  += days;
		} else
			day_of_month  += step_length_in_seconds / 86400;
		
		while(day_of_month > days_this_month) {
			day_of_month -= days_this_month;
			++month;
			
			if(month > 12) {
				day_of_year -= days_this_year;
				++year;
				days_this_year = year_length(year);
				month = 1;
			}
			
			days_this_month = month_length(year, month);
		}
		
		if(time_step.unit == Time_Step_Unit::month)
			compute_next_step_size();
	}
	
	void
	compute_next_step_size() {
		if(time_step.unit == Time_Step_Unit::second)
			step_length_in_seconds = time_step.magnitude;
		else {
			step_length_in_seconds = 0;
			s32 y = year;
			s32 m = month;
			for(s32 mm = 0; mm < time_step.magnitude; ++mm) {
				step_length_in_seconds += 86400*month_length(y, m);
				++m;
				if(m > 12) {m = 1; ++y; } 
			}
		}
	}
	
};

inline s64
divide_down(s64 a, s64 b) {
    s64 r = a/b;
    if(r < 0 && r*b != a)
        return r - 1;
    return r;
}

inline s64
steps_between(Date_Time from, Date_Time to, Time_Step_Size time_step) {
	s64 diff;
	if(time_step.unit == Time_Step_Unit::second)
		diff = to.seconds_since_epoch - from.seconds_since_epoch;
	else {
		s32 fy, fm, fd, ty, tm, td;
		
		//TODO: This could probably be optimized
		from.year_month_day(&fy, &fm, &fd);
		to.year_month_day(&ty, &tm, &td);

		diff = (tm - fm + 12*(ty - fy));
	}
	
	return divide_down(diff, (s64)time_step.magnitude);
}


#endif // DATETIME_H