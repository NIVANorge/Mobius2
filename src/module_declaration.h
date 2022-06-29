
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"

#include <unordered_map>

struct Module_Declaration;

enum class
Reg_Type : s16 {
	unrecognized,
	#define ENUM_VALUE(name) name,
	#include "reg_types.incl"
	#undef ENUM_VALUE
};

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

constexpr Entity_Id invalid_entity_id = {-1, Reg_Type::unrecognized, -1};

inline bool is_valid(Entity_Id id) { return id.module_id >= 0 && id.id >= 0 && id.reg_type != Reg_Type::unrecognized; }

template <typename Value_Type>
using string_map = std::unordered_map<String_View, Value_Type, String_View_Hash>;


struct
Entity_Registration_Base {
	Decl_Type       decl_type;
	bool            has_been_declared;
	Source_Location location;            // if it has_been_declared, this should be the declaration location, otherwise it should be the location where it was last referenced.
	String_View     handle_name;
	String_View     name;
};

template<Reg_Type reg_type> struct
Entity_Registration : Entity_Registration_Base {
	//TODO: delete constructor or something like that
};

template<> struct
Entity_Registration<Reg_Type::compartment> : Entity_Registration_Base {
	// no need for additional info right now..
};

template<> struct
Entity_Registration<Reg_Type::par_group> : Entity_Registration_Base {
	Entity_Id              compartment;  //TODO: could also be substance
	std::vector<Entity_Id> parameters;   //TODO: may not be necessary to store these here since the parameters already know what group they are in??
};

union
Parameter_Value {
	double    val_double;
	s64       val_int;
	u64       val_bool;
	u64       val_enum;
	Date_Time val_datetime;
	
	Parameter_Value() : val_datetime() {};
};

inline Parameter_Value
get_parameter_value(Token *token) {
	Parameter_Value result;
	if(token->type == Token_Type::real)
		result.val_double = token->double_value();
	else if(token->type == Token_Type::integer)
		result.val_int = token->val_int;
	else if(token->type == Token_Type::boolean)
		result.val_bool = token->val_bool;
	else
		fatal_error(Mobius_Error::internal, "Invalid use of get_parameter_value().");
	return result;
}

template<> struct
Entity_Registration<Reg_Type::parameter> : Entity_Registration_Base {
	Entity_Id       par_group;
	Entity_Id       unit;
	
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	
	String_View     description;
};

template<> struct
Entity_Registration<Reg_Type::unit> : Entity_Registration_Base {
	// TODO: put data here.
};

template<> struct
Entity_Registration<Reg_Type::property_or_substance> : Entity_Registration_Base {
	//NOTE: this is in practice used both for property and substance
	Entity_Id    unit;
};

enum class
Location_Type {
	nowhere, out, located,
};

struct
Value_Location {
	//TODO: this becomes more complicated with dissolved substances etc.
	Location_Type type;
	
	Entity_Id compartment;
	Entity_Id property_or_substance;
};

inline bool
operator==(const Value_Location &a, const Value_Location &b) {
	return a.type == b.type && a.compartment == b.compartment && a.property_or_substance == b.property_or_substance;
}

struct
Value_Location_Hash {
	int operator()(const Value_Location &loc) const {
		if(loc.type != Location_Type::located)
			fatal_error(Mobius_Error::internal, "Tried to look up the state variable of a non-located value.");
		// hopefully this one is ok...
		return
			 loc.compartment.module_id
		+ 23*loc.compartment.id
		+ 97*loc.property_or_substance.module_id
		+ 2237*loc.property_or_substance.id;
		// NOTE: no point using the reg_type here since they should be the same always (for now).
	}
};

template<> struct
Entity_Registration<Reg_Type::has> : Entity_Registration_Base {
	Value_Location value_location;
	Entity_Id      unit;
	//Entity_Id    conc_unit;
	//String_View 
	Math_Block_AST *code;
	Math_Block_AST *initial_code;
};

template<> struct
Entity_Registration<Reg_Type::flux> : Entity_Registration_Base {
	Value_Location   source;
	Value_Location   target;
	
	Math_Block_AST  *code;
};

struct Registry_Base {
	string_map<Entity_Id>          handle_name_to_handle;
	string_map<Entity_Id>          name_to_handle;
	Module_Declaration            *parent;
	
	virtual Entity_Id find_or_create(Token *handle_name, Token *name = nullptr, Decl_AST *declaration = nullptr);
	virtual Entity_Registration_Base *operator[](Entity_Id handle);
	
	Registry_Base(Module_Declaration *parent) : parent(parent) {}
};

template <Reg_Type reg_type> struct
Registry : Registry_Base {
	
	std::vector<Entity_Registration<reg_type>> registrations;
	
	Registry(Module_Declaration *parent) : Registry_Base(parent) {}
	
	Entity_Id
	find_or_create(Token *handle_name, Token *name = nullptr, Decl_AST *declaration = nullptr);
	
	Entity_Id
	create_compiler_internal(String_View handle_name, Decl_Type decl_type);
	
	Entity_Id
	standard_declaration(Decl_AST *decl);
	
	Entity_Registration<reg_type> *operator[](Entity_Id id);
	
	Entity_Id begin();
	Entity_Id end();
};

struct
Module_Declaration {
	String_View name;
	int major, minor, revision;
	
	s16 module_id;
	
	String_View doc_string;
	
	string_map<Entity_Id>           handles_in_scope;
	
	Registry<Reg_Type::compartment> compartments;
	Registry<Reg_Type::par_group>   par_groups;
	Registry<Reg_Type::parameter>   parameters;     // NOTE: par_real is a stand-in for all parameter declarations.
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::property_or_substance>    properties_and_substances;  // NOTE: is used for both Decl_Type::property and Decl_Type::substance.
	Registry<Reg_Type::has>         hases;
	Registry<Reg_Type::flux>        fluxes;
	
	Module_Declaration() : 
		compartments(this),
		par_groups  (this),
		parameters  (this),
		units       (this),
		properties_and_substances(this),
		hases       (this),
		fluxes      (this)
	{}
	
	Registry_Base *	registry(Reg_Type);
	
	
	Entity_Id dimensionless_unit;
};

template<Reg_Type reg_type>
Entity_Registration<reg_type> *Registry<reg_type>::operator[](Entity_Id id) {
	if(!is_valid(id) || id.id >= registrations.size() || id.module_id != parent->module_id || id.reg_type != reg_type)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
	return &registrations[id.id];
}

template<Reg_Type reg_type>
Entity_Id Registry<reg_type>::begin() { return {parent->module_id, reg_type, 0}; }

template<Reg_Type reg_type>
Entity_Id Registry<reg_type>::end()   { return {parent->module_id, reg_type, (s32)registrations.size()}; }

inline Reg_Type
get_reg_type(Decl_Type decl_type) {
	switch(decl_type) {
	#define ENUM_VALUE(decl_type, _a, reg_type) case Decl_Type::decl_type : return Reg_Type::reg_type;
	#include "decl_types.incl"
	#undef ENUM_VALUE
	}
	return Reg_Type::unrecognized;
}


inline Entity_Registration_Base *
find_entity(Module_Declaration *module, Entity_Id id) {
	return (*module->registry(id.reg_type))[id];
}

inline Entity_Id
find_handle(Module_Declaration *module, String_View handle_name) {
	Entity_Id result = invalid_entity_id;
	auto find = module->handles_in_scope.find(handle_name);
	if(find != module->handles_in_scope.end())
		result = find->second;
	return result;
}

inline Value_Location
make_value_location(Module_Declaration *module, Entity_Id compartment, Entity_Id property_or_substance) {
	Value_Location result;
	result.type = Location_Type::located;
	result.compartment = compartment;
	result.property_or_substance = property_or_substance;
	if(module->module_id != compartment.module_id || module->module_id != property_or_substance.module_id ||
		compartment.reg_type != Reg_Type::compartment || property_or_substance.reg_type != Reg_Type::property_or_substance) {
			fatal_error(Mobius_Error::internal, "Incorrect use of make_value_location().");
	}
	
	return result;
}

Module_Declaration *
process_module_declaration(s16 module_id, Decl_AST *decl);

#endif // MOBIUS_MODEL_BUILDER_H