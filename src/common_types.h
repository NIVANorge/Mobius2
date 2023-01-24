
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
	enum class Type : s16 {
		internal = 0, text_file, spreadsheet,
	} type;
	s16 tab;
	s32 line, column;
	
	void print_error() const;
	void print_error_header(Mobius_Error type = Mobius_Error::parsing) const;
	void print_warning_header() const;
};

char *
col_row_to_cell(int col, int row, char *buf);


struct
Module_Version {
	int major, minor, revision;
};

inline bool operator<=(const Module_Version &a, const Module_Version &b) {
	if(a.major < b.major) return true;
	if(a.major == b.major) {
		if(a.minor < b.minor) return true;
		if(a.minor == b.minor)
			return a.revision <= b.revision;
	}
	return false;
}

inline bool operator<(const Module_Version &a, const Module_Version &b) {
	if(a.major < b.major) return true;
	if(a.major == b.major) {
		if(a.minor < b.minor) return true;
		if(a.minor == b.minor)
			return a.revision < b.revision;
	}
	return false;
}

struct Var_Id {
	enum class Type : s32 {
		none = -1, state_var, series, additional_series
	} type;
	s32 id;
	
	Var_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Var_Id &operator++() { id++; return *this; }
};

inline bool
is_valid(Var_Id id) {
	return id.id >= 0;
}

constexpr Var_Id invalid_var = { Var_Id::Type::none, -1 };

inline bool
operator==(const Var_Id &a, const Var_Id &b) { return a.type == b.type && a.id == b.id; }
inline bool
operator!=(const Var_Id &a, const Var_Id &b) { return a.type != b.type || a.id != b.id; }
inline bool
operator<(const Var_Id &a, const Var_Id &b) { return a.id < b.id; } // Ideally this only gets called on ids with the same type..


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
	parameter, series, state_var, connection_info, index_count, local, no_override, connection,
	// "special" state variables
	#define TIME_VALUE(name, bits) time_##name,
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

inline Reg_Type
get_reg_type(Decl_Type decl_type) {
	switch(decl_type) {
	#define ENUM_VALUE(decl_type, _a, reg_type) case Decl_Type::decl_type : return Reg_Type::reg_type;
	#include "decl_types.incl"
	#undef ENUM_VALUE
	}
	return Reg_Type::unrecognized;
}

struct
Entity_Id {
	Reg_Type reg_type;
	s32      id;			// This could probably be s16 (You won't have more than 32k different entitities of the same type (and we could detect it if it happens)). Needs to be reflected correctly in mobipy.
	
	Entity_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Entity_Id &operator++() { id++; return *this; }
};

inline bool
operator==(const Entity_Id &a, const Entity_Id &b) {
	return a.reg_type == b.reg_type && a.id == b.id;
}

inline bool
operator!=(const Entity_Id &a, const Entity_Id &b) {
	return a.id != b.id || a.reg_type != b.reg_type;
}

inline bool
operator<(const Entity_Id &a, const Entity_Id &b) {
	if(a.reg_type == b.reg_type)
		return a.id < b.id;
	return a.reg_type < b.reg_type;
}

constexpr Entity_Id invalid_entity_id = {Reg_Type::unrecognized, -1};

inline bool is_valid(Entity_Id id) { return id.id >= 0 && id.reg_type != Reg_Type::unrecognized; }

//constexpr int max_dissolved_chain = 2;

constexpr int max_var_loc_components = 4;

struct
Var_Location {
	enum class Type : s32 {
		nowhere=0, out, located,
	}   type;
	s32 n_components;         //NOTE: it is here for better packing.
	Entity_Id components[max_var_loc_components];
	
	const Entity_Id &first() const { return components[0]; }
	const Entity_Id &last() const { return components[n_components-1]; }
	bool is_dissolved() const { return n_components > 2; }
};

inline bool
is_located(const Var_Location &loc) {
	return loc.type == Var_Location::Type::located;
}

constexpr Var_Location invalid_var_location = {Var_Location::Type::nowhere, 0, invalid_entity_id, invalid_entity_id, invalid_entity_id, invalid_entity_id};

inline bool
operator==(const Var_Location &a, const Var_Location &b) {
	if(a.type != b.type) return false;
	if(a.n_components != b.n_components) return false;
	for(int idx = 0; idx < a.n_components; ++idx)
		if(a.components[idx] != b.components[idx]) return false;
	return true;
}

inline bool operator!=(const Var_Location &a, const Var_Location &b) { return !(a == b); }

struct Index_T {
	Entity_Id index_set;
	s32       index;
	
	Index_T& operator++() { index++; return *this; }
};

//TODO: should we do sanity check on the index_set in the order comparison operators?
inline bool operator<(const Index_T &a, const Index_T &b) {	return a.index < b.index; }
inline bool operator>=(const Index_T &a, const Index_T &b) { return a.index >= b.index; }
inline bool operator==(const Index_T &a, const Index_T &b) { return a.index_set == b.index_set && a.index == b.index; }
inline bool operator!=(const Index_T &a, const Index_T &b) { return a.index_set != b.index_set || a.index != b.index; }

constexpr Index_T invalid_index = {invalid_entity_id, -1};

inline bool
is_valid(const Index_T &index) { return is_valid(index.index_set) && index.index >= 0; }


#endif // MOBIUS_COMMON_TYPES_H