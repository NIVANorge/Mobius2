
#include "model_declaration.h"
#include "function_tree.h"

void
register_state_variable(Mobius_Model *model, Module_Declaration *module, Decl_Type type, Entity_Id id) {
	
	//TODO: here we may have to do something with identifying things that were declared withe the same name multiple modules...
	
	Decl_Type var_type = type;
	Value_Location loc;
	
	if(type == Decl_Type::has) {
		auto has = module->hases[id];
		loc = has->value_location;
		var_type = module->properties_and_substances[loc.property_or_substance]->decl_type;
	} else if (type != Decl_Type::flux) {
		fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	
	State_Variable var = {};
	var.type = var_type;
	var.entity_id   = id;
	model->state_variables.push_back(var);
	state_var_id var_id = model->state_variables.size()-1;
	
	if(type == Decl_Type::has) {
		model->location_to_id[loc] = var_id;
	}
}


void
check_flux_location(Mobius_Model *model, Module_Declaration *module, Source_Location source_loc, Value_Location loc) {
	if(loc.type != Location_Type::located) return;
	auto hopefully_a_substance = module->properties_and_substances[loc.property_or_substance];
	if(hopefully_a_substance->decl_type != Decl_Type::substance) {
		source_loc.print_error_header();
		fatal_error("Fluxes can only be assigned to substances. \"", hopefully_a_substance->handle_name, "\" is a property, not a substance.");
	}
	if(!state_var_location_exists(model, loc)) {
		auto compartment = module->compartments[loc.compartment];
		source_loc.print_error_header();
		fatal_error("The compartment \"", compartment->handle_name, "\" does not have the substance \"", hopefully_a_substance->handle_name, "\".");
	}
}

void
Mobius_Model::add_module(Module_Declaration *module) {
	this->modules.push_back(module);
	
	for(Entity_Id id : module->hases) {
		auto has = module->hases[id];
		
		// TODO: fixup of unit of the has (inherits from the substance or property if not given here!)
		
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
Mobius_Model::compose() {
	for(State_Variable &var : state_variables) {
		Math_Expr_AST *ast = nullptr;
		if(var.type == Decl_Type::flux)
			ast = modules[var.entity_id.module_id]->fluxes[var.entity_id]->code;
		else if(var.type == Decl_Type::property)
			ast = modules[var.entity_id.module_id]->hases[var.entity_id]->code;
		
		if(ast) resolve_function_tree(this, ast, &var);
	}
}







