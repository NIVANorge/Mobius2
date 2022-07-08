
#include "units.h"

#include <cmath>


Rational<s16>
parse_magnitude(Token *token) {           // TODO: complete this and make sure it is correct
	String_View m = token->string_value;
	if(m == "k") return 3;
	else if(m == "h") return 2;
	else if(m == "da") return 1;
	else if(m == "d") return -1;
	else if(m == "c") return -2;
	else if(m == "m") return -3;
	
	token->print_error_header();
	fatal_error("Unrecognized SI prefix ", m, " .");  //TODO: what is the proper name for "magnitude" (si prefix?)
	return 0;
}

Compound_Unit
parse_compound_unit(Token *token) {
	String_View u = token->string_value;
	if(u == "m") return Compound_Unit::meter;
	else if(u == "s") return Compound_Unit::second;
	else if(u == "g") return Compound_Unit::gram;
	else if(u == "mol") return Compound_Unit::mol;
	else if(u == "deg_c") return Compound_Unit::deg_c;
	else if(u == "N") return Compound_Unit::newton;
	else if(u == "h") return Compound_Unit::hour;
	else if(u == "min") return Compound_Unit::minute;
	else if(u == "day") return Compound_Unit::day;
	
	token->print_error_header();
	fatal_error("Unrecognized unit ", u, " .");
	
	return Compound_Unit::meter;
}

Declared_Unit_Part
parse_unit(std::vector<Token> *tokens) {
	if(tokens->empty())
		fatal_error(Mobius_Error::internal, "Received empty set of tokens for unit in parse_unit().");
	Declared_Unit_Part result;
	bool error = false;
	int size = tokens->size();
	if((*tokens)[0].type == Token_Type::identifier) {
		int pow_idx;
		if(size >= 2 && (*tokens)[1].type == Token_Type::identifier) {
			result.magnitude = parse_magnitude(&(*tokens)[0]);
			result.unit = parse_compound_unit(&(*tokens)[1]);
			pow_idx = 2;
		} else {
			result.magnitude = 1;
			result.unit = parse_compound_unit(&(*tokens)[0]);
			pow_idx = 1;
		}
		
		//TODO: handle rationals
		if(size == pow_idx + 1) {
			if((*tokens)[pow_idx].type == Token_Type::integer)
				result.power = (*tokens)[pow_idx].val_int;
			else
				error = true;
		} else if (size > pow_idx + 1) error = true;
	}
	if(error) {
		(*tokens)[0].print_error_header();
		fatal_error("Malformed unit declaration.");
	}
	
	return result;
}

void
Unit_Data::set_standard_form() {
	Rational<s16> magnitude;
	
	standard_form.multiplier = 1;
	standard_form.magnitude = 0;
	for(int idx = 0; idx < (int)Unit_Atom::max; ++idx) standard_form.powers[idx] = 0;
	
	for(auto &part : declared_form) {
		if((int)part.unit <= (int)Unit_Atom::max)
			standard_form.powers[(int)part.unit] += part.power;
		else if(part.unit == Compound_Unit::newton) {
			standard_form.powers[(int)Unit_Atom::gram] += part.power;
			standard_form.powers[(int)Unit_Atom::meter] += part.power;
			standard_form.powers[(int)Unit_Atom::second] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // from the k in kg in N = kg m /s2
		} else {
			if(part.power.denom != 1)
				fatal_error(Mobius_Error::internal, "Unit standard form: can't handle roots of certain types.");
			
			if(part.unit == Compound_Unit::minute) {
				standard_form.powers[(int)Unit_Atom::second] += part.power;
				standard_form.magnitude += part.power;
				standard_form.multiplier *= pow_i<s64>(6, part.power.nom);
			} else if(part.unit == Compound_Unit::hour) {
				standard_form.powers[(int)Unit_Atom::second] += part.power;
				standard_form.magnitude += 2*part.power;
				standard_form.multiplier *= pow_i<s64>(36, part.power.nom);
			} else if(part.unit == Compound_Unit::day) {
				standard_form.powers[(int)Unit_Atom::second] += part.power;
				standard_form.magnitude += 2*part.power;
				standard_form.multiplier *= pow_i<s64>(864, part.power.nom);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled compound unit in set_standard_form().");
		} 
		magnitude += part.magnitude*part.power;
		//TODO: reduce multiplier if it is a power of 10?
	}
}


Standardized_Unit
operator*(const Standardized_Unit &a, const Standardized_Unit &b) {
	Standardized_Unit result;
	for(int idx = 0; idx < (int)Unit_Atom::max; ++idx)
		result.powers[idx] = a.powers[idx] + b.powers[idx];
	result.multiplier = a.multiplier * b.multiplier;   //TODO: reduce if it is a power of 10?
	result.magnitude  = a.magnitude + b.magnitude;
	return result;
}

Standardized_Unit
operator/(const Standardized_Unit &a, const Standardized_Unit &b) {
	Standardized_Unit result;
	for(int idx = 0; idx < (int)Unit_Atom::max; ++idx)
		result.powers[idx] = a.powers[idx] - b.powers[idx];
	result.multiplier = a.multiplier / b.multiplier;   //TODO: reduce if it is a power of 10?
	result.magnitude  = a.magnitude - b.magnitude;
	return result;
}

bool
match(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor) {  // the conversion factor so that factor*b = a
	for(int idx = 0; idx < (int)Unit_Atom::max; ++idx)
		if(a->powers[idx] != b->powers[idx]) return false;
	
	*conversion_factor = double(a->multiplier / b->multiplier) * std::pow(10.0, double(a->magnitude - b->magnitude));
	return true;
}

