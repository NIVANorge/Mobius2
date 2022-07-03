
#include "model_declaration.h"
#include "function_tree.h"

void
register_state_variable(Mobius_Model *model, Module_Declaration *module, Decl_Type type, Entity_Id id) {
	
	//TODO: here we may have to do something with identifying things that were declared withe the same name multiple modules...
	
	Decl_Type var_type = type;
	Value_Location loc = invalid_value_location;
	
	if(type == Decl_Type::has) {
		auto has = module->hases[id];
		loc = has->value_location;
		var_type = module->properties_and_quantities[loc.property_or_quantity]->decl_type;
	} else if (type != Decl_Type::flux) {
		fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	
	State_Variable var = {};
	var.type           = var_type;
	var.entity_id      = id;
	
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
	if(!is_valid(loc)) {
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
		register_state_variable(this, module, Decl_Type::has, id);
	}
	
	for(Entity_Id id : module->fluxes) {
		auto flux = module->fluxes[id];
		check_flux_location(this, module, flux->location, flux->source);
		check_flux_location(this, module, flux->location, flux->target);
		
		register_state_variable(this, module, Decl_Type::flux, id);
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
	for(auto dep_id : var->depends_on_state_var) {
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

void
Mobius_Model::compose() {
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		Math_Expr_AST *ast = nullptr;
		if(var->type == Decl_Type::flux)
			ast = modules[var->entity_id.module_id]->fluxes[var->entity_id]->code;
		else if(var->type == Decl_Type::property)
			ast = modules[var->entity_id.module_id]->hases[var->entity_id]->code;
		
		//TODO: For fluxes, with discrete solver, we also have to make sure they don't empty the given quantity.
		//Also, the order of fluxes are important.
		
		if(ast) {
			var->function_tree = make_cast(resolve_function_tree(this, var->entity_id.module_id, ast), Value_Type::real);
		} else {
			var->function_tree = quantity_codegen(this, var->entity_id);
		}
	}
	
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->function_tree)
			var->function_tree = prune_tree(var->function_tree);
	}
	
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->function_tree)
			register_dependencies(var->function_tree, var);
		if(var->type == Decl_Type::flux) {
			// remove dependencies of flux on its source and target (it should always use previous-time-step values
			auto flux = modules[var->entity_id.module_id]->fluxes[var->entity_id];
			if(flux->source.type == Location_Type::located)
				var->depends_on_state_var.erase(state_vars[flux->source]);
			if(flux->target.type == Location_Type::located)
				var->depends_on_state_var.erase(state_vars[flux->target]);
		} else if (var->type == Decl_Type::quantity) {
			// remove self-dependency of quantities (it will always use previous-time-step values).
			auto has = modules[var->entity_id.module_id]->hases[var->entity_id];
			var->depends_on_state_var.erase(state_vars[has->value_location]);
		}
	}
	
	for(auto var_id : state_vars) {
		bool success = topological_sort_state_vars_visit(this, var_id, &batch.state_vars);
		if(!success) mobius_error_exit();
	}
}







