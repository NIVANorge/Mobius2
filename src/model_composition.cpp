

/*
	This file is a part of a tightly linked system with model_compilation.cpp and model_codegen.cpp
	
	The functionality in this file is supposed to post-process declarations after model_declaration.cpp and check that they make sense,
	and organize the data for use in later code generation.
	
	These are important preparation steps before proper code generation can start.
	
	Functionality includes:
		- Figure out what state variables the model needs to keep track of
			- State variables that were declared in the model
			- Generate state variables for things like aggregates.
			- Set up data for all the state variables that is needed in model run
		- Resolve function code from ASTs.
			- Resolve symbols and point them to the right state variables, parameters, etc.
			- Consistency checks (many types)
		- Bake parameter values that can be considered constant (value put directly into the code instead of being looked up from memory - allows optimizations)
		- ...

*/


#include "model_application.h"
#include "function_tree.h"
#include "resolve_identifier.h"

#include <sstream>
#include <map>

void
check_if_var_loc_is_well_formed(Mobius_Model *model, Var_Location &loc, Source_Location &source_loc) {
	
	if(loc.type == Var_Location::Type::out) return;
	auto first = model->components[loc.first()];
	if(first->decl_type != Decl_Type::compartment) {
		source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("The first component of a variable location must be a compartment");
	}
	for(int idx = 1; idx < loc.n_components-1; ++idx) {
		auto comp = model->components[loc.components[idx]];
		if(comp->decl_type != Decl_Type::quantity) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Only quantities can be the middle components of a variable location.");
		}
	}
	auto last = model->components[loc.last()];
	if(last->decl_type == Decl_Type::compartment) {
		source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("The last component of a variable location must be a property or quantity.");
	}
}

template<State_Var::Type type> Var_Id
register_state_variable(Model_Application *app, Entity_Id decl_id, bool is_series, const std::string &name, bool no_store_override = false) {
	
	auto model = app->model;
	
	if(type == State_Var::Type::declared && !is_valid(decl_id))
		fatal_error(Mobius_Error::internal, "Didn't get a decl_id for a declared variable.");
	
	bool store_series = true;
	
	Var_Location loc = invalid_var_location;
	if(is_valid(decl_id)) {
		if(decl_id.reg_type == Reg_Type::var) {
			auto var = model->vars[decl_id];
			loc = var->var_location;
			check_if_var_loc_is_well_formed(model, loc, var->source_loc);
			store_series = var->store_series;
		} else if (decl_id.reg_type == Reg_Type::flux) {
			auto flux = model->fluxes[decl_id];
			store_series = flux->store_series;
		}
	}
	
	if(no_store_override)
		store_series = false;
	if(model->config.store_all_series)
		store_series = true;
	
	Var_Id::Type id_type = Var_Id::Type::state_var;
	if(is_series)
		id_type = Var_Id::Type::series;
	else if(!store_series)
		id_type = Var_Id::Type::temp_var;
	
	Var_Id var_id = app->vars.register_var<type>(loc, name, id_type);
	auto var = app->vars[var_id];
	
	if(var->name.empty())
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
	var->type          = type;
	var->store_series  = store_series;

	if(is_valid(decl_id)) {
		auto var2 = as<State_Var::Type::declared>(var);
		var2->decl_id = decl_id;
		
		if(decl_id.reg_type == Reg_Type::var) {
			auto var_decl = model->vars[decl_id];
			var->loc1 = loc;
			var2->decl_type = model->components[loc.last()]->decl_type;
			if(is_valid(var_decl->unit))
				var->unit = model->units[var_decl->unit]->data;
			
		} else if (decl_id.reg_type == Reg_Type::flux) {
			auto flux = model->fluxes[decl_id];
			var->loc1 = flux->source;
			var->loc2 = flux->target;
			var->bidirectional = flux->bidirectional;
			var->mixing_base   = flux->mixing;
			var2->decl_type = Decl_Type::flux;
			var2->set_flag(State_Var::flux);
			
			if(is_valid(flux->unit))
				var->unit = model->units[flux->unit]->data;
		} else
			fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	//warning_print("**** register ", var.name, is_series ? " as series" : " as state var", "\n");
	
	return var_id;
}

void
check_location(Model_Application *app, Source_Location &source_loc, Specific_Var_Location &loc, bool must_quantity = false, bool is_flux = false, bool is_source = false) {
	if(!is_located(loc)) return;
	
	auto model = app->model;
	
	Var_Id var_id = app->vars.id_of(loc);
	if(!is_valid(var_id)) {
		source_loc.print_error_header(Mobius_Error::model_building);
		error_print("The location ");
		error_print_location(model, loc);
		fatal_error(" has not been created using a 'var' declaration.");
	}
	
	// This check could be moved to model_declaration when the location is generated?
	if(must_quantity && model->components[loc.last()]->decl_type != Decl_Type::quantity) {
		source_loc.print_error_header(Mobius_Error::model_building);
		error_print("The location ");
		error_print_location(model, loc);
		fatal_error(" refers to something that is not a quantity. This is not allowed in this context.");
	}
	
	if(!is_flux) return;
	
	if(is_valid(loc.r1.connection_id)) {
		auto conn = app->model->connections[loc.r1.connection_id];
		if(conn->type == Connection_Type::grid1d) {
			if(is_source && loc.r1.type == Restriction::specific) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("'specific' can't be in the source of a flux for now.");
			}
		} else {
			if(is_source) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("For this connection type, the connection can't be specified in the source of the flux.");
			} else if (loc.r1.type != Restriction::below) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This connection type can't have fluxes with specified locations.");
			}
		}
		if(loc.r1.type == Restriction::above) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Fluxes can't go to 'above', instead declare a @bidirectional flux in the opposite direction.");
		}
		// TODO: We could just do the checks on loc.r2 here ?
		
		//TODO: This check should NOT use the connection_components. It must use the component options that are stored on the flux.
		auto &components = app->connection_components[loc.r1.connection_id].components;
		bool found = false;
		for(int idx = 0; idx < loc.n_components; ++idx) {
			for(auto &comp : components)
				if(loc.components[idx] == comp.id) found = true;
		}
		if(!found) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("None of the components on this var location are supported for the connection \"", conn->name, "\".");
		}
			
	} else if(loc.r1.type != Restriction::none) {
		fatal_error(Mobius_Error::internal, "Got a var location with a restriction that was not tied to a connection.");
	}
}

struct
Code_Special_Lookups {
	std::map<std::tuple<Var_Id, Entity_Id, bool>, std::vector<Var_Id>>     in_fluxes;
	std::map<Var_Id, std::pair<std::set<Entity_Id>, std::vector<Var_Id>>>  aggregates;
	/* aggregates[agg_of] is a pair ( (to_compartment), (looked_up_by) )
	  where
		agg_of is the variable that needs an aggregation variable
		to_compartment is a set of compartments whose point of view we need an aggregate from
		looked_up_by   is a list of other variables who need to veiw this aggregate from their function.
	*/
	std::map<Entity_Id, std::pair<std::set<Entity_Id>, std::vector<Var_Id>>> par_aggregates;
};

void
find_identifier_flags(Model_Application *app, Math_Expr_FT *expr, Code_Special_Lookups *specials, Var_Id looked_up_by, Entity_Id lookup_compartment) {
	for(auto arg : expr->exprs) find_identifier_flags(app, arg, specials, looked_up_by, lookup_compartment);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		
		auto ident = static_cast<Identifier_FT *>(expr);
			
		if(specials && ident->has_flag(Identifier_FT::in_flux))
			specials->in_fluxes[{ident->var_id, ident->other_connection, false}].push_back(looked_up_by);
		else if (specials && ident->has_flag(Identifier_FT::out_flux))
			specials->in_fluxes[{ident->var_id, ident->other_connection, true}].push_back(looked_up_by);
		
		if(ident->has_flag(Identifier_FT::aggregate)) {
			if(!is_valid(lookup_compartment)) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Can't use aggregate() in this function body because it does not belong to a compartment.");
			}
			
			if(specials) {
				if(ident->variable_type == Variable_Type::parameter) {
					auto &agg_data = specials->par_aggregates[ident->par_id];
					agg_data.first.insert(lookup_compartment);
					if(is_valid(looked_up_by))
						agg_data.second.push_back(looked_up_by);
				} else if (ident->is_computed_series()) {
					auto &agg_data = specials->aggregates[ident->var_id];
					agg_data.first.insert(lookup_compartment);
					if(is_valid(looked_up_by))
						agg_data.second.push_back(looked_up_by);
				} else if (ident->is_input_series()) {
					//TODO: Why did we determine that the state_var way of doing it doesn't work for input series?
					// TODO: Make it use the above code and debug what happens (fix errors).
					fatal_error(Mobius_Error::internal, "aggregate() is not implemented for input series.");
				} else
					fatal_error(Mobius_Error::internal, "Got an aggregate() for an unexpected variable type.");
			}	
		}
	}
}


Math_Expr_FT *
replace_flagged(Math_Expr_FT *expr, Var_Id replace_this, Var_Id with, Identifier_FT::Flags flag, Entity_Id connection_id = invalid_entity_id) {
	
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = replace_flagged(expr->exprs[idx], replace_this, with, flag, connection_id);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	if(ident->is_computed_series() 
		&& (ident->var_id == replace_this)
		&& (ident->has_flag(flag))
		&& ((ident->other_connection == connection_id) || (!is_valid(ident->other_connection) && !is_valid(connection_id)))
		) {
		
		if(is_valid(with)) {
			ident->var_id = with;
			ident->remove_flag(flag);
			ident->other_connection = invalid_entity_id;
			//return ident;
		} else {
			delete expr;
			return make_literal((double)0.0);
			ident->source_loc.print_log_header(Log_Mode::dev);
			log_print(Log_Mode::dev, "Warning: The specified variable does not exist, it is replaced by a 0.\n");
		}
	}

	return expr;
}

Math_Expr_FT *
replace_flagged_par(Math_Expr_FT *expr, Entity_Id replace_this, Var_Id with, Identifier_FT::Flags flag) {
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = replace_flagged_par(expr->exprs[idx], replace_this, with, flag);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	if(ident->variable_type == Variable_Type::parameter
		&& (ident->par_id == replace_this)
		&& (ident->has_flag(flag))
	) {
		//ident->par_id = invalid_entity_id; // Ooops, since this is a union, this should not be set along with ident->var_id
		ident->var_id = with;
		ident->variable_type = Variable_Type::series;
		ident->remove_flag(flag);
	}
	
	return expr;
}

Index_Set_Tuple
get_allowed_index_sets(Model_Application *app, Specific_Var_Location &loc1, Specific_Var_Location &loc2 = Specific_Var_Location() ) {
	
	auto model = app->model;
	
	Index_Set_Tuple index_sets;
	
	// Find the primary location
	Specific_Var_Location *loc = &loc1;
	if(!is_located(loc1))
		loc = &loc2;
	if(!is_located(*loc))
		return index_sets;
	
	// TODO: We should be able to get an exclude from both restrictions (if applicable).
	Entity_Id exclude = avoid_index_set_dependency(app, *loc);
	
	// The purpose of this first check is to allow a flux on a graph connection to reference something that depends on the edge index set of the graph situated at the source of the flux.
	if(is_valid(loc2.r1.connection_id)) {
		auto conn = model->connections[loc2.r1.connection_id];
		if(conn->type == Connection_Type::directed_graph) {
			auto find_source = app->find_connection_component(loc2.r1.connection_id, loc1.components[0], false);
			if(find_source && find_source->is_edge_indexed && conn->edge_index_set != exclude)
				insert_dependency(model, index_sets, conn->edge_index_set);
		}
	}
	
	for(int idx = 0; idx < loc->n_components; ++idx) {
		for(auto index_set : model->components[loc->components[idx]]->index_sets) {
			if(index_set != exclude)
				insert_dependency(model, index_sets, index_set);
		}
	}
	
	return index_sets;
}


bool
index_set_is_contained_in(Mobius_Model *model, Entity_Id first, Index_Set_Tuple &second) {
	
	if(second.has(first)) return true;
	
	// A union member *can* be used to address the union.
	auto set = model->index_sets[first];
	if(!set->union_of.empty()) {
		for(auto ui_id : set->union_of) {
			if(second.has(ui_id))
				return true;
		}
	}
	
	return false;
}


bool
index_sets_are_contained_in(Mobius_Model *model, Index_Set_Tuple &first, Index_Set_Tuple &second) {
	bool success = true;
	for(auto set : first) {
		if(!index_set_is_contained_in(model, set, second)) {
			success = false;
			break;
		}
	}
	return success;
}

bool
parameter_indexes_below_location(Model_Application *app, const Identifier_Data &dep, Index_Set_Tuple &allowed_index_sets) {
		
	auto model = app->model;
	auto par = model->parameters[dep.par_id];
	auto group = model->par_groups[par->scope_id];
	
	Entity_Id exclude = avoid_index_set_dependency(app, dep.restriction);
	
	Index_Set_Tuple maximal_group_sets = group->max_index_sets;
	maximal_group_sets.remove(exclude); // TODO: Is this always correct? Do we have to take into consideration removing union members if we remove a union??
	
	return index_sets_are_contained_in(model, maximal_group_sets, allowed_index_sets);
}

void
check_valid_distribution_of_dependencies(Model_Application *app, Math_Expr_FT *function, Index_Set_Tuple &allowed_index_sets) {
	
	// TODO: The loc in this function is really the below_loc. Maybe rename it to avoid confusion.
	
	auto model = app->model;
	
	std::set<Identifier_Data> code_depends;
	register_dependencies(function, &code_depends);
	
	// NOTE: We should not have undergone codegen yet, so the source location of the top node of the function should be valid.
	Source_Location source_loc = function->source_loc;
	
	// TODO: in these error messages we should really print out the two tuples of index sets.
	for(auto &dep : code_depends) {
		
		if(dep.variable_type == Variable_Type::parameter) {
			
			if(!parameter_indexes_below_location(app, dep, allowed_index_sets)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This code looks up the parameter \"", app->model->parameters[dep.par_id]->name, "\". This parameter belongs to a component that is distributed over a higher number of index sets than the context location of the code.");
			}
		} else if (dep.is_input_series()) {
			
			Specific_Var_Location ident_loc(app->vars[dep.var_id]->loc1, dep.restriction);
			auto maximal_series_sets = get_allowed_index_sets(app, ident_loc);
			
			if(!index_sets_are_contained_in(model, maximal_series_sets, allowed_index_sets)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This code looks up the input series \"", app->vars[dep.var_id]->name, "\". This series has a location that is distributed over a higher number of index sets than the context location of the code.");
			}
		
		} else if (dep.is_computed_series()) {
			auto dep_var = app->vars[dep.var_id];
		
			// For generated in_flux aggregation variables we are instead interested in the variable that is the target of the fluxes.
			if(dep_var->type == State_Var::Type::in_flux_aggregate)
				dep_var = app->vars[as<State_Var::Type::in_flux_aggregate>(dep_var)->in_flux_to];
			
			// If it is an aggregate, index set dependencies will be generated to be correct.
			if(dep_var->type == State_Var::Type::regular_aggregate || dep_var->type == State_Var::Type::parameter_aggregate)
				continue;
			
			// If it is a conc, check vs the mass instead.
			if(dep_var->type == State_Var::Type::dissolved_conc) {
				auto var2 = as<State_Var::Type::dissolved_conc>(dep_var);
				dep_var = app->vars[var2->conc_of];
			}
			
			if(dep_var->type == State_Var::Type::connection_aggregate)
				dep_var = app->vars[as<State_Var::Type::connection_aggregate>(dep_var)->agg_for];
			
			if(dep_var->is_flux() || !is_located(dep_var->loc1))
				fatal_error(Mobius_Error::internal, "Somehow a direct lookup of a flux or unlocated variable \"", dep_var->name, "\" in code tested with check_valid_distribution_of_dependencies().");
			
			Specific_Var_Location var_loc(dep_var->loc1, dep.restriction);
			auto maximal_var_sets = get_allowed_index_sets(app, var_loc);
			
			if(!index_sets_are_contained_in(model, maximal_var_sets, allowed_index_sets)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This code looks up the state variable \"", dep_var->name, "\". The latter state variable is distributed over a higher number of index sets than the context location of the prior code.");
			}
		} else if (dep.variable_type == Variable_Type::is_at) {
			auto conn_id = dep.restriction.r1.connection_id;
			if(!is_valid(conn_id)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This 'is_at' expression does not reference a connection.");
			}
			auto conn = model->connections[conn_id];
			if(conn->type != Connection_Type::grid1d) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("An 'is_at' is only supported for grid1d connections.");
			}
			auto index_set = conn->node_index_set;
			if(!index_set_is_contained_in(model, index_set, allowed_index_sets)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This 'is_at' expression references a connection over an index set \"", model->index_sets[index_set]->name, "\" that the context location of the code does not index over.");
			}
		}
	}
}

inline bool 
has_code(Var_Registration *var) {
	return var->code || var->initial_code || var->override_code;
}

void
resolve_no_carry(Model_Application *app, State_Var *var) {
	
	if(!var->is_flux() || var->type != State_Var::Type::declared) return; // Should not happen, but whatever.
	
	auto model = app->model;
	auto var2 = as<State_Var::Type::declared>(var);
	auto flux = model->fluxes[var2->decl_id];
	
	if(!flux->no_carry_ast && !flux->no_carry_by_default) return;
	
	if(!is_located(var->loc1))
		fatal_error(Mobius_Error::internal, "Got a flux without a source that has a no_carry.\n"); // NOTE: should be checked already in the model_declaration stage.

	if(flux->no_carry_by_default) {
		var2->no_carry_by_default = true;
		return;
	}
	
	auto code_scope = model->get_scope(flux->scope_id);
	Function_Resolve_Data res_data = { app, code_scope, var->loc1 };
	res_data.value_last_only = false;
	auto res = resolve_function_tree(flux->no_carry_ast, &res_data);
	
	for(auto expr : res.fun->exprs) {
		if(expr->expr_type != Math_Expr_Type::identifier) {
			expr->source_loc.print_error_header();
			fatal_error("Only quantity identifiers are allowed in a 'no_carry'.");
		}
		auto ident = static_cast<Identifier_FT *>(expr);
		if(!ident->is_computed_series()) {
			ident->source_loc.print_error_header();
			fatal_error("Only state variables are relevant for a 'no_carry'.");
		}
		if(ident->flags != Identifier_Data::Flags::none || ident->restriction.r1.type != Restriction::none) {
			ident->source_loc.print_error_header();
			fatal_error("Only plain variable identifiers are allowed in a 'no_carry'.");
		}
		auto carry_var = as<State_Var::Type::declared>(app->vars[ident->var_id]);
		auto loc = remove_dissolved(carry_var->loc1);
		if(loc != var->loc1 || carry_var->decl_type != Decl_Type::quantity) {
			ident->source_loc.print_error_header();
			fatal_error("This flux could not have carried this as a dissolved quantity.");
		}
		
		var2->no_carry.push_back(ident->var_id);
	}
	
	delete res.fun;
}

void
register_external_computations(Model_Application *app, std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> &external_targets) {
	
	// Unfortunately we have to do the below checks before resolving the function tree, because we need to know what locations are the targets of external computations before state variables are registered. Otherwise, a variable could be erroneously registered as an input series (since it lacked code), but should instead be computed by the external computation.
	
	// For now we just have a list here of allowed external functions (so that somebody don't call into some system function or something).
	// This should be replaced with a better system later.
	
	auto allowed = {
		"_special_test_",
		"nivafjord_place_river_flux",
		"nivafjord_place_horizontal_fluxes",
		"nivafjord_compute_pressure_difference",
		"nivafjord_vertical_realignment",
		"nivafjord_vertical_realignment_even_distrib",
		"nivafjord_vertical_realignment_slow",
		"magic_core",
	};
	
	auto model = app->model;
	
	for(auto external_id : model->external_computations) {
		
		auto external = model->external_computations[external_id];
		
		if(std::find(allowed.begin(), allowed.end(), external->function_name) == allowed.end()) {
			external->source_loc.print_error_header();
			fatal_error("The function \"", external->function_name, "\" is not among the allowed functions in an external computation.");
		}

		Var_Id var_id = register_state_variable<State_Var::Type::external_computation>(app, invalid_entity_id, false, external->name);
		as<State_Var::Type::external_computation>(app->vars[var_id])->decl_id = external_id;
		
		bool success = true;
		for(auto expr : external->code->exprs) {
			
			bool is_result = false;
			bool is_last   = false;
			auto check_expr = expr;
			if(expr->type == Math_Expr_Type::function_call) {
				auto fun = static_cast<Function_Call_AST *>(expr);
				
				auto &name = fun->name.string_value;
				is_result = (name == "result");
				is_last   = (name == "last");
				
				if((!is_result && !is_last) || fun->exprs.size() != 1) {
					success = false;
					break;
				}
				
				check_expr = fun->exprs[0];
			}
			if(check_expr->type != Math_Expr_Type::identifier) {
				success = false;
				break;
			}
			auto ident = static_cast<Identifier_Chain_AST *>(check_expr);
			if(is_result && !ident->bracketed_chain.empty()) {
				success = false;
				break;
			}
			if(!is_result) continue;
			
			auto scope = model->get_scope(external->scope_id);
			
			// TODO: This is a stupid way to do it. Instead make a function that takes the
			// chain directly.
			Argument_AST arg;
			arg.chain = ident->chain;
			
			Var_Location loc;
			resolve_simple_loc_argument(model, scope, &arg, loc);
			
			auto find = external_targets.find(loc);
			if(find != external_targets.end()) {
				ident->source_loc.print_error_header();
				error_print("This variable is declared as a 'result' of more than one external computation. See a different declaration here:\n");
				model->external_computations[as<State_Var::Type::external_computation>(app->vars[find->second])->decl_id]->source_loc.print_error();
				mobius_error_exit();
			}
			external_targets[loc] = var_id;
		}
		
		if(!success) {
			external->code->source_loc.print_error_header();
			fatal_error("A 'external_computation' body should just contain a list of identifiers of variables that go into the computation. Targets of the computation can be enclosed with a result(). Targets can not have a bracket restriction. Targets can not be last()");
		}
	}
}

inline bool
unit_match(Mobius_Model *model, Entity_Id unit1, Entity_Id unit2) {
	if(is_valid(unit1) != is_valid(unit2)) return false;
	if(!is_valid(unit1) && !is_valid(unit2)) return true;
	
	auto &u1 = model->units[unit1]->data.standard_form;
	auto &u2 = model->units[unit2]->data.standard_form;
	return match_exact(&u1, &u2);
}

void
check_variable_declaration_match(Mobius_Model *model, Entity_Id id1, Entity_Id id2) {

	auto var1  = model->vars[id1];
	auto var2 = model->vars[id2];	
	
	bool mismatch = false;
	if(var1->name != var2->name)
		mismatch = true;
	
	// TODO: If we allow default units, we also have to check against that.
	if(!mismatch && !unit_match(model, var1->unit, var2->unit))           mismatch = true;
	if(!mismatch && !unit_match(model, var1->conc_unit, var2->conc_unit)) mismatch = true;
	
	if(mismatch) {
		var1->source_loc.print_error_header(Mobius_Error::model_building);
		error_print("There is a mismatch of the name or unit between this declaration and another declaration here: ");
		var2->source_loc.print_error();
		mobius_error_exit();
	}
}

Var_Id
create_conc_var(Model_Application *app, Var_Id var_id, Var_Id dissolved_in_id, Entity_Id conc_unit, const std::string &var_name, char *varname) {
	
	auto model = app->model;
	
	auto dissolved_in = app->vars[dissolved_in_id];
	
	sprintf(varname, "concentration(%s, %s)", var_name.data(), dissolved_in->name.data());
	Var_Id gen_conc_id = register_state_variable<State_Var::Type::dissolved_conc>(app, invalid_entity_id, false, varname);
	auto conc_var = as<State_Var::Type::dissolved_conc>(app->vars[gen_conc_id]);
	conc_var->conc_of = var_id;
	conc_var->conc_in = dissolved_in_id;
	auto var_d = as<State_Var::Type::declared>(app->vars[var_id]);
	
	auto computed_conc_unit = divide(var_d->unit, dissolved_in->unit);
	
	if(is_valid(conc_unit)) {
		conc_var->unit = model->units[conc_unit]->data;
		bool success = match(&computed_conc_unit.standard_form, &conc_var->unit.standard_form, &conc_var->unit_conversion);
		if(!success) {
			auto var_decl = model->vars[var_d->decl_id];
			var_decl->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("We can't find a way to convert between the declared concentration unit ", conc_var->unit.to_utf8(), " and the computed concentration unit ", computed_conc_unit.to_utf8(), ".");
		}
	} else {
		conc_var->unit = computed_conc_unit;
		conc_var->unit_conversion = 1.0;
	}
	return gen_conc_id;
}

void
prelim_compose(Model_Application *app, std::vector<std::string> &input_names) {
	
	auto model = app->model;
	
	std::unordered_map<Var_Location, Var_Id, Var_Location_Hash> external_targets;
	register_external_computations(app, external_targets);
	
	// NOTE: There can be multiple var() declarations of the same state variable, so we have to decide on a canonical one.
	std::unordered_map<Var_Location, Entity_Id, Var_Location_Hash> has_location;
	
	for(Entity_Id id : model->vars) {
		auto var = model->vars[id];
		
		bool found_code = has_code(var);
		
		bool found_earlier = false;
		Var_Registration *var2 = nullptr;
		auto find = has_location.find(var->var_location);
		if(find != has_location.end()) {
			found_earlier = true;
			var2 = model->vars[find->second];
			
			if(found_code && has_code(var2)) {
				var->source_loc.print_error_header();
				error_print("Only one 'var' declaration for the same variable location can have code associated with it. There is a conflicting declaration here:\n");
				var2->source_loc.print_error();
				mobius_error_exit();
			}
			
			check_variable_declaration_match(model, id, find->second);
		}
		
		auto comp = model->components[var->var_location.last()];
		
		// Note: For properties we want to put the one that has code as the canonical one. If there isn't any with code, they will be considered input series
		if((comp->decl_type == Decl_Type::property && found_code) || comp->decl_type == Decl_Type::quantity || !found_earlier)
			has_location[var->var_location] = id;
	}
	
	std::vector<Var_Id> dissolvedes;
	
	// NOTE: We have to process these in order so that e.g. soil.water exists when we process soil.water.oc
	for(int n_components = 2; n_components <= max_var_loc_components; ++n_components) {
		for(Entity_Id id : model->vars) {
			auto var = model->vars[id];
			
			auto find = has_location.find(var->var_location);
			if(find == has_location.end())
				fatal_error(Mobius_Error::internal, "Something went wrong with setting up canonical var() declarations.");
			if(find->second != id) continue;  // This is not the var() declaration we decided should be used as the canonical one for this var location.
			
			if(var->var_location.n_components != n_components) continue;
			
			Decl_Type type = model->find_entity(var->var_location.last())->decl_type;
			
			if(var->var_location.is_dissolved()) {
				auto above_var = app->vars.id_of(remove_dissolved(var->var_location));
				if(!is_valid(above_var) && type == Decl_Type::quantity) {
					var->source_loc.print_error_header(Mobius_Error::model_building);
					//TODO: Print the above_var.
					fatal_error("The located quantity that this 'var' declaration is assigned to has itself not been created using a 'var' declaration.");
				}
			}
			
			auto comp = model->components[var->var_location.last()];
			bool with_code = has_code(var) || comp->default_code;
			
			auto name = var->var_name;
			if(name.empty())
				name = model->find_entity(var->var_location.last())->name;  //TODO: should this use the serial name instead?
			
			bool is_series = false;
			if(type == Decl_Type::property) {
				if(!with_code)
					is_series = true;
				
				// Properties can also be overridden with input series.
				// TODO: It should probably be declared explicitly on the property if this is OK.
				if(std::find(input_names.begin(), input_names.end(), name) != input_names.end()) {
					is_series = true;
					if(with_code)
						log_print("Overriding property \"", name, "\" with an input series.\n");
				}
			}

			auto external_id = invalid_var;
			auto find_external = external_targets.find(var->var_location);
			if(find_external != external_targets.end()) {
				external_id = find_external->second;
				is_series = false;
				if(with_code) {
					var->source_loc.print_error_header(Mobius_Error::model_building);
					error_print("This property is defined with code, but it is also assigned as a target of an 'external_computation' here: ");
					auto ext_id = as<State_Var::Type::external_computation>(app->vars[external_id])->decl_id;
					model->external_computations[ext_id]->source_loc.print_error();
					mobius_error_exit();
				}
			}
			
			Var_Id var_id;
			if(is_series) {
				var_id = register_state_variable<State_Var::Type::declared>(app, id, true, name);
			} else {
				var_id = register_state_variable<State_Var::Type::declared>(app, id, false, name);
				if(var->var_location.is_dissolved() && type == Decl_Type::quantity && !is_valid(external_id))
					dissolvedes.push_back(var_id);
				if(is_valid(external_id))
					as<State_Var::Type::declared>(app->vars[var_id])->external_computation = external_id;
			}
			
			auto state_var = as<State_Var::Type::declared>(app->vars[var_id]);
			state_var->allowed_index_sets = get_allowed_index_sets(app, state_var->loc1);
		}
	}
	
	for(auto loc_id : model->locs) {
		auto loc = model->locs[loc_id];
		if(is_valid(loc->val_id)) continue;
		check_location(app, loc->source_loc, loc->loc);
	}
	
	for(auto solve_id : model->solvers) {
		auto solver = model->solvers[solve_id];
		
		for(auto &loc : solver->locs)
			check_location(app, loc.second, loc.first, true);
	}
	
	//TODO: make better name generation system!
	char varname[1024];
	
	for(Entity_Id id : model->fluxes) {
		auto flux = model->fluxes[id];

		check_location(app, flux->source_loc, flux->source, true, true, true);
		check_location(app, flux->source_loc, flux->target, true, true, false);
		
		if(!is_located(flux->source) && is_valid(flux->target.r1.connection_id) && flux->target.r1.type == Restriction::below) {
			flux->source_loc.print_error_header(Mobius_Error::model_building); 
			fatal_error("You can't have a flux from 'out' to a connection.\n");
		}
		
		auto var_id = register_state_variable<State_Var::Type::declared>(app, id, false, flux->name);
		auto var = as<State_Var::Type::declared>(app->vars[var_id]);
		
		resolve_no_carry(app, var);
		
		var->allowed_index_sets = get_allowed_index_sets(app, var->loc1, var->loc2);
	}
	
	//NOTE: not that clean to have this part here, but it is just much easier if it is done before function resolution.
	// For instance, we want the var_id of each concentration variable to exist when we resolve the function trees since the code in those could refer to concentrations.
	for(auto var_id : dissolvedes) {
		auto var = app->vars[var_id];
		
		auto above_loc = remove_dissolved(var->loc1);
		std::vector<Var_Id> generate;
		for(auto flux_id : app->vars.all_fluxes()) {
			auto flux = app->vars[flux_id];
			if(!(static_cast<Var_Location &>(flux->loc1) == above_loc)) continue;
			if(is_located(flux->loc2)) {
				// If the target of the flux is a specific location, we can only send the dissolved quantity if it exists in that location.
				auto below_loc = add_dissolved(flux->loc2, var->loc1.last());
				auto below_var = app->vars.id_of(below_loc);
				if(!is_valid(below_var)) continue;
			}
			// See if it was declared that this flux should not carry this quantity (using a no_carry declaration)
			if(flux->type == State_Var::Type::declared) {
				// If this flux was itself generated, it won't have a declaration to look at.
				// TODO: However it is debatable whether or not 'no_carry' should carry over (no pun intended) from the parent flux.
				auto flux_var = as<State_Var::Type::declared>(flux);
				if(flux_var->no_carry_by_default)
					continue;
				
				if(std::find(flux_var->no_carry.begin(), flux_var->no_carry.end(), var_id) != flux_var->no_carry.end()) continue;
			}
			
			generate.push_back(flux_id);
		}
		
		Var_Location source  = var->loc1;
		std::string var_name = var->name;
		auto dissolved_in_id = app->vars.id_of(above_loc);
		
		auto var_d = as<State_Var::Type::declared>(app->vars[var_id]);
		auto var_decl = model->vars[var_d->decl_id];
		auto decl_conc_unit = var_decl->conc_unit;
		
		auto gen_conc_id = create_conc_var(app, var_id, dissolved_in_id, decl_conc_unit, var_name, &varname[0]);
		var_d->conc = gen_conc_id;
		
		for(auto flux_id : generate) {
			std::string &flux_name = app->vars[flux_id]->name;
			sprintf(varname, "carried_flux(%s, %s)", var_name.data(), flux_name.data());
			
			// It is just much cleaner in MobiView if this is off, but on the other hand we do want to know some of these fluxes quite often. Make a more granular way to specify which ones to store?
			bool no_store = true;
			
			Var_Id gen_flux_id = register_state_variable<State_Var::Type::dissolved_flux>(app, invalid_entity_id, false, varname, no_store);
			auto gen_flux = as<State_Var::Type::dissolved_flux>(app->vars[gen_flux_id]);
			auto flux = app->vars[flux_id];
			gen_flux->type = State_Var::Type::dissolved_flux;
			gen_flux->flux_of_medium = flux_id;
			gen_flux->set_flag(State_Var::flux);
			
			gen_flux->bidirectional = flux->bidirectional;
			if(flux->mixing_base)
				gen_flux->mixing = true;
			// In case it is a dissolved of a dissolved
			else if(flux->type == State_Var::Type::dissolved_flux)
				gen_flux->mixing = as<State_Var::Type::dissolved_flux>(flux)->mixing;
			
			// TODO: This is annoying. There should be a Specific_Var_Location::add_dissolved that preserves the restrictions.
			gen_flux->loc1 = source;
			gen_flux->loc1.r1 = flux->loc1.r1;
			gen_flux->loc1.r2 = flux->loc1.r2;
			
			if(is_located(flux->loc2))
				gen_flux->loc2 = add_dissolved(flux->loc2, source.last());
			gen_flux->loc2.type = flux->loc2.type;
			gen_flux->loc2.r1 = flux->loc2.r1;
			gen_flux->loc2.r2 = flux->loc2.r2;
			
			gen_flux->allowed_index_sets = get_allowed_index_sets(app, gen_flux->loc1, gen_flux->loc2);
		}
	
		// Check if we should generate an additional concentration variable for this in a 'higher' medium.
		if(is_located(var_decl->additional_conc_medium)) {
			
			bool found = false;
			Var_Location loc = var_decl->var_location;
			// TODO: make a 'can_be_dissolved_in' function?
			while(loc.is_dissolved()) {
				loc = remove_dissolved(loc);
				if(loc == var_decl->additional_conc_medium) {
					found = true;
					break;
				}
			}
			if(!found) {
				var_decl->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The 'show_conc' note refers to a variable that this variable is not dissolved in.");
			}
			
			auto dissolved_in_id = app->vars.id_of(var_decl->additional_conc_medium);
			
			create_conc_var(app, var_id, dissolved_in_id, var_decl->additional_conc_unit, var_name, &varname[0]);
			
			// TODO: Not sure if we should do this:
			app->vars[gen_conc_id]->store_series = false;
		}
	}
}

Math_Expr_FT *
get_aggregation_weight(Model_Application *app, const Var_Location &loc1, Entity_Id to_compartment, Entity_Id connection = invalid_entity_id) {
	
	auto model = app->model;
	auto source = model->components[loc1.first()];
	
	for(auto &agg : source->aggregations) {
		if(agg.to_compartment != to_compartment) continue;
		
		if(is_valid(agg.only_for_connection) && agg.only_for_connection != connection) continue;
		
		auto scope = model->get_scope(agg.scope_id);
		Standardized_Unit expected_unit = {};  // Expect dimensionless aggregation weights (unit conversion is something separate)
		Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters, expected_unit, connection };
		res_data.restrictive_lookups = true;
		
		auto fun = resolve_function_tree(agg.code, &res_data);
		
		if(!match_exact(&fun.unit, &expected_unit)) {
			agg.code->source_loc.print_error_header();
			fatal_error("Expected the unit of the aggregation_weight expression to resolve to ", expected_unit.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
		}
		
		auto agg_weight = make_cast(fun.fun, Value_Type::real);
		
		// TODO: It may be a bit inefficient that we build the maximal dependency set every time this aggregation
		// weight is resolved.
		Specific_Var_Location loc = loc1; // sigh
		auto allowed_index_sets = get_allowed_index_sets(app, loc);
		
		check_valid_distribution_of_dependencies(app, agg_weight, allowed_index_sets);
		
		return agg_weight;
	}
	
	return nullptr;
}

Math_Expr_FT *
get_unit_conversion(Model_Application *app, Var_Location &loc1, Var_Location &loc2, Var_Id flux_id) {
	
	Math_Expr_FT *unit_conv = nullptr;
	if(!is_located(loc1) || !is_located(loc2)) return unit_conv;
	
	auto model = app->model;
	auto source = model->components[loc1.first()];
	//error_print_location(Mobius_Model *model, const Specific_Var_Location &loc)
	auto first_id = app->vars.id_of(loc1);
	auto second_id = app->vars.id_of(loc2);
	
	if(!is_valid(first_id) || !is_valid(second_id)) return unit_conv;
	
	auto first = app->vars[first_id];
	auto second = app->vars[second_id];
	auto expected_unit = divide(second->unit, first->unit);
	
	bool need_conv = !expected_unit.standard_form.is_fully_dimensionless();
	
	Flux_Unit_Conversion_Data *found_conv = nullptr;
	bool found_conv_is_main = false;
	
	// TODO: This is ambiguous to what unit conversion is used if there is no "main" unit conversion, that is if it tries to get a unit conversion from something it is dissolved in,
	//   there is currently no preference about what it would choose. It should however choose the "nearest neighbor".
	for(auto &conv : source->unit_convs) {
		Var_Location try_loc1 = loc1;
		Var_Location try_loc2 = loc2;
		
		bool first = true;
		while(true) {
			if(found_conv && (found_conv->source.n_components > try_loc1.n_components || found_conv->target.n_components > try_loc2.n_components))
				break;
			if(try_loc1 == conv.source && try_loc2 == conv.target) {
				if(first) found_conv_is_main = true;
				found_conv = &conv;
				break;
			}
			if(try_loc1.is_dissolved() && try_loc2.is_dissolved()) {
				try_loc1 = remove_dissolved(try_loc1);
				try_loc2 = remove_dissolved(try_loc2);
			} else break;
			first = false;
		}
		if(found_conv && found_conv_is_main) break;
	}
	
	// If we did not need a unit conversion for this one, but found one for something this is dissolved in, we ignore it.
	if(!need_conv && !found_conv_is_main)
		return unit_conv;
	
	if(!found_conv && need_conv) {
		// TODO: Better error if it is a connection_aggregate, not a flux.
		fatal_error(Mobius_Error::model_building, "The units of the source and target of the flux \"", app->vars[flux_id]->name, "\" are not the same, but no unit conversion are provided between them in the model.");
	}
	
	auto ast   = found_conv->code;
	auto scope = model->get_scope(found_conv->scope_id);
	
	Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters, expected_unit.standard_form };
	res_data.restrictive_lookups = false; // TODO: We should monitor if this works out ok.
	res_data.allow_in_flux = false;
	
	auto fun = resolve_function_tree(ast, &res_data);
	unit_conv = make_cast(fun.fun, Value_Type::real);
	
	double conversion_factor;
	if(!match(&fun.unit, &expected_unit.standard_form, &conversion_factor)) {
		
		ast->source_loc.print_error_header();
		fatal_error("Expected the unit of this unit_conversion expression to resolve to a scalar multiple of ", expected_unit.standard_form.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ". It should convert from ", first->unit.standard_form.to_utf8(), " to ", second->unit.standard_form.to_utf8(), ". The error happened when finding unit conversions for the flux \"", app->vars[flux_id]->name, "\".");
	}
	if(conversion_factor != 1.0)
		unit_conv = make_binop('*', unit_conv, make_literal(conversion_factor));
	
	// TODO: It may be a bit inefficient that we build the maximal dependency set every time
	// this unit conversion is resolved.
	Specific_Var_Location loc = loc1;
	auto allowed_index_sets = get_allowed_index_sets(app, loc);
	
	check_valid_distribution_of_dependencies(app, unit_conv, allowed_index_sets);
	
	return unit_conv;
}

void
add_connection_agg_weights(Model_Application *app, std::vector<Conversion_Data> &conv_data, Var_Location &loc1, Var_Location &loc2, Var_Id target_var_id, Entity_Id target_comp, Entity_Id conn_id) {
	Conversion_Data data;
	data.source_id = app->vars.id_of(loc1);
	data.weight = owns_code(get_aggregation_weight(app, loc1, target_comp, conn_id));
	data.unit_conv = owns_code(get_unit_conversion(app, loc1, loc2, target_var_id));
	if(data.weight || data.unit_conv) conv_data.push_back(std::move(data));
}

void
register_connection_agg(Model_Application *app, bool is_out, Var_Id target_var_id, Entity_Id target_comp, Entity_Id conn_id, char *varname) {
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	
	auto var = as<State_Var::Type::declared>(app->vars[target_var_id]);
	
	// Should not have connection aggregates for overridden variables.
	if(var->function_tree)
		return;
	
	// See if we have a connection aggregate for this variable and connection already.
	auto &aggs = is_out ? var->conn_source_aggs : var->conn_target_aggs;
	for(auto existing_agg : aggs) {
		if(as<State_Var::Type::connection_aggregate>(app->vars[existing_agg])->connection == conn_id)
			return;
	}

	sprintf(varname, "%s(%s, %s)", is_out ? "out_flux" : "in_flux", connection->name.data(), app->vars[target_var_id]->name.data());

	Var_Id agg_id = register_state_variable<State_Var::Type::connection_aggregate>(app, invalid_entity_id, false, varname, true);
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	agg_var->agg_for = target_var_id;
	agg_var->is_out = is_out;
	agg_var->connection = conn_id;
	
	var = as<State_Var::Type::declared>(app->vars[target_var_id]);
	
	agg_var->unit = divide(var->unit, app->time_step_unit);
	if(is_out) {
		var->conn_source_aggs.push_back(agg_id);
	} else {
		var->conn_target_aggs.push_back(agg_id);
		
		// NOTE: aggregation weights only supported for compartments for now..
		if(model->components[target_comp]->decl_type != Decl_Type::compartment) return;
	
		// Get aggregation weights if relevant
		auto find = app->find_connection_component(conn_id, target_comp);
		for(auto source_id : find->possible_sources) {
			Var_Location loc2 = app->vars[target_var_id]->loc1;
			Var_Location loc = loc2;
			loc.components[0] = source_id;
			add_connection_agg_weights(app, agg_var->conversion_data, loc, loc2, target_var_id, target_comp, conn_id);
		}
		
		// For connections of type grid1d we could also have something like
		//      flux(soil.water, gw.water[vert.top])
		// where soil.water is not a connection component, but this still gets added to the connection aggregate for gw.water vert. So we have to look for aggregation weights for this case also.
		// Is there a more efficient way to do it?
		if(connection->type == Connection_Type::grid1d) {
			Var_Location loc2 = app->vars[target_var_id]->loc1;
			for(auto flux_id : app->vars.all_fluxes()) {
				auto flux = app->vars[flux_id];
				if(flux->loc2 != loc2 || flux->loc2.r1.connection_id != conn_id || !is_located(flux->loc1)) continue;
				add_connection_agg_weights(app, agg_var->conversion_data, flux->loc1, flux->loc2, target_var_id, target_comp, conn_id);
			}
		}
	}
}

void
process_state_var_code(Model_Application *app, Var_Id var_id, Code_Special_Lookups *specials, bool keep_code = true) {
	
	auto model = app->model;
	auto var = app->vars[var_id];
		
	if(var->is_flux()) {
		// NOTE: This part must also be done for generated (dissolved) fluxes, not just declared ones, which is why we don't skip non-declared ones yet.
		var->unit_conversion_tree = owns_code(get_unit_conversion(app, var->loc1, var->loc2, var_id));
		auto transported_id = invalid_var;
		if(is_located(var->loc1)) transported_id = app->vars.id_of(var->loc1);
		else if(is_located(var->loc2)) transported_id = app->vars.id_of(var->loc2);
		if(is_valid(transported_id)) {
			auto transported = app->vars[transported_id];
			auto unit = divide(transported->unit, app->time_step_unit);  // This is the unit we need the value to be in for use in state variable updates
			
			if(var->type == State_Var::Type::declared) {
				// Hmm, this actually allows not just for time scaling, but also other scaling. For instance, it allows [g, ha-1, day-1] to be converted to [n g, k m-2, day-1]
				auto var2 = as<State_Var::Type::declared>(var);
				bool success = match(&var->unit.standard_form, &unit.standard_form, &var2->flux_time_unit_conv);
				
				if(!success) {
					model->fluxes[var2->decl_id]->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("The flux \"", var2->name, "\" has been given a unit that is not compatible with the unit of the transported quantity, which is ", transported->unit.to_utf8(), ", or with the time step unit of the model, which is ", app->time_step_unit.to_utf8(), ".");
				}
			} else {
				// NOTE: we could also make it have the same time part as the parent flux, but it is tricky.
				var->unit = unit;
			}
		}
	}
	
	if(var->type != State_Var::Type::declared) return;
	auto var2 = as<State_Var::Type::declared>(var);
	
	Math_Expr_AST *ast = nullptr;
	Math_Expr_AST *init_ast = nullptr;
	Math_Expr_AST *override_ast = nullptr;
	Math_Expr_AST *specific_ast = nullptr;
	bool override_is_conc = false;
	bool initial_is_conc = false;
	bool init_is_override = false;
	
	Specific_Var_Location in_loc;
	in_loc.type = Var_Location::Type::out;
	Entity_Id from_compartment = invalid_entity_id;
	Entity_Id connection = invalid_entity_id;
	
	Decl_Scope *code_scope       = nullptr;
	Decl_Scope *other_code_scope = nullptr;
	
	if(var2->decl_id.reg_type == Reg_Type::flux) {
		 
		auto flux_decl = model->fluxes[var2->decl_id];
		ast = flux_decl->code;
		if(flux_decl->specific_target_ast)
			specific_ast = flux_decl->specific_target_ast;
		
		code_scope = model->get_scope(flux_decl->scope_id);
		other_code_scope = code_scope;
		
		// Not sure if it would be better to pack both locations to the function resolve data and look up the connections from it that way.
		// Or we could just get rid of the implicit connection.
		// Or we could just make it implicit for in_loc.
		connection = flux_decl->source.r1.connection_id;
		if(!is_valid(connection))
			connection = flux_decl->target.r1.connection_id;
		
		bool target_is_located = is_located(var->loc2);
		if(is_located(var->loc1)) {
			from_compartment = var->loc1.first();
			in_loc = var->loc1;
		} else if(target_is_located)
			in_loc = var->loc2;
		
		if(!is_valid(from_compartment)) from_compartment = in_loc.first();
		
	} else if(var2->decl_id.reg_type == Reg_Type::var) {
		auto var_decl = model->vars[var2->decl_id];
		ast      = var_decl->code;
		init_ast = var_decl->initial_code;
		override_ast = var_decl->override_code;
		override_is_conc = var_decl->override_is_conc;
		initial_is_conc  = var_decl->initial_is_conc;
		code_scope = model->get_scope(var_decl->scope_id);
		other_code_scope = code_scope;
		
		if(override_ast && !init_ast) {
			init_is_override = true;
			init_ast = override_ast;
			initial_is_conc = override_is_conc;
		}
		
		if((var2->decl_type == Decl_Type::quantity) && ast) {
			var_decl->source_loc.print_error_header();
			fatal_error("A quantity should not have an un-tagged code block.");
		}
		
		if(!ast) {
			auto comp = model->components[var_decl->var_location.last()];
			ast = comp->default_code;
			if(ast)
				code_scope = model->get_scope(comp->scope_id);
		}
		
		if(override_ast && (var2->decl_type != Decl_Type::quantity || (override_is_conc && !var->loc1.is_dissolved()))) {
			override_ast->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Either got an 'override' block on a property or a 'override_conc' block on a non-dissolved variable.");
		}
		in_loc = var_decl->var_location;
		from_compartment = in_loc.first();
	}
	
	Function_Resolve_Data res_data = { app, code_scope, in_loc, &app->baked_parameters, var->unit.standard_form, connection };
		
	if(ast) {
		auto res = resolve_function_tree(ast, &res_data);
		auto fun = make_cast(res.fun, Value_Type::real);
		find_identifier_flags(app, fun, specials, var_id, from_compartment);
		
		if(!match_exact(&res.unit, &res_data.expected_unit)) {
			ast->source_loc.print_error_header();
			fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
		}
		
		if(keep_code)
			var2->function_tree = owns_code(fun);
		else
			delete fun;
	}
	
	// See if there are any other declarations that 'add_to_existing' for this one.
	//   Hmm, this is now N^2. We could instead store info about this from an earlier pass.
	// TODO: Also, this will now not trigger at all if we 'add_to_existing' on something that doesn't exist.
	for(auto id : model->vars) {
		auto var_decl = model->vars[id];
		if(!var_decl->adds_code_to_existing) continue;
		if(var_decl->var_location != var->loc1) continue;
		if(var2->decl_type != Decl_Type::property) {  // Could also make it work for 'override'.
			var_decl->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("An 'add_to_existing' can only be done on a property, not on a quantity");
		}
		if(!ast) {
			var_decl->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This 'add_to_existing' adds to something that doesn't already have code.");
		}
		auto res_data2 = res_data;
		res_data2.scope = model->get_scope(var_decl->scope_id);
		auto res = resolve_function_tree(var_decl->adds_code_to_existing, &res_data2);
		auto fun = make_cast(res.fun, Value_Type::real);
		find_identifier_flags(app, fun, specials, var_id, from_compartment);
		
		if(!match_exact(&res.unit, &res_data2.expected_unit)) {
			var_decl->adds_code_to_existing->source_loc.print_error_header();
			fatal_error("Expected the unit of this expression to resolve to ", res_data2.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
		}
		if(keep_code) {
			auto existing = copy(var2->function_tree.get());
			var2->function_tree = owns_code(make_binop('+', existing, fun));
		} else
			delete fun;
	}
	
	res_data.scope = other_code_scope;
	if(init_ast) {
		if(initial_is_conc)
			res_data.expected_unit = app->vars[var2->conc]->unit.standard_form;
		
		res_data.allow_in_flux = false;
		res_data.allow_last = !init_is_override; // NOTE: Only make an error for occurrences of 'last' if the block came from an @initial not an @override
		
		auto res = resolve_function_tree(init_ast, &res_data);
		auto fun = make_cast(res.fun, Value_Type::real);
		
		find_identifier_flags(app, fun, specials, var_id, from_compartment);
		var2->initial_is_conc = initial_is_conc;
		
		if(!match_exact(&res.unit, &res_data.expected_unit)) {
			init_ast->source_loc.print_error_header();
			fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
		}
		
		if(initial_is_conc && (var2->decl_type != Decl_Type::quantity || !var->loc1.is_dissolved())) {
			init_ast->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Got an \"initial_conc\" block for a non-dissolved variable");
		}
		
		if(keep_code)
			var2->initial_function_tree = owns_code(fun);
		else
			delete fun;
	}
	
	if(override_ast) {
		
		if(ast)
			fatal_error(Mobius_Error::internal, "Somehow got both a main and @override block for a variable.");
		
		if(override_is_conc)
			res_data.expected_unit = app->vars[var2->conc]->unit.standard_form;
		else
			res_data.expected_unit = var2->unit.standard_form; //In case it was overwritten above..
		
		res_data.allow_no_override = true;
		res_data.allow_in_flux = true;
		auto res = resolve_function_tree(override_ast, &res_data);
		auto fun = res.fun;
		
		// NOTE: It is not that clean to do this copy and prune, but we need to know if the expression resolves to 'no_override'. TODO: Make a separate function to check for that?
		auto override_check = prune_tree(copy(fun));
		bool no_override = false;
		if(override_check->expr_type == Math_Expr_Type::identifier) {
			auto ident = static_cast<Identifier_FT *>(override_check);
			no_override = (ident->variable_type == Variable_Type::no_override);
		}
		delete override_check;
		
		if(!no_override) {
			
			fun = make_cast(fun, Value_Type::real);
			
			find_identifier_flags(app, fun, specials, var_id, from_compartment);
			var2->override_is_conc = override_is_conc;
			
			if(!match_exact(&res.unit, &res_data.expected_unit)) {
				init_ast->source_loc.print_error_header();
				fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
			}
			
			if(keep_code)
				var2->function_tree = owns_code(fun);
			else
				delete fun;
		}
	}
	
	if(specific_ast) {
		res_data.allow_no_override = false;
		res_data.allow_in_flux = false;
		res_data.expected_unit = {};
		res_data.allow_no_override = false;
		auto res = resolve_function_tree(specific_ast, &res_data);
		auto fun = make_cast(res.fun, Value_Type::integer);
		
		find_identifier_flags(app, fun, specials, var_id, from_compartment);
		
		if(!match_exact(&res.unit, &res_data.expected_unit)) {
			init_ast->source_loc.print_error_header();
			fatal_error("Expected the unit of this expression to resolve to dimensionless, but got, ", res.unit.to_utf8(), ".");
		}
		
		if(keep_code)
			var2->specific_target = owns_code(fun);
		else
			delete fun;
	}
}

void
Model_Application::compose_and_resolve() {
	
	for(auto par_id : model->parameters) {
		auto par = model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_bool || par->decl_type == Decl_Type::par_enum) {
			s64 count = parameter_structure.instance_count(par_id);
			if(count == 1) {// TODO: Would like to get rid of this requirement, but without it we may end up needing a separate code tree per index tuple for a given state variable :(
				baked_parameters.push_back(par_id);
				//warning_print("Baking parameter \"", par->name, "\".\n");
			}
		}
	}
	
	//TODO: make better name generation system!
	char varname[1024];
	
	// TODO: check if there are unused aggregation data or unit conversion data!
	
	Code_Special_Lookups specials;
	
	for(auto var_id : vars.all_state_vars())
		process_state_var_code(this, var_id, &specials);
	
	// NOTE: properties that are overridden as series should still have the function trees processed for correctness.
	for(auto var_id : vars.all_series())
		process_state_var_code(this, var_id, nullptr, false);
	
	for(auto var_id : vars.all_state_vars()) {
		auto var = vars[var_id];
		if(var->type != State_Var::Type::external_computation) continue;
		
		auto var2 = as<State_Var::Type::external_computation>(var);
		auto external = model->external_computations[var2->decl_id];
		
		Function_Resolve_Data res_data;
		res_data.app = this;
		res_data.scope = model->get_scope(external->scope_id);
		res_data.baked_parameters = &baked_parameters;
		res_data.allow_result = true;
		res_data.value_last_only = false; // This is a list of expressions, not a function evaluating to a single value.
		
		auto res = resolve_function_tree(external->code, &res_data);
		
		// TODO: This could be separated out in its own function
		auto external_comp = new External_Computation_FT();
		external_comp->source_loc = external->code->source_loc;
		external_comp->function_name = external->function_name;
		external_comp->connection_component = external->connection_component;
		external_comp->connection = external->connection;
		
		for(auto arg : res.fun->exprs) {
			if(arg->expr_type != Math_Expr_Type::identifier)
				fatal_error(Mobius_Error::internal, "Got a '", name(arg->expr_type), "' expression in the body of a external_computation.");
			
			auto ident = static_cast<Identifier_FT *>(arg);
			// TODO: Also implement for input series
			if(!ident->is_computed_series() && ident->variable_type != Variable_Type::parameter) {
				ident->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("We only support state variables and parameters as the arguments to a 'external_computation'.");
			}
			
			external_comp->arguments.push_back(*ident);
			if(ident->has_flag(Identifier_FT::result)) {
				if(!ident->is_computed_series()) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Only state variables can be a 'result' of a 'external_computation'.");
				}
				auto result_var = as<State_Var::Type::declared>(vars[ident->var_id]);
				if(result_var->decl_type != Decl_Type::property) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Only a 'property' can be a 'result' of a 'external_computation'.");
				}
					
				var2->targets.push_back(ident->var_id);
			}
			if(ident->restriction.r1.type != Restriction::none) {
				if(ident->has_flag(Identifier_FT::result)) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("A result of an 'external_computation' can't have a connection restriction.");
				}
				if(!ident->is_computed_series()) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Connection restrictions are not supported for parameters in 'external_computation'.");
				}
				auto ident_var = vars[ident->var_id];
				if(external->connection_component != ident_var->loc1.first() || external->connection != ident->restriction.r1.connection_id) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("To allow looking up a variable with a connection restriction, that connection and source compartment must be specified with an 'allow_connection' note.");
				}
			}
		}
		delete res.fun;
		var2->code = owns_code(external_comp);
	}
	
	for(auto flux_id : vars.all_fluxes()) {
		// We have to copy "specific target" to all dissolved child fluxes. We could not have done that before since the code was only just resolved above.
		auto flux = vars[flux_id];
		if(flux->type != State_Var::Type::dissolved_flux) continue;
		
		if(	   flux->loc1.r1.type != Restriction::specific 
			&& flux->loc1.r2.type != Restriction::specific
			&& flux->loc2.r1.type != Restriction::specific
			&& flux->loc2.r2.type != Restriction::specific) continue;
			
		auto orig_flux = flux;
		while(orig_flux->type == State_Var::Type::dissolved_flux)
			orig_flux = vars[as<State_Var::Type::dissolved_flux>(orig_flux)->flux_of_medium];
		if(!orig_flux->specific_target)
			fatal_error(Mobius_Error::internal, "Somehow we got a specific restriction on a flux without specific target code.");
		//log_print("Setting specific target for ", flux->name, "\n");
		flux->specific_target = owns_code(copy(orig_flux->specific_target.get()));
	}
	
	// Invalidate fluxes if both source and target is 'out' or overridden.
	{
		std::map<Var_Id, bool> could_be_invalidated;
		for(auto var_id : vars.all_fluxes()) {
			auto var = vars[var_id];
			
			bool valid_source = false;
			if(is_located(var->loc1)) {
				auto source = as<State_Var::Type::declared>(vars[vars.id_of(var->loc1)]);
				if(!source->function_tree)
					valid_source = true;
			}
			bool valid_target = false;
			if(is_located(var->loc2)) {
				auto target = as<State_Var::Type::declared>(vars[vars.id_of(var->loc2)]);
				if(!target->function_tree)
					valid_target = true;
			}
			if(is_valid(var->loc2.r1.connection_id))
				valid_target = true;
			bool invalidate = !valid_source && !valid_target;
			could_be_invalidated[var_id] = invalidate;
			if(invalidate) continue;
			
			// NOTE: If this flux could not be invalidated, neither could any of its parents since this one relies on them.
			// Edit: This is no longer true. It only relies on the topmost one. Let's see if it works to turn off this code.
			/*
			auto above_id = var_id;
			auto above_var = var;
			while(above_var->type == State_Var::Type::dissolved_flux) {
				above_id = as<State_Var::Type::dissolved_flux>(above_var)->flux_of_medium;
				could_be_invalidated[above_id] = false;
				above_var = vars[above_id];
			}
			*/
		}
		for(auto &pair : could_be_invalidated) {
			if(!pair.second) continue;
			auto var = vars[pair.first];
			var->set_flag(State_Var::invalid);
			log_print(Log_Mode::dev, "Invalidating \"", var->name, "\" due to both source and target being 'out' or overridden.\n");
		}
	}
	
	// Invalidate connection fluxes if they have no possible targets
	{
		for(auto var_id : vars.all_fluxes()) {
			auto var = vars[var_id];
			
			auto &res = var->loc2.r1;
			
			if(res.type == Restriction::below) {
				auto type = model->connections[res.connection_id]->type;
				if(type == Connection_Type::directed_graph) {
					auto comp = find_connection_component(res.connection_id, var->loc1.first(), false);
					if(!comp || comp->possible_targets.empty()) {
						var->set_flag(State_Var::invalid);
						log_print(Log_Mode::dev, "Invalidating \"", var->name, "\" due to it not having any possible targets.\n");
					}
				}
			}
		}
	}
	
	
	// TODO: Is this necessary, or could we just do this directly when we build the connections below ??
	// 	May have to do with aggregates of fluxes having that decl_type, and that is confusing? But they should not have connections any way.
	std::set<std::pair<Entity_Id, Var_Id>> may_need_connection_target;
	for(auto var_id : vars.all_fluxes()) {
		auto var = vars[var_id];
		
		if(var->loc1.r1.type != Restriction::none) {
			auto connection_id = var->loc1.r1.connection_id;
			auto type = model->connections[connection_id]->type;
			if(type != Connection_Type::grid1d)
				fatal_error(Mobius_Error::internal, "Expected only grid1d connection as a source");
			if(!is_located(var->loc1))
				fatal_error(Mobius_Error::internal, "Did not expect non-located grid1d source.");
			
			Var_Id source_id = vars.id_of(var->loc1);
			may_need_connection_target.insert({connection_id, source_id});
		}
		
		if(var->loc2.r1.type != Restriction::none) {
			auto connection_id = var->loc2.r1.connection_id;
			auto type = model->connections[connection_id]->type;
			Var_Id source_id = invalid_var;
			if(type == Connection_Type::grid1d) {
				if(is_located(var->loc2))
					source_id = vars.id_of(var->loc2);
				else if(is_located(var->loc1))
					source_id = vars.id_of(var->loc1);  // TODO: Does this give an error if the source is not on the connection?
				else
					fatal_error(Mobius_Error::internal, "Unable to locate grid1d connection aggregate variable.");
				
			} else if(type == Connection_Type::directed_graph) {
				if(!is_located(var->loc1))
					fatal_error(Mobius_Error::internal, "Did not expect a directed_graph connection starting from a non-located source.");
				source_id = vars.id_of(var->loc1);
			}
			if(is_valid(source_id))
				may_need_connection_target.insert({connection_id, source_id});
		}
	}
	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	// TODO: interaction between in_flux and aggregate declarations (i.e. we have something like an explicit aggregate(in_flux(soil.water)) in the code.
	// Or what about last(aggregate(in_flux(bla))) ?
	
	// Note: We always generate an aggregate if the source compartment of a flux has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the weight is trivial
	for(auto var_id : vars.all_fluxes()) {
		auto var = vars[var_id];

		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		
		auto max_target_indexes = get_allowed_index_sets(this, var->loc2);
		
		bool is_below;
		if(var->type == State_Var::Type::declared) {
			// We have computed the index sets of loc1 already.
			is_below = index_sets_are_contained_in(model, as<State_Var::Type::declared>(var)->allowed_index_sets, max_target_indexes);
		} else {
			auto max_source_index_sets = get_allowed_index_sets(this, var->loc1, var->loc2);
			is_below = index_sets_are_contained_in(model, max_source_index_sets, max_target_indexes);
		}
		
		if(!is_below && !is_valid(var->loc2.r1.connection_id)) {
			
			/*
			if(is_valid(var->loc2.r1.connection_id)) {
				// NOTE: This could happen if it is a located connection like "water[vert.top]". TODO: Make it possible to support this.
				auto &loc = model->fluxes[as<State_Var::Type::declared>(var)->decl_id]->source_loc;
				loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This flux would need a regular aggregate, but that is not currently supported for fluxes with a connection target.");
			}
			*/
			
			specials.aggregates[var_id].first.insert(var->loc2.first());
		}
	}
	
		
	for(auto &need_agg : specials.aggregates) {
		auto var_id = need_agg.first;
		auto var = vars[var_id];
		if(!var->is_valid()) continue;
		
		auto loc1 = var->loc1;
		if(var->type == State_Var::Type::dissolved_conc) {
			auto var2 = as<State_Var::Type::dissolved_conc>(var);
			loc1 = vars[var2->conc_of]->loc1;
		}
		
		for(auto to_compartment : need_agg.second.first) {
			
			Math_Expr_FT *agg_weight = get_aggregation_weight(this, loc1, to_compartment);
			var->set_flag(State_Var::has_aggregate);
			
			//TODO: We also have to handle the case where the agg. variable was a series!
			
			sprintf(varname, "aggregate(%s, %s)", var->name.data(), model->components[to_compartment]->name.data());
			Var_Id agg_id = register_state_variable<State_Var::Type::regular_aggregate>(this, invalid_entity_id, false, varname, true);
			
			auto agg_var = as<State_Var::Type::regular_aggregate>(vars[agg_id]);
			agg_var->agg_of = var_id;
			agg_var->aggregation_weight_tree = owns_code(agg_weight);
			if(var->is_flux()) {
				agg_var->set_flag(State_Var::flux);
			
				// TODO: Set the unit of the agg_var (only relevant if it is displayed somewhere, it is not function critical).
				//   It is a bit tricky because of potential unit conversions and the fact that the unit of aggregated fluxes could be re-scaled to the time step.
				
				
				var = vars[var_id];   //NOTE: had to look it up again since we may have resized the vector var pointed into
				// NOTE: it makes a lot of the operations in model_compilation more natural if we decouple the fluxes like this:
				agg_var->loc1.type = Var_Location::Type::out;
				agg_var->loc2 = var->loc2;
				var->loc2.type = Var_Location::Type::out;
				
				// NOTE: it is easier to keep track of who is supposed to use the unit conversion if we only keep a reference to it on the one that is going to use it.
				//   we only multiply with the unit conversion at the point where we add the flux to the target, so it is only needed on the aggregate.
				agg_var->unit_conversion_tree = std::move(var->unit_conversion_tree);
			}
			
			agg_var->agg_to_compartment = to_compartment;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = as<State_Var::Type::declared>(vars[looked_up_by]);
				
				Entity_Id lu_compartment = lu->loc1.first();
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.first();
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the structure?
				
				if(lu->function_tree)
					replace_flagged(lu->function_tree.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->initial_function_tree)
					replace_flagged(lu->initial_function_tree.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->specific_target)
					replace_flagged(lu->specific_target.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
			}
		}
	}
	
	for(auto &need_agg : specials.par_aggregates) {
		auto par_id = need_agg.first;
		auto par = model->parameters[par_id];
		auto par_group = model->par_groups[par->scope_id];
		
		// TODO: This restriction may not be necessary.
		if(par_group->components.size() > 1) {
			// TODO; Would like to print a source location here.
			fatal_error(Mobius_Error::model_building, "Trying to declare an aggregate() of the parameter \"", par->name, "\", but it does not belong to a group that is attached to exactly 0 or 1 components.");
		}
		
		for(auto to_compartment : need_agg.second.first) {
			
			Math_Expr_FT *agg_weight = nullptr;
			
			if(par_group->components.size() > 0) {
				// Hmm, a bit hacky in order to reuse other API
				Specific_Var_Location loc1;
				loc1.type = Var_Location::Type::located;
				loc1.n_components = 1;
				loc1.components[0] = par_group->components[0];
				get_aggregation_weight(this, loc1, to_compartment);
			}
			
			sprintf(varname, "aggregate(%s, %s)", par->name.data(), model->components[to_compartment]->name.data());
			Var_Id agg_id = register_state_variable<State_Var::Type::parameter_aggregate>(this, invalid_entity_id, false, varname, true);
			
			auto agg_var = as<State_Var::Type::parameter_aggregate>(vars[agg_id]);
			agg_var->agg_of = par_id;
			agg_var->aggregation_weight_tree = owns_code(agg_weight);
			
			// TODO: Set the unit of the agg_var (only relevant if it is displayed somewhere, it is not function critical).
			agg_var->agg_to_compartment = to_compartment;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = as<State_Var::Type::declared>(vars[looked_up_by]);
				
				Entity_Id lu_compartment = lu->loc1.first();
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.first();
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the structure?
				
				if(lu->function_tree)
					replace_flagged_par(lu->function_tree.get(), par_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->initial_function_tree)
					replace_flagged_par(lu->initial_function_tree.get(), par_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->specific_target)
					replace_flagged_par(lu->specific_target.get(), par_id, agg_id, Identifier_FT::Flags::aggregate);
			}
		}
	}
	
	
	// TODO: Should we give an error if there is a connection flux on an overridden variable?
	
	for(auto &pair : may_need_connection_target) {
		
		auto &conn_id = pair.first;
		auto &source_id  = pair.second;
		
		auto connection = model->connections[conn_id];
		
		if(connection->type == Connection_Type::directed_graph) {
			
			auto find_source = find_connection_component(conn_id, vars[source_id]->loc1.components[0], false);
			if(find_source && find_source->is_edge_indexed) // NOTE: Could instead be find_source->max_outgoing_edges, but it is tricky wrt index set dependencies for the flux.
				register_connection_agg(this, true, source_id, invalid_entity_id, conn_id, &varname[0]); // Aggregation variable for outgoing fluxes.
			
			for(auto &target_comp : connection_components[conn_id].components) {
				
				// If it wasn't actually put as a possible target in the data set, don't bother with it.
				if(target_comp.possible_sources.empty()) continue;

				auto target_loc = vars[source_id]->loc1;
				target_loc.components[0] = target_comp.id;
				auto target_id = vars.id_of(target_loc);
				if(!is_valid(target_id))   // NOTE: the target may not have that state variable. This can especially happen for dissolvedes.
					continue;
				
				register_connection_agg(this, false, target_id, target_comp.id, conn_id, &varname[0]); // Aggregation variable for incoming fluxes.
			}
		} else if (connection->type == Connection_Type::grid1d) {
			if(connection->components.size() != 1)
				fatal_error(Mobius_Error::internal, "Expected exactly one compartment for this type of connection."); // Should have been detected earlier
			
			// Note: A grid1d connection can only go over one node type.
			auto target_comp = connection->components[0];
			
			// TODO: Find a way to get rid of connection aggregates for grid1d.
			register_connection_agg(this, false, source_id, target_comp, conn_id, &varname[0]);
			
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection type in compose_and_resolve()");
		}
	}
	
	for(auto &in_flux : specials.in_fluxes) {
		
		auto &key = in_flux.first;
		Var_Id target_id     = std::get<0>(key);
		Entity_Id connection = std::get<1>(key);
		bool is_out          = std::get<2>(key);
		auto target = as<State_Var::Type::declared>(vars[target_id]); // Hmm, this could maybe trigger an internal error unless we check that a declared one was provided.
		
		Var_Id in_flux_id = invalid_var;
		if(!is_valid(connection)) {
			if(is_out)
				sprintf(varname, "out_flux(%s)", target->name.data());
			else
				sprintf(varname, "in_flux(%s)", target->name.data());
			in_flux_id = register_state_variable<State_Var::Type::in_flux_aggregate>(this, invalid_entity_id, false, varname, true);
			auto in_flux_var = as<State_Var::Type::in_flux_aggregate>(vars[in_flux_id]);
			in_flux_var->in_flux_to = target_id;
			in_flux_var->is_out     = is_out;
			
			vars[in_flux_id]->unit = divide(target->unit, time_step_unit); //NOTE: In codegen, the components in the sum are rescaled to this unit.
		} else {
			// Note: if a connection aggregate exists it has already been created.
			// TODO: For source aggregates, they will not always be created even if they are relevant. Should we create them here if relevant?
			if(!is_out) {
				for(auto conn_agg_id : target->conn_target_aggs) {
					if( as<State_Var::Type::connection_aggregate>(vars[conn_agg_id])->connection == connection) {
						in_flux_id = conn_agg_id;
						break;
					}
				}
			} else {
				for(auto conn_agg_id : target->conn_source_aggs) {
					if( as<State_Var::Type::connection_aggregate>(vars[conn_agg_id])->connection == connection) {
						in_flux_id = conn_agg_id;
						break;
					}
				}
			}
		}
		
		// NOTE: If in_flux_id is invalid at this point (since the referenced connection aggregate did not exist), replace_flagged will put a literal 0.0 in place of this value, which is how we want it to work, but we print a warning in case it was unintentional.
		
		// NOTE: We have disallowed in_flux lookups in initial code and specific_target code for now.
		auto replace_flag = is_out ? Identifier_FT::Flags::out_flux : Identifier_FT::Flags::in_flux;
		for(auto rep_id : in_flux.second) {
			auto var = as<State_Var::Type::declared>(vars[rep_id]);
			if(var->function_tree)
				replace_flagged(var->function_tree.get(), target_id, in_flux_id, replace_flag, connection);
		}
	}
	
	for(auto solver_id : model->solvers) {
		auto solver = model->solvers[solver_id];
		sprintf(varname, "solver_step_resolution(%s)", solver->name.data());
		auto var_id = register_state_variable<State_Var::Type::step_resolution>(this, invalid_entity_id, false, varname);
		auto var = as<State_Var::Type::step_resolution>(vars[var_id]);
		var->solver_id = solver_id;
		var->unit = time_step_unit;
	}
	
	// See if the code for computing a variable looks up other values that are distributed over index sets that the var is not distributed over.
	// NOTE: This must be done after all calls to replace_flagged or manipulation of Identifier_FT's in the function trees, 
	//  otherwise identifier references may not be in their final state.
	for(auto var_id : vars.all_state_vars()) {
		auto var = vars[var_id];
		
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);

		if(var2->function_tree)
			check_valid_distribution_of_dependencies(this, var2->function_tree.get(), var2->allowed_index_sets);
		if(var2->initial_function_tree)
			check_valid_distribution_of_dependencies(this, var2->initial_function_tree.get(), var2->allowed_index_sets);
		if(var2->specific_target)
			check_valid_distribution_of_dependencies(this, var2->specific_target.get(), var2->allowed_index_sets);
	}
	
	for(auto var_id : vars.all_state_vars()) {
		auto var = vars[var_id];
		serial_to_id[serialize(var_id)] = var_id;
	}
	for(auto var_id : vars.all_series()) {
		auto var = vars[var_id];
		serial_to_id[serialize(var_id)] = var_id;
	}
	
	
	// Create convenient warnings for unused module load arguments and library loads.

	for(auto module_id : model->modules)
		model->modules[module_id]->scope.check_for_unreferenced_things(model);
	for(auto lib_id : model->libraries)
		model->libraries[lib_id]->scope.check_for_unreferenced_things(model);
	model->top_scope.check_for_unreferenced_things(model);
}




