
#include "model_declaration.h"
#include "function_tree.h"


#include <sstream>

void
register_state_variable(Mobius_Model *model, Module_Declaration *module, Decl_Type type, Entity_Id id, bool is_series) {
	
	//TODO: here we may have to do something with identifying things that were declared withe the same name multiple modules...
	
	State_Variable var = {};
	var.type           = type;
	var.entity_id      = id;
	
	Value_Location loc = invalid_value_location;
	
	if(type == Decl_Type::has) {
		auto has = module->hases[id];
		loc = has->value_location;
		var.loc1 = loc;
		var.type = module->properties_and_quantities[loc.property_or_quantity]->decl_type;
	} else if (type == Decl_Type::flux) {
		auto flux = module->fluxes[id];
		var.loc1 = flux->source;
		var.loc2 = flux->target;
	} else
		fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	
	
	
	auto name = module->find_entity(id)->name;
	if(!name && type == Decl_Type::has) //TODO: this is a pretty poor stopgap.
		name = module->find_entity(loc.property_or_quantity)->name;
		
	var.name = name;
	if(!var.name)
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
		
	
	//warning_print("Var ", var.name, " is series: ", is_series, "\n");
	
	if(is_series)
		model->series.register_var(var, loc);
	else
		model->state_vars.register_var(var, loc);
}


void
check_flux_location(Mobius_Model *model, Module_Declaration *module, Source_Location source_loc, Value_Location loc) {
	if(loc.type != Location_Type::located) return;
	auto hopefully_a_quantity = module->properties_and_quantities[loc.property_or_quantity];
	if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
		source_loc.print_error_header();
		fatal_error("Fluxes can only be assigned to quantities. \"", hopefully_a_quantity->handle_name, "\" is a property, not a quantity.");
	}
	Var_Id var_id = model->state_vars[loc];
	if(!is_valid(var_id)) {
		auto compartment = module->compartments[loc.compartment];
		source_loc.print_error_header();
		fatal_error("The compartment \"", compartment->handle_name, "\" does not have the quantity \"", hopefully_a_quantity->handle_name, "\".");
	}
}

void
Mobius_Model::add_module(Module_Declaration *module) {
	this->modules.push_back(module);
	
	for(Entity_Id id : module->hases) {
		auto has = module->hases[id];
		
		// TODO: fixup of unit of the has (inherits from the quantity or property if not given here!)
		// TODO: check for duplicate has!
		Decl_Type type = module->find_entity(has->value_location.property_or_quantity)->decl_type;
		bool is_series = !has->code && (type != Decl_Type::quantity); // TODO: this is not necessarily determinable at this stage once we have multiple modules. Must be done in composition instead
		register_state_variable(this, module, Decl_Type::has, id, is_series);
	}
	
	for(Entity_Id id : module->fluxes) {
		auto flux = module->fluxes[id];
		check_flux_location(this, module, flux->location, flux->source);
		check_flux_location(this, module, flux->location, flux->target);
		
		register_state_variable(this, module, Decl_Type::flux, id, false);
	}
}



void
error_print_location(Mobius_Model *model, Value_Location loc) {
	auto comp = model->find_entity(loc.compartment);
	auto prop = model->find_entity(loc.property_or_quantity);
	error_print(comp->handle_name, ".", prop->handle_name);
}

void
error_print_state_var(Mobius_Model *model, Var_Id id) {
	
	//TODO: this has to be improved
	auto var = model->state_vars[id];
	auto entity = model->find_entity(var->entity_id);
	if(entity->handle_name.data)
		error_print(entity->handle_name);
	else if(entity->name.data)
		error_print(entity->name);
	else {
		if(var->type == Decl_Type::quantity || var->type == Decl_Type::property) {
			auto has = model->modules[var->entity_id.module_id]->hases[var->entity_id];
			error_print("has(");
			error_print_location(model, has->value_location);
			error_print(")");
		} else {
			auto flux = model->modules[var->entity_id.module_id]->fluxes[var->entity_id];
			error_print("flux(");
			error_print_location(model, flux->source);
			error_print(", ");
			error_print_location(model, flux->target);
			error_print(")");
		}
	}
}

void
print_partial_dependency_trace(Mobius_Model *model, Var_Id id, Var_Id dep) {
	error_print_state_var(model, id);
	error_print(" <-- ");
	error_print_state_var(model, dep);
	error_print("\n");
}

bool
topological_sort_state_vars_visit(Mobius_Model *model, Var_Id var_id, std::vector<Var_Id> *push_to) {
	auto var = model->state_vars[var_id];
	if(var->visited) return true;
	if(var->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the state variables:\n");
		return false;
	}
	var->temp_visited = true;
	for(auto dep_id : var->depends.on_state_var) {
		bool success = topological_sort_state_vars_visit(model, dep_id, push_to);
		if(!success) {
			print_partial_dependency_trace(model, var_id, dep_id);
			return false;
		}
	}
	var->visited = true;
	push_to->push_back(var_id);
	return true;
}

bool
topological_sort_initial_state_vars_visit(Mobius_Model *model, Var_Id var_id, std::vector<Var_Id> *push_to) {
	auto var = model->state_vars[var_id];
	if(var->visited || !var->initial_function_tree) return true;
	if(var->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the state variables:\n");
		return false;
	}
	var->temp_visited = true;
	for(auto dep_id : var->initial_depends.on_state_var) {
		bool success = topological_sort_initial_state_vars_visit(model, dep_id, push_to);
		if(!success) {
			print_partial_dependency_trace(model, var_id, dep_id);
			return false;
		}
	}
	var->visited = true;
	push_to->push_back(var_id);
	return true;
}

void
Mobius_Model::compose() {
	warning_print("compose begin\n");
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		if(var->type == Decl_Type::flux)
			ast = find_entity<Reg_Type::flux>(var->entity_id)->code;
		else if(var->type == Decl_Type::property || var->type == Decl_Type::quantity) {
			auto has = find_entity<Reg_Type::has>(var->entity_id);
			ast      = has->code;
			init_ast = has->initial_code;
		}
		
		//TODO: For fluxes, with discrete solver, we also have to make sure they don't empty the given quantity.
		//Also, the order of fluxes are important.
		
		if(ast) {
			var->function_tree = make_cast(resolve_function_tree(this, var->entity_id.module_id, ast, nullptr), Value_Type::real);
			if(var->type == Decl_Type::flux) {
				if(var->loc1.type == Location_Type::located) {
					auto source = state_vars[var->loc1];
					var->function_tree = restrict_flux(var->function_tree, source);
				}
			}
		} else
			var->function_tree = nullptr; // NOTE: this is for substances. They are computed a different way.
		
		if(init_ast) {
			warning_print("found initial function tree for ", var->name, "\n");
			var->initial_function_tree = make_cast(resolve_function_tree(this, var->entity_id.module_id, init_ast, nullptr), Value_Type::real);
		} else
			var->initial_function_tree = nullptr;
	}
	
	warning_print("Prune begin\n");
	//TODO: this should only be done after code for an entire batch is generated.
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->function_tree)
			var->function_tree = prune_tree(var->function_tree);
		if(var->initial_function_tree)
			var->initial_function_tree = prune_tree(var->initial_function_tree);
	}
	
	warning_print("Fluxing & dependencies begin\n");
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		
		if(var->function_tree)
			register_dependencies(var->function_tree, &var->depends);
		
		//TODO: do something like in Mobius1 where you make a function stand in for its initial value if it is referenced.
		if(var->initial_function_tree)
			register_dependencies(var->initial_function_tree, &var->initial_depends);
		
		if(var->type == Decl_Type::flux) {
			if(var->loc1.type == Location_Type::located) {
				auto source = state_vars[state_vars[var->loc1]];
				source->depends.on_state_var.insert(var_id);
			}
			if(var->loc2.type == Location_Type::located) {
				auto target = state_vars[state_vars[var->loc2]];
				target->depends.on_state_var.insert(var_id);
			}
		}
		if(var->type == Decl_Type::flux) {
			// remove dependencies of flux on its source. Even though it compares agains the source, it should be ordered before it in the execution batches
			if(var->loc1.type == Location_Type::located)
				var->depends.on_state_var.erase(state_vars[var->loc1]);
			if(var->loc2.type == Location_Type::located)
				var->depends.on_state_var.erase(state_vars[var->loc2]);
		}
	}
	
	warning_print("Sorting begin\n");
	for(auto var_id : state_vars) {
		bool success = topological_sort_state_vars_visit(this, var_id, &batch.state_vars);
		if(!success) mobius_error_exit();
	}
	
	for(auto var_id : state_vars) {
		state_vars[var_id]->visited = false;
		state_vars[var_id]->temp_visited = false;
	}
	
	for(auto var_id : state_vars) {
		bool success = topological_sort_initial_state_vars_visit(this, var_id, &initial_batch.state_vars);
		if(!success) mobius_error_exit();
	}
	
	is_composed = true;
}







