
#ifndef MOBIUS_UNITS_H
#define MOBIUS_UNITS_H


#include "mobius_common.h"
#include "rational.h"
#include "lexer.h"

#include <vector>
#include <string>

// NOTE year and month can't quite be denoted in seconds since the years and months have variable length
enum class
Base_Unit {
	m = 0, s, g, mol, deg_c, deg, month, year, max
};

enum class
Compound_Unit { 
	#define COMPOUND_UNIT(handle, name) handle,
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
};


struct
Declared_Unit_Part {
	Rational<s16> magnitude;
	Rational<s16> power;
	Compound_Unit unit;
};

struct
Standardized_Unit {
	Rational<s64> multiplier;
	Rational<s16> magnitude;
	Rational<s16> powers[(int)Base_Unit::max];
	// this represents a unit on the form   multiplier * 10^magnitude * m^powers[0] * s^powers[1] ....
	
	Standardized_Unit() : multiplier(1), magnitude(0) {
		for(int idx = 0; idx < (int)Base_Unit::max; ++idx) powers[idx] = 0;
	}
};

struct
Unit_Data {
	std::vector<Declared_Unit_Part> declared_form;
	Standardized_Unit               standard_form;
	
	std::string to_utf8();
	// TODO: also to_latex etc.
	
	void set_standard_form();
};

Declared_Unit_Part
parse_unit(std::vector<Token> *tokens);

#endif // MOBIUS_UNITS_H
