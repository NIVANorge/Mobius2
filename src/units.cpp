
#include "units.h"

#include <cmath>
#include <sstream>

// TODO: complete this and make sure it is correct
Rational<s16>
parse_si_prefix(Token *token) {
	String_View m = token->string_value;
	if(m == "Y") return 24;
	else if(m == "Z") return 21;
	else if(m == "E") return 18;
	else if(m == "P") return 15;
	else if(m == "T") return 12;
	else if(m == "G") return 9;
	else if(m == "M") return 6;
	else if(m == "k") return 3;
	else if(m == "h") return 2;
	else if(m == "da") return 1;
	else if(m == "d") return -1;
	else if(m == "c") return -2;
	else if(m == "m") return -3;
	else if(m == "mu") return -6; // TODO: Would have to update the lexer to read "µ" as an identifier if this should recognize that symbol.
	else if(m == "n") return -9;
	else if(m == "p") return -12;
	else if(m == "f") return -15;
	else if(m == "a") return -18;
	else if(m == "z") return -21;
	else if(m == "y") return -24;
	
	//token->print_error_header();
	//fatal_error("Unrecognized SI prefix ", m, " .");
	warning_print("Unrecognized SI prefix ", m, " .\n");
	return 0;
}

// TODO: Complete this
const char *
get_si_prefix(int pow10) {
	static const char *prefixes[] = {"G", "100M", "10M", "M", "100k", "10k", "k", "h", "da", "", "d", "c", "m", "100µ", "10µ", "µ", "100n", "10n", "n"};
	int idx = 9-pow10;
	return prefixes[idx];
}

Compound_Unit
parse_compound_unit(Token *token) {
	String_View u = token->string_value;

	if(false){}
	#define COMPOUND_UNIT(handle, name) if(u == #handle) return Compound_Unit::handle;
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
	
	//token->print_error_header();
	//fatal_error("Unrecognized unit ", u, " .");
	warning_print("Unrecognized unit ", u, " .\n");
	
	return Compound_Unit::m;
}

Declared_Unit_Part
parse_unit(std::vector<Token> *tokens) {
	if(tokens->empty())
		fatal_error(Mobius_Error::internal, "Received empty list of tokens for unit in parse_unit().");
	Declared_Unit_Part result;
	bool error = false;
	int size = tokens->size();
	if((*tokens)[0].type == Token_Type::identifier) {
		int pow_idx;
		if(size >= 2 && (*tokens)[1].type == Token_Type::identifier) {
			result.magnitude = parse_si_prefix(&(*tokens)[0]);
			result.unit = parse_compound_unit(&(*tokens)[1]);
			pow_idx = 2;
		} else {
			result.magnitude = 0;
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

	for(auto &part : declared_form) {
		if((int)part.unit <= (int)Base_Unit::max)
			standard_form.powers[(int)part.unit] += part.power;
		else if(part.unit == Compound_Unit::N) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // N = 10^3 g m s^-2
		} else if(part.unit == Compound_Unit::l) {
			standard_form.powers[(int)Base_Unit::m] += 3*part.power;
			standard_form.magnitude -= 3*part.power;   // l = 10^-3 m^3
		} else if(part.unit == Compound_Unit::ha) {
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.magnitude += 4*part.power;
		} else if(part.unit == Compound_Unit::Pa) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] -= part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // Pa = 10^3 g m^-1 s^-2
		} else {
			if(part.power.denom != 1)
				fatal_error(Mobius_Error::internal, "Unit standard form: can't handle roots of certain types.");
			
			if(part.unit == Compound_Unit::min) {
				standard_form.powers[(int)Base_Unit::s] += part.power;
				standard_form.magnitude += part.power;
				standard_form.multiplier *= pow_i<s64>(6, part.power.nom);
			} else if(part.unit == Compound_Unit::hr) {
				standard_form.powers[(int)Base_Unit::s] += part.power;
				standard_form.magnitude += 2*part.power;
				standard_form.multiplier *= pow_i<s64>(36, part.power.nom);
			} else if(part.unit == Compound_Unit::day) {
				standard_form.powers[(int)Base_Unit::s] += part.power;
				standard_form.magnitude += 2*part.power;
				standard_form.multiplier *= pow_i<s64>(864, part.power.nom);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled compound unit in set_standard_form().");
		}
		//magnitude += part.magnitude*part.power;
		//TODO: reduce multiplier if it is a power of 10?
	}
}


Standardized_Unit
operator*(const Standardized_Unit &a, const Standardized_Unit &b) {
	Standardized_Unit result;
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		result.powers[idx] = a.powers[idx] + b.powers[idx];
	result.multiplier = a.multiplier * b.multiplier;   //TODO: reduce if it is a power of 10?
	result.magnitude  = a.magnitude + b.magnitude;
	return result;
}

Standardized_Unit
operator/(const Standardized_Unit &a, const Standardized_Unit &b) {
	Standardized_Unit result;
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		result.powers[idx] = a.powers[idx] - b.powers[idx];
	result.multiplier = a.multiplier / b.multiplier;   //TODO: reduce if it is a power of 10?
	result.magnitude  = a.magnitude - b.magnitude;
	return result;
}

bool
match(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor) {  // the conversion factor so that factor*b = a
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		if(a->powers[idx] != b->powers[idx]) return false;
	
	*conversion_factor = double(a->multiplier / b->multiplier) * std::pow(10.0, double(a->magnitude - b->magnitude));
	return true;
}

template<typename T>
std::ostream &Rational<T>::operator<<(std::ostream &os) {
	if(nom == 0) {
		os << "0";
		return os;
	}
	if(denom == 1) {
		if(nom == 1) return os;
		os << nom;
		return os;
	}
	os << nom << "/" << denom;
	return os;
}

std::string
Unit_Data::to_utf8() {
	//TODO: implementation is a bit quick and messy.
	
	static const char *unit_symbols[] = {
		#define COMPOUND_UNIT(handle, name) name,
		#include "compound_units.incl"
		#undef COMPOUND_UNIT
	};
	
	if(declared_form.empty())
		return "dimensionless";

	std::stringstream ss;
	int idx = 0;
	for(auto &part : declared_form) {
		int mag = part.magnitude.nom; //TODO: fractional?
		ss << get_si_prefix(mag) << unit_symbols[(int)part.unit];
		//TODO: make it work for fractional powers too!
		int nom = part.power.nom;
		static char buf[32];
		itoa(nom, buf, 10);
		char *c = &buf[0];
		while(*c) {
			if(*c == '-')
				ss << u8"\u207b";
			else if(*c == '0' && nom != 0)
				ss << u8"\u2070";
			else if(*c == '1' && nom != 1)
				ss << u8"\u00b9";
			else if(*c == '2')
				ss << u8"\u00b2";
			else if(*c == '3')
				ss << u8"\u00b3";
			else if(*c == '4')
				ss << u8"\u2074";
			else if(*c == '5')
				ss << u8"\u2075";
			else if(*c == '6')
				ss << u8"\u2076";
			else if(*c == '7')
				ss << u8"\u2077";
			else if(*c == '8')
				ss << u8"\u2078";
			else if(*c == '9')
				ss << u8"\u2079";
			c++;
		}
		if(idx != declared_form.size()-1)
			ss << " ";
		
		++idx;
	}
	return ss.str();
}

void
set_unit_data(Unit_Data &data, Decl_AST *decl) {
	for(Argument_AST *arg : decl->args) {
		if(!Arg_Pattern().matches(arg)) {
			decl->location.print_error_header();
			fatal_error("Invalid argument to unit declaration.");
		}
	}
	for(auto arg : decl->args)
		data.declared_form.push_back(parse_unit(&arg->sub_chain));
	data.set_standard_form();
}
