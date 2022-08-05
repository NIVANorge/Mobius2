
#ifndef MOBIUS_COMMON_TYPES_H
#define MOBIUS_COMMON_TYPES_H

#include "mobius_common.h"
#include "linear_memory.h"
#include "datetime.h"

enum class Token_Type : char {
	#define ENUM_VALUE(handle, name) handle,
	#include "token_types.incl"
	#undef ENUM_VALUE
	max_multichar = 20,
};

inline const char *
name(Token_Type type) {
	if(type <= Token_Type::max_multichar)                   
		switch(type) {
			#define ENUM_VALUE(handle, name) case Token_Type::handle: return name;
			#include "token_types.incl"
			#undef ENUM_VALUE
		}
	else {
		static char buf[2] = {0, 0};
		buf[0] = (char)type;
		return buf;
	}
	return "";
}

struct
Source_Location {
	String_View filename;
	s32 line, column;
	
	void print_error();
	void print_error_header();
};

struct Var_Id {
	s32 id;
	
	Var_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Var_Id &operator++() { id++; return *this; }
};

inline bool
is_valid(Var_Id id) {
	return id.id >= 0;
}

constexpr Var_Id invalid_var = {-1};

inline bool
operator==(const Var_Id &a, const Var_Id &b) { return a.id == b.id; }
inline bool
operator!=(const Var_Id &a, const Var_Id &b) { return a.id != b.id; }
inline bool
operator<(const Var_Id &a, const Var_Id &b) { return a.id < b.id; }


enum class
Decl_Type {
	unrecognized,
	#define ENUM_VALUE(name, body_type, _) name,
	#include "decl_types.incl"
	#undef ENUM_VALUE
};

inline const char *
name(Decl_Type type) {
	#define ENUM_VALUE(name, body_type, _) if(type == Decl_Type::name) return #name;
	#include "decl_types.incl"
	#undef ENUM_VALUE
	return "unrecognized";
}

enum class
Value_Type {
	unresolved = 0, none, real, integer, boolean,    // NOTE: enum would resolve to bool.
};

inline String_View
name(Value_Type type) {
	if(type == Value_Type::unresolved) return "unresolved";
	if(type == Value_Type::none)       return "none";
	if(type == Value_Type::real)       return "real";
	if(type == Value_Type::integer)    return "integer";
	if(type == Value_Type::boolean)    return "boolean";
	return "unresolved";
}

inline Value_Type
get_value_type(Decl_Type decl_type) {
	if(decl_type == Decl_Type::par_real) return Value_Type::real;
	if(decl_type == Decl_Type::par_int)  return Value_Type::integer;
	if(decl_type == Decl_Type::par_bool) return Value_Type::boolean;
	
	return Value_Type::unresolved;
}

inline Value_Type
get_value_type(Token_Type type) {
	if(type == Token_Type::real) return Value_Type::real;
	else if(type == Token_Type::integer) return Value_Type::integer;
	else if(type == Token_Type::boolean) return Value_Type::boolean;
	
	return Value_Type::unresolved;
}

inline Token_Type
get_token_type(Decl_Type type) {
	if(type == Decl_Type::par_real) return Token_Type::real;
	if(type == Decl_Type::par_int) return Token_Type::integer;
	if(type == Decl_Type::par_bool) return Token_Type::boolean;
	if(type == Decl_Type::par_enum) return Token_Type::identifier;
	
	return Token_Type::unknown;
}

enum class
Variable_Type {
	parameter, series, state_var, neighbor_info, local, 
	// "special" state variables
	#define TIME_VALUE(name, bits, pos) time_##name,
	#include "time_values.incl"
	#undef TIME_VALUE
	// Maybe also computed_parameter eventually.
};

struct
Parameter_Value {
	union {
		double    val_real;
		s64       val_integer;
		u64       val_boolean;
		Date_Time val_datetime;
	};
	
	Parameter_Value() : val_datetime() {};
};


#endif // MOBIUS_COMMON_TYPES_H