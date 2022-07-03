

#ifndef MOBIUS_MODEL_DECLARATION_H
#define MOBIUS_MODEL_DECLARATION_H

#include "module_declaration.h"
#include "function_tree.h"

#include <set>

struct
State_Variable {
	Decl_Type type; //either flux, quantity or property

	Entity_Id entity_id;  // This is the ID of the declaration, either has(...) or flux(...)
	
	std::set<Entity_Id>    depends_on_parameter;
	std::set<Var_Id>       depends_on_series;
	std::set<Var_Id>       depends_on_state_var;
	
	Math_Expr_FT *function_tree;
	
	State_Variable() : function_tree(nullptr), visited(false), temp_visited(false) {};
	
	// Note: these two are used for topological sorts during batch generation in the model composition stage.
	bool visited;
	bool temp_visited;
};

struct Var_Registry {
	std::vector<State_Variable> vars;
	std::unordered_map<Value_Location, Var_Id, Value_Location_Hash> location_to_id;
	
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
	
	Var_Id register_var(State_Variable var, Value_Location loc) {
		if(is_valid(loc) && loc.type == Location_Type::located) {
			Var_Id id = (*this)[loc];
			if(is_valid(id))
				fatal_error(Mobius_Error::internal, "Re-registering a variable.");
		}
		
		vars.push_back(var);
		Var_Id id = {(s32)vars.size()-1};
		if(is_valid(loc) && loc.type == Location_Type::located)
			location_to_id[loc] = id;
		return id;
	}
	
	Var_Id begin() { return {0}; }
	Var_Id end()   { return {(s32)vars.size() - 1}; }
};

struct Run_Batch {
	std::vector<Var_Id> state_vars;
};

struct
Mobius_Model {
	std::vector<Module_Declaration *> modules;   //TODO: allow multiple modules!
	
	Var_Registry state_vars;
	Var_Registry series;
	
	
	void add_module(Module_Declaration *module);
	void compose();
	
	Run_Batch batch;    //TODO: eventually many of these
	
	
	Entity_Registration_Base *
	find_entity(Entity_Id id) {
		if(id.module_id < 0 || id.module_id >= (s16)modules.size())
			fatal_error(Mobius_Error::internal, "find_entity() with invalid module id.");
		return modules[id.module_id]->find_entity(id);
	}
};



#endif // MOBIUS_MODEL_DECLARATION_H