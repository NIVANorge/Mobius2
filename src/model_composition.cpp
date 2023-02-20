

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

#include <sstream>
#include <map>

void
check_if_var_loc_is_well_formed(Mobius_Model *model, Var_Location &loc, Source_Location &source_loc) {
	// Technically we shouldn't encounter an "out" at this point, but that is checked for separately in check_flux_location
	if(loc.type == Var_Location::Type::nowhere || loc.type == Var_Location::Type::out) return;
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
register_state_variable(Model_Application *app, Entity_Id decl_id, bool is_series, const std::string &name) {
	
	// TODO: Ideally we want to remove decl_type as an argument here, but then it would have to be made irrelevant for non-declared variables.
	
	auto model = app->model;
	
	if(type == State_Var::Type::declared && !is_valid(decl_id))
		fatal_error(Mobius_Error::internal, "Didn't get a decl_id for a declared variable.");
	
	Var_Location loc = invalid_var_location;
	if(is_valid(decl_id) && decl_id.reg_type == Reg_Type::has) {
		auto has = model->hases[decl_id];
		loc = has->var_location;
		check_if_var_loc_is_well_formed(model, loc, has->source_loc);
	}
	
	Var_Id var_id = invalid_var;
	State_Var *var = nullptr;
	if(is_series) {
		var_id = app->series.register_var<type>(loc, name);
		var = app->series[var_id];
	} else {
		var_id = app->state_vars.register_var<type>(loc, name);
		var = app->state_vars[var_id];
	}
	
	if(var->name.empty())
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
	var->type          = type;

	if(is_valid(decl_id)) {
		auto var2 = as<State_Var::Type::declared>(var);
		var2->decl_id = decl_id;
		
		if(decl_id.reg_type == Reg_Type::has) {
			auto has = model->hases[decl_id];
			var->loc1 = loc;
			var2->decl_type = model->components[loc.last()]->decl_type;
			if(is_valid(has->unit))
				var->unit = model->units[has->unit]->data;
		} else if (decl_id.reg_type == Reg_Type::flux) {
			auto flux = model->fluxes[decl_id];
			var->loc1 = flux->source;
			var->loc2 = flux->target;
			var2->decl_type = Decl_Type::flux;
			var2->flags = (State_Var::Flags)(var2->flags | State_Var::Flags::flux);
			// These may not be needed, since we would check if the locations exist in any case (and the source location is confusing as it stands here).
			// check_if_loc_is_well_formed(model, var.loc1, flux->source_loc);
			// check_if_loc_is_well_formed(model, var.loc2, flux->source_loc);
			if(is_valid(flux->connection_target))
				var2->connection = flux->connection_target;
			if(is_valid(flux->unit))
				var->unit = model->units[flux->unit]->data;
		} else
			fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	}
	//warning_print("**** register ", var.name, is_series ? " as series" : " as state var", "\n");
	
	return var_id;
}

void
check_flux_location(Model_Application *app, Decl_Scope *scope, Source_Location source_loc, Var_Location &loc) {
	if(!is_located(loc)) return;
	
	auto model = app->model;
	
	auto hopefully_a_quantity = model->find_entity(loc.last());
	if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
		source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("Fluxes can only be assigned to quantities. '", (*scope)[loc.last()], "' is a property, not a quantity.");
	}
	
	Var_Id var_id = app->state_vars.id_of(loc);
	if(!is_valid(var_id)) {
		source_loc.print_error_header(Mobius_Error::model_building);
		error_print("The variable location ");
		error_print_location(scope, loc);
		fatal_error(" has not been created using a 'has' declaration.");
	}
}

void
remove_lasts(Math_Expr_FT *expr, bool make_error) {
	for(auto arg : expr->exprs) remove_lasts(arg, make_error);
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && (ident->flags & Identifier_FT::Flags::last_result)) {
			if(make_error) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Did not expect a last() in an initial value function.");
			}
			ident->flags = (Identifier_FT::Flags)(ident->flags & ~Identifier_FT::Flags::last_result);
		}
	}
}

typedef std::map<std::pair<Var_Id, Entity_Id>, std::vector<Var_Id>> Var_Map;
typedef std::map<int, std::pair<std::set<Entity_Id>, std::vector<Var_Id>>> Var_Map2;

void
find_other_flags(Math_Expr_FT *expr, Var_Map &in_fluxes, Var_Map2 &aggregates, Var_Id looked_up_by, Entity_Id lookup_compartment, bool make_error_in_flux) {
	for(auto arg : expr->exprs) find_other_flags(arg, in_fluxes, aggregates, looked_up_by, lookup_compartment, make_error_in_flux);
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->flags & Identifier_FT::Flags::in_flux) {
			if(make_error_in_flux) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Did not expect an in_flux() in an initial value function.");
			}
			in_fluxes[{ident->var_id, ident->connection}].push_back(looked_up_by);
		}
		if(ident->flags & Identifier_FT::Flags::aggregate) {
			if(!is_valid(lookup_compartment)) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Can't use aggregate() in this function body because it does not belong to a compartment.");
			}
				
			aggregates[ident->var_id.id].first.insert(lookup_compartment);         //OOOps!!!! This is not correct if this was applied to an input series!
			
			if(is_valid(looked_up_by)) {
				aggregates[ident->var_id.id].second.push_back(looked_up_by);
			}
		}
	}
}

Math_Expr_FT *
replace_flagged(Math_Expr_FT *expr, Var_Id replace_this, Var_Id with, Identifier_FT::Flags flag) {
	
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = replace_flagged(expr->exprs[idx], replace_this, with, flag);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && ident->var_id == replace_this && (ident->flags & flag)) {
			if(is_valid(with)) {
				ident->var_id = with;
				ident->flags = (Identifier_FT::Flags)(ident->flags & ~flag);
			} else {
				delete expr;
				return make_literal(0.0);
			}
		}
	}
	return expr;
}

void
replace_conc(Model_Application *app, Math_Expr_FT *expr) {
	// TODO: Why is this not just baked into resolve_function_tree ?
	
	for(auto arg : expr->exprs) replace_conc(app, arg);
	if(expr->expr_type != Math_Expr_Type::identifier) return;
	auto ident = static_cast<Identifier_FT *>(expr);
	if((ident->variable_type != Variable_Type::state_var) || !(ident->flags & Identifier_FT::Flags::conc)) return;
	auto var = app->state_vars[ident->var_id];
	
	if(var->type != State_Var::Type::declared)
		fatal_error(Mobius_Error::internal, "Somehow we tried to look up the conc of a generated state variable");
	auto var2 = as<State_Var::Type::declared>(var);
	if(!is_valid(var2->conc)) {
		expr->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("This variable does not have a concentration");
	}
	ident->var_id = var2->conc;
	ident->flags = (Identifier_FT::Flags)(ident->flags & ~Identifier_FT::Flags::conc);
}

void
restrictive_lookups(Math_Expr_FT *expr, Decl_Type decl_type, std::set<Entity_Id> &parameter_refs) {
	//TODO : Should we just reuse register_dependencies() ?
	for(auto arg : expr->exprs) restrictive_lookups(arg, decl_type, parameter_refs);
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type != Variable_Type::local
			&& ident->variable_type != Variable_Type::parameter) {
			expr->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("The function body for a ", name(decl_type), " declaration is only allowed to look up parameters, no other types of state variables.");
		} else if (ident->variable_type == Variable_Type::parameter)
			parameter_refs.insert(ident->par_id);
	}
}

bool
location_indexes_below_location(Mobius_Model *model, const Var_Location &loc, const Var_Location &below_loc, 
	Entity_Id exclude_index_set_from_loc = invalid_entity_id, Entity_Id exclude_index_set_from_var = invalid_entity_id) {
	
	if(!is_located(loc) || !is_located(below_loc))
		fatal_error(Mobius_Error::internal, "Got a non-located location to a location_indexes_below_location() call.");
	
	for(int idx1 = 0; idx1 < loc.n_components; ++idx1) {
		for(auto index_set : model->components[loc.components[idx1]]->index_sets) {
			bool found = false;
			for(auto idx2 = 0; idx2 < below_loc.n_components; ++idx2) {
				if(index_set == exclude_index_set_from_loc) { // We don't care if the loc has this index_set.
					found = true;
					break;
				}
				for(auto index_set2 : model->components[below_loc.components[idx2]]->index_sets) {
					if(index_set2 == exclude_index_set_from_var) continue;
					if(index_set == index_set2) {
						found = true;
						break;
					}
				}
				if(found) break;
			}
			if(!found) return false;
		}
	}

	return true;
}

bool
parameter_indexes_below_location(Mobius_Model *model, Entity_Id par_id, const Var_Location &below_loc,
	Entity_Id exclude_index_set_from_loc = invalid_entity_id, Entity_Id exclude_index_set_from_var = invalid_entity_id) {
		
	auto par = model->parameters[par_id];
	auto par_comp_id = model->par_groups[par->par_group]->component;
	// TODO: invalid component should only happen for the "System" par group, and in that case we should probably not have referenced that parameter, but it is a bit out of scope for this function to handle it.
	if(!is_valid(par_comp_id)) return false;
	
	// NOTE: This is a bit of a hack that allows us to reuse location_indexes_below_location. We have to monitor that it doesn't break.
	Var_Location loc;
	loc.type = Var_Location::Type::located;
	loc.n_components = 1;
	loc.components[0] = par_comp_id;
	
	return location_indexes_below_location(model, loc, below_loc, exclude_index_set_from_loc, exclude_index_set_from_var);
}

void
check_valid_distribution_of_dependencies(Model_Application *app, Math_Expr_FT *function, State_Var *var, bool initial) {
	Dependency_Set code_depends;
	register_dependencies(function, &code_depends);
	
	// NOTE: We should not have undergone codegen yet, so the source location of the top node of the function should be valid.
	Source_Location source_loc = function->source_loc;
	
	Var_Location loc = var->loc1;
	loc = var->loc1;
	
	if(!is_located(loc))
		loc = var->loc2;

	if(!is_located(loc))
		fatal_error(Mobius_Error::internal, "Somehow a totally unlocated variable checked in check_valid_distribution_of_dependencies().");
	
	String_View err_begin = initial ? "The code for the initial value of the state variable \"" : "The code for the state variable \"";
	
	Entity_Id exclude_index_set_from_var = invalid_entity_id;
	if(var->boundary_type != Boundary_Type::none) {
		auto connection = connection_of_flux(var);
		exclude_index_set_from_var = app->get_single_connection_index_set(connection);
	}
	
	// TODO: in this error messages we should really print out the two tuples of index sets.
	for(auto &dep : code_depends.on_parameter) {
		// TODO: Factor out
		Entity_Id exclude_index_set_from_loc = invalid_entity_id;
		if(dep.flags & Identifier_Data::Flags::top_bottom)
			exclude_index_set_from_loc = app->get_single_connection_index_set(dep.connection);
		
		if(!parameter_indexes_below_location(app->model, dep.par_id, loc, exclude_index_set_from_loc, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the parameter \"", app->model->parameters[dep.par_id]->name, "\". This parameter belongs to a component that is distributed over a higher number of index sets than the the state variable.");
		}
	}
	for(auto &dep : code_depends.on_series) {
		Entity_Id exclude_index_set_from_loc = invalid_entity_id;
		if(dep.flags & Identifier_Data::Flags::top_bottom)
			exclude_index_set_from_loc = app->get_single_connection_index_set(dep.connection);
		
		if(!location_indexes_below_location(app->model, app->series[dep.var_id]->loc1, loc, exclude_index_set_from_loc, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the input series \"", app->series[dep.var_id]->name, "\". This series has a location that is distributed over a higher number of index sets than the state variable.");
		}
	}
	
	for(auto &dep : code_depends.on_state_var) {
		auto dep_var = app->state_vars[dep.var_id];
		
		// For generated in_flux aggregation variables we are instead interested in the variable that is the target of the fluxes.
		if(dep_var->type == State_Var::Type::in_flux_aggregate)
			dep_var = app->state_vars[as<State_Var::Type::in_flux_aggregate>(dep_var)->in_flux_to];
		
		// If it is an aggregate, index set dependencies will be generated to be correct.
		//TODO: Do we need this here, or would it work anyway with the new system?
		if(dep_var->type == State_Var::Type::regular_aggregate)
			continue;
		
		// If it is a conc, check vs the mass instead.
		if(dep_var->type == State_Var::Type::dissolved_conc) {
			auto var2 = as<State_Var::Type::dissolved_conc>(dep_var);
			dep_var = app->state_vars[var2->conc_of];
		}
		
		if(dep_var->type == State_Var::Type::connection_aggregate)
			dep_var = app->state_vars[as<State_Var::Type::connection_aggregate>(dep_var)->agg_for];
		
		if(dep_var->is_flux() || !is_located(dep_var->loc1))
			fatal_error(Mobius_Error::internal, "Somehow a direct lookup of a flux or unlocated variable \"", dep_var->name, "\" in code tested with check_valid_distribution_of_dependencies().");
		
		Entity_Id exclude_index_set_from_loc = invalid_entity_id;
		if(dep.flags & Identifier_Data::Flags::top_bottom)
			exclude_index_set_from_loc = app->get_single_connection_index_set(dep.connection);
		
		if(!location_indexes_below_location(app->model, dep_var->loc1, loc, exclude_index_set_from_loc, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error(err_begin, var->name, "\" looks up the state variable \"", dep_var->name, "\". The latter state variable is distributed over a higher number of index sets than the the prior.");
		}
	}
}

inline bool 
has_code(Entity_Registration<Reg_Type::has> *has) {
	return has->code || has->initial_code || has->override_code;
}

void
prelim_compose(Model_Application *app, std::vector<std::string> &input_names) {
	warning_print("Compose begin\n");
	
	auto model = app->model;
	
	for(auto conn_id : model->connections) {
		auto conn = model->connections[conn_id];
		if(conn->type == Connection_Type::unrecognized) {
			conn->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This connection never received a type. Declare it as connection(name, type) somewhere.");
		}
	}
	
	for(auto index_set_id : model->index_sets) {
		auto index_set = model->index_sets[index_set_id];
		if(is_valid(index_set->sub_indexed_to)) {
			auto super = model->index_sets[index_set->sub_indexed_to];
			if(is_valid(super->sub_indexed_to)) {
				index_set->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("We currently don't support sub-indexing an index set to another index set that is again sub-indexed.");
			}
		}
	}
	
	
	// NOTE: determine if a given var_location has code to compute it (otherwise it will be an input series)
	// also make sure there are no conflicting has declarations of the same var_location (across modules)
	std::unordered_map<Var_Location, Entity_Id, Var_Location_Hash> has_location;
	
	for(Entity_Id id : model->hases) {
		auto has = model->hases[id];
		
		bool found_code = has_code(has);
		
		// TODO: check for mismatching units and names between declarations.
		Entity_Registration<Reg_Type::has> *has2 = nullptr;
		auto find = has_location.find(has->var_location);
		if(find != has_location.end()) {
			has2 = model->hases[find->second];
			
			if(found_code && has_code(has2)) {
				has->source_loc.print_error_header();
				error_print("Only one has declaration for the same variable location can have code associated with it. There is a conflicting declaration here:\n");
				has2->source_loc.print_error();
				mobius_error_exit();
			}
		}
		
		if(!found_code) {
			// If it is a property, the property itself could have default code.
			auto prop = model->components[has->var_location.last()];
			if(prop->default_code) found_code = true;
		}
		
		Decl_Type type = model->find_entity(has->var_location.last())->decl_type;
		// Note: for properties we only want to put the one that has code as the canonical one. If there isn't any with code, they will be considered input series
		if(type == Decl_Type::property && found_code)
			has_location[has->var_location] = id;
		
		// Note: for quantities, we can have some that don't have code associated with them at all, and we still need to choose one canonical one to use for the state variable registration below.
		//     (thus quantities can also not be input series)
		if(type == Decl_Type::quantity && (!has2 || !has_code(has2)))
			has_location[has->var_location] = id;
	}
	
	warning_print("State var registration begin.\n");
	
	std::vector<Var_Id> dissolvedes;
	
	// TODO: we could move some of the checks in this loop to the above loop.

	for(int n_components = 2; n_components <= max_var_loc_components; ++n_components) { // NOTE: We have to process these in order so that e.g. soil.water exists when we process soil.water.oc
		for(Entity_Id id : model->hases) {
			auto has = model->hases[id];
			if(has->var_location.n_components != n_components) continue;
			
			if(has->var_location.is_dissolved()) {
				auto above_var = app->state_vars.id_of(remove_dissolved(has->var_location));
				if(!is_valid(above_var)) {
					has->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("The located quantity that this 'has' declaration is assigned to has itself not been created using a 'has' declaration.");
				}
			}
			
			auto name = has->var_name;
			if(name.empty())
				name = model->find_entity(has->var_location.last())->name;  //TODO: this is a pretty poor stopgap. We could generate something based on the Var_Location instead?
			
			Decl_Type type = model->find_entity(has->var_location.last())->decl_type;
			auto find = has_location.find(has->var_location);
			
			bool is_series = false;
			if(find == has_location.end()) is_series = true; // No declaration provided code for this series, so it is an input series.
			else if(type == Decl_Type::property) {
				// For properties, they can be overridden with input series.
				// TODO: It should probably be declared explicitly on the property if this is OK.
				if(std::find(input_names.begin(), input_names.end(), name) != input_names.end()) {
					is_series = true;
					warning_print("Overriding property \"", name, "\" with an input series.\n");
				}
			}
			
			if(is_series) {
				// If was already registered by another module, we don't need to re-register it.
				// TODO: still need to check for conflicts (unit, name) (ideally bake this check into the check where we build the has_location data.
				if(is_valid(app->series.id_of(has->var_location))) continue;
				
				if(has->var_location.is_dissolved()) {
					has->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("For now we don't support input series with chained locations.");
				}
				register_state_variable<State_Var::Type::declared>(app, id, true, name);
			} else if (id == find->second) {
				// This is the particular has declaration that provided the code, so we register a state variable using this one.
				Var_Id var_id = register_state_variable<State_Var::Type::declared>(app, id, false, name);
				if(has->var_location.is_dissolved() && type == Decl_Type::quantity)
					dissolvedes.push_back(var_id);
			}
		}
	}
	
	//TODO: make better name generation system!
	char varname[1024];
	
	for(Entity_Id id : model->fluxes) {
		auto flux = model->fluxes[id];

		auto scope = model->get_scope(flux->code_scope);
		check_flux_location(app, scope, flux->source_loc, flux->source);
		check_flux_location(app, scope, flux->source_loc, flux->target); //TODO: The scope may not be correct if the flux was redirected!!!
		
		auto var_id = register_state_variable<State_Var::Type::declared>(app, id, false, flux->name);
		auto var = app->state_vars[var_id];
		auto conn_id = connection_of_flux(var);
		
		var->boundary_type = flux->boundary_type;
		
		if(flux->boundary_type == Boundary_Type::none) {
			if(is_valid(conn_id) && !is_located(var->loc1)) {
				flux->source_loc.print_error_header(Mobius_Error::model_building); // TODO: The source loc is wrong if the connection comes from a redirection.
				fatal_error("You can't have a flux from nowhere to a connection.\n");
			}
		} else {
			if(!is_valid(conn_id) || model->connections[conn_id]->type != Connection_Type::grid1d) {
				flux->code->source_loc.print_error_header();
				fatal_error("A boundary flux can only be on a grid1d connection.");
			}
		}
	}
	
	warning_print("Generate fluxes and concentrations for dissolved quantities.\n");
	
	
	//NOTE: not that clean to have this part here, but it is just much easier if it is done before function resolution.
	for(auto var_id : dissolvedes) {
		auto var = app->state_vars[var_id];
		
		auto above_loc = remove_dissolved(var->loc1);
		std::vector<Var_Id> generate;
		for(auto flux_id : app->state_vars) {
			auto flux = app->state_vars[flux_id];
			if(!flux->is_valid() || !flux->is_flux()) continue;
			if(!(flux->loc1 == above_loc)) continue;
			if(is_located(flux->loc2)) {
				// If the target of the flux is a specific location, we can only send the dissolved quantity if it exists in that location.
				auto below_loc = add_dissolved(flux->loc2, var->loc1.last());
				auto below_var = app->state_vars.id_of(below_loc);
				if(!is_valid(below_var)) continue;
			}
			// See if it was declared that this flux should not carry this quantity (using a no_carry declaration)
			if(flux->type == State_Var::Type::declared) { // If this flux was itself generated, it won't have a declaration to look at.
				auto flux_reg = model->fluxes[as<State_Var::Type::declared>(flux)->decl_id];
				if(flux_reg->no_carry_by_default) continue;
				if(std::find(flux_reg->no_carry.begin(), flux_reg->no_carry.end(), var->loc1) != flux_reg->no_carry.end()) continue;
			}
			
			generate.push_back(flux_id);
		}
		
		Var_Location source  = var->loc1; // Note we have to copy it since we start registering new state variables, invalidating our pointer to var.
		std::string var_name = var->name; // Same: Have to copy, not take reference.
		auto dissolved_in_id = app->state_vars.id_of(above_loc);
		auto dissolved_in = app->state_vars[dissolved_in_id];
		
		sprintf(varname, "concentration(%s, %s)", var_name.data(), dissolved_in->name.data());
		Var_Id gen_conc_id = register_state_variable<State_Var::Type::dissolved_conc>(app, invalid_entity_id, false, varname);
		auto conc_var = as<State_Var::Type::dissolved_conc>(app->state_vars[gen_conc_id]);
		conc_var->conc_of = var_id;
		auto var_d = as<State_Var::Type::declared>(app->state_vars[var_id]);
		var_d->conc = gen_conc_id;
		{
			auto computed_conc_unit = divide(var_d->unit, dissolved_in->unit);
			auto has = model->hases[var_d->decl_id];
			if(is_valid(has->conc_unit)) {
				conc_var->unit = model->units[has->conc_unit]->data;
				bool success = match(&computed_conc_unit.standard_form, &conc_var->unit.standard_form, &conc_var->unit_conversion);
				if(!success) {
					has->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("We can't find a way to convert between the declared concentration unit ", conc_var->unit.to_utf8(), " and the computed concentration unit ", computed_conc_unit.to_utf8(), ".");
				}
				//warning_print("******* Unit conversion from ", computed_conc_unit.to_utf8(), " to ", conc_var->unit.to_utf8(), " was ", conc_var->unit_conversion, ".\n");
			} else {
				conc_var->unit = computed_conc_unit;
				conc_var->unit_conversion = 1.0;
			}
		}
		
		for(auto flux_id : generate) {
			std::string &flux_name = app->state_vars[flux_id]->name;
			sprintf(varname, "dissolved_flux(%s, %s)", var_name.data(), flux_name.data());
			Var_Id gen_flux_id = register_state_variable<State_Var::Type::dissolved_flux>(app, invalid_entity_id, false, varname);
			auto gen_flux = as<State_Var::Type::dissolved_flux>(app->state_vars[gen_flux_id]);
			auto flux = app->state_vars[flux_id];
			gen_flux->type = State_Var::Type::dissolved_flux;
			gen_flux->flux_of_medium = flux_id;
			gen_flux->flags = (State_Var::Flags)(gen_flux->flags | State_Var::Flags::flux);
			gen_flux->conc = gen_conc_id;
			gen_flux->loc1 = source;
			gen_flux->loc2.type = flux->loc2.type;
			gen_flux->boundary_type = flux->boundary_type;
			if(is_located(flux->loc2))
				gen_flux->loc2 = add_dissolved(flux->loc2, source.last());
			auto conn_id = connection_of_flux(flux);
			if(is_valid(conn_id))
				gen_flux->connection = conn_id;
		}
	}
}

Math_Expr_FT *
get_aggregation_weight(Model_Application *app, const Var_Location &loc1, Entity_Id to_compartment, Entity_Id connection = invalid_entity_id) {
	
	auto model = app->model;
	auto source = model->components[loc1.first()];
	Math_Expr_FT *agg_weight = nullptr;
	
	for(auto &agg : source->aggregations) {
		if(agg.to_compartment != to_compartment) continue;
		
		auto scope = model->get_scope(agg.code_scope);
		Standardized_Unit expected_unit = {};  // Expect dimensionless aggregation weights (unit conversion is something separate)
		Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters, expected_unit, connection };
		auto fun = resolve_function_tree(agg.code, &res_data);
		
		if(!match_exact(&fun.unit, &expected_unit)) {
			agg.code->source_loc.print_error_header();
			fatal_error("Expected the unit an aggregation_weight expression to resolve to ", expected_unit.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
		}
		
		agg_weight = make_cast(fun.fun, Value_Type::real);
		std::set<Entity_Id> parameter_refs;
		restrictive_lookups(agg_weight, Decl_Type::aggregation_weight, parameter_refs);
		
		for(auto par_id : parameter_refs) {
			bool ok = parameter_indexes_below_location(model, par_id, loc1);
			if(!ok) {
				agg.code->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The parameter \"", (*scope)[par_id], "\" is distributed over index sets that the source of the aggregation weight is not distributed over.");
			}
		}
		break;
	}
	
	return agg_weight;
}

Math_Expr_FT *
get_unit_conversion(Model_Application *app, Var_Location &loc1, Var_Location &loc2) {
	
	Math_Expr_FT *unit_conv = nullptr;
	if(!is_located(loc1) || !is_located(loc2)) return unit_conv;
	
	auto model = app->model;
	auto source = model->components[loc1.first()];
	
	for(auto &conv : source->unit_convs) {
		if(loc1 != conv.source || loc2 != conv.target) continue;
		
		auto ast   = conv.code;
		auto scope = model->get_scope(conv.code_scope);
		
		// TODO: Are we guaranteed that these exist when this gets called??
		auto first = app->state_vars[app->state_vars.id_of(loc1)];
		auto second = app->state_vars[app->state_vars.id_of(loc2)];
		auto expected_unit = divide(second->unit, first->unit);
		
		Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters, expected_unit.standard_form };
		auto fun = resolve_function_tree(ast, &res_data);
		unit_conv = make_cast(fun.fun, Value_Type::real);
		std::set<Entity_Id> parameter_refs;
		restrictive_lookups(unit_conv, Decl_Type::unit_conversion, parameter_refs);
		
		if(!match_exact(&fun.unit, &expected_unit.standard_form)) {
			ast->source_loc.print_error_header();
			fatal_error("Expected the unit of this unit_conversion expression to resolve to ", expected_unit.standard_form.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
		}
		
		for(auto par_id : parameter_refs) {
			bool ok = parameter_indexes_below_location(model, par_id, loc1);
			if(!ok) {
				ast->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The parameter \"", (*scope)[par_id], "\" is distributed over index sets that the source of the unit conversion is not distributed over.");
			}
		}
		break;
	}
	
	return unit_conv;
}

void
register_connection_agg(Model_Application *app, bool is_source, Var_Id target_var_id, Entity_Id target_comp, Entity_Id conn_id, char *varname) {
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	
	auto var = as<State_Var::Type::declared>(app->state_vars[target_var_id]);
	
	// See if we have a connection aggregate for this connection already.
	auto &aggs = is_source ? var->conn_source_aggs : var->conn_target_aggs;
	for(auto existing_agg : aggs) {
		if(as<State_Var::Type::connection_aggregate>(app->state_vars[existing_agg])->connection == conn_id)
			return;
	}
	
	if(is_source)
		sprintf(varname, "in_flux_connection_source(%s, %s)", connection->name.data(), app->state_vars[target_var_id]->name.data());
	else
		sprintf(varname, "in_flux_connection_target(%s, %s)", connection->name.data(), app->state_vars[target_var_id]->name.data());
	
	Var_Id agg_id = register_state_variable<State_Var::Type::connection_aggregate>(app, invalid_entity_id, false, varname);
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->state_vars[agg_id]);
	agg_var->agg_for = target_var_id;
	agg_var->is_source = is_source;
	agg_var->connection = conn_id;
	
	var = as<State_Var::Type::declared>(app->state_vars[target_var_id]);
	if(is_source)
		var->conn_source_aggs.push_back(agg_id);
	else {
		var->conn_target_aggs.push_back(agg_id);
		
		// NOTE: aggregation weights only supported for compartments for now..
		if(model->components[target_comp]->decl_type != Decl_Type::compartment) return;
		
		// Get aggregation weights if relevant
		auto find = app->find_connection_component(conn_id, target_comp);
		for(auto source_id : find->possible_sources) {
			Var_Location loc = app->state_vars[target_var_id]->loc1;
			loc.components[0] = source_id;
			auto wt = get_aggregation_weight(app, loc, target_comp, conn_id);
			auto source_var_id = app->state_vars.id_of(loc);
			if(wt) agg_var->weights.push_back({source_var_id, wt});
		}
	}
}

void
compose_and_resolve(Model_Application *app) {
	
	auto model = app->model;
	
	for(auto par_id : model->parameters) {
		auto par = model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_bool || par->decl_type == Decl_Type::par_enum) {
			s64 count = app->parameter_structure.instance_count(par_id);
			if(count == 1) {// TODO: Would like to get rid of this requirement, but without it we may end up needing a separate code tree per index tuple for a given state variable :(
				app->baked_parameters.push_back(par_id);
				//warning_print("Baking parameter \"", par->name, "\".\n");
			}
		}
	}
	
	//TODO: make better name generation system!
	char varname[1024];

	warning_print("Function tree resolution begin.\n");
	
	// TODO: check if there are unused aggregation data or unit conversion data!
	// TODO: warning or error if a flux is still marked as "out" (?)   -- though this could be legitimate in some cases where you just want to run a sub-module and not link it up with anything.
	
	Var_Map in_flux_map;
	Var_Map2 needs_aggregate;
	
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		
		if(!var->is_valid()) continue;
		if(var->is_flux()) {
			// NOTE: This part must also be done for generated (dissolved) fluxes, not just declared ones, which is why we don't skip non-declared ones yet.
			var->unit_conversion_tree = get_unit_conversion(app, var->loc1, var->loc2);
			auto transported_id = invalid_var;
			if(is_located(var->loc1)) transported_id = app->state_vars.id_of(var->loc1);
			else if(is_located(var->loc2)) transported_id = app->state_vars.id_of(var->loc2);
			if(is_valid(transported_id)) {
				auto transported = app->state_vars[transported_id];
				auto unit = divide(transported->unit, app->time_step_unit);  // This is the unit we need the value to be in for use in state variable updates
				
				if(var->type == State_Var::Type::declared) {
					// Hmm, this actually allows not just for time scaling, but also other scaling. For instance, it allows [g, ha-1, day-1] to be converted to [n g, k m-2, day-1]
					auto var2 = as<State_Var::Type::declared>(var);
					bool success = match(&var->unit.standard_form, &unit.standard_form, &var2->flux_time_unit_conv);
					
					if(!success) {
						model->fluxes[var2->decl_id]->source_loc.print_error_header(Mobius_Error::model_building);
						fatal_error("The flux \"", var2->name, "\" has been given a unit that is not compatible with the unit of the transported quantity, which is ", transported->unit.to_utf8(), ", or with the time step unit of the model, which is ", app->time_step_unit.to_utf8(), ".");
					}
					//warning_print("Time unit conversion is ", var2->flux_time_unit_conv, ".\n");
				} else {
					// NOTE: we could also make it have the same time part as the parent flux, but it is tricky.
					var->unit = unit;
				}
				//var->unit = divide(transported->unit, app->time_step_unit);
			}
		}
		
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);
		
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		Math_Expr_AST *override_ast = nullptr;
		bool override_is_conc = false;
		bool initial_is_conc = false;
		
		//TODO: it would probably be better to default in_loc to be loc1 regardless (except when loc1 is not located).
		Var_Location in_loc;
		in_loc.type = Var_Location::Type::nowhere;
		Entity_Id from_compartment = invalid_entity_id;
		Entity_Id connection = invalid_entity_id;
		
		Decl_Scope *code_scope       = nullptr;
		Decl_Scope *other_code_scope = nullptr;
		
		if(var2->decl_id.reg_type == Reg_Type::flux) {
			bool target_was_out = false;
			
			auto flux_decl = model->fluxes[var2->decl_id];
			target_was_out = flux_decl->target_was_out;
			ast = flux_decl->code;
			
			code_scope = model->get_scope(flux_decl->code_scope);
			connection = flux_decl->connection_target;
			
			bool target_is_located = is_located(var->loc2) && !target_was_out; // Note: the target could have been re-directed by the model. In this setting we only care about how it was declared originally.
			if(is_located(var->loc1)) {
				from_compartment = var->loc1.first();
				//if(!target_is_located || var->loc1 == var->loc2)    // Hmm, it seems to make more sense to always let the source be the context if it is located.
				in_loc = var->loc1;
			} else if(target_is_located)
				in_loc = var->loc2;
			
			if(!is_valid(from_compartment)) from_compartment = in_loc.first();
			
		} else if(var2->decl_id.reg_type == Reg_Type::has) {
			auto has = model->hases[var2->decl_id];
			ast      = has->code;
			init_ast = has->initial_code;
			override_ast = has->override_code;
			override_is_conc = has->override_is_conc;
			initial_is_conc  = has->initial_is_conc;
			code_scope = model->get_scope(has->code_scope);
			other_code_scope = code_scope;
			
			if(var2->decl_type == Decl_Type::quantity && ast) {
				has->source_loc.print_error_header();
				fatal_error("A quantity should not have an un-tagged code block.");
			}
			
			if(!ast) {
				auto comp = model->components[has->var_location.last()];
				ast = comp->default_code;
				if(ast)
					code_scope = model->get_scope(comp->code_scope);
			}
			
			if(override_ast && (var2->decl_type != Decl_Type::quantity || (override_is_conc && !var->loc1.is_dissolved()))) {
				override_ast->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Either got an '.override' block on a property or a '.override_conc' block on a non-dissolved variable.");
			}
			in_loc = has->var_location;
			from_compartment = in_loc.first();
		}
		
			
		Function_Resolve_Data res_data = { app, code_scope, in_loc, &app->baked_parameters, var->unit.standard_form, connection };
		Math_Expr_FT *fun = nullptr;
		if(ast) {
			auto res = resolve_function_tree(ast, &res_data);
			fun = res.fun;
			fun = make_cast(fun, Value_Type::real);
			replace_conc(app, fun); // Replace explicit conc() calls by pointing them to the conc variable.
			find_other_flags(fun, in_flux_map, needs_aggregate, var_id, from_compartment, false);
			
			if(!match_exact(&res.unit, &res_data.expected_unit)) {
				ast->source_loc.print_error_header();
				fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
			}
		}
		var2->function_tree = fun;
		
		res_data.scope = other_code_scope;
		if(init_ast) {
			if(initial_is_conc)
				res_data.expected_unit = app->state_vars[var2->conc]->unit.standard_form;
			
			auto fun = resolve_function_tree(init_ast, &res_data);
			var2->initial_function_tree = make_cast(fun.fun, Value_Type::real);
			remove_lasts(var2->initial_function_tree, true);
			replace_conc(app, var2->initial_function_tree);  // Replace explicit conc() calls by pointing them to the conc variable
			find_other_flags(var2->initial_function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, true);
			var2->initial_is_conc = initial_is_conc;
			
			if(!match_exact(&fun.unit, &res_data.expected_unit)) {
				init_ast->source_loc.print_error_header();
				fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
			}
			
			if(initial_is_conc && (var2->decl_type != Decl_Type::quantity || !var->loc1.is_dissolved())) {
				init_ast->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Got an \"initial_conc\" block for a non-dissolved variable");
			}
		} else
			var2->initial_function_tree = nullptr;
		
		if(override_ast) {
			if(override_is_conc)
				res_data.expected_unit = app->state_vars[var2->conc]->unit.standard_form;
			else
				res_data.expected_unit = var2->unit.standard_form; //In case it was overwritten above..
			
			auto fun = resolve_function_tree(override_ast, &res_data);
			auto override_tree = prune_tree(fun.fun);
			bool no_override = false;
			if(override_tree->expr_type == Math_Expr_Type::identifier) {
				auto ident = static_cast<Identifier_FT *>(override_tree);
				no_override = (ident->variable_type == Variable_Type::no_override);
			}
			if(no_override)
				var2->override_tree = nullptr;
			else {
				if(!match_exact(&fun.unit, &res_data.expected_unit)) {
					init_ast->source_loc.print_error_header();
					fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
				}
				
				var2->override_tree = make_cast(override_tree, Value_Type::real);
				var2->override_is_conc = override_is_conc;
				replace_conc(app, var2->override_tree);
				find_other_flags(var2->override_tree, in_flux_map, needs_aggregate, var_id, from_compartment, false);
			}
		} else
			var2->override_tree = nullptr;
	}
	
	// Invalidate dissolved fluxes if both source and target is overridden.
	
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(var->type == State_Var::Type::dissolved_flux) {
			bool valid_source = true;
			if(is_located(var->loc1)) {
				auto source = as<State_Var::Type::declared>(app->state_vars[app->state_vars.id_of(var->loc1)]);
				if(source->override_tree)
					valid_source = false;
			} else
				valid_source = false;
			bool valid_target = true;
			if(is_located(var->loc2)) {
				auto target = as<State_Var::Type::declared>(app->state_vars[app->state_vars.id_of(var->loc2)]);
				if(target->override_tree)
					valid_target = false;
			} else
				valid_target = false;
			if(!valid_source && !valid_target)
				var->flags = (State_Var::Flags)(var->flags | State_Var::Flags::invalid);
		}
	}
	
	// TODO: Is this necessary, or could we just do this directly when we build the connections below ??
	// 	May have to do with aggregates of fluxes having that decl_type, and that is confusing? But they should not have connections any way.
	std::set<std::pair<Entity_Id, Var_Id>> may_need_connection_target;
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(!var->is_valid() || !var->is_flux()) continue;
		
		auto conn_id = connection_of_flux(var);
		if(is_valid(conn_id)) {
			Var_Location loc = var->loc1;
			if(var->boundary_type == Boundary_Type::top)
				loc = var->loc2; // NOTE: For top_boundary only the target is set.
			
			// TODO: Do we need to check that loc *must* be located if this is not a bottom_boundary (or is that taken care of elsewhere?)
			if(is_located(loc)) {
				Var_Id target_id = app->state_vars.id_of(loc);
				may_need_connection_target.insert({conn_id, target_id});
			}
		}
	}
	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	// TODO: interaction between in_flux and aggregate declarations (i.e. we have something like an explicit aggregate(in_flux(soil.water)) in the code.
	// Or what about last(aggregate(in_flux(bla))) ?
	
	// note: We always generate an aggregate if the source compartment of a flux has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the weight is trivial
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(!var->is_valid() || !var->is_flux()) continue;
		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		
		Entity_Id exclude_index_set_from_loc = invalid_entity_id;
		if(var->boundary_type == Boundary_Type::bottom) {
			auto conn_id = connection_of_flux(var);
			exclude_index_set_from_loc = app->get_single_connection_index_set(conn_id);
		}
		
		if(!location_indexes_below_location(model, var->loc1, var->loc2, exclude_index_set_from_loc))
			needs_aggregate[var_id.id].first.insert(var->loc2.first());
	}
	
	warning_print("Generate state vars for aggregates.\n");
		
	for(auto &need_agg : needs_aggregate) {
		auto var_id = Var_Id {Var_Id::Type::state_var, need_agg.first};
		auto var = app->state_vars[var_id];
		if(var->flags & State_Var::Flags::invalid) continue;
		
		auto loc1 = var->loc1;
		if(var->type == State_Var::Type::dissolved_conc) {
			auto var2 = as<State_Var::Type::dissolved_conc>(var);
			loc1 = app->state_vars[var2->conc_of]->loc1;
		}
		
		auto source = model->components[loc1.first()];
		
		for(auto to_compartment : need_agg.second.first) {
			
			Math_Expr_FT *agg_weight = get_aggregation_weight(app, loc1, to_compartment);
			
			if(!agg_weight) {
				auto target = model->components[to_compartment];
				//TODO: need to give much better feedback about where the variable was declared and where it was used with an aggregate()
				if(var->is_flux()) {
					fatal_error(Mobius_Error::model_building, "The flux \"", var->name, "\" goes from compartment \"", source->name, "\" to compartment, \"", target->name, "\", but the first compartment is distributed over a higher number of index sets than the second. This is only allowed if you specify an aggregation_weight between the two compartments.");
				} else {
					fatal_error(Mobius_Error::model_building, "Missing aggregation_weight for variable ", var->name, " between compartments \"", source->name, "\" and \"", target->name, "\".\n");
				}
			}
			var->flags = (State_Var::Flags)(var->flags | State_Var::has_aggregate);
			
			//TODO: We also have to handle the case where the agg. variable was a series!
			// note: can't reference "var" below this (without looking it up again). The vector it resides in may have reallocated.
			
			sprintf(varname, "aggregate(%s)", var->name.data());
			Var_Id agg_id = register_state_variable<State_Var::Type::regular_aggregate>(app, invalid_entity_id, false, varname);
			
			auto agg_var = as<State_Var::Type::regular_aggregate>(app->state_vars[agg_id]);
			agg_var->agg_of = var_id;
			agg_var->aggregation_weight_tree = agg_weight;
			if(var->is_flux())
				agg_var->flags = (State_Var::Flags)(agg_var->flags | State_Var::Flags::flux);
			
			var = app->state_vars[var_id];   //NOTE: had to look it up again since we may have resized the vector var pointed into

			// NOTE: it makes a lot of the operations in model_compilation more natural if we decouple the fluxes like this:
			agg_var->loc1.type = Var_Location::Type::nowhere;
			agg_var->loc2 = var->loc2;
			var->loc2.type = Var_Location::Type::nowhere;
			
			agg_var->unit_conversion_tree = var->unit_conversion_tree;
			// NOTE: it is easier to keep track of who is supposed to use the unit conversion if we only keep a reference to it on the one that is going to use it.
			//   we only multiply with the unit conversion at the point where we add the flux to the target, so it is only needed on the aggregate.
			var->unit_conversion_tree = nullptr;
			agg_var->agg_to_compartment = to_compartment;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = as<State_Var::Type::declared>(app->state_vars[looked_up_by]);
				
				Entity_Id lu_compartment = lu->loc1.first();
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.first();
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the Var_Map2
				
				if(lu->function_tree)
					replace_flagged(lu->function_tree, var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->override_tree)
					replace_flagged(lu->override_tree, var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->initial_function_tree)
					replace_flagged(lu->initial_function_tree, var_id, agg_id, Identifier_FT::Flags::aggregate);
			}
		}
	}
	
	// TODO: Should we give an error if there is a connection flux on an overridden variable?
	
	warning_print("Generate state vars for in_flux_connection.\n");
	// TODO: Support multiple connections for the same state variable.
	for(auto &pair : may_need_connection_target) {
		
		auto &conn_id = pair.first;
		auto &source_id  = pair.second;
		
		auto connection = model->connections[conn_id];
		
		if(connection->type == Connection_Type::directed_tree) {
			
			for(auto &target_comp : app->connection_components[conn_id.id]) {
				
				// If it wasn't actually put as a possible target in the data set, don't bother about it.
				if(target_comp.possible_sources.empty()) continue;

				auto target_loc = app->state_vars[source_id]->loc1;
				target_loc.components[0] = target_comp.id;
				auto target_id = app->state_vars.id_of(target_loc);
				if(!is_valid(target_id))   // NOTE: the target may not have that state variable. This can especially happen for dissolvedes.
					continue;
				
				// TODO: Even if it is present in the connection data, it may not appear as a target, only as a source. Should also be checked eventually.
				
				register_connection_agg(app, false, target_id, target_comp.id, conn_id, &varname[0]);
			}
		} else if (connection->type == Connection_Type::all_to_all || connection->type == Connection_Type::grid1d) {
			if(connection->components.size() != 1 || app->connection_components[conn_id.id].size() != 1)
				fatal_error(Mobius_Error::internal, "Expected exactly one compartment for this type of connection."); // Should have been detected earlier
			
			// NOTE: all_to_all and grid1d connections can (currently) only go from one state variable to another
			// instance of itself ( the source_id ).
			auto target_comp = app->connection_components[conn_id.id][0].id;
			if(connection->type == Connection_Type::all_to_all)
				register_connection_agg(app, true, source_id, target_comp, conn_id, &varname[0]);
			register_connection_agg(app, false, source_id, target_comp, conn_id, &varname[0]);
			
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection type in compose_and_resolve()");
		}
	}
	
	warning_print("Generate state vars for in_flux.\n");
	for(auto &in_flux : in_flux_map) {
		
		auto &key = in_flux.first;
		//auto [target_id, connection] = key;
		Var_Id target_id = key.first;
		auto target = as<State_Var::Type::declared>(app->state_vars[target_id]);
		Entity_Id connection = key.second;
		
		Var_Id in_flux_id = invalid_var;
		if(!is_valid(connection)) {
			sprintf(varname, "in_flux(%s)", target->name.data());
			in_flux_id = register_state_variable<State_Var::Type::in_flux_aggregate>(app, invalid_entity_id, false, varname);
			auto in_flux_var = as<State_Var::Type::in_flux_aggregate>(app->state_vars[in_flux_id]);
			in_flux_var->in_flux_to = target_id;
		} else {
			//We don't have to register an aggregate for the connection since that will always have been done for a variable on a connection if it is at all relevant.
			for(auto conn_agg_id : target->conn_target_aggs) {
				if( as<State_Var::Type::connection_aggregate>(app->state_vars[conn_agg_id])->connection == connection)
					in_flux_id = conn_agg_id;
			}
		}
		// NOTE: if there was no connection aggregate it means that there was no flux going there. This is not an error in the use of in_flux because the current module could not always know about it. In that case, replace_flagged will still correctly put a literal 0 there instead.
		
		for(auto rep_id : in_flux.second) {
			auto var = as<State_Var::Type::declared>(app->state_vars[rep_id]);
			replace_flagged(var->function_tree, target_id, in_flux_id, Identifier_FT::Flags::in_flux);
		}
	}

	// See if the code for computing a variable looks up other values that are distributed over index sets that the var is not distributed over.
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(!var->is_valid()) continue;
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);
		
		if(var2->function_tree)
			check_valid_distribution_of_dependencies(app, var2->function_tree,         var, false);
		if(var2->initial_function_tree)
			check_valid_distribution_of_dependencies(app, var2->initial_function_tree, var, true);
	}
	
	
	// TODO: we can't really free everything here since the ASTs may need to be reused to build a new model app if it is recompiled later.
	//   however, we want to have memory management for these things eventually.
	
	/*
	// Free memory of loaded source files and all abstract syntax trees.
	file_handler.unload_all();
	
	delete main_decl;
	main_decl = nullptr;
	// NOTE: can't just iterate over all modules, because some could be inlined in the main decl. We can only free directly the ones that were loaded as top declarations.
	for(auto &file : parsed_decls) {
		for(auto &parsed : file.second) {
			if(parsed.second.reg_type == Reg_Type::module) {
				auto module = modules[parsed.second];
				delete module->decl;
				module->decl = nullptr;
			} else if(parsed.second.reg_type == Reg_Type::library) {
				auto lib = libraries[parsed.second];
				delete lib->decl;
				lib->decl = nullptr;
			}
		}
	}
	// NOTE: this also invalidates all other ASTs since they point into the top decl ASTs. Maybe we should null them too.
	// The intention in any case is that all of them are translated into processed forms by this point and are no longer needed.
	*/
}




