
#ifndef MOBIUS_UNITS_H
#define MOBIUS_UNITS_H


#include "mobius_common.h"
#include "rational.h"
#include "lexer.h"
//#include "ast.h"

#include <vector>
#include <string>

// NOTE year and month can't quite be denoted in seconds since the years and months have variable length
// TODO: Why didn't we just decide that year = 12*month??
enum class
Base_Unit {
	m = 0,
	s = 1,
	g = 2,
	mol = 3,
	deg_c = 4,
	deg = 5,
	month = 6,
	year = 7,
	K = 8,
	A = 9,
	eq = 10,
	max = 11
};

inline bool is_time(Base_Unit bu) {
	return bu == Base_Unit::s || bu == Base_Unit::month || bu == Base_Unit::year;
}

enum class
Compound_Unit { 
	#define COMPOUND_UNIT(handle, name) handle,
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
};

struct
Declared_Unit_Part {
	s16           magnitude;
	Rational<s16> power;
	Compound_Unit unit;
};

struct
Standardized_Unit {
	Rational<s64> multiplier;
	Rational<s16> magnitude;
	Rational<s16> powers[(int)Base_Unit::max];  //TODO: probably change to std::vector as it is more efficient to std::move.
	// this represents a unit on the form   multiplier * 10^magnitude * m^powers[0] * s^powers[1] * g^powers[2] ....
	
	Standardized_Unit() : multiplier(1), magnitude(0) {
		for(int idx = 0; idx < (int)Base_Unit::max; ++idx) powers[idx] = 0;
	}
	std::string to_utf8();
	void reduce();
	bool is_atom(Base_Unit bu);
	bool is_dimensionless();
	bool is_fully_dimensionless();
};

Standardized_Unit
multiply(const Standardized_Unit &a, const Standardized_Unit &b, int power = 1);

bool
pow(const Standardized_Unit &a, Standardized_Unit &result, Rational<s16> power);

inline Standardized_Unit
unit_atom(Base_Unit bu, s64 multiplier = 1) {
	Standardized_Unit result;
	result.powers[(int)bu] = 1;
	result.multiplier = multiplier;
	result.reduce();
	return result;
}

struct Decl_AST;

struct
Unit_Data {
	Rational<s64>                   declared_multiplier = {1, 1};
	std::vector<Declared_Unit_Part> declared_form;
	Standardized_Unit               standard_form;
	
	std::string to_utf8();
	std::string to_decl_str();
	std::string to_latex();
	
	//bool operator==(const Unit_Data &other);
	Time_Step_Size  to_time_step(bool &success);
	
	void set_data(Decl_AST *decl);
	void set_standard_form();
};

Unit_Data
multiply(const Unit_Data &a, const Unit_Data &b, int power = 1);

inline Unit_Data
divide(const Unit_Data &a, const Unit_Data &b) { return multiply(a, b, -1); }

bool
match(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor);
bool
match_offset(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor);
bool
match_exact(Standardized_Unit *a, Standardized_Unit *b);

inline Compound_Unit
agg_period_to_compound_unit(Aggregation_Period agg) {
	if(agg == Aggregation_Period::weekly)
		return Compound_Unit::week;
	else if(agg == Aggregation_Period::monthly)
		return Compound_Unit::month;
	else if(agg == Aggregation_Period::yearly)
		return Compound_Unit::year;
	return Compound_Unit::s;
}

Unit_Data
unit_of_sum(const Unit_Data &unit, const Unit_Data &ts_unit, Aggregation_Period agg_period);

#endif // MOBIUS_UNITS_H
