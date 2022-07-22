
#include "model_declaration.h"
#include "function_tree.h"


#include <sstream>


Var_Id
register_state_variable(Mobius_Model *model, Decl_Type type, Entity_Id id, bool is_series, String_View given_name = "") {
	
	State_Variable var = {};
	var.type           = type;
	var.entity_id      = id;
	
	Value_Location loc = invalid_value_location;
	
	if(is_valid(id)) {
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
	}
	
	auto name = given_name;
	if(!name && is_valid(id))
		name = model->find_entity(id)->name;
	if(!name && type == Decl_Type::has) //TODO: this is a pretty poor stopgap.
		name = model->find_entity(loc.property_or_quantity)->name;
		
	var.name = name;
	if(!var.name)
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
		
	//warning_print("Var ", var.name, " is series: ", is_series, "\n");
	
	if(is_series)
		return model->series.register_var(var, loc);
	else
		return model->state_vars.register_var(var, loc);
	
	return invalid_var;
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
remove_lasts(Math_Expr_FT *expr, bool make_error) {
	for(auto arg : expr->exprs) remove_lasts(arg, make_error);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && (ident->flags & ident_flags_last_result)) {
			if(make_error) {
				expr->location.print_error_header();
				fatal_error("Did not expect a last() in an initial value function.");
			}
			ident->flags = (Identifier_Flags)(ident->flags & ~ident_flags_last_result);
		}
	}
}

typedef std::unordered_map<int, std::vector<Var_Id>> Var_Map;

void
find_in_fluxes(Math_Expr_FT *expr, Var_Map &var_map, Var_Id push, bool make_error) {
	for(auto arg : expr->exprs) find_in_fluxes(arg, var_map, push, make_error);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && (ident->flags & ident_flags_in_flux)) {
			if(make_error) {
				expr->location.print_error_header();
				fatal_error("Did not expect an in_flux() in an initial value function.");
			}
			var_map[ident->state_var.id].push_back(push);
		}
	}
}

void
replace_in_flux(Math_Expr_FT *expr, Var_Id target_id, Var_Id in_flux_id) {
	for(auto arg : expr->exprs) replace_in_flux(arg, target_id, in_flux_id);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && ident->state_var == target_id && (ident->flags & ident_flags_in_flux)) {
			ident->state_var = in_flux_id;
			ident->flags = (Identifier_Flags)(ident->flags & ~ident_flags_in_flux);
		}
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
	
	// TODO: we should check that all referenced entities in all modules have actually been declared ( has_been_declared flag on Entity_Registration )
	
	warning_print("State var registration begin.\n");
	
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

	warning_print("Function tree resolution begin.\n");
	
	Var_Map in_flux_map;
	
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
			find_in_fluxes(var->function_tree, in_flux_map, var_id, false);
		} else
			var->function_tree = nullptr; // NOTE: this is for substances. They are computed a different way.
		
		if(init_ast) {
			//warning_print("found initial function tree for ", var->name, "\n");
			var->initial_function_tree = make_cast(resolve_function_tree(this, var->entity_id.module_id, init_ast, nullptr), Value_Type::real);
			remove_lasts(var->initial_function_tree, true);
			find_in_fluxes(var->initial_function_tree, in_flux_map, var_id, true);
		} else
			var->initial_function_tree = nullptr;
	}
	
	warning_print("Generate state vars for in_fluxes.\n");
	for(auto &in_flux : in_flux_map) {
		Var_Id target_id = {in_flux.first};
		
		Var_Id in_flux_id = register_state_variable(this, Decl_Type::has, invalid_entity_id, false, "in_flux");   //TODO: generate a better name
		auto in_flux_var = state_vars[in_flux_id];
		// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
		//   Make a lookup accelleration for this?
		Math_Expr_FT *flux_sum = make_literal(0.0);
		for(auto flux_id : state_vars) {
			auto var = state_vars[flux_id];
			if(var->type == Decl_Type::flux && var->loc2.type == Location_Type::located && state_vars[var->loc2] == target_id)
				flux_sum = make_binop('+', flux_sum, make_state_var_identifier(flux_id));
		}
		in_flux_var->function_tree         = flux_sum;
		in_flux_var->initial_function_tree = nullptr;
		
		for(auto rep_id : in_flux.second)
			replace_in_flux(state_vars[rep_id]->function_tree, target_id, in_flux_id);
	}
	
	warning_print("Put solvers begin.\n");
	for(auto id : modules[0]->solves) {
		auto solve = modules[0]->solves[id];
		Var_Id var_id = state_vars[solve->loc];
		if(!is_valid(var_id)) {
			solve->source_location.print_error_header();
			fatal_error("This compartment does not have that quantity.");  // TODO: give the handles names in the error message.
		}
		auto hopefully_a_quantity = find_entity(solve->loc.property_or_quantity);
		if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
			solve->source_location.print_error_header();
			fatal_error("Solvers can only be put on quantities, not on properties.");
		}
		state_vars[var_id]->solver = solve->solver;
	}
	
	warning_print("Dependencies begin.\n");
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		
		if(var->function_tree)
			register_dependencies(var->function_tree, &var->depends);
		
		if(var->initial_function_tree)
			register_dependencies(var->initial_function_tree, &var->initial_depends);
	}
	
	is_composed = true;
}







