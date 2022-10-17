
#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

// NOTE: this is a work-in-progress replacement for the module declaration and scope system.

#include <string>
#include <set>
#include <unordered_set>

#include "ast.h"
#include "ode_solvers.h"
//#include "run_model.h"
#include "units.h"

struct
Scope_Entity {
	std::string handle;
	Entity_Id id  = invalid_entity_id;
	bool external = false;
	Source_Location source_loc;
};

struct Mobius_Model;

struct
Decl_Scope {
	struct Entity_Id_Hash {
		int operator()(const Entity_Id &id) const { return 97*(int)id.reg_type + id.id; }
	};
	
	std::unordered_map<std::string, Scope_Entity>                  visible_entities;
	std::unordered_map<Entity_Id, std::string, Entity_Id_Hash>     handles;
	std::unordered_set<Entity_Id, Entity_Id_Hash>                  all_ids;   //TODO: it is annoying to have this extra set just for has() to work.
	
	Entity_Id parent_id = invalid_entity_id; // Id of module or library this is the scope of. Invalid if it is the global or model scope.
	
	void add_local(const std::string &handle, Source_Location source_loc, Entity_Id id);
	void import(const Decl_Scope &other, Source_Location *import_loc = nullptr);
	void check_for_missing_decls(Mobius_Model *model);
	
	Scope_Entity *operator[](const std::string &handle) {
		auto find = visible_entities.find(handle);
		if(find != visible_entities.end())
			return &find->second;
		return nullptr;
	}
	
	const std::string& operator[](Entity_Id id) {
		auto find = handles.find(id);
		if(find == handles.end())
			fatal_error(Mobius_Error::internal, "Attempt to look up handle name of an entity id in a scope where it was not declared.");
		return find->second;
	}
	
	bool has(Entity_Id id) { return all_ids.find(id) != all_ids.end(); }
};


struct
Entity_Registration_Base {
	Decl_Type       decl_type;
	Source_Location source_loc;         // if it has_been_declared, this should be the declaration location. Will not always be canonical for all entity types (some can be redeclared)
	std::string     name;
	bool            has_been_declared = false;
};

template<Reg_Type reg_type> struct
Entity_Registration : Entity_Registration_Base {
	Entity_Registration() = delete;  // NOTE: prevent instantiation of the completely generic version.
};

template<> struct
Entity_Registration<Reg_Type::module> : Entity_Registration_Base {
	Module_Version version;
	Decl_Scope     scope;
	Decl_AST      *decl;
	bool           has_been_processed;
	std::string    doc_string;
	std::string    normalized_path;
	
	Entity_Registration() : decl(nullptr), has_been_processed(false) {}
};

template<> struct
Entity_Registration<Reg_Type::library> : Entity_Registration_Base {
	Decl_Scope     scope;
	Decl_AST      *decl;
	bool           has_been_processed ;
	std::string    doc_string;
	std::string    normalized_path;
	
	Entity_Registration() : decl(nullptr), has_been_processed(false) {}
};

struct
Aggregation_Data {
	Entity_Id to_compartment;
	Math_Block_AST *code;
	Entity_Id       code_scope;
};

struct
Flux_Unit_Conversion_Data {
	Var_Location source;
	Var_Location target;
	Math_Block_AST *code;
	Entity_Id       code_scope;
};

template<> struct
Entity_Registration<Reg_Type::compartment> : Entity_Registration_Base {
	std::vector<Entity_Id> index_sets; //TODO: more info about distribution?
	std::vector<Aggregation_Data> aggregations;
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
};

template<> struct
Entity_Registration<Reg_Type::par_group> : Entity_Registration_Base {
	Entity_Id              compartment;  //TODO: could also be quantity
	
	//TODO: may not be necessary to store these here since the parameters already know what group they are in? Easier this way for serialization though.
	std::vector<Entity_Id> parameters;
	
	Entity_Registration() : compartment(invalid_entity_id) {}
};

template<> struct
Entity_Registration<Reg_Type::parameter> : Entity_Registration_Base {
	Entity_Id       par_group;
	Entity_Id       unit;
	
	Parameter_Value default_val;
	Parameter_Value min_val;
	Parameter_Value max_val;
	
	std::vector<std::string> enum_values;
	
	std::string     description;
	
	Entity_Registration() : unit(invalid_entity_id), par_group(invalid_entity_id) {}
};

template<> struct
Entity_Registration<Reg_Type::property_or_quantity> : Entity_Registration_Base {
	//Entity_Id    unit;            //NOTE: tricky. could clash between different scopes. Better just to have it on the "has" ?
	
	Math_Block_AST *default_code;
	Entity_Id code_scope;  // NOTE: module id where default code was provided.
	
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
	Entity_Id       code_scope;
};

template<> struct
Entity_Registration<Reg_Type::flux> : Entity_Registration_Base {
	Var_Location   source;
	Var_Location   target;
	bool target_was_out;           // We some times need info about if the target was re-directed by a 'to' declaration.
	Entity_Id      neighbor_target;
	
	std::vector<Var_Location> no_carry;  // Dissolved substances that should not be carried by the flux.
	
	Math_Block_AST  *code;
	Entity_Id        code_scope;
	
	Entity_Registration() : neighbor_target(invalid_entity_id), code(nullptr) {}
};

enum class
Function_Type {
	decl, external, intrinsic,
};

template<> struct
Entity_Registration<Reg_Type::function> : Entity_Registration_Base {
	std::vector<std::string> args;
	
	Function_Type    fun_type;
	Math_Block_AST  *code;
	Entity_Id        code_scope; // id of where the code was provided.
	
	// TODO: may need some info about how it transforms units.
	// TODO: may need some info on expected argument types (especially for externals)
	Entity_Registration() : code(nullptr) {}
};

template<> struct
Entity_Registration<Reg_Type::constant> : Entity_Registration_Base {
	double    value;   //Hmm, should we allow integer constants too? But that would require two declaration types.
	Entity_Id unit;
	
	Entity_Registration() {}
};

template<> struct
Entity_Registration<Reg_Type::unit> : Entity_Registration_Base {
	Unit_Data data;
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

struct
Registry_Base {
	std::unordered_map<std::string, Entity_Id>          name_to_id;
	
	virtual Entity_Id find_or_create(Token *handle = nullptr, Decl_Scope *scope = nullptr, Token *name = nullptr, Decl_AST *declaration = nullptr) = 0;
	virtual Entity_Registration_Base *operator[](Entity_Id id) = 0;
	
	Entity_Id
	find_by_name(const std::string &name) {
		auto find = name_to_id.find(name);
		if(find != name_to_id.end())
			return find->second;
		return invalid_entity_id;
	}
};

template <Reg_Type reg_type> struct
Registry : Registry_Base {
	std::vector<Entity_Registration<reg_type>> registrations;
	
	Entity_Id
	find_or_create(Token *handle = nullptr, Decl_Scope *scope = nullptr, Token *name = nullptr, Decl_AST *declaration = nullptr);
	
	Entity_Id
	standard_declaration(Decl_Scope *decl_scope, Decl_AST *decl);
	
	Entity_Id
	create_internal(const std::string &handle, Decl_Scope *scope, const std::string &name, Decl_Type decl_type);
	
	Entity_Registration<reg_type> *operator[](Entity_Id id);
	
	s64 count() { return (s64)registrations.size(); }
	
	Entity_Id begin() { return { reg_type, 0 }; }
	Entity_Id end() { return { reg_type, (s32)registrations.size() }; }
};

enum
Dependency_Type : u32 {
	dep_type_none             = 0x0,
	dep_type_earlier_step     = 0x1,
};

struct
State_Var_Dependency {
	Var_Id            var_id;
	Dependency_Type   type;
};

inline bool operator<(const State_Var_Dependency &a, const State_Var_Dependency &b) { if(a.var_id < b.var_id) return true; return (u32)a.type < (u32)b.type; }

struct
Dependency_Set {
	std::set<Entity_Id>             on_parameter;
	std::set<Var_Id>                on_series;
	std::set<State_Var_Dependency>  on_state_var;
};

// TODO; Hmm, It is kind of weird that we use the same State_Variable struct both for series and state variables. There is also no guard against using a var_id belonging to a series to look up a state variable or vice versa.

struct Math_Expr_FT;

struct
Var_Location_Hash {
	int operator()(const Var_Location &loc) const {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to hash a non-located value location.");
		
		// hopefully this one is ok...
		constexpr int mod = 10889;
		int res = loc.compartment.id;
		res = (res*11 + loc.property_or_quantity.id) % mod;
		for(int idx = 0; idx < loc.n_dissolved; ++idx)
			res = (res*11 + loc.dissolved_in[idx].id) % mod;
		
		return res;
	}
};

struct
State_Variable {
	Decl_Type type; //either flux, quantity or property
	
	enum Flags {
		f_none                = 0x00,
		f_in_flux             = 0x01,
		f_in_flux_neighbor    = 0x02,
		f_is_aggregate        = 0x04, 
		f_has_aggregate       = 0x08,
		f_clear_series_to_nan = 0x10,
		f_dissolved_flux      = 0x20,
		f_dissolved_conc      = 0x40,
		f_invalid             = 0x1000,
	} flags;
	
	//TODO: could probably combine some members of this struct in a union. They are not all going to be relevant at the same time.
	
	std::string name;

	Entity_Id entity_id;  // This is the ID of the declaration (if the variable is not auto-generated), either has(...) or flux(...)
	
	Unit_Data unit; //NOTE: this can't just be an Entity_Id, because we need to be able to generate units for these.
	
	Entity_Id neighbor; // For a flux that points at a neighbor.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Var_Location   loc1;
	Var_Location   loc2;
	
	// if f_is_aggregate, this is what it aggregates. if f_has_aggregate, this is who aggregates it.
	Var_Id         agg;
	Entity_Id      agg_to_compartment;
	
	// if f_in_flux is set (this is the aggregation variable for the in fluxes), agg points at the quantity that is the target of the fluxes.
	Var_Id         in_flux_target;
	
	// If this is the target variable of a neighbor flux, neighbor_agg points to the aggregation variable for the neighbor flux.
	// If this is the aggregate ( f_in_flux_neighbor is set ), neighbor_agg points to the target of the neighbor flux(es) (which is the same as the source).
	Var_Id         neighbor_agg;
	
	// If this is a generated flux for a dissolved quantity (f_dissolved_flux is set), dissolved_conc is the respective generated conc of the quantity. dissolved_flux is the flux of the quantity that this one is dissolved in.
	// If this is the generated conc (f_dissolved_conc is set), dissolved_conc is the variable for the mass of the quantity.
	// If none of the flags are set and this is the mass of the quantity, dissolved_conc also points to the conc.
	Var_Id         dissolved_conc;
	Var_Id         dissolved_flux;
	
	Math_Expr_FT *function_tree;
	bool initial_is_conc;
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	bool override_is_conc;
	Math_Expr_FT *override_tree;
	
	State_Variable() : function_tree(nullptr), initial_function_tree(nullptr), initial_is_conc(false), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), override_tree(nullptr), override_is_conc(false), flags(f_none), agg(invalid_var), neighbor(invalid_entity_id), neighbor_agg(invalid_var), dissolved_conc(invalid_var), dissolved_flux(invalid_var) {};
};

template <Var_Id::Type var_type>
struct Var_Registry {
	std::vector<State_Variable> vars;
	std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> location_to_id;
	std::unordered_map<std::string, std::set<Var_Id>>           name_to_id;
	
	State_Variable *operator[](Var_Id id) {
		if(id.type != var_type)
			fatal_error(Mobius_Error::internal, "Tried to look up a variable of wrong type.");
		if(!is_valid(id) || id.id >= vars.size())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid id.");
		return &vars[id.id];
	}
	
	Var_Id operator[](const Var_Location &loc) {
		if(!is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using a non-located location.");
		auto find = location_to_id.find(loc);
		if(find == location_to_id.end())
			return invalid_var;
		return find->second;
	}
	
	//NOTE: wanted this to return a reference, but then it can't return {} when something is not found.
	const std::set<Var_Id> operator[](std::string name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			return {};
			//fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid name \"", name, "\".");
		return find->second;
	}
	
	Var_Id register_var(State_Variable var, Var_Location loc) {
		if(is_located(loc)) {
			Var_Id id = (*this)[loc];
			if(is_valid(id))
				fatal_error(Mobius_Error::internal, "Re-registering a variable."); //TODO: hmm, this only catches cases where the location was valid.
		}
		
		vars.push_back(var);
		Var_Id id = {var_type, (s32)vars.size()-1};
		if(is_located(loc))
			location_to_id[loc] = id;
		name_to_id[var.name].insert(id);
		return id;
	}
	
	Var_Id begin() { return {var_type, 0}; }
	Var_Id end()   { return {var_type, (s32)vars.size()}; }
	size_t count() { return vars.size(); }
};

struct
Mobius_Model {
	std::string model_name;
	std::string doc_string;
	
	std::string  path;
	
	Registry<Reg_Type::module>      modules;
	Registry<Reg_Type::library>     libraries;
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::compartment> compartments;
	Registry<Reg_Type::par_group>   par_groups;
	Registry<Reg_Type::parameter>   parameters;
	Registry<Reg_Type::function>    functions;
	Registry<Reg_Type::constant>    constants;
	Registry<Reg_Type::property_or_quantity>    properties_and_quantities;
	Registry<Reg_Type::has>         hases;
	Registry<Reg_Type::flux>        fluxes;
	Registry<Reg_Type::index_set>   index_sets;
	Registry<Reg_Type::solver>      solvers;
	Registry<Reg_Type::solve>       solves;
	Registry<Reg_Type::neighbor>    neighbors;
	
	Decl_Scope model_decl_scope;
	Decl_Scope global_scope;
	bool is_composed = false;
	
	Registry_Base *
	registry(Reg_Type reg_type);
	
	Entity_Registration_Base *
	find_entity(Entity_Id id) { return (*registry(id.reg_type))[id]; }
	
	Decl_Scope *
	get_scope(Entity_Id id);
	
	// TODO: free file_handler, all Decl_AST * stored in what is pointed to by parsed_decls, and main_decl.
	//     that will free all ASTs, all others are sub-trees of those.
	File_Data_Handler file_handler;
	std::unordered_map<std::string, std::unordered_map<std::string, Entity_Id>> parsed_decls;
	Decl_AST *main_decl = nullptr;
	
	Var_Registry<Var_Id::Type::state_var> state_vars;
	Var_Registry<Var_Id::Type::series>    series;
	
	void compose();
	
	template<Reg_Type reg_type> struct
	By_Scope {
		By_Scope(Decl_Scope *scope) : scope(scope) {
			end_it.scope = scope;
			end_it.it = scope->all_ids.end();
		}
		
		struct
		Scope_It {
			std::unordered_set<Entity_Id>::iterator it;
			Decl_Scope *scope;
			Scope_It &operator++() {
				do it++; while(it != scope->all_ids.end() && (it->reg_type != reg_type));
				return *this;
			}
			Entity_Id operator*() { return *it; }
			bool operator!=(const Scope_It &other) { return it != other.it; }
		};
		
		Scope_It end_it;
		Decl_Scope *scope;
		
		Scope_It begin() {
			Scope_It it;
			it.scope = scope;
			it.it = scope->all_ids.begin();
			while((it.it->reg_type != reg_type) && it != end_it)
				++it.it;
			return it;
		}
		Scope_It end() { return end_it; }
	};
	
	template<Reg_Type reg_type> By_Scope<reg_type>
	by_scope(Entity_Id scope_id) { return By_Scope<reg_type>(get_scope(scope_id)); }
};

Mobius_Model *
load_model(String_View file_name);

template<Reg_Type reg_type> Entity_Registration<reg_type> *
Registry<reg_type>::operator[](Entity_Id id) {
	if(!is_valid(id))
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an invalid id.");
	else if(id.reg_type != reg_type)
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using an id of a wrong type. Expected ", name(reg_type), " got ", name(id.reg_type), ".");
	else if(id.id >= registrations.size())
		fatal_error(Mobius_Error::internal, "Tried to look up an entity using a id that was out of bounds.");
	return &registrations[id.id];
}

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

inline s64
enum_int_value(Entity_Registration<Reg_Type::parameter> *reg, const std::string &name) {
	auto find = std::find(reg->enum_values.begin(), reg->enum_values.end(), name);
	if(find != reg->enum_values.end())
		return (s64)(find - reg->enum_values.begin());
	return -1;
}

// TODO: these could be moved to common_types.h (along with impl.)
Var_Location
remove_dissolved(const Var_Location &loc);

Var_Location
add_dissolved(const Var_Location &loc, Entity_Id quantity);

void
error_print_location(Decl_Scope *scope, const Var_Location &loc);
void
debug_print_location(Decl_Scope *scope, const Var_Location &loc);


#endif // MOBIUS_MODEL_DECLARATION_H