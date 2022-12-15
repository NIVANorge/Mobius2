
#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

// NOTE: this is a work-in-progress replacement for the module declaration and scope system.

#include <string>
#include <set>
#include <unordered_set>

#include "ast.h"
#include "ode_solvers.h"
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


// REFACTOR: It doesn't look like there's a point in having module and library be separate reg types (?)

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
	bool           has_been_processed;
	bool           is_being_processed;
	std::string    doc_string;
	std::string    normalized_path;
	
	Entity_Registration() : decl(nullptr), has_been_processed(false), is_being_processed(false) {}
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
Entity_Registration<Reg_Type::component> : Entity_Registration_Base {
	//Entity_Id    unit;            //NOTE: tricky. could clash between different scopes. Better just to have it on the "has" ?
	
	// For compartments:
	std::vector<Aggregation_Data> aggregations;
	std::vector<Flux_Unit_Conversion_Data> unit_convs;
	
	// For compartments and quantities:
	std::vector<Entity_Id> index_sets; //TODO: more info about distribution?
	
	// For properties:
	Math_Block_AST *default_code;
	Entity_Id code_scope;  // NOTE: module id where default code was provided.
	
	Entity_Registration() : default_code(nullptr) {}
};

template<> struct
Entity_Registration<Reg_Type::par_group> : Entity_Registration_Base {
	Entity_Id              component;
	
	//TODO: may not be necessary to store these here since the parameters already know what group they are in? Easier this way for serialization though.
	std::vector<Entity_Id> parameters;
	
	Entity_Registration() : component(invalid_entity_id) {}
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
	std::string     symbol;
	
	Entity_Registration() : unit(invalid_entity_id), par_group(invalid_entity_id) {}
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
	Entity_Id      connection_target;
	
	std::vector<Var_Location> no_carry;  // Dissolved substances that should not be carried by the flux.
	bool no_carry_by_default;
	
	Math_Block_AST  *code;
	Entity_Id        code_scope;
	
	Entity_Registration() : connection_target(invalid_entity_id), code(nullptr), no_carry_by_default(false) {}
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
	//Entity_Id connection_structure;   // TODO: In time we could have several, e.g. for lateral vs vertical connections etc.
	
	Entity_Registration() {};//: connection_structure(invalid_entity_id) {}
};

enum class
Connection_Structure_Type {
	unrecognized = 0, directed_tree,
};

template<> struct
Entity_Registration<Reg_Type::connection> : Entity_Registration_Base {
	Connection_Structure_Type type;
	// TODO: This should eventually also have data about the regex.
	std::vector<Entity_Id> compartments;
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

struct
Mobius_Model {
	std::string model_name;
	std::string doc_string;
	
	std::string  path;
	
	Registry<Reg_Type::module>      modules;
	Registry<Reg_Type::library>     libraries;
	Registry<Reg_Type::unit>        units;
	Registry<Reg_Type::par_group>   par_groups;
	Registry<Reg_Type::parameter>   parameters;
	Registry<Reg_Type::function>    functions;
	Registry<Reg_Type::constant>    constants;
	Registry<Reg_Type::component>   components;  // compartment, quantity, property
	Registry<Reg_Type::has>         hases;
	Registry<Reg_Type::flux>        fluxes;
	Registry<Reg_Type::index_set>   index_sets;
	Registry<Reg_Type::solver>      solvers;
	Registry<Reg_Type::solve>       solves;
	Registry<Reg_Type::connection>  connections;
	
	Decl_Scope model_decl_scope;
	Decl_Scope global_scope;
	
	Registry_Base *
	registry(Reg_Type reg_type);
	
	Entity_Registration_Base *
	find_entity(Entity_Id id) { return (*registry(id.reg_type))[id]; }
	
	Decl_Scope *
	get_scope(Entity_Id id);
	
	File_Data_Handler file_handler;
	std::unordered_map<std::string, std::unordered_map<std::string, Entity_Id>> parsed_decls;
	Decl_AST *main_decl = nullptr;
	
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

//TODO: Does this function need to be in this header?
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