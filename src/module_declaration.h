
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"
#include "ode_solvers.h"
#include "run_model.h"

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

struct Mobius_Model;

Value_Location
remove_dissolved(const Value_Location &loc);

Value_Location
add_dissolved(Mobius_Model *model, const Value_Location &loc, Entity_Id quantity);

inline int
entity_id_hash(const Entity_Id &id) {
	return 97*id.module_id + id.id;
}

struct
Value_Location_Hash {
	int operator()(const Value_Location &loc) const {
		if(loc.type != Location_Type::located)
			fatal_error(Mobius_Error::internal, "Tried to hash a non-located value location.");
		// hopefully this one is ok...
		
		constexpr int mod = 10889;
		int res = entity_id_hash(loc.compartment);
		res = (res*11 + entity_id_hash(loc.property_or_quantity)) % mod;
		for(int idx = 0; idx < loc.n_dissolved; ++idx)
			res = (res*11 + entity_id_hash(loc.dissolved_in[idx])) % mod;
		
		return res;
	}
};

void
error_print_location(Mobius_Model *model, Value_Location &loc);


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

struct
Flux_Unit_Conversion_Data {
	Value_Location source;
	Value_Location target;
	Math_Block_AST *code;
};

template<> struct
Entity_Registration<Reg_Type::compartment> : Entity_Registration_Base {
	Entity_Id global_id;
	
	// NOTE: These will only be set in the "global" module.
	std::vector<Entity_Id> index_sets; //TODO: more info about distribution?
	std::vector<Aggregation_Data> aggregations; 
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
	
	Entity_Registration() : global_id(invalid_entity_id) {}
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
	
	std::vector<String_View> enum_values;
	
	String_View     description;
};

inline s64
enum_int_value(Entity_Registration<Reg_Type::parameter> *reg, String_View name) {
	auto find = std::find(reg->enum_values.begin(), reg->enum_values.end(), name);
	if(find != reg->enum_values.end())
		return (s64)(find - reg->enum_values.begin());
	return -1;
}

template<> struct
Entity_Registration<Reg_Type::unit> : Entity_Registration_Base {
	// TODO: put data here.
};

template<> struct
Entity_Registration<Reg_Type::property_or_quantity> : Entity_Registration_Base {
	Entity_Id    global_id;
	//Entity_Id    unit;            //NOTE: tricky. could clash between different scopes. Better just to have it on the "has" ?
	
	Entity_Registration() : global_id(invalid_entity_id) {}
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
	bool target_was_out;           // We some times need info about if the target was re-directed by a 'to' declaration.
	
	std::vector<Value_Location> no_carry;  // Dissolved substances that should not be carried by the flux.
	
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
	
	// TODO: may need some info about how it transforms units.
	// TODO: may need some info on expected argument types (especially for externals)
};

template<> struct
Entity_Registration<Reg_Type::index_set> : Entity_Registration_Base {
	Entity_Id neighbor_structure;   // TODO: In time we could have several, e.g. for lateral vs vertical neighbors etc.
	
	Entity_Registration() : neighbor_structure(invalid_entity_id) {}
};

enum class
Neighbor_Structure_Type {
	unrecognized = 0, directed_tree,
};
template<> struct
Entity_Registration<Reg_Type::neighbor> : Entity_Registration_Base {
	Neighbor_Structure_Type type;
	Entity_Id index_set;
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
};

template<> struct
Entity_Registration<Reg_Type::constant> : Entity_Registration_Base {
	double    value;   //Hmm, should we allow integer constants too? But that would require two declaration types.
	Entity_Id unit;
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
	find_by_name(Token *name) {
		return find_or_create(nullptr, name, nullptr);
	}
	
	Entity_Id
	find_by_name(String_View name) {
		auto find = name_to_handle.find(name);
		if(find != name_to_handle.end())
			return find->second;
		return invalid_entity_id;
	}
	
	Entity_Id
	find_by_handle_name(String_View handle_name) {
		auto find = handle_name_to_handle.find(handle_name);
		if(find != handle_name_to_handle.end())
			return find->second;
		return invalid_entity_id;
	}
	
	Entity_Id
	create_compiler_internal(String_View handle_name, Decl_Type decl_type, String_View name = "");
	
	Entity_Id
	standard_declaration(Decl_AST *decl);
	
	Entity_Registration<reg_type> *operator[](Entity_Id id);
	
	size_t count() { return registrations.size(); }
	
	Entity_Id begin();
	Entity_Id end();
};

struct Mobius_Model;

struct
Module_Declaration {	
	String_View module_name;
	int major, minor, revision;
	
	s16 module_id;
	Module_Declaration *global_scope;          //TODO: this is a bad name. It is the registration data for the model. Very little here is visible to the other modules.
	Mobius_Model       *model;
	
	String_View doc_string;
	
	String_View source_path;
	
	string_map<Entity_Id>           handles_in_scope;
	
	Registry<Reg_Type::compartment> compartments;
	Registry<Reg_Type::property_or_quantity>    properties_and_quantities;  // NOTE: is used for both Decl_Type::property and Decl_Type::quantity.
	Registry<Reg_Type::par_group>   par_groups;
	Registry<Reg_Type::parameter>   parameters;     // NOTE: is used for all parameter decl types.
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::has>         hases;
	Registry<Reg_Type::flux>        fluxes;
	Registry<Reg_Type::function>    functions;
	Registry<Reg_Type::constant>    constants;
	//TODO: it is a bit wasteful to have these here since they are only relevant for the global module.
	//   similarly, not all of the above are relevant for the global module.
	//     Could maybe template over module type, but that quickly gets gnarly.
	Registry<Reg_Type::index_set>   index_sets;
	Registry<Reg_Type::solver>      solvers;
	Registry<Reg_Type::solve>       solves;
	Registry<Reg_Type::neighbor>    neighbors;
	
	Module_Declaration() : 
		compartments(this),
		par_groups  (this),
		parameters  (this),
		units       (this),
		properties_and_quantities(this),
		hases       (this),
		fluxes      (this),
		functions   (this),
		constants   (this),
		index_sets  (this),
		solvers     (this),
		solves      (this),
		neighbors   (this),
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

Value_Location
make_value_location(Mobius_Model *model, Entity_Id compartment, Entity_Id property_or_quantity);

Module_Declaration *
process_module_declaration(Module_Declaration *global_scope, s16 module_id, Decl_AST *decl);

#endif // MOBIUS_MODEL_BUILDER_H