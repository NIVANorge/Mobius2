
#include "model_declaration.h"
#include "function_tree.h"


#include <sstream>


void
process_to_declaration(Mobius_Model *model, string_map<s16> *module_ids, Decl_AST *decl) {
	//TODO!
	// sn.p_rain.to(soil.water)
	match_declaration(decl, {{Token_Type::identifier}}, 2, false);
	String_View module_handle = decl->decl_chain[0].string_value;
	auto find = module_ids->find(module_handle);
	if(find == module_ids->end()) {
		decl->decl_chain[0].print_error_header();
		fatal_error("The module handle \"", module_handle, "\" was not declared.");
	}
	s16 module_id = find->second;
	Module_Declaration *module = model->modules[module_id];
	
	String_View flux_handle = decl->decl_chain[1].string_value;
	Entity_Id flux_id = module->find_handle(flux_handle);
	if(!is_valid(flux_id)) {
		decl->decl_chain[1].print_error_header();
		fatal_error("The module \"", module->name, "\" with handle \"", module_handle, "\" does not have a flux with handle \"", flux_handle, "\".");
	}
	
	auto flux = module->fluxes[flux_id];
	if(flux->target.type != Location_Type::out) {
		decl->decl_chain[1].print_error_header();
		fatal_error("The flux \"", flux_handle, "\" does not have the target \"out\", and so we can't re-assign its target.");
	}
	
	Token *comp_tk = &decl->args[0]->sub_chain[0];
	if(decl->args[0]->sub_chain.size() != 2) {
		comp_tk->print_error_header();
		fatal_error("This is not a well-formatted flux target. Expected something on the form a.b .");
	}
	Token *quant_tk = &decl->args[0]->sub_chain[1];
	auto comp_id = model->modules[0]->find_handle(comp_tk->string_value);
	if(!is_valid(comp_id)) {
		comp_tk->print_error_header();
		fatal_error("The compartment \"", comp_tk->string_value, "\" has not been declared in the local scope.");
	}
	auto quant_id = model->modules[0]->find_handle(quant_tk->string_value);
	if(!is_valid(quant_id)) {
		quant_tk->print_error_header();
		fatal_error("The property or quantity \"", quant_tk->string_value, "\" has not been declared in the local scope.");
	}
	
	flux->target = make_value_location(model->modules[0], comp_id, quant_id);
}

Mobius_Model *
load_model(String_View file_name) {
	Mobius_Model *model = new Mobius_Model();
	
	String_View model_data = model->file_handler.load_file(file_name);
	model->this_path = file_name;
	
	Token_Stream stream(file_name, model_data);
	Decl_AST *decl = parse_decl(&stream);
	if(decl->type != Decl_Type::model || stream.peek_token().type != Token_Type::eof) {
		decl->type_name.print_error_header();
		fatal_error("Model files should only have a single model declaration in the top scope.");
	}
	
	match_declaration(decl, {{Token_Type::quoted_string}}, 0, false);
	model->name = single_arg(decl, 0)->string_value;
	
	//note: it is a bit annoying that we can't reuse Registry or Var_Registry for this, but it would also be too gnarly to factor out any more functionality from those, I think.
	string_map<s16> module_ids;
	
	auto body = reinterpret_cast<Decl_Body_AST *>(decl->bodies[0]);
	
	if(body->doc_string.type == Token_Type::quoted_string)
		model->doc_string = body->doc_string.string_value;
	
	auto global_scope = new Module_Declaration();
	global_scope->module_id = 0;
	
	//note: this must happen before we load the modules, otherwise this will be viewed as a re-declaration
	for(Decl_AST *child : body->child_decls) {
		switch (child->type) {
			case Decl_Type::compartment : {
				process_declaration<Reg_Type::compartment>(global_scope, child);
			} break;
			
			case Decl_Type::quantity : {
				process_declaration<Reg_Type::property_or_quantity>(global_scope, child);
			} break;
		}
	}
	
	model->modules.push_back(global_scope);
	
	for(Decl_AST *child : body->child_decls) {
		switch (child->type) {
			case Decl_Type::load : {
				match_declaration(child, {{Token_Type::quoted_string, Decl_Type::module}}, 0, false);
				String_View file_name = single_arg(child, 0)->string_value;
				Decl_AST *module_spec = child->args[1]->decl;
				match_declaration(module_spec, {{Token_Type::quoted_string}});  //TODO: allow specifying the version also?
				String_View module_name = single_arg(module_spec, 0)->string_value;
				
				s16 module_id = model->load_module(file_name, module_name);
				if(module_spec->handle_name.string_value)
					module_ids[module_spec->handle_name.string_value] = module_id;
			} break;
		}
	}
	
	for(Decl_AST *child : body->child_decls) {
		switch (child->type) {
			case Decl_Type::to : {
				process_to_declaration(model, &module_ids, child);
			} break;
			
			case Decl_Type::compartment :
			case Decl_Type::quantity :
			case Decl_Type::load : {
				// Don't do anything. We handled it above already
			} break;
			
			default : {
				child->type_name.print_error_header();
				fatal_error("Did not expect a declaration of type ", child->type_name.string_value, " inside a model declaration.");
			};
		}
	}
	
	return model;
}


s16
Mobius_Model::load_module(String_View file_name, String_View module_name) {
	String_View file_data = file_handler.load_file(file_name, this_path);
	Token_Stream stream(file_name, file_data);
	
	s16 module_id = (s16)modules.size();
	bool found = false;
	while(true) {
		if(stream.peek_token().type == Token_Type::eof) break;
		Decl_AST *module_decl = parse_decl(&stream);
		if(module_decl->type != Decl_Type::module) {
			module_decl->type_name.print_error_header();
			fatal_error("Module files should only have modules in the top scope.");
		}
		if(module_decl->args.size() >= 1 && module_decl->args[0]->sub_chain.size() >= 1 && module_decl->args[0]->sub_chain[0].string_value == module_name) {
			auto global_scope = modules[0];
			Module_Declaration *module = process_module_declaration(global_scope, module_id, module_decl);
			modules.push_back(module);
			found = true;
			break;
		} else {
			// TODO: this is wasteful for now. We could cache the ast in case it is loaded by another load_module call, then delete the ones we did not use
			delete module_decl;
		}
	}
	if(!found) {
		//TODO: this should give the location of where it was requested?
		fatal_error(Mobius_Error::parsing, "Could not find the module ", module_name, " in the file ", file_name, ".");
	}
	return module_id;
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

/*
Value_Location
make_global(Module_Declaration *module, Value_Location loc) {
	auto comp = module->compartments[loc.compartment];
	auto prop = module->properties_and_quantities[loc.property_or_quantity];
	
}
*/

void
register_state_variable(Mobius_Model *model, Decl_Type type, Entity_Id id, bool is_series) {
	
	//TODO: here we may have to do something with identifying things that were declared withe the same name in multiple modules...
	
	State_Variable var = {};
	var.type           = type;
	var.entity_id      = id;
	
	Value_Location loc = invalid_value_location;
	
	if(type == Decl_Type::has) {
		auto has = model->find_entity<Reg_Type::has>(id);
		loc = has->value_location;
		var.loc1 = loc;
		var.type = model->find_entity(loc.property_or_quantity)->decl_type;
	} else if (type == Decl_Type::flux) {
		auto flux = model->find_entity<Reg_Type::flux>(id);
		var.loc1 = flux->source;
		var.loc2 = flux->target;
	} else
		fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	
	
	
	auto name = model->find_entity(id)->name;
	if(!name && type == Decl_Type::has) //TODO: this is a pretty poor stopgap.
		name = model->find_entity(loc.property_or_quantity)->name;
		
	var.name = name;
	if(!var.name)
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
		
	warning_print("Var ", var.name, " is series: ", is_series, "\n");
	
	if(is_series)
		model->series.register_var(var, loc);
	else
		model->state_vars.register_var(var, loc);
}


void
check_flux_location(Mobius_Model *model, Source_Location source_loc, Value_Location loc) {
	if(loc.type != Location_Type::located) return;
	auto hopefully_a_quantity = model->find_entity(loc.property_or_quantity);
	if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
		source_loc.print_error_header();
		fatal_error("Fluxes can only be assigned to quantities. \"", hopefully_a_quantity->handle_name, "\" is a property, not a quantity.");
	}
	Var_Id var_id = model->state_vars[loc];
	if(!is_valid(var_id)) {
		auto compartment = model->find_entity(loc.compartment);
		source_loc.print_error_header();
		fatal_error("The compartment \"", compartment->handle_name, "\" does not have the quantity \"", hopefully_a_quantity->handle_name, "\".");
	}
}

void
Mobius_Model::compose() {
	warning_print("compose begin\n");
	
	/*
	TODO: we have to check for mismatching has declarations or re-declaration of code multiple places.
		only if there is no code anywhere can we be sure that it is a series.
		So maybe a pass first to check this, *then* do the state var registration.
	*/
	
	int idx = -1;
	for(auto module : modules) {
		++idx;
		if(idx == 0) continue;
		
		for(Entity_Id id : module->hases) {
			auto has = module->hases[id];
			
			Decl_Type type = find_entity(has->value_location.property_or_quantity)->decl_type;
			bool is_series = !has->code && (type != Decl_Type::quantity); // TODO: this can't be determined this way! We have instead to do a pass later to see if not it was given code somewhere else!
			register_state_variable(this, Decl_Type::has, id, is_series);
		}
	}
	
	idx = -1;
	for(auto module : modules) {
		++idx;
		if(idx == 0) continue;
		
		for(Entity_Id id : module->fluxes) {
			auto flux = module->fluxes[id];
			check_flux_location(this, flux->location, flux->source);
			check_flux_location(this, flux->location, flux->target);
			
			register_state_variable(this, Decl_Type::flux, id, false);
		}
	}
	
	warning_print("State var registration begin.\n");
	
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
			//warning_print("found initial function tree for ", var->name, "\n");
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







