
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
	if(type <= Token_Type::max_multichar) {            
		switch(type) {
			#define ENUM_VALUE(handle, name) case Token_Type::handle: return name;
			#include "token_types.incl"
			#undef ENUM_VALUE
		}
	} else {
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
	void print_log_header() const;
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

// TODO: Could probably narrow these to s16 also (though need to reflect in mobipy)
struct Var_Id {
	enum class Type : s32 {
		none = -1, state_var = 0, temp_var = 1, series = 2, additional_series = 3,
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
	unresolved = 0, none, iterate, real, integer, boolean,
};

inline bool is_value(Value_Type type) { return (s32)type >= (s32)Value_Type::real; }

inline String_View
name(Value_Type type) {
	if(type == Value_Type::unresolved) return "unresolved";
	if(type == Value_Type::none)       return "none";
	if(type == Value_Type::real)       return "real";
	if(type == Value_Type::integer)    return "integer";
	if(type == Value_Type::boolean)    return "boolean";
	if(type == Value_Type::iterate)    return "iterate";
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
	if(type == Decl_Type::par_int)  return Token_Type::integer;
	if(type == Decl_Type::par_bool) return Token_Type::boolean;
	if(type == Decl_Type::par_enum) return Token_Type::identifier;
	
	return Token_Type::unknown;
}

enum class
Variable_Type {
	parameter, series, state_var, connection_info, index_count, local,
	// Not really variables, but identifier types:
	no_override, is_at, connection,
	// "special" state variables
	#define TIME_VALUE(name, bits) time_##name,
	#include "time_values.incl"
	#undef TIME_VALUE
	time_fractional_step,
};

inline const char *
name(Variable_Type type) {
	if(type == Variable_Type::parameter) return "parameter";
	if(type == Variable_Type::series) return "series";
	if(type == Variable_Type::state_var) return "state_var";
	if(type == Variable_Type::connection_info) return "connection_info";
	if(type == Variable_Type::index_count) return "index_count";
	if(type == Variable_Type::local) return "local";
	if(type == Variable_Type::no_override) return "no_override";
	if(type == Variable_Type::connection) return "connection";
	if(type == Variable_Type::time_fractional_step) return "time.fractional_step";
	#define TIME_VALUE(name, bits) if(type == Variable_Type::time_##name) return "time."#name;
	#include "time_values.incl"
	#undef TIME_VALUE
	return "unknown_variable_type";
}

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
	s16      id;
	
	static constexpr Entity_Id invalid() { return { Reg_Type::unrecognized, -1}; }
	
	Entity_Id &operator *() { return *this; }  //trick so that it can be an iterator to itself..
	Entity_Id &operator++() { id++; return *this; }
};

constexpr Entity_Id invalid_entity_id = Entity_Id::invalid();
inline bool is_valid(Entity_Id id) { return id.id >= 0 && id.reg_type != Reg_Type::unrecognized; }

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

constexpr int max_var_loc_components = 6;

struct
Var_Location {
	enum class Type : s32 {
		out=0, located, connection,
	}   type;
	s32 n_components = 0;         //NOTE: it is here for better packing.
	Entity_Id components[max_var_loc_components];
	
	const Entity_Id &first() const { return components[0]; }
	const Entity_Id &last() const { return components[n_components-1]; }
	bool is_dissolved() const { return n_components > 2; }
};

inline bool
is_located(const Var_Location &loc) {
	return loc.type == Var_Location::Type::located;
}

constexpr Var_Location invalid_var_location = {Var_Location::Type::out, 0, invalid_entity_id, invalid_entity_id, invalid_entity_id, invalid_entity_id};

inline bool
operator==(const Var_Location &a, const Var_Location &b) {
	if(a.type != b.type) return false;
	if(a.n_components != b.n_components) return false;
	for(int idx = 0; idx < a.n_components; ++idx)
		if(a.components[idx] != b.components[idx]) return false;
	return true;
}

inline bool operator!=(const Var_Location &a, const Var_Location &b) { return !(a == b); }


struct
Restriction {
	Entity_Id        connection_id = invalid_entity_id;
	enum Type {
		none, top, bottom, above, below, specific
	}                type = none;
	
	Restriction() {}
	Restriction(Entity_Id connection_id, Type type) : connection_id(connection_id), type(type) {}
};

inline bool operator<(const Restriction &a, const Restriction &b) {
	if(a.connection_id == b.connection_id) return (int)a.type < (int)b.type;
	return a.connection_id < b.connection_id;
}

inline bool operator==(const Restriction &a, const Restriction &b) {
	return a.connection_id == b.connection_id && a.type == b.type;
}

struct
Var_Loc_Restriction {
	
	Restriction r1;
	Restriction r2;

	// NOTE: These two are only supposed to be used for tree aggregates where the source/target could be ambiguous.
	// TODO: It would be nice to be able to be able to remove these. Go over how they are used and see if not one could use a similar thing to directed_graph instead?
	//Entity_Id        source_comp = invalid_entity_id;
	//Entity_Id        target_comp = invalid_entity_id;
	
	Var_Loc_Restriction() {};
	Var_Loc_Restriction(Entity_Id connection_id, Restriction::Type type) : r1(connection_id, type) {}
};

inline bool operator==(const Var_Loc_Restriction &a, const Var_Loc_Restriction &b) {
	return a.r1 == b.r1 && a.r2 == b.r2;
}

inline bool operator<(const Var_Loc_Restriction &a, const Var_Loc_Restriction &b) {
	if(a.r1 == b.r1)
		return a.r2 < b.r2;
	return a.r1 < b.r1;
}

struct
Specific_Var_Location : Var_Location, Var_Loc_Restriction {
	Specific_Var_Location() : Var_Location(), Var_Loc_Restriction() {}
	Specific_Var_Location(const Var_Location &loc) : Var_Location(loc), Var_Loc_Restriction() {}
	Specific_Var_Location(const Var_Location &loc, const Var_Loc_Restriction &res) : Var_Location(loc), Var_Loc_Restriction(res) {}
};

inline bool operator==(const Specific_Var_Location &a, const Specific_Var_Location &b) {
	return static_cast<const Var_Location &>(a) == static_cast<const Var_Location &>(b) && static_cast<const Var_Loc_Restriction &>(a) == static_cast<const Var_Loc_Restriction &>(b);
}

struct
Index_Set_Tuple {
	
	inline bool add_bits(u64 to_add) {
		bool change = (to_add & bits) != to_add;
		bits |= to_add;
		return change;
	}
	inline bool remove_bits(u64 to_remove) {
		bool change = (to_remove & bits);
		bits &= ~to_remove;
		return change;
	}
	bool insert(Entity_Id index_set_id) { return add_bits((u64(1) << index_set_id.id));	}
	bool insert(Index_Set_Tuple &other) { return add_bits(other.bits); }
	bool remove(Entity_Id index_set_id) { return remove_bits((u64(1) << index_set_id.id)); }
	bool remove(Index_Set_Tuple &other) { return remove_bits(other.bits); }
	bool has(Entity_Id index_set_id) { return bits & (u64(1) << index_set_id.id); }
	bool has_some(Index_Set_Tuple &other) { return bits & other.bits; }
	bool has_all(Index_Set_Tuple &other) { return (bits & other.bits) == other.bits; }
	bool empty() { return bits == 0; }
	
	bool operator!=(const Index_Set_Tuple &other) const { return bits != other.bits; }
	
	struct Iterator {
		int at = 0;
		u64 bits;
		Iterator(u64 bits, int at) : bits(bits), at(at) { advance_to_next(); }
		Iterator &operator++(){ ++at; advance_to_next(); return *this; }
		Entity_Id operator*() { return Entity_Id { Reg_Type::index_set, s16(at) }; }
		bool operator!=(Iterator &other) { return at != other.at; }
		void advance_to_next() { while( !(bits & (u64(1) << at)) && at < 64 ) ++at; }
	};
	
	Iterator begin() { return Iterator(bits, 0); }
	Iterator end() { return Iterator(0, 64); }

	u64 bits = 0;
};

enum class
Aggregation_Period {
	none = 0,
	weekly,
	monthly,
	yearly,
};

#endif // MOBIUS_COMMON_TYPES_H