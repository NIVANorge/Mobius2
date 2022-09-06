
#ifndef MOBIUS_COMMON_TYPES_H
#define MOBIUS_COMMON_TYPES_H

#include "mobius_common.h"
// Hmm, don't like having to import all of linear_memory just for String_View...
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
	void print_warning_header();
};

struct
Module_Version {
	int major, minor, revision;
};

struct Var_Id {
	s32 type;
	s32 id;
	
	Var_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Var_Id &operator++() { id++; return *this; }
};

inline bool
is_valid(Var_Id id) {
	return id.id >= 0;
}

constexpr Var_Id invalid_var = {-1, -1};

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
Value_Type : s32 {
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

enum class
Reg_Type : s16 {
	unrecognized,
	#define ENUM_VALUE(name) name,
	#include "reg_types.incl"
	#undef ENUM_VALUE
};

inline String_View
name(Reg_Type type) {
	#define ENUM_VALUE(name) if(type == Reg_Type::name) return #name;
	#include "reg_types.incl"
	#undef ENUM_VALUE
	return "unrecognized";
}

struct
Entity_Id {
	s16      module_id;
	Reg_Type reg_type;
	s32      id;
	
	Entity_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Entity_Id &operator++() { id++; return *this; }
};

inline bool
operator==(const Entity_Id &a, const Entity_Id &b) {
	return a.module_id == b.module_id && a.reg_type == b.reg_type && a.id == b.id;
}

inline bool
operator!=(const Entity_Id &a, const Entity_Id &b) {
	return a.module_id != b.module_id || a.id != b.id || a.reg_type != b.reg_type;
}

inline bool
operator<(const Entity_Id &a, const Entity_Id &b) {
	if(a.module_id == b.module_id) {
		if(a.reg_type == b.reg_type)
			return a.id < b.id;
		return a.reg_type < b.reg_type;
	}
	return a.module_id < b.module_id;
}

constexpr Entity_Id invalid_entity_id = {-1, Reg_Type::unrecognized, -1};

inline bool is_valid(Entity_Id id) { return id.module_id >= 0 && id.id >= 0 && id.reg_type != Reg_Type::unrecognized; }


enum class
Location_Type : s32 {
	nowhere, out, located, neighbor,
};

constexpr int max_dissolved_chain = 2;

struct
Value_Location {
	//TODO: this becomes more complicated with dissolved quantities etc.
	Location_Type type;
	s32 n_dissolved;         //NOTE: it is here for better packing.
	
	Entity_Id neighbor; // This should only be referenced if type == neighbor
	
	// These should only be referenced if type == located.
	Entity_Id compartment;
	Entity_Id property_or_quantity;
	Entity_Id dissolved_in[max_dissolved_chain];
};

inline bool
is_located(Value_Location &loc) {
	return loc.type == Location_Type::located;
}

constexpr Value_Location invalid_value_location = {Location_Type::nowhere, 0, invalid_entity_id, invalid_entity_id, invalid_entity_id};

inline bool
is_valid(Value_Location &a) { //TODO: is this needed at all?
	return (a.type != Location_Type::located) || (is_valid(a.compartment) && is_valid(a.property_or_quantity));
}

inline bool
operator==(const Value_Location &a, const Value_Location &b) {
	if(a.type != b.type) return false;
	if(a.type == Location_Type::neighbor) return a.neighbor == b.neighbor;
	if(a.type == Location_Type::located) {
		if(a.compartment != b.compartment || a.property_or_quantity != b.property_or_quantity || a.n_dissolved != b.n_dissolved) return false;
		for(int idx = 0; idx < a.n_dissolved; ++idx)
			if(a.dissolved_in[idx] != b.dissolved_in[idx]) return false;
	}
	return true;
}

#endif // MOBIUS_COMMON_TYPES_H