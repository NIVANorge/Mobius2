
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
			//warning_print("Found state variable ", model->find_entity(loc.compartment)->handle_name, ".", model->find_entity(loc.property_or_quantity)->handle_name, "\n");
		} else if (type == Decl_Type::flux) {
			auto flux = model->find_entity<Reg_Type::flux>(id);
			var.loc1 = flux->source;
			var.loc2 = flux->target;
			if(var.loc2.type == Location_Type::neighbor) {  //TODO: move this test somewhere else to make it cleaner.
				if(!is_located(var.loc1)) {
					flux->location.print_error_header();
					fatal_error("You can't have a flux from nowhere to a neighbor.\n");
				}
				//var.loc2 = var.loc1;     // A bit superfluous.
				//var.neighbor = var.loc2.neighbor;
			} 
		} else
			fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	
	auto name = given_name;
	if(!name && is_valid(id))
		name = model->find_entity(id)->name;
	if(!name && type == Decl_Type::has) //TODO: this is a pretty poor stopgap. Could generate "Compartmentname Quantityname" or something like that.
		name = model->find_entity(loc.property_or_quantity)->name;
		
	var.name = name;
	if(!var.name)
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
	
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
typedef std::unordered_map<int, std::pair<std::set<Entity_Id>, std::vector<Var_Id>>> Var_Map2;

void
find_other_flags(Math_Expr_FT *expr, Var_Map &in_fluxes, Var_Map2 &aggregates, Var_Id looked_up_by, Entity_Id lookup_compartment, bool make_error_in_flux) {
	for(auto arg : expr->exprs) find_other_flags(arg, in_fluxes, aggregates, looked_up_by, lookup_compartment, make_error_in_flux);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->flags & ident_flags_in_flux) {
			if(make_error_in_flux) {
				expr->location.print_error_header();
				fatal_error("Did not expect an in_flux() in an initial value function.");
			}
			in_fluxes[ident->state_var.id].push_back(looked_up_by);
		}
		if(ident->flags & ident_flags_aggregate) {
			if(!is_valid(lookup_compartment)) {
				expr->location.print_error_header();
				fatal_error("Can't use aggregate() in this function body because it does not belong to a compartment.");
			}
				
			aggregates[ident->state_var.id].first.insert(lookup_compartment);         //OOOps!!!! This is not correct if this was applied to an input series!
			
			if(is_valid(looked_up_by)) {
				aggregates[ident->state_var.id].second.push_back(looked_up_by);
			}
		}
	}
}

void
replace_flagged(Math_Expr_FT *expr, Var_Id replace_this, Var_Id with, Identifier_Flags flag) {
	for(auto arg : expr->exprs) replace_flagged(arg, replace_this, with, flag);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && ident->state_var == replace_this && (ident->flags & flag)) {
			ident->state_var = with;
			ident->flags = (Identifier_Flags)(ident->flags & ~flag);
		}
	}
}

void
restrictive_lookups(Math_Expr_FT *expr, Decl_Type decl_type) {
	for(auto arg : expr->exprs) restrictive_lookups(arg, decl_type);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type != Variable_Type::local 
			&& ident->variable_type != Variable_Type::parameter) {
			expr->location.print_error_header();
			fatal_error("The function body for a ", name(decl_type), " declaration is only allowed to look up parameters, no other types of state variables.");
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
	
	// TODO: check if there are unused aggregation data or unit conversion data!
	// TODO: warning or error if a flux is still marked as "out" (?)   -- though this could be legitimate in some cases where you just want to run a sub-module and not link it up with anything.
	
	Var_Map in_flux_map;
	Var_Map2 needs_aggregate;
	
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		Math_Expr_AST *unit_conv_ast = nullptr;
		
		Entity_Id in_compartment = invalid_entity_id;
		Entity_Id from_compartment = invalid_entity_id;
		if(var->type == Decl_Type::flux) {
			auto flux = find_entity<Reg_Type::flux>(var->entity_id);
			ast = flux->code;
			bool target_is_located = is_located(flux->target) && !flux->target_was_out; // Note: the target could have been re-directed by the model. We only care about how it was declared
			if(is_located(flux->source)) {
				from_compartment = flux->source.compartment;
				if(!target_is_located || flux->source == flux->target)	
					in_compartment = from_compartment;
			} else if(target_is_located) {
				in_compartment = flux->target.compartment;
			}
			if(!is_valid(from_compartment)) from_compartment = in_compartment;
			// note : the only case where from_compartment != in_compartment is when both source and target are located, but different. In that case in_compartment is invalid, but from_compartment is not.
			
			if(is_located(flux->source)) {
				for(auto &unit_conv : modules[0]->compartments[flux->source.compartment]->unit_convs) {
					if(flux->source == unit_conv.source && flux->target == unit_conv.target) 
						unit_conv_ast = unit_conv.code;
				}
			}
		} else if(var->type == Decl_Type::property || var->type == Decl_Type::quantity) {
			auto has = find_entity<Reg_Type::has>(var->entity_id);
			ast      = has->code;
			init_ast = has->initial_code;
			from_compartment = in_compartment = has->value_location.compartment;
		}
				
		// TODO: instead of passing the in_compartment, we could just pass the var_id and give the function resolution more to work with.
		Function_Resolve_Data res_data = { this, var->entity_id.module_id, in_compartment };
		if(ast) {
			var->function_tree = make_cast(resolve_function_tree(ast, &res_data), Value_Type::real);
			find_other_flags(var->function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, false);
		} else
			var->function_tree = nullptr; // NOTE: this is for substances. They are computed a different way.
		
		if(init_ast) {
			//warning_print("found initial function tree for ", var->name, "\n");
			var->initial_function_tree = make_cast(resolve_function_tree(init_ast, &res_data), Value_Type::real);
			remove_lasts(var->initial_function_tree, true);
			find_other_flags(var->initial_function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, true);
		} else
			var->initial_function_tree = nullptr;
		
		if(unit_conv_ast) {
			auto res_data2 = res_data;
			res_data2.module_id = 0;
			var->flux_unit_conversion_tree = make_cast(resolve_function_tree(unit_conv_ast, &res_data2), Value_Type::real);
			restrictive_lookups(var->flux_unit_conversion_tree, Decl_Type::unit_conversion);
		} else
			var->flux_unit_conversion_tree = nullptr;
	}
	
	warning_print("Generate state vars for aggregates.\n");
	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	
	// note: We always generate an aggregate if the source compartment has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the aggregate is trivial
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->type != Decl_Type::flux) continue;
		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		auto source = find_entity<Reg_Type::compartment>(var->loc1.compartment);
		auto target = find_entity<Reg_Type::compartment>(var->loc2.compartment);
		
		for(auto index_set : source->index_sets)
			if(std::find(target->index_sets.begin(), target->index_sets.end(), index_set) == target->index_sets.end()) {
				needs_aggregate[var_id.id].first.insert(var->loc2.compartment);   //NOTE: The aggregate is going to be "looked up from" the target compartment of the flux in this case.
				break;
			}
	}
		
	for(auto &need_agg : needs_aggregate) {
		auto var_id = Var_Id {need_agg.first};
		auto var = state_vars[var_id];
		
		auto source = find_entity<Reg_Type::compartment>(var->loc1.compartment);
		
		for(auto to_compartment : need_agg.second.first) {
			//warning_print("********* Create an aggregate\n");
			
			Math_Expr_FT *agg_weight = nullptr;
			for(auto &agg : source->aggregations) {
				if(agg.to_compartment == to_compartment) {
					// Note: the module id is always 0 here since aggregation_weight should only be declared in model scope.
					Function_Resolve_Data res_data = { this, 0, invalid_entity_id };
					agg_weight = make_cast(resolve_function_tree(agg.code, &res_data), Value_Type::real);
					restrictive_lookups(agg_weight, Decl_Type::aggregation_weight);
					break;
				}
			}
			if(!agg_weight) {
				auto target = find_entity<Reg_Type::compartment>(to_compartment);
				//TODO: need to give much better feedback about where the variable was declared and where it was used with an aggregate()
				if(var->type == Decl_Type::flux) {
					
					fatal_error(Mobius_Error::model_building, "The flux \"", var->name, "\" goes from compartment \"", source->name, "\" to compartment, \"", target->name, "\", but the first compartment is distributed over a higher number of index sets than the second. This is only allowed if you specify an aggregation_weight between the two compartments.");
				} else {
					fatal_error(Mobius_Error::model_building, "Missing aggregation_weight for variable ", var->name, " between compartments \"", source->name, "\" and \"", target->name, "\".\n");
				}
			}
			var->flags = (State_Variable::Flags)(var->flags | State_Variable::f_has_aggregate);
			
			//TODO: We also have to handle the case where the agg. variable was a series!
			// note: can't reference "var" below this (without looking it up again). The vector it resides in may have reallocated.
			
			//TODO: make better name generation system!
			char varname[1024];
			sprintf(varname, "aggregate(%.*s)", var->name.count, var->name.data);
			auto varname2 = allocator.copy_string_view(varname);
			Var_Id agg_id = register_state_variable(this, var->type, invalid_entity_id, false, varname2);
			
			auto agg_var = state_vars[agg_id];
			agg_var->flags = (State_Variable::Flags)(agg_var->flags | State_Variable::f_is_aggregate);
			agg_var->agg = var_id;
			agg_var->function_tree = agg_weight;
			
			var = state_vars[var_id];   //NOTE: had to look it up again since we may have resized the vector var pointed into
			agg_var->loc1 = var->loc1;
			agg_var->loc2 = var->loc2;
			agg_var->flux_unit_conversion_tree = var->flux_unit_conversion_tree;
			agg_var->agg_to_compartment = to_compartment;
			state_vars[var_id]->agg = agg_id;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = state_vars[looked_up_by];
				
				Entity_Id lu_compartment = lu->loc1.compartment;
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.compartment;
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the Var_Map2
				
				replace_flagged(lu->function_tree, var_id, agg_id, ident_flags_aggregate);
			}
		}
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
			if(var->type == Decl_Type::flux && is_located(var->loc2) && state_vars[var->loc2] == target_id 
					&& !(var->flags & State_Variable::Flags::f_has_aggregate)) { //NOTE: if it has an aggregate, we should only count the aggregate, not this on its own.
				auto flux_code = make_state_var_identifier(flux_id);
				if(var->flux_unit_conversion_tree)
					flux_code = make_binop('*', flux_code, copy(var->flux_unit_conversion_tree)); // NOTE: we need to copy it here since it is also inserted somewhere else
				flux_sum = make_binop('+', flux_sum, flux_code);
			}
		}
		in_flux_var->function_tree         = flux_sum;
		in_flux_var->initial_function_tree = nullptr;
		in_flux_var->flags = (State_Variable::Flags)(in_flux_var->flags | State_Variable::f_in_flux);
		
		for(auto rep_id : in_flux.second)
			replace_flagged(state_vars[rep_id]->function_tree, target_id, in_flux_id, ident_flags_in_flux);
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
	
	
	//TODO: we have to make a system where we throw an error for variables if they reference something that can have more indexes than they themselves are allowed to! This also has to check the unit conversions and aggregation weights.
	   // ( but user could specify an aggregate() on the reference to avoid it. We then also have to make a state var for that aggregation.
	// NOTE: we could just do this in the index set dependency resolution, but that would not trip in all cases, and it would be harder to detect why it happened (to give good error message).
	
	is_composed = true;
}







