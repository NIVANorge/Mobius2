
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"
#include "ode_solvers.h"

#include <unordered_map>

struct Module_Declaration;

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



struct
Parameter_Value {
	union {
		double    val_real;
		s64       val_integer;
		u64       val_boolean;
		u64       val_enum;
		Date_Time val_datetime;
	};
	
	Parameter_Value() : val_datetime() {};
};

inline Parameter_Value
get_parameter_value(Token *token, Token_Type type) {
	if((type == Token_Type::integer || type == Token_Type::boolean) && token->type == Token_Type::real)
		fatal_error(Mobius_Error::internal, "Invalid use of get_parameter_value().");
	Parameter_Value result;
	if(type == Token_Type::real)
		result.val_real = token->double_value();
	else if(type == Token_Type::integer) 
		result.val_integer = token->val_int;
	else if(type == Token_Type::boolean)
		result.val_boolean = token->val_bool;
	else
		fatal_error(Mobius_Error::internal, "Invalid use of get_parameter_value().");
	return result;
}

enum class
Location_Type {
	nowhere, out, located,
};

struct
Value_Location {
	//TODO: this becomes more complicated with dissolved quantities etc.
	Location_Type type;
	
	Entity_Id compartment;
	Entity_Id property_or_quantity;
};

constexpr Value_Location invalid_value_location = {Location_Type::nowhere, invalid_entity_id, invalid_entity_id};

inline bool
is_valid(Value_Location a) {
	return (a.type != Location_Type::located) || (is_valid(a.compartment) && is_valid(a.property_or_quantity));
}

inline bool
operator==(const Value_Location &a, const Value_Location &b) {
	return a.type == b.type && a.compartment == b.compartment && a.property_or_quantity == b.property_or_quantity;
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
		+ 97*loc.property_or_quantity.module_id
		+ 2237*loc.property_or_quantity.id;
		// NOTE: no point using the reg_type here since they should be the same always (for now).
	}
};



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

struct
Aggregation_Data {
	Entity_Id to_compartment;
	Math_Block_AST *code;
};

template<> struct
Entity_Registration<Reg_Type::compartment> : Entity_Registration_Base {
	Entity_Id global_id;
	
	std::vector<Entity_Id> index_sets; //NOTE: will only be set on the global module. TODO: more info about distribution?
	std::vector<Aggregation_Data> aggregations; //NOTE: will only be set on the global module.
};

template<> struct
Entity_Registration<Reg_Type::par_group> : Entity_Registration_Base {
	Entity_Id              compartment;  //TODO: could also be quantity
	std::vector<Entity_Id> parameters;   //TODO: may not be necessary to store these here since the parameters already know what group they are in??
};

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
Entity_Registration<Reg_Type::property_or_quantity> : Entity_Registration_Base {
	Entity_Id    global_id;
	//Entity_Id    unit;            //NOTE: tricky. could clash between different scopes. Better just to have it on the "has" ?
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


enum class
Function_Type {
	decl, external, intrinsic,
};

template<> struct
Entity_Registration<Reg_Type::function> : Entity_Registration_Base {
	std::vector<String_View> args;
	
	Function_Type    fun_type;
	Math_Block_AST  *code;
	
	// TODO: need some info about how it transforms units.
	// TODO: may need some info on expected input types (especially for externals)
};

template<> struct
Entity_Registration<Reg_Type::index_set> : Entity_Registration_Base {
	//TODO: eventually index_set_type
};

template<> struct
Entity_Registration<Reg_Type::solver> : Entity_Registration_Base {
	Solver_Function *solver_fun;
	double h;
	double hmin;
};

template<> struct
Entity_Registration<Reg_Type::solve> : Entity_Registration_Base {
	Entity_Id solver;
	Value_Location loc;
	Source_Location source_location;
};


struct Registry_Base {
	string_map<Entity_Id>          handle_name_to_handle;
	string_map<Entity_Id>          name_to_handle;
	Module_Declaration            *parent;
	
	virtual Entity_Id find_or_create(Token *handle_name, Token *name = nullptr, Decl_AST *declaration = nullptr) = 0;
	virtual Entity_Registration_Base *operator[](Entity_Id handle) = 0;
	
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
	
	size_t count() { return registrations.size(); }
	
	Entity_Id begin();
	Entity_Id end();
};



struct
Module_Declaration {	
	String_View name;
	int major, minor, revision;
	
	Decl_AST *decl;
	
	s16 module_id;
	Module_Declaration *global_scope;
	
	String_View doc_string;
	
	string_map<Entity_Id>           handles_in_scope;
	
	Registry<Reg_Type::compartment> compartments;
	Registry<Reg_Type::property_or_quantity>    properties_and_quantities;  // NOTE: is used for both Decl_Type::property and Decl_Type::quantity.
	Registry<Reg_Type::par_group>   par_groups;
	Registry<Reg_Type::parameter>   parameters;     // NOTE: is used for all parameter decl types.
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::has>         hases;
	Registry<Reg_Type::flux>        fluxes;
	Registry<Reg_Type::function>    functions;
	//TODO: it is a bit wasteful to have these here since they are only relevant for the global module.
	//   similarly, not all of the above are relevant for the global module.
	//     Could maybe template over module type, but that quickly gets gnarly.
	Registry<Reg_Type::index_set>   index_sets;
	Registry<Reg_Type::solver>      solvers;
	Registry<Reg_Type::solve>       solves;
	
	Module_Declaration() : 
		compartments(this),
		par_groups  (this),
		parameters  (this),
		units       (this),
		properties_and_quantities(this),
		hases       (this),
		fluxes      (this),
		functions   (this),
		index_sets  (this),
		solvers     (this),
		solves      (this),
		global_scope(nullptr)
	{}
	
	Registry_Base *	registry(Reg_Type);
	
	//Entity_Id dimensionless_unit;
	
	Entity_Registration_Base *
	find_entity(Entity_Id id) {
		return (*registry(id.reg_type))[id];
	}
	
	template<Reg_Type reg_type> Entity_Registration<reg_type> *
	find_entity(Entity_Id id) {
		if(id.reg_type != reg_type)
			fatal_error(Mobius_Error::internal, "Incorrect type passed to find_entity().");
		return reinterpret_cast<Entity_Registration<reg_type> *>(find_entity(id));
	}
	
	inline Entity_Id
	find_handle(String_View handle_name) {
		Entity_Id result = invalid_entity_id;
		auto find = handles_in_scope.find(handle_name);
		if(find != handles_in_scope.end())
			result = find->second;
		return result;
	}
};

template<Reg_Type reg_type> Entity_Registration<reg_type> *
Registry<reg_type>::operator[](Entity_Id id) {
	if(!is_valid(id))
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
	else if(id.module_id != parent->module_id)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle from a different module.");
	else if(id.reg_type != reg_type)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle of a wrong type.");
	else if(id.id >= registrations.size())
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle that was out of bounds.");
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

//TODO: clean this up. We could just put the code for make_global directly in make_value_location, and then use that directly in module_declaration.cpp
Value_Location
make_global(Module_Declaration *module, Value_Location loc);

inline Value_Location
make_value_location(Module_Declaration *module, Entity_Id compartment, Entity_Id property_or_quantity) {
	Value_Location result;
	result.type = Location_Type::located;
	result.compartment = compartment;
	result.property_or_quantity = property_or_quantity;
	if(module->module_id != compartment.module_id || module->module_id != property_or_quantity.module_id ||
		compartment.reg_type != Reg_Type::compartment || property_or_quantity.reg_type != Reg_Type::property_or_quantity) {
			fatal_error(Mobius_Error::internal, "Incorrect use of make_value_location().");
	}
	
	return make_global(module, result);
}

Module_Declaration *
process_module_declaration(Module_Declaration *global_scope, s16 module_id, Decl_AST *decl);

#endif // MOBIUS_MODEL_BUILDER_H