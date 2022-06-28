
#include "model_declaration.h"


void
register_state_variable(Mobius_Model *model, Module_Declaration *module, Decl_Type type, entity_id id) {
	
	Decl_Type var_type = type;
	Value_Location loc;
	
	if(type == Decl_Type::has) {
		auto has = module->hases[id];
		loc = has->value_location;
		var_type = module->properties_and_substances[loc.property_or_substance]->type;
	} else if (type != Decl_Type::flux) {
		fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	
	State_Variable var = {var_type, id};
	model->state_variables.push_back(var);
	state_var_id var_id = model->state_variables.size()-1;
	
	if(type == Decl_Type::has) {
		model->location_to_id[loc] = var_id;
	}
}


inline bool
state_var_location_exists(Mobius_Model *model, Value_Location loc) {
	if(loc.type != Location_Type::located)
		fatal_error(Mobius_Error::internal, "Unlocated value in state_var_location_exists().");
	
	return model->location_to_id.find(loc) != model->location_to_id.end();
}

void
check_flux_location(Mobius_Model *model, Module_Declaration *module, Source_Location source_loc, Value_Location loc) {
	if(loc.type != Location_Type::located) return;
	auto hopefully_a_substance = module->properties_and_substances[loc.property_or_substance];
	if(hopefully_a_substance->type != Decl_Type::substance) {
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
	//TODO: allow for more modules.
	this->module = module;
	
	for(entity_id id : module->hases) {
		auto has = module->hases[id];
		register_state_variable(this, module, Decl_Type::has, id);
	}
	
	for(entity_id id : module->fluxes) {
		auto flux = module->fluxes[id];
		check_flux_location(this, module, flux->location, flux->source);
		check_flux_location(this, module, flux->location, flux->target);
	}
}




