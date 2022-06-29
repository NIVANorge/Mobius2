
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

Math_Expr_FT *resolve_function_tree(Mobius_Model *model, Math_Expr_AST *ast, State_Variable *var, Linear_Allocator *allocator);

void
Mobius_Model::compose(Linear_Allocator *allocator) {
	
	for(State_Variable &var : state_variables) {
		Math_Expr_AST *ast = nullptr;
		if(var.type == Decl_Type::flux)
			ast = modules[var.entity_id.module_id]->fluxes[var.entity_id]->code;
		else if(var.type == Decl_Type::property)
			ast = modules[var.entity_id.module_id]->hases[var.entity_id]->code;
		
		if(ast) resolve_function_tree(this, ast, &var, allocator);
	}
}


Math_Expr_FT *
resolve_function_tree(Mobius_Model *model, Math_Expr_AST *ast, State_Variable *var, Linear_Allocator *allocator){
	Math_Expr_FT *result;
	
	Module_Declaration *module = model->modules[var->entity_id.module_id];
	switch(ast->type) {
		case Math_Expr_Type::block : {
			auto block = reinterpret_cast<Math_Block_AST *>(ast);
			auto new_block = allocator->make_new<Math_Block_FT>();
			for(Math_Expr_AST *expr : block->exprs)
				new_block->exprs.push_back(resolve_function_tree(model, expr, var, allocator));
			
			// the value of a block is the value of the last expression in the block.
			new_block->value_type = new_block->exprs.back()->value_type;
			new_block->unit       = new_block->exprs.back()->unit;
			result = new_block;
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_Chain_AST *>(ast);
			auto new_ident = allocator->make_new<Identifier_FT>();
			if(ident->chain.size() == 1) {
				//TODO: for now, just assume this is a parameter. Could eventually be a local variable.
				new_ident->variable_type = Variable_Type::parameter;
				
				Entity_Id par_id = find_handle(module, ident->chain[0].string_value);
				
				if(!is_valid(par_id) || par_id.reg_type != Reg_Type::parameter) {
					ident->chain[0].print_error_header();
					fatal_error("Can not resolve the name \"", ident->chain[0].string_value, "\".");
				}
				//TODO: could allow refering to properties by just a single identifier if the compartment is obvious.
				
				new_ident->variable_type = Variable_Type::parameter;
				new_ident->parameter = par_id;
				new_ident->unit      = module->parameters[par_id]->unit;
				new_ident->value_type = get_value_type(module->parameters[par_id]->decl_type);
				// TODO: make it so that we can't reference datetime values (if we implement those)
				//  also special handling of for enum values!
			} else if(ident->chain.size() == 2) {
				//TODO: may need fixup for multiple modules.
				Entity_Id first  = find_handle(module, ident->chain[0].string_value);
				Entity_Id second = find_handle(module, ident->chain[1].string_value);
				//TODO: may need fixup for special identifiers.
				
				if(!is_valid(first) || !is_valid(second) || first.reg_type != Reg_Type::compartment || second.reg_type != Reg_Type::property_or_substance) {
					ident->chain[0].print_error_header();
					fatal_error("Unable to resolve value.");  //TODO: make message more specific.
				}
				state_var_id var_id = find_state_var(model, make_value_location(module, first, second));
				if(var_id < 0) {    // TODO: make proper id system...
					// unresolved, but valid. Assume this is an input series instead.
					warning_print("Input series unimplemented.\n"); //TODO!!!
					
					new_ident->variable_type = Variable_Type::input_series;
					
				} else {
					new_ident->variable_type = Variable_Type::state_var;
					new_ident->state_var     = var_id;
					new_ident->unit          = module->hases[model->state_variables[var_id].entity_id]->unit;
					new_ident->value_type    = Value_Type::real;
				}
			} else {
				ident->chain[0].print_error_header();
				fatal_error("Too many identifiers in chain.");
			}
			result = new_ident;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_AST *>(ast);
			auto new_literal = allocator->make_new<Literal_FT>();
			
			new_literal->value_type = get_value_type(literal->value.type);
			new_literal->unit       = module->dimensionless_unit;
			new_literal->value      = get_parameter_value(&literal->value);
			
			result = new_literal;
		} break;
		
		case Math_Expr_Type::function_call : {
		} break;
		
		case Math_Expr_Type::unary_operator : {
		} break;
		
		case Math_Expr_Type::binary_operator : {
		} break;
		
		case Math_Expr_Type::if_chain : {
		} break;
	}
	
	result->ast = ast;
	
	/*
	TODO: should have stored location on each ast node so that we could make error messages like these!
	if(reult->value_type == Value_Type::unresolved) {
		ast->location.
	}
	*/
	
	return result;
}




