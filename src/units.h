
#ifndef MOBIUS_UNITS_H
#define MOBIUS_UNITS_H


#include "mobius_common.h"
#include "rational.h"
#include "lexer.h"
#include "ast.h"

#include <vector>
#include <string>

// NOTE year and month can't quite be denoted in seconds since the years and months have variable length
enum class
Base_Unit {
	m = 0, s = 1, g = 2, mol = 3, deg_c = 4, deg = 5, month = 6, year = 7, K = 8, max = 9
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
	//s64          multiplier;
	s16           magnitude;
	Rational<s16> power;
	Compound_Unit unit;
};

struct
Standardized_Unit {
	Rational<s64> multiplier;
	Rational<s16> magnitude;
	Rational<s16> powers[(int)Base_Unit::max];
	// this represents a unit on the form   multiplier * 10^magnitude * m^powers[0] * s^powers[1] * g^powers[2] ....
	
	Standardized_Unit() : multiplier(1), magnitude(0) {
		for(int idx = 0; idx < (int)Base_Unit::max; ++idx) powers[idx] = 0;
	}
};

struct
Unit_Data {
	std::vector<Declared_Unit_Part> declared_form;
	Standardized_Unit               standard_form;
	
	std::string to_utf8();
	// TODO: also to_latex(), and maybe to_mathml().
	
	//bool operator==(const Unit_Data &other);
	Time_Step_Size  to_time_step(bool &success);
	
	void set_standard_form();
};

Unit_Data
multiply(const Unit_Data &a, const Unit_Data &b, int power = 1);

inline Unit_Data
divide(const Unit_Data &a, const Unit_Data &b) { return multiply(a, b, -1); }

Declared_Unit_Part
parse_unit(std::vector<Token> *tokens);

void
set_unit_data(Unit_Data &data, Decl_AST *decl);

bool
match(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor);

#endif // MOBIUS_UNITS_H
