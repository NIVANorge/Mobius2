

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
	std::set<state_var_id> depends_on_input_series;
	std::set<state_var_id> depends_on_state_var;
	
	Math_Expr_FT *function_tree;
	
	State_Variable() : function_tree(nullptr) {};
};

struct Code_Batch {
	std::vector<state_var_id> state_vars;
};

struct
Mobius_Model {
	std::vector<Module_Declaration *> modules;   //TODO: allow multiple modules!
	
	std::vector<State_Variable> state_variables;
	std::unordered_map<Value_Location, state_var_id, Value_Location_Hash> location_to_id;
	
	void add_module(Module_Declaration *module);
	void compose();
	
	Code_Batch batch;    //TODO: eventually many of these
};





inline bool
state_var_location_exists(Mobius_Model *model, Value_Location loc) {
	if(loc.type != Location_Type::located)
		fatal_error(Mobius_Error::internal, "Unlocated value in state_var_location_exists().");
	
	return model->location_to_id.find(loc) != model->location_to_id.end();
}

inline state_var_id
find_state_var(Mobius_Model *model, Value_Location loc) {
	if(loc.type != Location_Type::located)
		fatal_error(Mobius_Error::internal, "Unlocated value in find_state_var().");
	state_var_id result = -1;
	auto find = model->location_to_id.find(loc);
	if(find != model->location_to_id.end())
		result = find->second;
	return result;
}



#endif // MOBIUS_MODEL_DECLARATION_H