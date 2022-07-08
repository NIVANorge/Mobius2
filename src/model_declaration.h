

#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

#include "module_declaration.h"
#include "function_tree.h"

#include <set>

struct
Dependency_Set {
	std::set<Entity_Id>    on_parameter;
	std::set<Var_Id>       on_series;
	std::set<Var_Id>       on_state_var;
};

struct
State_Variable {
	Decl_Type type; //either flux, quantity or property

	String_View name;

	Entity_Id entity_id;  // This is the ID of the declaration, either has(...) or flux(...)
	
	// If this is a quantity or property, loc1 is the location of this variable.
	// If this is a flux, loc1 and loc2 are the source and target of the flux resp.
	Value_Location loc1;
	Value_Location loc2;
	
	Dependency_Set depends;
	Dependency_Set initial_depends;
	
	Math_Expr_FT *function_tree;
	Math_Expr_FT *initial_function_tree;
	
	State_Variable() : function_tree(nullptr), visited(false), temp_visited(false) {};
	
	// Note: these two are used for topological sorts during batch generation in the model composition stage.
	bool visited;
	bool temp_visited;
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
	
	Var_Id operator[](Value_Location loc) {
		if(!is_valid(loc) || loc.type != Location_Type::located)
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid location.");
		auto find = location_to_id.find(loc);
		if(find == location_to_id.end())
			return invalid_var;
		return find->second;
	}
	
	std::set<Var_Id> *operator[](String_View name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			fatal_error(Mobius_Error::internal, "Tried to look up a variable using an invalid name \"", name, "\".");
		return &find->second;
	}
	
	Var_Id register_var(State_Variable var, Value_Location loc) {
		if(is_valid(loc) && loc.type == Location_Type::located) {
			Var_Id id = (*this)[loc];
			if(is_valid(id))
				fatal_error(Mobius_Error::internal, "Re-registering a variable."); //TODO: hmm, this only catches cases where the location was valid.
		}
		
		vars.push_back(var);
		Var_Id id = {(s32)vars.size()-1};
		if(is_valid(loc) && loc.type == Location_Type::located)
			location_to_id[loc] = id;
		name_to_id[var.name].insert(id);
		return id;
	}
	
	Var_Id begin() { return {0}; }
	Var_Id end()   { return {(s32)vars.size()}; }
};

struct Run_Batch {
	std::vector<Var_Id> state_vars;
};

struct
Mobius_Model {
	std::vector<Module_Declaration *> modules;   //TODO: allow multiple modules!
	
	Var_Registry state_vars;
	Var_Registry series;
	
	Mobius_Model() : allocator(32*1024*1024) {}    //NOTE: for now, just to build string names.
	
	
	void add_module(Module_Declaration *module);
	void compose();
	bool is_composed = false;
	
	Run_Batch initial_batch;
	
	Run_Batch batch;    //TODO: eventually many of these
	
	Linear_Allocator allocator;
	
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
};



#endif // MOBIUS_MODEL_DECLARATION_H