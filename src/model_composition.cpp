
#include "model_declaration.h"
#include "function_tree.h"


#include <sstream>


Var_Id
register_state_variable(Mobius_Model *model, Decl_Type type, Entity_Id id, bool is_series, String_View given_name = "") {
	
	State_Variable var = {};
	var.type           = type;
	var.entity_id      = id;
	
	Var_Location loc = invalid_var_location;
	
	if(is_valid(id)) {
		if(type == Decl_Type::has) {
			auto has = model->find_entity<Reg_Type::has>(id);
			loc = has->var_location;
			var.loc1 = loc;
			var.type = model->find_entity(loc.property_or_quantity)->decl_type;
			//warning_print("Found state variable ", model->find_entity(loc.compartment)->handle_name, ".", model->find_entity(loc.property_or_quantity)->handle_name, "\n");
		} else if (type == Decl_Type::flux) {
			auto flux = model->find_entity<Reg_Type::flux>(id);
			var.loc1 = flux->source;
			var.loc2 = flux->target;
			if(is_valid(flux->neighbor_target)) {  //TODO: move this test somewhere else to make it cleaner.
				if(!is_located(var.loc1)) {
					flux->location.print_error_header();
					fatal_error("You can't have a flux from nowhere to a neighbor.\n");
				}
				var.neighbor = flux->neighbor_target;
				var.loc2 = var.loc1;
			} 
		} else
			fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	} else if (type == Decl_Type::has)
		var.type = Decl_Type::property;
	
	auto name = given_name;
	if(!name && is_valid(id))
		name = model->find_entity(id)->name;
	if(!name && type == Decl_Type::has) //TODO: this is a pretty poor stopgap. We could generate "Compartmentname Quantityname" or something like that instead.
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
check_flux_location(Mobius_Model *model, Source_Location source_loc, Var_Location &loc) {
	if(!is_located(loc)) return;
	auto hopefully_a_quantity = model->find_entity(loc.property_or_quantity);
	if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
		source_loc.print_error_header();
		fatal_error("Fluxes can only be assigned to quantities. \"", hopefully_a_quantity->handle_name, "\" is a property, not a quantity.");
	}
	for(int idx = 0; idx < loc.n_dissolved; ++idx) {
		auto hopefully_a_quantity = model->find_entity(loc.dissolved_in[idx]);
		if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
			source_loc.print_error_header();
			fatal_error("Fluxes can only be assigned to quantities. \"", hopefully_a_quantity->handle_name, "\" is a property, not a quantity.");
		}
	}
	Var_Id var_id = model->state_vars[loc];
	if(!is_valid(var_id)) {
		source_loc.print_error_header();
		error_print("The variable location ");
		error_print_location(model, loc);
		fatal_error(" has not been created using a \"has\" declaration.");
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

#include <bitset>

void
replace_conc(Mobius_Model *model, Math_Expr_FT *expr) {
	for(auto arg : expr->exprs) replace_conc(model, arg);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if((ident->variable_type == Variable_Type::state_var) && (ident->flags & ident_flags_conc)) {
			
			auto var = model->state_vars[ident->state_var];
			if(!is_valid(var->dissolved_conc)) {
				expr->location.print_error_header();
				fatal_error("This variable does not have a concentration");
			}
			ident->state_var = var->dissolved_conc;
			ident->flags = (Identifier_Flags)(ident->flags & ~ident_flags_conc);
		}
	}
}

void
restrictive_lookups(Math_Expr_FT *expr, Decl_Type decl_type, std::set<Entity_Id> &parameter_refs) {
	//TODO : Should we just reuse register_dependencies() ?
	for(auto arg : expr->exprs) restrictive_lookups(arg, decl_type, parameter_refs);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type != Variable_Type::local 
			&& ident->variable_type != Variable_Type::parameter) {
			expr->location.print_error_header();
			fatal_error("The function body for a ", name(decl_type), " declaration is only allowed to look up parameters, no other types of state variables.");
		} else if (ident->variable_type == Variable_Type::parameter)
			parameter_refs.insert(ident->parameter);
	}
}

bool
location_indexes_below_location(Mobius_Model *model, Var_Location &loc, Var_Location &below_loc) {
	
	if(!is_located(loc) || !is_located(below_loc))
		fatal_error(Mobius_Error::internal, "Got a non-located location to a location_indexes_below_location() call.");
	// TODO: when we implement distributing substances over index sets, we have to take that into account here.
	auto comp       = model->find_entity<Reg_Type::compartment>(loc.compartment); //NOTE: right now we are only guaranteed that loc.compartment is valid. See note in parameter_indexes_below_location.
	auto below_comp = model->find_entity<Reg_Type::compartment>(below_loc.compartment);
	
	for(auto index_set : comp->index_sets) {
		if(std::find(below_comp->index_sets.begin(), below_comp->index_sets.end(), index_set) == below_comp->index_sets.end())
			return false;
	}
	return true;
}

bool
parameter_indexes_below_location(Mobius_Model *model, Entity_Id par_id, Var_Location &below_loc) {
	auto par = model->find_entity<Reg_Type::parameter>(par_id);
	auto par_comp_id = model->find_entity<Reg_Type::par_group>(par->par_group)->compartment;
	// TODO: invalid compartment should only happen for the "System" par group, and in that case we should probably not have referenced that parameter, but it is a bit out of scope for this function to handle it.
	if(!is_valid(par_comp_id)) return false;
	// TODO: need a better system for looking up things that automatically gives us the global one if it exists.
	auto par_comp = model->find_entity<Reg_Type::compartment>(par_comp_id);
	if(is_valid(par_comp->global_id))
		par_comp_id = par_comp->global_id;
	
	// NOTE: This is a bit of a hack that allows us to reuse location_indexes_below_location. We have to monitor that it doesn't break.
	Var_Location loc;
	loc.type = Var_Location::Type::located;
	loc.compartment = par_comp_id;
	
	return location_indexes_below_location(model, loc, below_loc);
}

void
check_valid_distribution_of_dependencies(Mobius_Model *model, Math_Expr_FT *function, State_Variable *var, bool initial) {
	Dependency_Set code_depends;
	register_dependencies(function, &code_depends);
	
	// NOTE: We should not have undergone codegen yet, so the source location of the top node of the function should be valid.
	Source_Location source_loc = function->location;
	
	Var_Location loc = var->loc1;
	loc = var->loc1;
	//TODO: There is a question about what to do with fluxes with source nowhere. Should we then check the target like we do here?
	//TODO: we just have to test how that works.
	if(!is_located(loc))
		loc = var->loc2;

	if(!is_located(loc))
		fatal_error(Mobius_Error::internal, "Somehow a totally unlocated variable checked in check_valid_distribution_of_dependencies().");
	
	// TODO: It may be better to have these correctness tests in model_composition, but then we would have to look up the dependency sets twice (can't keep them because some of the function trees undergo codegen in between).
	
	String_View err_begin = initial ? "The code for the initial value of the state variable \"" : "The code for the state variable \"";
	
	for(auto par_id : code_depends.on_parameter) {
		if(!parameter_indexes_below_location(model, par_id, loc)) {
			source_loc.print_error_header();
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the parameter \"", model->find_entity<Reg_Type::parameter>(par_id)->name, "\". This parameter belongs to a compartment that is distributed over a higher number of index sets than the the state variable.");
		}
	}
	for(auto series_id : code_depends.on_series) {
		if(!location_indexes_below_location(model, model->series[series_id]->loc1, loc)) {
			source_loc.print_error_header();
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the input series \"", model->series[series_id]->name, "\". This series belongs to a compartment that is distributed over a higher number of index sets than the state variable.");
		}
	}
	
	for(auto dep : code_depends.on_state_var) {
		auto dep_var = model->state_vars[dep.var_id];
		
		// For generated in_flux aggregation variables we are instead interested in the variable that is the target of the fluxes.
		if(dep_var->flags & State_Variable::Flags::f_in_flux)
			dep_var = model->state_vars[dep_var->in_flux_target];
		
		// If it is an aggregate, index set dependencies will be generated to be correct.
		if(dep_var->flags & State_Variable::Flags::f_is_aggregate)
			continue;
		
		// If it is a conc, check vs the mass instead.
		if(dep_var->flags & State_Variable::Flags::f_dissolved_conc)
			dep_var = model->state_vars[dep_var->dissolved_conc];
		
		if(dep_var->type == Decl_Type::flux || !is_located(dep_var->loc1))
			fatal_error(Mobius_Error::internal, "Somehow a direct lookup of a flux or unlocated variable \"", dep_var->name, "\" in code tested with check_valid_distribution_of_dependencies().");
		
		if(!location_indexes_below_location(model, dep_var->loc1, loc)) {
			source_loc.print_error_header();
			fatal_error(err_begin, var->name, "\" looks up the state variable \"", dep_var->name, "\". The latter state variable is distributed over a higher number of index sets than the the prior.");
		}
	}
}

void
Mobius_Model::compose() {
	warning_print("compose begin\n");
	
	
	//TODO: make better name generation system!
	char varname[1024];
	
	/*
	TODO: we have to check for mismatching has declarations or re-declaration of code multiple places.
		only if there is no code anywhere can we be sure that it is a series.
		So maybe a pass first to check this, *then* do the state var registration.
	*/
	
	// TODO: we should check that all referenced entities in all modules have actually been declared ( has_been_declared flag on Entity_Registration )
	
	warning_print("State var registration begin.\n");
	
	std::vector<Var_Id> dissolvedes;
	
	for(int n_dissolved = 0; n_dissolved < max_dissolved_chain; ++n_dissolved) { // NOTE: We have to process these in order so that e.g. soil.water exists when we process soil.water.oc
		int module_id = -1;
		for(auto module : modules) {
			++module_id;
			if(module_id == 0) continue;
		
			for(Entity_Id id : module->hases) {
				auto has = module->hases[id];
				if(has->var_location.n_dissolved != n_dissolved) continue;
				
				for(int idx = 0; idx < n_dissolved; ++idx) {
					auto diss_in = find_entity(has->var_location.dissolved_in[idx]);
					if(diss_in->decl_type != Decl_Type::quantity) {
						has->location.print_error_header();
						fatal_error("Only compartments or quantities can be assigned something using a \"has\". \"", diss_in->name, "\" is a property, not a quantity.");
					}
				}
				Decl_Type type = find_entity(has->var_location.property_or_quantity)->decl_type;
				if(n_dissolved > 0) {
					if(type != Decl_Type::quantity) {
						has->location.print_error_header();
						fatal_error("Properties can only be assigned to compartments, not to quantities.");   //TODO: (for now!)
					}
					Var_Location above_loc = remove_dissolved(has->var_location);
					
					auto above_var = state_vars[above_loc];
					if(!is_valid(above_var)) {
						has->location.print_error_header();
						fatal_error("The located quantity that this \"has\" declaration is assigned to has not itself been created using a \"has\" declaration.");
					}
				}
				
				bool is_series = !has->code && (type != Decl_Type::quantity); // TODO: this can't be determined this way! We have instead to do a pass later to see if it was given code somewhere else!
				Var_Id var_id = register_state_variable(this, Decl_Type::has, id, is_series);
				if(n_dissolved > 0)
					dissolvedes.push_back(var_id);
			}
		}
	}
	
	int module_id = -1;
	for(auto module : modules) {
		++module_id;
		if(module_id == 0) continue;
		
		for(Entity_Id id : module->fluxes) {
			auto flux = module->fluxes[id];

			check_flux_location(this, flux->location, flux->source);
			check_flux_location(this, flux->location, flux->target);
			
			register_state_variable(this, Decl_Type::flux, id, false);
		}
	}
	
	warning_print("Generate fluxes and concentrations for dissolved quantities.\n");
	
	// TODO: Not sure if this system works correctly for chain-dissolved quantities yet (dissolved in dissolved)!
	for(auto var_id : dissolvedes) {
		auto var = state_vars[var_id];
		// these are already guaranteed since we put it in dissolvedes..
		//if(var->type != Decl_Type::quantity) continue;
		//if(var->loc1.n_dissolved == 0) continue;
		
		auto above_loc = remove_dissolved(var->loc1);
		std::vector<Var_Id> generate;
		for(auto flux_id : state_vars) {
			auto flux = state_vars[flux_id];
			if(flux->type != Decl_Type::flux) continue;
			if(!(flux->loc1 == above_loc)) continue;
			if(is_located(flux->loc2)) {
				// If the target of the flux is a specific location, we can only send the dissolved quantity if it exists in that location.
				auto below_loc = add_dissolved(this, flux->loc2, var->loc1.property_or_quantity);
				auto below_var = state_vars[below_loc];
				if(!is_valid(below_var)) continue;
			}
			// See if it was declared that this flux should not carry this quantity (using a no_carry declaration)
			auto flux_reg = find_entity<Reg_Type::flux>(flux->entity_id);
			if(std::find(flux_reg->no_carry.begin(), flux_reg->no_carry.end(), var->loc1) != flux_reg->no_carry.end()) continue;
			
			// TODO: need system to exclude some fluxes for this quantity (e.g. evapotranspiration should not carry DOC).
			generate.push_back(flux_id);
		}
		
		Var_Location source  = var->loc1; // Note we have to copy it since we start registering new state variables, invalidating our pointer to var.
		String_View var_name = var->name;
		
		sprintf(varname, "conc(%.*s)", var_name.count, var_name.data);
		Var_Id gen_conc_id = register_state_variable(this, Decl_Type::has, invalid_entity_id, false, allocator.copy_string_view(varname));
		auto conc_var = state_vars[gen_conc_id];
		conc_var->flags = State_Variable::Flags::f_dissolved_conc;
		conc_var->dissolved_conc = var_id;
		state_vars[var_id]->dissolved_conc = gen_conc_id;
		
		for(auto flux_id : generate) {
			String_View flux_name = state_vars[flux_id]->name;
			sprintf(varname, "dissolved_flux(%.*s, %.*s)", var_name.count, var_name.data, flux_name.count, flux_name.data);
			Var_Id gen_flux_id = register_state_variable(this, Decl_Type::flux, invalid_entity_id, false, allocator.copy_string_view(varname));
			auto gen_flux = state_vars[gen_flux_id];
			auto flux = state_vars[flux_id];
			gen_flux->flags = State_Variable::Flags::f_dissolved_flux;
			gen_flux->dissolved_flux = flux_id;
			gen_flux->dissolved_conc = gen_conc_id;
			gen_flux->loc1 = source;
			gen_flux->loc2.type = flux->loc2.type;
			if(is_located(flux->loc2))
				gen_flux->loc2 = add_dissolved(this, flux->loc2, source.property_or_quantity);
			if(is_valid(flux->neighbor))
				gen_flux->neighbor = flux->neighbor;
		}
	}

	warning_print("Function tree resolution begin.\n");
	
	// TODO: check if there are unused aggregation data or unit conversion data!
	// TODO: warning or error if a flux is still marked as "out" (?)   -- though this could be legitimate in some cases where you just want to run a sub-module and not link it up with anything.
	
	Var_Map in_flux_map;
	Var_Map2 needs_aggregate;
	Var_Map2 needs_aggregate_initial;
	
	std::set<Var_Id> may_need_neighbor_target;
	
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		Math_Expr_AST *unit_conv_ast = nullptr;
		Math_Expr_AST *override_ast = nullptr;
		bool override_is_conc = false;
		bool initial_is_conc = false;
		
		Entity_Id in_compartment = invalid_entity_id;
		Entity_Id from_compartment = invalid_entity_id;
		if(var->type == Decl_Type::flux) {
			bool target_was_out = false;
			if(is_valid(var->entity_id)) {
				auto flux_decl = find_entity<Reg_Type::flux>(var->entity_id);
				target_was_out = flux_decl->target_was_out;
				ast = flux_decl->code;
			}
			bool target_is_located = is_located(var->loc2) && !target_was_out; // Note: the target could have been re-directed by the model. We only care about how it was declared
			if(is_located(var->loc1)) {
				from_compartment = var->loc1.compartment;
				if(!target_is_located || var->loc1 == var->loc2)	
					in_compartment = from_compartment;
			} else if(target_is_located) {
				in_compartment = var->loc2.compartment;
			}
			if(!is_valid(from_compartment)) from_compartment = in_compartment;
			// note : the only case where from_compartment != in_compartment is when both source and target are located, but different. In that case in_compartment is invalid, but from_compartment is not.
			
			if(is_valid(var->neighbor)) {
				Var_Id target_id = state_vars[var->loc1]; // loc1==loc2 for neighbor fluxes.
				may_need_neighbor_target.insert(state_vars[var->loc1]); // We only need these for non-discrete variables.
			}
			
			if(is_located(var->loc1)) {
				for(auto &unit_conv : modules[0]->compartments[var->loc1.compartment]->unit_convs) {
					if(var->loc1 == unit_conv.source && var->loc2 == unit_conv.target)
						unit_conv_ast = unit_conv.code;
				}
			}
		} else if((var->type == Decl_Type::property || var->type == Decl_Type::quantity) && is_valid(var->entity_id)) {
			auto has = find_entity<Reg_Type::has>(var->entity_id);
			ast      = has->code;
			init_ast = has->initial_code;
			override_ast = has->override_code;
			override_is_conc = has->override_is_conc;
			initial_is_conc  = has->initial_is_conc;
			
			if(override_ast && (var->type != Decl_Type::quantity || (override_is_conc && (var->loc1.n_dissolved == 0)))) {
				override_ast->location.print_error_header();
				fatal_error("Either got an \".override\" block on a property or a \".override_conc\" block on a non-dissolved variable.");
			}
			
			from_compartment = in_compartment = has->var_location.compartment;
		}
		
		// TODO: instead of passing the in_compartment, we could just pass the var_id and give the function resolution more to work with.
		Function_Resolve_Data res_data = { this, var->entity_id.module_id, in_compartment };
		if(ast) {
			var->function_tree = make_cast(resolve_function_tree(ast, &res_data), Value_Type::real);
			replace_conc(this, var->function_tree); // Replace explicit conc() calls by pointing them to the conc variable.
			find_other_flags(var->function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, false);
		} else
			var->function_tree = nullptr; // NOTE: this is for substances. They are computed a different way.
		
		if(init_ast) {
			//TODO: handle initial_is_conc!
			var->initial_function_tree = make_cast(resolve_function_tree(init_ast, &res_data), Value_Type::real);
			remove_lasts(var->initial_function_tree, true);
			replace_conc(this, var->initial_function_tree);  // Replace explicit conc() calls by pointing them to the conc variable
			find_other_flags(var->initial_function_tree, in_flux_map, needs_aggregate_initial, var_id, from_compartment, true);
			var->initial_is_conc = initial_is_conc;
			
			if(initial_is_conc && (var->type != Decl_Type::quantity ||(var->loc1.n_dissolved == 0))) {
				init_ast->location.print_error_header();
				fatal_error("Got an \"initial_conc\" block for a non-dissolved variable");
			}
		} else
			var->initial_function_tree = nullptr;
		
		if(unit_conv_ast) {
			auto res_data2 = res_data;
			res_data2.module_id = 0;
			var->unit_conversion_tree = make_cast(resolve_function_tree(unit_conv_ast, &res_data2), Value_Type::real);
			std::set<Entity_Id> parameter_refs;
			restrictive_lookups(var->unit_conversion_tree, Decl_Type::unit_conversion, parameter_refs);

			for(auto par_id : parameter_refs) {
				bool ok = parameter_indexes_below_location(this, par_id, var->loc1);
				if(!ok) {
					unit_conv_ast->location.print_error_header();
					fatal_error("The parameter \"", find_entity<Reg_Type::parameter>(par_id)->handle_name, "\" belongs to a compartment that is distributed over index sets that the source compartment of the unit conversion is not distributed over."); 
				}
			}
		} else
			var->unit_conversion_tree = nullptr;
		
		if(override_ast) {
			var->override_tree = make_cast(resolve_function_tree(override_ast, &res_data), Value_Type::real);
			var->override_is_conc = override_is_conc;
			replace_conc(this, var->override_tree);
			find_other_flags(var->override_tree, in_flux_map, needs_aggregate, var_id, from_compartment, true);
			// TODO: Should audit this one for flags also!
			
			//TODO: what do we do with fluxes that has this as a source ?
		} else
			var->override_tree = nullptr;
	}
	
	// Invalidate dissolved fluxes if both source and target is overridden.
	
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_dissolved_flux) {
			bool valid_source = true;
			if(is_located(var->loc1)) {
				auto source = state_vars[state_vars[var->loc1]];
				if(source->override_tree)
					valid_source = false;
			} else
				valid_source = false;
			bool valid_target = true;
			if(is_located(var->loc2)) {
				auto target = state_vars[state_vars[var->loc2]];
				if(target->override_tree)
					valid_target = false;
			} else
				valid_target = false;
			if(!valid_source && !valid_target)
				var->flags = (State_Variable::Flags)(var->flags | State_Variable::Flags::f_invalid);
		}
	}
	
	if(needs_aggregate_initial.size() > 0)
		fatal_error(Mobius_Error::internal, "aggregate() declarations inside initial value code is not yet supported.");

	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	// TODO: interaction between in_flux and aggregate declarations (i.e. we have something like an explicit aggregate(in_flux(soil.water)) in the code.
	// Or what about last(aggregate(in_flux(bla))) ?
	
	// note: We always generate an aggregate if the source compartment of a flux has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the weight is trivial
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_invalid) continue;
		if(var->type != Decl_Type::flux) continue;
		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		
		if(!location_indexes_below_location(this, var->loc1, var->loc2))
			needs_aggregate[var_id.id].first.insert(var->loc2.compartment);
	}
	
	warning_print("Generate state vars for aggregates.\n");
		
	for(auto &need_agg : needs_aggregate) {
		auto var_id = Var_Id {0, need_agg.first};   //TODO: Not good way to do it!
		auto var = state_vars[var_id];
		
		warning_print("**** Make aggregate for ", var->name, "\n");
		
		auto loc1 = var->loc1;
		if(var->flags & State_Variable::Flags::f_dissolved_conc)
			loc1 = state_vars[var->dissolved_conc]->loc1;
		
		auto source = find_entity<Reg_Type::compartment>(loc1.compartment);
		
		for(auto to_compartment : need_agg.second.first) {
			
			warning_print("to_compartment is ", find_entity(to_compartment)->name, "\n");
			
			Math_Expr_FT *agg_weight = nullptr;
			for(auto &agg : source->aggregations) {
				if(agg.to_compartment == to_compartment) {
					// Note: the module id is always 0 here since aggregation_weight should only be declared in model scope.
					Function_Resolve_Data res_data = { this, 0, invalid_entity_id };
					agg_weight = make_cast(resolve_function_tree(agg.code, &res_data), Value_Type::real);
					std::set<Entity_Id> parameter_refs;
					restrictive_lookups(agg_weight, Decl_Type::aggregation_weight, parameter_refs);
					
					for(auto par_id : parameter_refs) {
						bool ok = parameter_indexes_below_location(this, par_id, loc1);
						if(!ok) {
							agg.code->location.print_error_header();
							fatal_error("The parameter \"", find_entity<Reg_Type::parameter>(par_id)->handle_name, "\" belongs to a compartment that is distributed over index sets that the source compartment of the aggregation weight is not distributed over."); 
						}
					}
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
			
			sprintf(varname, "aggregate(%.*s)", var->name.count, var->name.data);
			Var_Id agg_id = register_state_variable(this, var->type, invalid_entity_id, false, allocator.copy_string_view(varname));
			
			auto agg_var = state_vars[agg_id];
			agg_var->flags = (State_Variable::Flags)(agg_var->flags | State_Variable::f_is_aggregate);
			agg_var->agg = var_id;
			agg_var->aggregation_weight_tree = agg_weight;
			
			var = state_vars[var_id];   //NOTE: had to look it up again since we may have resized the vector var pointed into

			// NOTE: it makes a lot of the operations in model_compilation more natural if we decouple the fluxes like this:
			agg_var->loc1.type = Var_Location::Type::nowhere;
			agg_var->loc2 = var->loc2;
			var->loc2.type = Var_Location::Type::nowhere;
			
			agg_var->unit_conversion_tree = var->unit_conversion_tree;
			// NOTE: it is easier to keep track of who is supposed to use the unit conversion if we only keep a reference to it on the one that is going to use it.
			//   we only multiply with the unit conversion at the point where we add the flux to the target, so it is only needed on the aggregate.
			var->unit_conversion_tree = nullptr;
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
				
				if(lu->function_tree)
					replace_flagged(lu->function_tree, var_id, agg_id, ident_flags_aggregate);
				if(lu->override_tree)
					replace_flagged(lu->override_tree, var_id, agg_id, ident_flags_aggregate);
			}
		}
	}
	
	
	
	
	// NOTE: Unfortunately we need to know about solvers here, but we don't want to store the solver on the State Variable since that encourages messy code in model_compilation.
	std::vector<int> has_solver;
	has_solver.resize(state_vars.count(), 0);
	
	for(auto id : modules[0]->solves) {
		auto solve = modules[0]->solves[id];
		Var_Id var_id = state_vars[solve->loc];
		if(!is_valid(var_id)) {
			solve->location.print_error_header();
			fatal_error("This compartment does not have that quantity.");  // TODO: give the handles names in the error message.
		}
		auto hopefully_a_quantity = find_entity(solve->loc.property_or_quantity);
		if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
			solve->location.print_error_header();
			fatal_error("Solvers can only be put on quantities, not on properties.");
		}
		has_solver[var_id.id] = 1;
	}
	
	// TODO: Should we give an error if there is a neighbor flux on an overridden variable?
	
	warning_print("Generate state vars for in_flux_neighbor.\n");
	// TODO: What happens if there are multiple neighbor fluxes for the same variable?
	//   currently it looks like only the last one will be added to the target in the end ( neighbor_agg is overwritten by the last one we create ).
	for(auto var_id : may_need_neighbor_target) {
		if(!has_solver[var_id.id]) continue;
		
		Var_Id n_agg_id = register_state_variable(this, Decl_Type::has, invalid_entity_id, false, "in_flux_neighbor"); //TODO: generate a better name
		auto n_agg_var = state_vars[n_agg_id];
		n_agg_var->flags = State_Variable::Flags::f_in_flux_neighbor;
		//n_agg_var->solver = solver;
		n_agg_var->neighbor_agg = var_id;
		
		state_vars[var_id]->neighbor_agg = n_agg_id;
	}
	
	warning_print("Generate state vars for in_flux.\n");
	for(auto &in_flux : in_flux_map) {
		Var_Id target_id = {0, in_flux.first}; //TODO: Not good way to do it!
		
		Var_Id in_flux_id = register_state_variable(this, Decl_Type::has, invalid_entity_id, false, "in_flux");   //TODO: generate a better name
		auto in_flux_var = state_vars[in_flux_id];
		
		in_flux_var->in_flux_target        = target_id;
		in_flux_var->function_tree         = nullptr; // This is instead generated in model_compilation
		in_flux_var->initial_function_tree = nullptr;
		in_flux_var->flags = (State_Variable::Flags)(in_flux_var->flags | State_Variable::f_in_flux);
		
		for(auto rep_id : in_flux.second)
			replace_flagged(state_vars[rep_id]->function_tree, target_id, in_flux_id, ident_flags_in_flux);
	}

	
	// See if the code for computing a variable looks up other values that are distributed over index sets that the var is not distributed over.
	for(auto var_id : state_vars) {
		auto var = state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_invalid) continue;
		
		if(var->function_tree)
			check_valid_distribution_of_dependencies(this, var->function_tree, var, false);
		if(var->initial_function_tree)
			check_valid_distribution_of_dependencies(this, var->initial_function_tree, var, true);
	}
	
	
	is_composed = true;
}







