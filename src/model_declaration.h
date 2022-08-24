

#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

#include "module_declaration.h"
#include "function_tree.h"

#include <set>

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
	} flags;
	
	//TODO: could probably combine some members of this struct in a union. They are not all going to be relevant at the same time.
	
	String_View name;

	Entity_Id entity_id;  // This is the ID of the declaration (if the variable is not auto-generated), either has(...) or flux(...)
	
	Entity_Id neighbor; // For a flux that points at a neighbor.
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Value_Location loc1;
	Value_Location loc2;
	
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
	Math_Expr_FT *initial_function_tree;
	Math_Expr_FT *aggregation_weight_tree;
	Math_Expr_FT *unit_conversion_tree;
	Math_Expr_FT *override_conc_tree;
	
	State_Variable() : function_tree(nullptr), initial_function_tree(nullptr), aggregation_weight_tree(nullptr), unit_conversion_tree(nullptr), flags(f_none), agg(invalid_var), neighbor(invalid_entity_id), neighbor_agg(invalid_var), dissolved_conc(invalid_var), dissolved_flux(invalid_var) {};
};

struct Var_Registry {
	std::vector<State_Variable> vars;
	std::unordered_map<Value_Location, Var_Id, Value_Location_Hash> location_to_id;
	string_map<std::set<Var_Id>>                                    name_to_id;
	
	State_Variable *operator[](Var_Id id) {
		if(!is_valid(id) || id.id >= vars.size())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid id.");
		return &vars[id.id];
	}
	
	Var_Id operator[](Value_Location &loc) {
		if(!is_valid(loc) || !is_located(loc))
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid location.");
		auto find = location_to_id.find(loc);
		if(find == location_to_id.end())
			return invalid_var;
		return find->second;
	}
	
	const std::set<Var_Id> &operator[](String_View name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			return {};
			//fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid name \"", name, "\".");
		return find->second;
	}
	
	Var_Id register_var(State_Variable var, Value_Location loc) {
		if(is_valid(loc) && is_located(loc)) {
			Var_Id id = (*this)[loc];
			if(is_valid(id))
				fatal_error(Mobius_Error::internal, "Re-registering a variable."); //TODO: hmm, this only catches cases where the location was valid.
		}
		
		vars.push_back(var);
		Var_Id id = {(s32)vars.size()-1};
		if(is_valid(loc) && is_located(loc))
			location_to_id[loc] = id;
		name_to_id[var.name].insert(id);
		return id;
	}
	
	Var_Id begin() { return {0}; }
	Var_Id end()   { return {(s32)vars.size()}; }
	size_t count() { return vars.size(); }
};


struct
Mobius_Model {
	
	String_View model_name;
	String_View doc_string;
	
	
	Mobius_Model() : allocator(1024*4) {}    //NOTE: for now, the allocator is just to store / build string names..
	
	// TODO: destructor.
	
	void compose();
	
	Entity_Registration_Base *
	find_entity(Entity_Id id) {
		if(id.module_id < 0 || id.module_id >= (s16)modules.size())
			fatal_error(Mobius_Error::internal, "find_entity() with invalid module id.");
		return modules[id.module_id]->find_entity(id);
	}
	
	template<Reg_Type reg_type> Entity_Registration<reg_type> *
	find_entity(Entity_Id id) {
		if(id.module_id < 0 || id.module_id >= (s16)modules.size())
			fatal_error(Mobius_Error::internal, "find_entity() with invalid module id.");
		return modules[id.module_id]->find_entity<reg_type>(id);
	}
	
	s16 load_module(String_View file_name, String_View module_name);
	
	bool is_composed = false;
	
	File_Data_Handler file_handler;
	string_map<string_map<Decl_AST *>> parsed_decls;
	
	Linear_Allocator allocator;
	
	std::vector<Module_Declaration *> modules;
	//note: it is a bit annoying that we can't reuse Registry or Var_Registry for this, but it would also be too gnarly to factor out any more functionality from those, I think.
	string_map<s16> module_ids;  // Maps the handle name to the id.
	
	Var_Registry state_vars;
	Var_Registry series;
};

template<Reg_Type reg_type> Entity_Id
process_declaration(Mobius_Model *model, Decl_AST *decl);

Mobius_Model *
load_model(String_View file_name);

#endif // MOBIUS_MODEL_DECLARATION_H