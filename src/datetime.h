
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
	year_month_day(s32 *year_out, s32 *month_out, s32 *day_out) const
	{
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
	second_of_day() const 	{
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
		else
		{
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


/*
enum class
Timestep_Unit {
	Second = 0,
	Month  = 1,
	//Year,    //TODO: Should probably have this as an optimization!
};

struct Timestep_Size {
	Timestep_Unit unit;
	s32           magnitude;
};


Timestep_Size
parse_timestep_size(const char *format)
{
	//TODO: Has to parse a string view instead.
	
	// Format is "XY", where X is a number and Y is one of "s", "m", "h", "D", "M", "Y".
	Timestep_Size result;
	char type;
	int found = sscanf(format, "%d%c", &result.magnitude, &type);
	if(found != 2 || result.magnitude <= 0)
		fatal_error(Mobius_Error::model_building, "The size of the timestep must be declared on the format \"nx\", where n is a positive whole number, and x is one of 's', 'm', 'h', 'D', 'M', or 'Y'.");
	if(type == 's')
		result.unit = Timestep_Unit::second;
	else if(Type == 'm') {
		result.unit = Timestep_Unit::second;
		result.magnitude *= 60;
	}
	else if(Type == 'h') {
		result.unit = Timestep_Unit::second;
		result.magnitude *= 3600;
	}
	else if(Type == 'D') {
		result.unit = Timestep_Unit::second;
		result.magnitude *= 86400;
	}
	else if(Type == 'M')
		result.unit = Timestep_Unit::month;
	else if(Type == 'Y') {
		result.unit = Timestep_Unit::month;
		result.magnitude *= 12;
	}
	else
		fatal_error(Mobius_Error::model_building, "The size of the timestep must be declared on the format \"nx\", where n is a positive whole number, and x is one of 's', 'm', 'h', 'D', 'M', or 'Y'.");
	
	return result;
}

String_View
timestep_size_as_unit_name(const char *format, linear_memory *memory)
{
	//NOTE: This one doesn't do error checking, instead we assume that Format has already been passed to ParseTimestepSize once
	char type;
	s32  magnitude;
	int Found = sscanf(format, "%d%c", &magnitude, &type);

	const char *Name;
	if(Type == 's')
		Name = "seconds";
	else if(Type == 'm')
		Name = "minutes";
	else if(Type == 'h')
		Name = "hours";
	else if(Type == 'D')
		Name = "days";
	else if(Type == 'M')
		Name = "months";
	else if(Type == 'Y')
		Name = "years";
	
	if(Magnitude != 1)
	{
		char Buffer[256];
		sprintf(Buffer, "%d %s", Magnitude, Name);
		Name = Buffer;
	}
	
	token_string StrName(Name);
	return StrName.Copy(Alloc);
}

struct expanded_datetime
{
	datetime DateTime;
	s32      Year;
	s32      Month;
	s32      DayOfYear;
	s32      DayOfMonth;
	s64      SecondOfDay;
	
	s32      DaysThisYear;
	s32      DaysThisMonth;
	
	timestep_size Timestep;
	s64      StepLengthInSeconds;
	
	expanded_datetime(datetime Base, timestep_size Timestep)
	{
		this->DateTime = Base;
		this->Timestep = Timestep;
		
		//NOTE: This does double work, but it should not matter that much.
		DateTime.YearMonthDay(&Year, &Month, &DayOfMonth);
		DateTime.DayOfYear(&DayOfYear, &Year);
		
		DaysThisYear = YearLength(Year);
		DaysThisMonth = MonthLength(Year, Month);
		
		SecondOfDay = DateTime.SecondOfDay();
		
		ComputeNextStepSize();
	}
	
	expanded_datetime()
	{
		Year = 1970;
		Month = 1;
		DayOfYear = 1;
		DayOfMonth = 1;
		DaysThisYear = 365;
		DaysThisMonth = 30;
		StepLengthInSeconds = 86400;
		Timestep.Unit = Timestep_Second;
		Timestep.Magnitude = 86400;
		SecondOfDay = 0;
	}
	
	void
	Advance()
	{
		if(Timestep.Unit == Timestep_Second)
		{
			DateTime.SecondsSinceEpoch += StepLengthInSeconds;
			SecondOfDay                += StepLengthInSeconds;
			
			s32 Days = SecondOfDay / 86400;
			SecondOfDay -= 86400*Days;
			DayOfYear   += Days;
			DayOfMonth  += Days;
		}
		else
		{
			assert(Timestep.Unit == Timestep_Month);
			
			DateTime.SecondsSinceEpoch += StepLengthInSeconds;
			DayOfMonth                 += StepLengthInSeconds / 86400;
		}
		
		while(DayOfMonth > DaysThisMonth)
		{
			DayOfMonth -= DaysThisMonth;
			Month++;
			
			if(Month > 12)
			{
				
				DayOfYear -= DaysThisYear;
				Year++;
				DaysThisYear = YearLength(Year);
				Month = 1;
			}
			
			DaysThisMonth = MonthLength(Year, Month);
		}
		
		if(Timestep.Unit == Timestep_Month)
			ComputeNextStepSize();
	}
	
	void
	ComputeNextStepSize()
	{
		if(Timestep.Unit == Timestep_Second)
			StepLengthInSeconds = Timestep.Magnitude;
		else
		{
			assert(Timestep.Unit == Timestep_Month);
			
			StepLengthInSeconds = 0;
			s32 Y = Year;
			s32 M = Month;
			for(s32 MM = 0; MM < Timestep.Magnitude; ++MM)
			{
				StepLengthInSeconds += 86400*MonthLength(Y, M);
				M++;
				if(M > 12) {M = 1; Y++; } 
			}
		}
	}
	
};

std::ostream& operator<<(std::ostream& Os, const expanded_datetime &Dt)
{
	Os << Dt.DateTime.ToString();
	return Os;
}


static s64
DivideDown(s64 a, s64 b)
{
    s64 r = a/b;
    if(r < 0 && r*b != a)
        return r - 1;
    return r;
}

static s64
FindTimestep(datetime Start, datetime Current, timestep_size Timestep)
{
	s64 Diff;
	if(Timestep.Unit == Timestep_Second)
		Diff = Current.SecondsSinceEpoch - Start.SecondsSinceEpoch;
	else
	{
		assert(Timestep.Unit == Timestep_Month);
		
		s32 SY, SM, SD, CY, CM, CD;
		
		//TODO: This could probably be optimized
		Start.YearMonthDay(&SY, &SM, &SD);
		Current.YearMonthDay(&CY, &CM, &CD);

		Diff = (CM - SM + 12*(CY-SY));
	}
	
	return DivideDown(Diff, (s64)Timestep.Magnitude);
}
*/

#endif // DATETIME_H