
#include "units.h"
#include "ast.h"

#include <cmath>
#include <sstream>

s16
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
	else if(m == "mu" || m == "u") return -6; // TODO: Would have to update the lexer to read "µ" as an identifier if this should recognize that symbol.
	else if(m == "n") return -9;
	else if(m == "p") return -12;
	else if(m == "f") return -15;
	else if(m == "a") return -18;
	else if(m == "z") return -21;
	else if(m == "y") return -24;
	
	token->print_error_header();
	fatal_error("Unrecognized SI prefix ", m, " .");
	return 0;
}

// TODO: Fill in with all possible ones.
const char *
get_si_prefix(int pow10, bool declared=false) {
	static const char *prefixes[] = {"G", "100M", "10M", "M", "100k", "10k", "k", "h", "da", "", "d", "c", "m", "100µ", "10µ", "µ", "100n", "10n", "n"};
	static const char *prefixes2[] = {"G", "100M", "10M", "M", "100k", "10k", "k", "h", "da", "", "d", "c", "m", "100u", "10u", "u", "100n", "10n", "n"};
	int idx = 9-pow10;
	if(idx < 0 || idx > 18)
		fatal_error(Mobius_Error::internal, "Unimplemented SI prefix.");
	return declared ? prefixes2[idx] : prefixes[idx];
}

Compound_Unit
parse_compound_unit(Token *token) {
	String_View u = token->string_value;

	if(false){}
	#define COMPOUND_UNIT(handle, name) if(u == #handle) return Compound_Unit::handle;
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
	
	token->print_error_header();
	fatal_error("Unrecognized unit ", u, " .");

	return Compound_Unit::m;
}

Declared_Unit_Part
parse_unit(std::vector<Token> *tokens) {
	if(tokens->empty())
		fatal_error(Mobius_Error::internal, "Received empty list of tokens for unit in parse_unit().");
	Declared_Unit_Part result;
	result.power = 1;
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
		
		if(size >= pow_idx + 1) {
			if((*tokens)[pow_idx].type == Token_Type::integer)
				result.power = (*tokens)[pow_idx].val_int;
			else
				error = true;
			if(size == pow_idx + 3) {
				if((char)(*tokens)[pow_idx+1].type == '/' && (*tokens)[pow_idx+2].type == Token_Type::integer)
					result.power /= (*tokens)[pow_idx+2].val_int;
				else
					error = true;
			} else if (size != pow_idx + 1)
				error = true;
		}
	} else
		error = true;
	
	if(error) {
		(*tokens)[0].print_error_header();
		fatal_error("Malformed unit declaration.");
	}
	
	return result;
}

void
Unit_Data::set_standard_form() {
	
	standard_form.multiplier = declared_multiplier;
	standard_form.magnitude  = 0;
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		standard_form.powers[idx] = 0;
	standard_form.reduce();

	// TODO: There has to be a more efficient way to organize the below data:

	for(auto &part : declared_form) {
		if((int)part.unit < (int)Base_Unit::max)
			standard_form.powers[(int)part.unit] += part.power;
		else if(part.unit == Compound_Unit::N) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // Newton = (10^3 g) m s^-2
		} else if(part.unit == Compound_Unit::J) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // Joule = (10^3 g) m^2 s^-2
		} else if(part.unit == Compound_Unit::W) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.powers[(int)Base_Unit::s] -= 3*part.power;
			standard_form.magnitude += 3*part.power;  // Watt = (10^3 g) m^2 s^-3
		} else if(part.unit == Compound_Unit::l) {
			standard_form.powers[(int)Base_Unit::m] += 3*part.power;
			standard_form.magnitude -= 3*part.power;   // liter = 10^-3 m^3
		} else if(part.unit == Compound_Unit::ha) {
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.magnitude += 4*part.power;   // hectare = (10^2 m)^2
		} else if(part.unit == Compound_Unit::Pa) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] -= part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // Pascal = (10^3 g) m^-1 s^-2
		} else if(part.unit == Compound_Unit::bar) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] -= part.power;
			standard_form.powers[(int)Base_Unit::s] -= 2*part.power;
			standard_form.magnitude += 8*part.power;  // bar = 10^5*(10^3 g) m^-1 s^-2
		} else if(part.unit == Compound_Unit::V) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.powers[(int)Base_Unit::s] -= 3*part.power;
			standard_form.powers[(int)Base_Unit::A] -= part.power;
			standard_form.magnitude += 3*part.power;  // Volt = (10^3 g) m^2 s^-3 A^-1
		} else if(part.unit == Compound_Unit::ohm) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.powers[(int)Base_Unit::m] += 2*part.power;
			standard_form.powers[(int)Base_Unit::s] -= 3*part.power;
			standard_form.powers[(int)Base_Unit::A] -= 2*part.power;
			standard_form.magnitude += 3*part.power;  // Ohm = (10^3 g) m^2 s^-3 A^-2
		} else if(part.unit == Compound_Unit::perc) {
			standard_form.magnitude -= 2*part.power;  // % = 1/100
		} else if(part.unit == Compound_Unit::ton) {
			standard_form.powers[(int)Base_Unit::g] += part.power;
			standard_form.magnitude += 6*part.power;  // ton = 10^6 g
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
			} else if(part.unit == Compound_Unit::week) {
				standard_form.powers[(int)Base_Unit::s] += part.power;
				standard_form.magnitude += 2*part.power;
				standard_form.multiplier *= pow_i<s64>(6048, part.power.nom);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled compound unit in set_standard_form().");
		}
		standard_form.magnitude += ((int)part.magnitude)*part.power;
		standard_form.reduce();
	}
}

void
Standardized_Unit::reduce() {
	while(multiplier.nom % 10 == 0) {
		magnitude = magnitude + Rational<s16>(1);
		multiplier.nom /= 10;
	}
	while(multiplier.denom % 10 == 0) {
		magnitude = magnitude - Rational<s16>(1);
		multiplier.denom /= 10;
	}
}

bool
Standardized_Unit::is_atom(Base_Unit bu) {
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx) {
		if(((int)bu != idx && powers[idx] != Rational<s16>(0)) || ((int)bu == idx && powers[idx] != Rational<s16>(1)))
			return false;
	}
	return true;
}

bool
Standardized_Unit::is_dimensionless() {
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx) {
		if(powers[idx] != Rational<s16>(0))
			return false;
	}
	return true;
}

bool
Standardized_Unit::is_fully_dimensionless() {
	if(!is_dimensionless()) return false;
	if(multiplier != Rational<s64>(1)) return false;
	if(magnitude != Rational<s16>(0)) return false;
	return true;
}

Standardized_Unit
multiply(const Standardized_Unit &a, const Standardized_Unit &b, int power) {
	Standardized_Unit result;
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		result.powers[idx] = a.powers[idx] + b.powers[idx]*power;
	result.multiplier = a.multiplier * pow_i(b.multiplier, (s64)power);
	result.magnitude  = a.magnitude + b.magnitude*power;
	result.reduce();
	return result;
}

bool
pow(const Standardized_Unit &a, Standardized_Unit &result, Rational<s16> power) {
	
	// TODO: Should in some instances still be able to resolve this power.
	if(!power.is_int() && a.multiplier != Rational<s64>(1))
		return false;
	
	result = a;
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		result.powers[idx] *= power;
	result.magnitude *= power;
	result.multiplier = pow_i<s64>(result.multiplier, power.nom);
	return true;
}

bool
match(Standardized_Unit *a, Standardized_Unit *b, double *conversion_factor) { 
	// the conversion_factor so that factor*b = a
	
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx)
		if(a->powers[idx] != b->powers[idx]) return false;
	
	// NOTE: because of the reduce() operation, two (multiplier, magnitude) pairs should be
	// identical if they correspond to the same real number. Thus the conversion_factor will
	// be identical to 1.0 if the effective multipliers are the same real number.
	*conversion_factor = 1.0;
	if(a->multiplier != b->multiplier)
		*conversion_factor = (a->multiplier / b->multiplier).to_double();
	if(a->magnitude != b->magnitude)
		*conversion_factor *= std::pow(10.0, (a->magnitude - b->magnitude).to_double());
	return true;
}

bool
match_exact(Standardized_Unit *a, Standardized_Unit *b) {
	double conversion_factor = 0.0;
	bool success = match(a, b, &conversion_factor);
	return success && (conversion_factor == 1.0);
}

bool
match_offset(Standardized_Unit *a, Standardized_Unit *b, double *conversion_offset) {
	// the conversion_offset so that  offset + b = a
	
	// TODO: This should also be able to match if the rest of the unit is the same, e.g. deg_C/m^3 -> K/m^3.
	if(a->multiplier != b->multiplier || a->magnitude != b->magnitude) return false;
	bool success = false;
	if(a->is_atom(Base_Unit::deg_c) && b->is_atom(Base_Unit::K)) {
		*conversion_offset = 273.15;
		success = true;
	} else if (a->is_atom(Base_Unit::K) && b->is_atom(Base_Unit::deg_c)) {
		*conversion_offset = -273.15;
		success = true;
	}
	if(a->multiplier != Rational<s64>(1))
		*conversion_offset *= a->multiplier.to_double();
	if(a->magnitude != Rational<s16>(0))
		*conversion_offset *= std::pow(10.0, a->magnitude.to_double());
	return success;
}

Unit_Data
multiply(const Unit_Data &a, const Unit_Data &b, int power) {
	Unit_Data result;
	result.declared_form = a.declared_form;
	result.declared_multiplier = a.declared_multiplier * pow_i<s64>(b.declared_multiplier, (s64)power);
	for(auto decl : b.declared_form) {
		decl.power *= power;
		result.declared_form.push_back(decl);
		// TODO: Not sure if we should do work to merge matching units here.
	}
	result.set_standard_form();
	return std::move(result);
}

Unit_Data
unit_of_sum(const Unit_Data &unit, const Unit_Data &ts_unit, Aggregation_Period agg_period) {
	auto result = unit;
	if(agg_period == Aggregation_Period::none)
		return result;
	
	int found_idx = -1;
	const auto &ts_part = ts_unit.declared_form[0];
	for(int idx = 0; idx < unit.declared_form.size(); ++idx) {
		const auto &part = unit.declared_form[idx];
		if(part.power == Rational<s16>(-1) && part.unit == ts_part.unit && part.magnitude == ts_part.magnitude)   // TODO: is checking the magnitude necessary?
			found_idx = idx;
	}
	if(found_idx < 0)
		return result;
	
	result.declared_form.erase(result.declared_form.begin() + found_idx);
	Declared_Unit_Part part = {
		ts_part.magnitude,
		Rational<s16>(-1),
		agg_period_to_compound_unit(agg_period)
	};
	result.declared_form.push_back(part);
	result.set_standard_form();
	
	return std::move(result);
}

void
write_utf8_superscript_char(std::ostream &ss, char c, int number = 0) {
	if(c == '-')
		ss << u8"\u207b";
	else if(c == '/')      // Trying to find a unicode symbol that looks like a forward slash in raised position, but none of them are perfect.
		ss << u8"\u2032";
		//ss << u8"\u141f";
		//ss << "´";
	else if(c == '0' && number != 0)
		ss << u8"\u2070";
	else if(c == '1' && number != 1)
		ss << u8"\u00b9";
	else if(c == '2')
		ss << u8"\u00b2";
	else if(c == '3')
		ss << u8"\u00b3";
	else if(c == '4')
		ss << u8"\u2074";
	else if(c == '5')
		ss << u8"\u2075";
	else if(c == '6')
		ss << u8"\u2076";
	else if(c == '7')
		ss << u8"\u2077";
	else if(c == '8')
		ss << u8"\u2078";
	else if(c == '9')
		ss << u8"\u2079";
}

void
write_utf8_superscript_number(std::ostream &ss, int number) {
	static char buf[32];
	//itoa(number, buf, 10);
	sprintf(buf, "%d", number);
	char *c = &buf[0];
	while(*c) {
		write_utf8_superscript_char(ss, *c, number);
		c++;
	}
}

static const char *unit_symbols[] = {
	#define COMPOUND_UNIT(handle, name) name,
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
};

std::string
Unit_Data::to_utf8() {
	
	if(declared_form.empty() && declared_multiplier != Rational<s64>(1))
		return "dimensionless";

	std::stringstream ss;
	if(declared_multiplier != Rational<s64>(1))
		ss << declared_multiplier << " ";
	int idx = 0;
	for(auto &part : declared_form) {
		int mag = part.magnitude;
		ss << get_si_prefix(mag) << unit_symbols[(int)part.unit];
		write_utf8_superscript_number(ss, part.power.nom);
		if(part.power.denom != 1) {
			write_utf8_superscript_char(ss, '/');
			write_utf8_superscript_number(ss, part.power.denom);
		}
		if(idx != declared_form.size()-1)
			ss << " ";
		
		++idx;
	}
	return ss.str();
}

static const char *unit_symbols_declared[] = {
	#define COMPOUND_UNIT(handle, name) #handle,
	#include "compound_units.incl"
	#undef COMPOUND_UNIT
};

std::string
Unit_Data::to_decl_str() {
	std::stringstream ss;
	ss << "[";
	int idx = 0;
	if(declared_multiplier != Rational<s64>(1)) {
		ss << declared_multiplier;
		if(!declared_form.empty()) ss<< ", ";
	}
	for(auto &part : declared_form) {
		// TODO: Clean up to not have the space in case the prefix or power is empty.
		if(part.magnitude != 0)
			ss << get_si_prefix(part.magnitude, true) << " ";
		ss << unit_symbols_declared[(int)part.unit];
		if(part.power != Rational<s16>(1)) {
			ss << " " << part.power;
		}
		++idx;
		if(idx != declared_form.size()) ss << ", ";
	}
	ss << "]";
	return ss.str();
}

template<typename T> void
print_latex_rational(std::stringstream &ss, Rational<T> r) {
	if(r.denom != 1) {
		if(r.nom < 0)
			ss << "-";
		ss << "\\frac{" << std::abs(r.nom) << "}{" << r.denom << "}";
	} else {
		ss << r.nom;
	}
}

std::string
Unit_Data::to_latex() {
	std::stringstream ss;
	if(declared_multiplier != Rational<s64>(1))
		print_latex_rational(ss, declared_multiplier);
	else if (declared_form.empty())
		ss << "1";
	
	for(auto &part : declared_form) {
		ss << "\\mathrm{";
		if(part.magnitude != 0)
			ss << get_si_prefix(part.magnitude, false);
		ss << unit_symbols[(int)part.unit]; //TODO: Do we need a separate LaTeX version for greek letters?
		ss << "}"; // end mathrm
		if(part.power != Rational<s16>(1)) {
			ss << "^{";
			print_latex_rational(ss, part.power);
			ss << "}";
		}
		ss << "\\,";
	}
	return ss.str();
}

std::string
Standardized_Unit::to_utf8() {
	std::stringstream ss;
	
	if(multiplier != Rational<s64>(1))
		ss << multiplier << " ";
	if(magnitude != Rational<s16>(0)) {
		ss << "10";
		if(magnitude != Rational<s16>(1)) {
			write_utf8_superscript_number(ss, magnitude.nom);
			if(magnitude.denom != 1) {
				write_utf8_superscript_char(ss, '/');
				write_utf8_superscript_number(ss, magnitude.denom);
			}
		}
	}
	
	for(int idx = 0; idx < (int)Base_Unit::max; ++idx) {
		if(powers[idx] == Rational<s16>(0)) continue;
		ss << " " << unit_symbols[idx];
		if(powers[idx] == Rational<s16>(1)) continue;
		write_utf8_superscript_number(ss, powers[idx].nom);
		if(powers[idx].denom == 1) continue;
		write_utf8_superscript_char(ss, '/');
		write_utf8_superscript_number(ss, powers[idx].denom);
	}
	
	auto result = ss.str();
	if(result.empty())
		return "1";   // or "dimensionless" ?
	
	return std::move(result);
}

void
Unit_Data::set_data(Decl_AST *decl) {
	if(decl->type != Decl_Type::unit)
		fatal_error("Received a non-unit decl to set_data().");
	declared_form.clear();
	declared_multiplier = 1;
	int idx = 0;
	for(int idx = 0; idx < decl->args.size(); ++idx) {
		auto arg = decl->args[idx];
		bool skip = false;
		if(idx == 0) {
			if(arg->chain.size() == 1 && arg->chain[0].type == Token_Type::integer) {
				skip = true;
				declared_multiplier = arg->chain[0].val_int;
			} else if (arg->chain.size() == 3 && arg->chain[0].type == Token_Type::integer 
				&& (char)arg->chain[1].type == '/' && arg->chain[2].type == Token_Type::integer) {
				skip = true;
				declared_multiplier = Rational<s64>(arg->chain[0].val_int, arg->chain[2].val_int);
			}
			if(declared_multiplier.nom < 0) {
				arg->chain[0].print_error_header();
				fatal_error("A unit can not have a negative size.");
			}
		}
		if(!skip)
			declared_form.push_back(parse_unit(&arg->chain));
	}
	set_standard_form();
}

Time_Step_Size
Unit_Data::to_time_step(bool &success) {
	Time_Step_Size ts;
	
	if(!standard_form.multiplier.is_int() || !standard_form.magnitude.is_int() || standard_form.magnitude.nom < 0 || standard_form.multiplier.nom <= 0) {
		success = false;
		return ts;
	}
	
	int n_time = 0;
	success = true;
	Base_Unit which;
	for(int base = 0; base < (int)Base_Unit::max; ++base) {
		if(!standard_form.powers[base].is_int()) { success = false; break; }
		int pow = standard_form.powers[base].nom;
		if(is_time((Base_Unit)base)) {
			if(pow == 1) {
				n_time++;
				which = (Base_Unit)base;
				if(n_time > 1) success = false;
			} else if(pow != 0)
				success = false;
		} else if (pow != 0)
			success = false;
	}
	if(n_time == 0) success = false;
	if(!success) return ts;
	ts.multiplier = standard_form.multiplier.nom;
	for(int p = 0; p < standard_form.magnitude.nom; ++p)
		ts.multiplier *= 10;
	if(which == Base_Unit::s) {
		ts.unit      = Time_Step_Size::Unit::second;
	} else if (which == Base_Unit::month) {
		ts.unit      = Time_Step_Size::Unit::month;
	} else if (which == Base_Unit::year) {
		ts.unit      = Time_Step_Size::Unit::month;
		ts.multiplier *= 12;
	} else
		fatal_error(Mobius_Error::internal, "Unhandled time step unit type in to_time_step()");
	return ts;
}