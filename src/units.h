
#ifndef MOBIUS_UNITS_H
#define MOBIUS_UNITS_H


#include "mobius_common.h"
#include "rational.h"
#include "lexer.h"

#include <vector>

//TODO: this system may be too rigid. Should allow user-defined ones too.
//TODO: make many more!
enum class
Unit_Atom {                                     //TODO: better name?
	meter = 0, second, gram, mol, deg_c, max
};

enum class
Compound_Unit { // Note: start of this must match Unit_Atom
	meter = 0, second, gram, mol, deg_c, newton, minute, hour, day,
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
	Rational<s16> powers[(int)Unit_Atom::max];
	
	// this represents a unit on the form   multiplier * 10^magnitude * m^powers[0] * s^powers[1] ....
};

struct
Unit_Data {
	std::vector<Declared_Unit_Part> declared_form;
	Standardized_Unit               standard_form;
	
	void set_standard_form();
};

Declared_Unit_Part
parse_unit(const std::vector<Token> *tokens);

#endif // MOBIUS_UNITS_H
