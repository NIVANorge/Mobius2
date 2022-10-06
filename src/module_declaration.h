
#ifndef MOBIUS_MODEL_BUILDER_H
#define MOBIUS_MODEL_BUILDER_H

#include "ast.h"
#include "ode_solvers.h"
#include "run_model.h"
#include "units.h"

#include <unordered_map>

struct Module_Declaration;

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

struct Mobius_Model;

Var_Location
remove_dissolved(const Var_Location &loc);

Var_Location
add_dissolved(Mobius_Model *model, const Var_Location &loc, Entity_Id quantity);

inline int
entity_id_hash(const Entity_Id &id) {
	return 97*id.module_id + id.id;
}

struct
Var_Location_Hash {
	int operator()(const Var_Location &loc) const {
		if(!is_located(loc))
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
error_print_location(Mobius_Model *model, const Var_Location &loc);
void
debug_print_location(Mobius_Model *model, const Var_Location &loc);


struct
Entity_Registration_Base {
	Decl_Type       decl_type;
	bool            has_been_declared;
	Source_Location location;            // if it has_been_declared, this should be the declaration location, otherwise it should be the location where it was last referenced.
	String_View     handle_name;
	String_View     name;
	Entity_Id       global_id;
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
	Var_Location source;
	Var_Location target;
	Math_Block_AST *code;
};

template<> struct
Entity_Registration<Reg_Type::compartment> : Entity_Registration_Base {
	// NOTE: These will only be set in the "global" module.
	std::vector<Entity_Id> index_sets; //TODO: more info about distribution?
	std::vector<Aggregation_Data> aggregations;
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
};

template<> struct
Entity_Registration<Reg_Type::par_group> : Entity_Registration_Base {
	Entity_Id              compartment;  //TODO: could also be quantity
	
	//TODO: may not be necessary to store these here since the parameters already know what group they are in? Easier this way for serialization though.
	std::vector<Entity_Id> parameters;
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
	
	Entity_Registration() : unit(invalid_entity_id), par_group(invalid_entity_id) {}
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
	
	Unit_Data data;
};

template<> struct
Entity_Registration<Reg_Type::property_or_quantity> : Entity_Registration_Base {
	//Entity_Id    unit;            //NOTE: tricky. could clash between different scopes. Better just to have it on the "has" ?
	
	// TODO: we need to store what scope this default_code was declared in so that it can be resolved in that scope.
	Math_Block_AST *default_code;
	
	Entity_Registration() : default_code(nullptr) {}
};

template<> struct
Entity_Registration<Reg_Type::has> : Entity_Registration_Base {
	Var_Location   var_location;
	Entity_Id      unit;
	//Entity_Id    conc_unit;
	
	Math_Block_AST *code;
	bool initial_is_conc;
	Math_Block_AST *initial_code;
	bool override_is_conc;
	Math_Block_AST *override_code;
};

template<> struct
Entity_Registration<Reg_Type::flux> : Entity_Registration_Base {
	Var_Location   source;
	Var_Location   target;
	bool target_was_out;           // We some times need info about if the target was re-directed by a 'to' declaration.
	Entity_Id      neighbor_target;
	
	std::vector<Var_Location> no_carry;  // Dissolved substances that should not be carried by the flux.
	
	Math_Block_AST  *code;
	
	Entity_Registration() : neighbor_target(invalid_entity_id), code(nullptr) {}
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
	Var_Location loc;
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
	Module_Version version;
	
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
		if(!is_valid(id))
			fatal_error(Mobius_Error::internal, "Invalid id passed to find_entity().");
		if(id.reg_type != reg_type)
			fatal_error(Mobius_Error::internal, "Incorrect type passed to find_entity(). Expected ", name(reg_type), ", got ", name(id.reg_type), ".");
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
	
	Entity_Id get_global(Entity_Id id) {
		Entity_Id global_id = find_entity(id)->global_id;
		if(is_valid(global_id)) return global_id;
		return id;
	}
};

template<Reg_Type reg_type> Entity_Registration<reg_type> *
Registry<reg_type>::operator[](Entity_Id id) {
	if(!is_valid(id))
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid handle.");
	else if(id.module_id != parent->module_id)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle from a different module.");
	else if(id.reg_type != reg_type)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle of a wrong type. Expected ", name(reg_type), " got ", name(id.reg_type), ".");
	else if(id.id >= registrations.size())
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a handle that was out of bounds.");
	return &registrations[id.id];
}

template<Reg_Type reg_type>
Entity_Id Registry<reg_type>::begin() { return {parent->module_id, reg_type, 0}; }

template<Reg_Type reg_type>
Entity_Id Registry<reg_type>::end()   { return {parent->module_id, reg_type, (s32)registrations.size()}; }

Var_Location
make_var_location(Mobius_Model *model, Entity_Id compartment, Entity_Id property_or_quantity);

Module_Declaration *
process_module_declaration(Module_Declaration *global_scope, s16 module_id, Decl_AST *decl);

#endif // MOBIUS_MODEL_BUILDER_H