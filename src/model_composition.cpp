

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
register_state_variable(Model_Application *app, Entity_Id decl_id, bool is_series, const std::string &name) {
	
	auto model = app->model;
	
	if(type == State_Var::Type::declared && !is_valid(decl_id))
		fatal_error(Mobius_Error::internal, "Didn't get a decl_id for a declared variable.");
	
	Var_Location loc = invalid_var_location;
	if(is_valid(decl_id) && decl_id.reg_type == Reg_Type::var) {
		auto var = model->vars[decl_id];
		loc = var->var_location;
		check_if_var_loc_is_well_formed(model, loc, var->source_loc);
	}
	
	Var_Id var_id = app->vars.register_var<type>(loc, name, is_series ? Var_Id::Type::series : Var_Id::Type::state_var);
	auto var = app->vars[var_id];
	
	if(var->name.empty())
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
	var->type          = type;

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
	
	if(is_valid(loc.connection_id)) {
		auto conn = app->model->connections[loc.connection_id];
		if(conn->type == Connection_Type::grid1d) {
			if(is_source && loc.restriction == Var_Loc_Restriction::top) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("'top' can't be in the source of a flux.");
			}
			if(!is_source && loc.restriction == Var_Loc_Restriction::bottom) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("'bottom' can't be in the target of a flux.");
			}
			if(is_source && loc.restriction == Var_Loc_Restriction::specific) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("'specific' can't be in the source of a flux.");
			}
		} else {
			if(is_source) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("For this connection type, the connection can't be specified in the source of the flux.");
			} else if (loc.restriction != Var_Loc_Restriction::below) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This connection type can't have fluxes with specified locations.");
			}
		}
		
		//TODO: This check should NOT use the connection_components. It must use the component options that are stored on the flux.
		auto &components = app->connection_components[loc.connection_id].components;
		bool found = false;
		for(int idx = 0; idx < loc.n_components; ++idx) {
			for(auto &comp : components)
				if(loc.components[idx] == comp.id) found = true;
		}
		if(!found) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("None of the components on this var location are supported for the connection \"", conn->name, "\".");
		}
			
	} else if(loc.restriction != Var_Loc_Restriction::none) {
		fatal_error(Mobius_Error::internal, "Got a var location with a restriction that was not tied to a connection.");
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
find_identifier_flags(Math_Expr_FT *expr, Var_Map &in_fluxes, Var_Map2 &aggregates, Var_Id looked_up_by, Entity_Id lookup_compartment) {
	for(auto arg : expr->exprs) find_identifier_flags(arg, in_fluxes, aggregates, looked_up_by, lookup_compartment);
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->flags & Identifier_FT::Flags::in_flux)
			in_fluxes[{ident->var_id, ident->other_connection}].push_back(looked_up_by);
		
		if(ident->flags & Identifier_FT::Flags::aggregate) {
			if(!is_valid(lookup_compartment)) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Can't use aggregate() in this function body because it does not belong to a compartment.");
			}
				
			aggregates[ident->var_id.id].first.insert(lookup_compartment);         //OOOps!!!! This is not correct if this was applied to an input series!
			
			if(is_valid(looked_up_by))
				aggregates[ident->var_id.id].second.push_back(looked_up_by);
		}
	}
}


Math_Expr_FT *
replace_flagged(Math_Expr_FT *expr, Var_Id replace_this, Var_Id with, Identifier_FT::Flags flag, Entity_Id connection_id = invalid_entity_id) {
	
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = replace_flagged(expr->exprs[idx], replace_this, with, flag, connection_id);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	if(ident->variable_type == Variable_Type::state_var 
		&& (ident->var_id == replace_this)
		&& (ident->flags & flag)
		&& ((ident->other_connection == connection_id) || (!is_valid(ident->other_connection) && !is_valid(connection_id)))
		) {
		
		if(is_valid(with)) {
			ident->var_id = with;
			ident->flags = (Identifier_FT::Flags)(ident->flags & ~flag);
			ident->other_connection = invalid_entity_id;
			//return ident;
		} else {
			delete expr;
			return make_literal((double)0.0);
		}
	}

	return expr;
}

bool
location_indexes_below_location(Model_Application *app, const Var_Location &loc, const Var_Location &below_loc, const Specific_Var_Location &loc2,
	Entity_Id exclude_index_set_from_loc = invalid_entity_id, Entity_Id exclude_index_set_from_var = invalid_entity_id) {
	
	auto model = app->model;
	
	if(!is_located(loc) || !is_located(below_loc))
		fatal_error(Mobius_Error::internal, "Got a non-located location to a location_indexes_below_location() call.");
	
	// Ugh, also a bit hacky. Should be factored out.
	// The purpose of this is to allow a flux on a graph connection to reference something that depends on the edge index set of the graph from the source of the flux.
	if(!is_valid(exclude_index_set_from_loc) && is_valid(loc2.connection_id) && model->connections[loc2.connection_id]->type == Connection_Type::directed_graph) {
		auto find_source = app->find_connection_component(loc2.connection_id, below_loc.components[0], false);
		if(find_source)
			exclude_index_set_from_loc = find_source->edge_index_set;
	}
	
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
parameter_indexes_below_location(Model_Application *app, const Identifier_Data &dep, const Var_Location &below_loc, const Specific_Var_Location &loc2,
	Entity_Id exclude_index_set_from_var = invalid_entity_id) {
		
	auto par = app->model->parameters[dep.par_id];
	auto par_comp_id = app->model->par_groups[par->par_group]->component;
	
	// Global parameters should be accessible from anywhere.
	if(!is_valid(par_comp_id)) return true;
	
	// NOTE: This is a bit of a hack that allows us to reuse location_indexes_below_location. We have to monitor that it doesn't break.
	Var_Location loc;
	loc.type = Var_Location::Type::located;
	loc.n_components = 1;
	loc.components[0] = par_comp_id;
	
	Entity_Id exclude_index_set_from_loc = avoid_index_set_dependency(app, dep.restriction);
	
	return location_indexes_below_location(app, loc, below_loc, loc2, exclude_index_set_from_loc, exclude_index_set_from_var);
}

void
check_valid_distribution_of_dependencies(Model_Application *app, Math_Expr_FT *function, Specific_Var_Location &loc, const Specific_Var_Location &loc2 = Specific_Var_Location()) {
	
	// TODO: The loc in this function is really the below_loc. Maybe rename it to avoid confusion.
	
	Dependency_Set code_depends;
	register_dependencies(function, &code_depends);
	
	// NOTE: We should not have undergone codegen yet, so the source location of the top node of the function should be valid.
	Source_Location source_loc = function->source_loc;
	
	if(!is_located(loc))
		fatal_error(Mobius_Error::internal, "Somehow a totally unlocated variable checked in check_valid_distribution_of_dependencies().");
	
	Entity_Id exclude_index_set_from_var = avoid_index_set_dependency(app, loc);
	
	// TODO: in these error messages we should really print out the two tuples of index sets.
	for(auto &dep : code_depends.on_parameter) {
		
		if(!parameter_indexes_below_location(app, dep, loc, loc2, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This code looks up the parameter \"", app->model->parameters[dep.par_id]->name, "\". This parameter belongs to a component that is distributed over a higher number of index sets than the context location of the code.");
		}
	}
	for(auto &dep : code_depends.on_series) {
		Entity_Id exclude_index_set_from_loc = avoid_index_set_dependency(app, dep.restriction);
		
		if(!location_indexes_below_location(app, app->vars[dep.var_id]->loc1, loc, loc2, exclude_index_set_from_loc, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This code looks up the input series \"", app->vars[dep.var_id]->name, "\". This series has a location that is distributed over a higher number of index sets than the context location of the code.");
		}
	}
	
	for(auto &dep : code_depends.on_state_var) {
		auto dep_var = app->vars[dep.var_id];
		
		// For generated in_flux aggregation variables we are instead interested in the variable that is the target of the fluxes.
		if(dep_var->type == State_Var::Type::in_flux_aggregate)
			dep_var = app->vars[as<State_Var::Type::in_flux_aggregate>(dep_var)->in_flux_to];
		
		// If it is an aggregate, index set dependencies will be generated to be correct.
		if(dep_var->type == State_Var::Type::regular_aggregate)
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
		
		Entity_Id exclude_index_set_from_loc = avoid_index_set_dependency(app, dep.restriction);
		
		if(!location_indexes_below_location(app, dep_var->loc1, loc, loc2, exclude_index_set_from_loc, exclude_index_set_from_var)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This code looks up the state variable \"", dep_var->name, "\". The latter state variable is distributed over a higher number of index sets than the context location of the prior code.");
		}
	}
}

inline bool 
has_code(Entity_Registration<Reg_Type::var> *var) {
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
	auto res = resolve_function_tree(flux->no_carry_ast, &res_data);
	
	for(auto expr : res.fun->exprs) {
		if(expr->expr_type != Math_Expr_Type::identifier) {
			expr->source_loc.print_error_header();
			fatal_error("Only quantity identifiers are allowed in a 'no_carry'.");
		}
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type != Variable_Type::state_var) {
			ident->source_loc.print_error_header();
			fatal_error("Only state variables are relevant for a 'no_carry'.");
		}
		if(ident->flags != Identifier_Data::Flags::none || ident->restriction.restriction != Var_Loc_Restriction::Restriction::none) {
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
prelim_compose(Model_Application *app, std::vector<std::string> &input_names) {
	
	auto model = app->model;
	
	// TODO: We could maybe put a 'check_validity' method on Entity_Registration, and loop over all of them and call that. Could even be done at the end of load_model in model_declaration.cpp
	
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
	
	for(auto group_id : model->par_groups) {
		auto par_group = model->par_groups[group_id];
		if(is_valid(par_group->component) && model->components[par_group->component]->decl_type == Decl_Type::property) {
			par_group->source_loc.print_error_header();
			fatal_error("A 'par_group' can not be attached to a 'property', only to a 'compartment' or 'quantity'.");
		}
	}
	
	
	// NOTE: determine if a given var_location has code to compute it (otherwise it will be an input series)
	// also make sure there are no conflicting var declarations of the same var_location (across modules)
	std::unordered_map<Var_Location, Entity_Id, Var_Location_Hash> has_location;
	
	for(Entity_Id id : model->vars) {
		auto var = model->vars[id];
		
		bool found_code = has_code(var);
		
		// TODO: check for mismatching units and names between declarations.
		Entity_Registration<Reg_Type::var> *var2 = nullptr;
		auto find = has_location.find(var->var_location);
		if(find != has_location.end()) {
			var2 = model->vars[find->second];
			
			if(found_code && has_code(var2)) {
				var->source_loc.print_error_header();
				error_print("Only one 'var' declaration for the same variable location can have code associated with it. There is a conflicting declaration here:\n");
				var2->source_loc.print_error();
				mobius_error_exit();
			}
		}
		
		auto comp = model->components[var->var_location.last()];
		// Check if it has default code.
		if(!found_code)
			if(comp->default_code) found_code = true;
		
		// Note: for properties we only want to put the one that has code as the canonical one. If there isn't any with code, they will be considered input series
		if(comp->decl_type == Decl_Type::property && found_code)
			has_location[var->var_location] = id;
		
		// Note: for quantities, we can have some that don't have code associated with them at all, and we still need to choose one canonical one to use for the state variable registration below.
		//     (thus quantities can also not be input series)
		if(comp->decl_type == Decl_Type::quantity && (!var2 || !has_code(var2)))
			has_location[var->var_location] = id;
	}
	
	std::vector<Var_Id> dissolvedes;
	
	// TODO: we could move some of the checks in this loop to the above loop.

	for(int n_components = 2; n_components <= max_var_loc_components; ++n_components) { // NOTE: We have to process these in order so that e.g. soil.water exists when we process soil.water.oc
		for(Entity_Id id : model->vars) {
			auto var = model->vars[id];
			if(var->var_location.n_components != n_components) continue;
			
			if(var->var_location.is_dissolved()) {
				auto above_var = app->vars.id_of(remove_dissolved(var->var_location));
				if(!is_valid(above_var)) {
					var->source_loc.print_error_header(Mobius_Error::model_building);
					//TODO: Print the above_var.
					fatal_error("The located quantity that this 'var' declaration is assigned to has itself not been created using a 'var' declaration.");
				}
			}
			
			auto name = var->var_name;
			if(name.empty())
				name = model->find_entity(var->var_location.last())->name;  //TODO: should this use the serial name instead?
			
			Decl_Type type = model->find_entity(var->var_location.last())->decl_type;
			auto find = has_location.find(var->var_location);
			
			bool is_series = false;
			if(find == has_location.end()) is_series = true; // No declaration provided code for this series, so it is an input series.
			else if(type == Decl_Type::property) {
				// For properties, they can be overridden with input series.
				// TODO: It should probably be declared explicitly on the property if this is OK.
				// TODO: This warning is printed for every .var() declaration of it instead of just once. This problem may go away with the new module declaration system.
				if(std::find(input_names.begin(), input_names.end(), name) != input_names.end()) {
					is_series = true;
					log_print("Overriding property \"", name, "\" with an input series.\n");
				}
			}
			
			// It is a bit annoying that we have to do this loop for every state variable registration, but we have to intercept it so that it doesn't become registered as a series.
			auto special_id = invalid_entity_id;
			for(auto special_id0 : model->special_computations) {
				auto special = model->special_computations[special_id0];
				if(special->target == var->var_location) {
					is_series = false;
					special_id = special_id0;
					if(type != Decl_Type::property) {
						special->source_loc.print_error_header();
						fatal_error("A special_computation can only be assigned to a property.");
					}
					break;
				}
			}
			
			// If was already registered by another module, we don't need to re-register it.
			// TODO: still need to check for conflicts (unit, name) (ideally bake this check into the check where we build the has_location data (which is not implemented yet))
			if(is_valid(app->vars.id_of(var->var_location))) continue;
			
			if(is_series) {
				register_state_variable<State_Var::Type::declared>(app, id, true, name);
			} else if (is_valid(special_id) || id == find->second) {
				// This is the particular var declaration that provided the code, so we register a state variable using this one.
				Var_Id var_id = register_state_variable<State_Var::Type::declared>(app, id, false, name);
				if(var->var_location.is_dissolved() && type == Decl_Type::quantity && !is_valid(special_id))
					dissolvedes.push_back(var_id);
				if(is_valid(special_id))
					as<State_Var::Type::declared>(app->vars[var_id])->special_computation = special_id;
			}
		}
	}
	
	for(auto loc_id : model->locs) {
		auto loc = model->locs[loc_id];
		if(!is_valid(loc->par_id)) continue;
		check_location(app, loc->source_loc, loc->loc);
	}
	
	for(auto solve_id : model->solvers) {
		auto solver = model->solvers[solve_id];
		
		for(auto &loc : solver->locs)
			check_location(app, solver->source_loc, loc, true);
	}
	
	//TODO: make better name generation system!
	char varname[1024];
	
	for(Entity_Id id : model->fluxes) {
		auto flux = model->fluxes[id];

		check_location(app, flux->source_loc, flux->source, true, true, true);
		check_location(app, flux->source_loc, flux->target, true, true, false);
		
		if(!is_located(flux->source) && is_valid(flux->target.connection_id) && flux->target.restriction == Var_Loc_Restriction::below) {
			flux->source_loc.print_error_header(Mobius_Error::model_building); 
			fatal_error("You can't have a flux from 'out' to a connection.\n");
		}
		
		auto var_id = register_state_variable<State_Var::Type::declared>(app, id, false, flux->name);
		auto var = app->vars[var_id];
		
		resolve_no_carry(app, var);
	}
	
	//NOTE: not that clean to have this part here, but it is just much easier if it is done before function resolution.
	// For instance, we want the var_id of each concentration variable to exist when we resolve the function trees since the code in those could refer to concentrations.
	for(auto var_id : dissolvedes) {
		auto var = app->vars[var_id];
		
		auto above_loc = remove_dissolved(var->loc1);
		std::vector<Var_Id> generate;
		for(auto flux_id : app->vars.all_fluxes()) {
			auto flux = app->vars[flux_id];
			if(!(flux->loc1 == above_loc)) continue;
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
		auto dissolved_in = app->vars[dissolved_in_id];
		
		sprintf(varname, "concentration(%s, %s)", var_name.data(), dissolved_in->name.data());
		Var_Id gen_conc_id = register_state_variable<State_Var::Type::dissolved_conc>(app, invalid_entity_id, false, varname);
		auto conc_var = as<State_Var::Type::dissolved_conc>(app->vars[gen_conc_id]);
		conc_var->conc_of = var_id;
		auto var_d = as<State_Var::Type::declared>(app->vars[var_id]);
		var_d->conc = gen_conc_id;
		{
			auto computed_conc_unit = divide(var_d->unit, dissolved_in->unit);
			auto var_decl = model->vars[var_d->decl_id];
			if(is_valid(var_decl->conc_unit)) {
				conc_var->unit = model->units[var_decl->conc_unit]->data;
				bool success = match(&computed_conc_unit.standard_form, &conc_var->unit.standard_form, &conc_var->unit_conversion);
				if(!success) {
					var_decl->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("We can't find a way to convert between the declared concentration unit ", conc_var->unit.to_utf8(), " and the computed concentration unit ", computed_conc_unit.to_utf8(), ".");
				}
			} else {
				conc_var->unit = computed_conc_unit;
				conc_var->unit_conversion = 1.0;
			}
		}
		
		for(auto flux_id : generate) {
			std::string &flux_name = app->vars[flux_id]->name;
			sprintf(varname, "dissolved_flux(%s, %s)", var_name.data(), flux_name.data());
			Var_Id gen_flux_id = register_state_variable<State_Var::Type::dissolved_flux>(app, invalid_entity_id, false, varname);
			auto gen_flux = as<State_Var::Type::dissolved_flux>(app->vars[gen_flux_id]);
			auto flux = app->vars[flux_id];
			gen_flux->type = State_Var::Type::dissolved_flux;
			gen_flux->flux_of_medium = flux_id;
			gen_flux->set_flag(State_Var::flux);
			gen_flux->conc = gen_conc_id;
			
			// Hmm, this is a bit annoying.
			gen_flux->loc1 = source;
			gen_flux->loc1.connection_id = flux->loc1.connection_id;
			gen_flux->loc2.restriction = flux->loc2.restriction;
			
			if(is_located(flux->loc2))
				gen_flux->loc2 = add_dissolved(flux->loc2, source.last());
			gen_flux->loc2.type = flux->loc2.type;
			gen_flux->loc2.restriction = flux->loc2.restriction;
			gen_flux->loc2.connection_id = flux->loc2.connection_id;
		}
	}
}

Math_Expr_FT *
get_aggregation_weight(Model_Application *app, const Var_Location &loc1, Entity_Id to_compartment, Entity_Id connection = invalid_entity_id) {
	
	auto model = app->model;
	auto source = model->components[loc1.first()];
	
	for(auto &agg : source->aggregations) {
		if(agg.to_compartment != to_compartment) continue;
		
		auto scope = model->get_scope(agg.scope_id);
		Standardized_Unit expected_unit = {};  // Expect dimensionless aggregation weights (unit conversion is something separate)
		Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters, expected_unit, connection };
		res_data.restrictive_lookups = true;
		res_data.source_compartment = loc1.first();
		res_data.target_compartment = to_compartment;
		
		auto fun = resolve_function_tree(agg.code, &res_data);
		
		if(!match_exact(&fun.unit, &expected_unit)) {
			agg.code->source_loc.print_error_header();
			fatal_error("Expected the unit an aggregation_weight expression to resolve to ", expected_unit.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ".");
		}
		
		auto agg_weight = make_cast(fun.fun, Value_Type::real);
		
		Specific_Var_Location loc = loc1; // sigh
		check_valid_distribution_of_dependencies(app, agg_weight, loc);
		
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
	
	auto first = app->vars[app->vars.id_of(loc1)];
	auto second = app->vars[app->vars.id_of(loc2)];
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
	res_data.restrictive_lookups = true;
	res_data.source_compartment = loc1.first();
	res_data.target_compartment = loc2.first();
	
	auto fun = resolve_function_tree(ast, &res_data);
	unit_conv = make_cast(fun.fun, Value_Type::real);
	
	double conversion_factor;
	if(!match(&fun.unit, &expected_unit.standard_form, &conversion_factor)) {
		
		ast->source_loc.print_error_header();
		fatal_error("Expected the unit of this unit_conversion expression to resolve to a scalar multiple of ", expected_unit.standard_form.to_utf8(), " (standard form), but got, ", fun.unit.to_utf8(), ". It should convert from ", first->unit.standard_form.to_utf8(), " to ", second->unit.standard_form.to_utf8(), ". The error happened when finding unit conversions for the flux \"", app->vars[flux_id]->name, "\".");
	}
	if(conversion_factor != 1.0)
		unit_conv = make_binop('*', unit_conv, make_literal(conversion_factor));
	
	Specific_Var_Location loc = loc1;
	check_valid_distribution_of_dependencies(app, unit_conv, loc); 
	
	return unit_conv;
}

void
register_connection_agg(Model_Application *app, bool is_source, Var_Id target_var_id, Entity_Id target_comp, Entity_Id conn_id, char *varname) {
	
	auto model = app->model;
	auto connection = model->connections[conn_id];
	
	auto var = as<State_Var::Type::declared>(app->vars[target_var_id]);
	
	// See if we have a connection aggregate for this variable and connection already.
	auto &aggs = is_source ? var->conn_source_aggs : var->conn_target_aggs;
	for(auto existing_agg : aggs) {
		if(as<State_Var::Type::connection_aggregate>(app->vars[existing_agg])->connection == conn_id)
			return;
	}
	
	if(is_source)
		sprintf(varname, "in_flux_connection_source(%s, %s)", connection->name.data(), app->vars[target_var_id]->name.data());
	else
		sprintf(varname, "in_flux_connection_target(%s, %s)", connection->name.data(), app->vars[target_var_id]->name.data());
	
	Var_Id agg_id = register_state_variable<State_Var::Type::connection_aggregate>(app, invalid_entity_id, false, varname);
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	agg_var->agg_for = target_var_id;
	agg_var->is_source = is_source;
	agg_var->connection = conn_id;
	
	var = as<State_Var::Type::declared>(app->vars[target_var_id]);
	
	agg_var->unit = divide(var->unit, app->time_step_unit);
	if(is_source) {
		var->conn_source_aggs.push_back(agg_id);
	} else {
		var->conn_target_aggs.push_back(agg_id);
		
		// NOTE: aggregation weights only supported for compartments for now..
		if(model->components[target_comp]->decl_type != Decl_Type::compartment) return;
		
		// Get aggregation weights if relevant
		// TODO: Won't this crash if there is an all_to_all flux that is over a quantity not a
		// compartment and it finds a weight?
		auto find = app->find_connection_component(conn_id, target_comp);
		for(auto source_id : find->possible_sources) {
			Var_Location loc2 = app->vars[target_var_id]->loc1;
			Var_Location loc = loc2;
			loc.components[0] = source_id;
			Conversion_Data data;
			data.source_id = app->vars.id_of(loc);
			data.weight = owns_code(get_aggregation_weight(app, loc, target_comp, conn_id));
			data.unit_conv = owns_code(get_unit_conversion(app, loc, loc2, target_var_id));
			if(data.weight || data.unit_conv) agg_var->conversion_data.push_back(std::move(data));
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
	
	// TODO: check if there are unused aggregation data or unit conversion data!
	
	Var_Map in_flux_map;
	Var_Map2 needs_aggregate;
	
	for(auto var_id : app->vars.all_state_vars()) {
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
		
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);
		
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		Math_Expr_AST *override_ast = nullptr;
		Math_Expr_AST *specific_ast = nullptr;
		bool override_is_conc = false;
		bool initial_is_conc = false;
		bool init_is_override = false;
		
		//TODO: it would probably be better to default in_loc to be loc1 regardless (except when loc1 is not located).
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
			connection = flux_decl->source.connection_id;
			if(!is_valid(connection))
				connection = flux_decl->target.connection_id;
			
			bool target_is_located = is_located(var->loc2);
			if(is_located(var->loc1)) {
				from_compartment = var->loc1.first();
				//if(!target_is_located || var->loc1 == var->loc2)    // Hmm, it seems to make more sense to always let the source be the context if it is located.
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
			
			if(var2->decl_type == Decl_Type::quantity && ast) {
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
		res_data.source_compartment = in_loc.first();
		
		if(ast) {
			auto res = resolve_function_tree(ast, &res_data);
			auto fun = res.fun;
			fun = make_cast(fun, Value_Type::real);
			find_identifier_flags(fun, in_flux_map, needs_aggregate, var_id, from_compartment);
			
			if(!match_exact(&res.unit, &res_data.expected_unit)) {
				ast->source_loc.print_error_header();
				fatal_error("Expected the unit of this expression to resolve to ", res_data.expected_unit.to_utf8(), " (standard form), but got, ", res.unit.to_utf8(), ".");
			}
			
			var2->function_tree = owns_code(fun);
		}
		
		if(is_valid(var2->special_computation)) {
			auto special = model->special_computations[var2->special_computation];
			if(var2->function_tree) {
				special->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The variable was assigned a special computation, but it also has a regular equation assigned to it.");
			}
			auto res_data2 = res_data;
			res_data2.scope = model->get_scope(special->scope_id);
			auto res = resolve_function_tree(special->code, &res_data2);
			
			// TODO: This could be separated out in its own function
			auto special_comp = new Special_Computation_FT();
			special_comp->target = var_id;
			special_comp->function_name = special->function_name;
			for(auto arg : res.fun->exprs) {
				if(arg->expr_type != Math_Expr_Type::identifier)
					fatal_error(Mobius_Error::internal, "Got a '", name(arg->expr_type), "' expression in the body of a special_computation.");
				auto ident = static_cast<Identifier_FT *>(arg);
				if(ident->variable_type != Variable_Type::state_var && ident->variable_type != Variable_Type::parameter) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("We only support state variables and parameters as the arguments to a 'special_computation'.");
				}
				special_comp->arguments.push_back(*ident);
			}
			delete res.fun;
			var2->function_tree = owns_code(special_comp);
		}
		
		res_data.scope = other_code_scope;
		if(init_ast) {
			if(initial_is_conc)
				res_data.expected_unit = app->vars[var2->conc]->unit.standard_form;
			
			res_data.allow_in_flux = false;
			auto fun = resolve_function_tree(init_ast, &res_data);
			var2->initial_function_tree = owns_code(make_cast(fun.fun, Value_Type::real));
			remove_lasts(var2->initial_function_tree.get(), !init_is_override); // Only make an error for occurrences of 'last' if the block came from an @initial not an @override
			find_identifier_flags(var2->initial_function_tree.get(), in_flux_map, needs_aggregate, var_id, from_compartment);
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
				res_data.expected_unit = app->vars[var2->conc]->unit.standard_form;
			else
				res_data.expected_unit = var2->unit.standard_form; //In case it was overwritten above..
			
			res_data.allow_no_override = true;
			res_data.allow_in_flux = true;
			auto fun = resolve_function_tree(override_ast, &res_data);
			// NOTE: It is not that clean to do this here, but we need to know if the expression resolves to 'no_override'
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
				var2->override_tree = owns_code(make_cast(override_tree, Value_Type::real));
				var2->override_is_conc = override_is_conc;
				find_identifier_flags(var2->override_tree.get(), in_flux_map, needs_aggregate, var_id, from_compartment);
			}
		} else
			var2->override_tree = nullptr;
		
		if(specific_ast) {
			res_data.allow_no_override = false;
			res_data.allow_in_flux = false; // Do we??
			res_data.expected_unit = {};
			res_data.allow_no_override = false;
			auto fun = resolve_function_tree(specific_ast, &res_data);
			var2->specific_target = owns_code(make_cast(fun.fun, Value_Type::integer));
			if(!match_exact(&fun.unit, &res_data.expected_unit)) {
				init_ast->source_loc.print_error_header();
				fatal_error("Expected the unit of this expression to resolve to dimensionless, but got, ", fun.unit.to_utf8(), ".");
			}
			// TODO: We have to do more of the flag checking business here!
		} else
			var2->specific_target = nullptr;
	}
	
	for(auto flux_id : app->vars.all_fluxes()) {
		// We have to copy "specific target" to all dissolved child fluxes. We could not have done that before since the code was only just resolved above.
		auto flux = app->vars[flux_id];
		auto restriction = restriction_of_flux(flux);
		if(restriction.restriction != Var_Loc_Restriction::specific || flux->type != State_Var::Type::dissolved_flux) continue;
		auto orig_flux = flux;
		while(orig_flux->type == State_Var::Type::dissolved_flux)
			orig_flux = app->vars[as<State_Var::Type::dissolved_flux>(orig_flux)->flux_of_medium];
		if(!orig_flux->specific_target)
			fatal_error(Mobius_Error::internal, "Somehow we got a specific restriction on a flux without specific target code.");
		flux->specific_target = owns_code(copy(orig_flux->specific_target.get()));
	}
	
	// Invalidate fluxes if both source and target is 'out' or overridden.
	{
		std::map<Var_Id, bool> could_be_invalidated;
		for(auto var_id : app->vars.all_fluxes()) {
			auto var = app->vars[var_id];
			
			bool valid_source = false;
			if(is_located(var->loc1)) {
				auto source = as<State_Var::Type::declared>(app->vars[app->vars.id_of(var->loc1)]);
				if(!source->override_tree)
					valid_source = true;
			}
			bool valid_target = false;
			if(is_located(var->loc2)) {
				auto target = as<State_Var::Type::declared>(app->vars[app->vars.id_of(var->loc2)]);
				if(!target->override_tree)
					valid_target = true;
			}
			if(is_valid(var->loc2.connection_id))
				valid_target = true;
			bool invalidate = !valid_source && !valid_target;
			could_be_invalidated[var_id] = invalidate;
			if(invalidate) continue;
			
			// NOTE: If this flux could not be invalidated, neither could any of its parents since this one relies on them.
			auto above_id = var_id;
			auto above_var = var;
			while(above_var->type == State_Var::Type::dissolved_flux) {
				above_id = as<State_Var::Type::dissolved_flux>(above_var)->flux_of_medium;
				could_be_invalidated[above_id] = false;
				above_var = app->vars[above_id];
			}
		}
		for(auto &pair : could_be_invalidated) {
			if(!pair.second) continue;
			auto var = app->vars[pair.first];
			var->set_flag(State_Var::invalid);
			log_print("Invalidating \"", var->name, "\" due to both source or target being 'out' or overridden.\n");
		}
	}
	
	
	// TODO: Is this necessary, or could we just do this directly when we build the connections below ??
	// 	May have to do with aggregates of fluxes having that decl_type, and that is confusing? But they should not have connections any way.
	std::set<std::pair<Entity_Id, Var_Id>> may_need_connection_target;
	for(auto var_id : app->vars.all_fluxes()) {
		auto var = app->vars[var_id];
		
		auto &restriction = restriction_of_flux(var);
		if(is_valid(restriction.connection_id)) {
			Var_Location loc = var->loc1;
			if(restriction.restriction == Var_Loc_Restriction::top || restriction.restriction == Var_Loc_Restriction::specific)
				loc = var->loc2; // NOTE: For top and specific the relevant location is the target.

			if(is_located(loc)) {
				Var_Id source_id = app->vars.id_of(loc);
				may_need_connection_target.insert({restriction.connection_id, source_id});
			}
		}
	}
	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	// TODO: interaction between in_flux and aggregate declarations (i.e. we have something like an explicit aggregate(in_flux(soil.water)) in the code.
	// Or what about last(aggregate(in_flux(bla))) ?
	
	// Note: We always generate an aggregate if the source compartment of a flux has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the weight is trivial
	for(auto var_id : app->vars.all_fluxes()) {
		auto var = app->vars[var_id];

		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		
		Entity_Id exclude_index_set_from_loc = avoid_index_set_dependency(app, var->loc1);
		
		if(!location_indexes_below_location(app, var->loc1, var->loc2, Specific_Var_Location(), exclude_index_set_from_loc)) {
			// NOTE: This could happen if it is a located connection like "water[vert.top]". TODO: Make it possible to support this.
			if(is_valid(var->loc2.connection_id)) {
				auto &loc = model->fluxes[as<State_Var::Type::declared>(var)->decl_id]->source_loc;
				loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This flux would need a regular aggregate, but that is not currently supported for fluxes with a bracketed target.");
			}
			
			needs_aggregate[var_id.id].first.insert(var->loc2.first());
		}
	}
	
		
	for(auto &need_agg : needs_aggregate) {
		auto var_id = Var_Id {Var_Id::Type::state_var, need_agg.first};
		auto var = app->vars[var_id];
		if(!var->is_valid()) continue;
		
		auto loc1 = var->loc1;
		if(var->type == State_Var::Type::dissolved_conc) {
			auto var2 = as<State_Var::Type::dissolved_conc>(var);
			loc1 = app->vars[var2->conc_of]->loc1;
		}
		
		auto source = model->components[loc1.first()];
		
		for(auto to_compartment : need_agg.second.first) {
			
			Math_Expr_FT *agg_weight = get_aggregation_weight(app, loc1, to_compartment);
			var->set_flag(State_Var::has_aggregate);
			
			//TODO: We also have to handle the case where the agg. variable was a series!
			// note: can't reference "var" below this (without looking it up again). The vector it resides in may have reallocated.
			
			sprintf(varname, "aggregate(%s, %s)", var->name.data(), model->components[to_compartment]->name.data());
			Var_Id agg_id = register_state_variable<State_Var::Type::regular_aggregate>(app, invalid_entity_id, false, varname);
			
			auto agg_var = as<State_Var::Type::regular_aggregate>(app->vars[agg_id]);
			agg_var->agg_of = var_id;
			agg_var->aggregation_weight_tree = owns_code(agg_weight);
			if(var->is_flux())
				agg_var->set_flag(State_Var::flux);
			
			// TODO: Set the unit of the agg_var (only relevant if it is displayed somewhere, it is not function critical).
			//   It is a bit tricky because of potential unit conversions and the fact that the unit of aggregated fluxes could be re-scaled to the time step.
			
			var = app->vars[var_id];   //NOTE: had to look it up again since we may have resized the vector var pointed into

			// NOTE: it makes a lot of the operations in model_compilation more natural if we decouple the fluxes like this:
			agg_var->loc1.type = Var_Location::Type::out;
			agg_var->loc2 = var->loc2;
			var->loc2.type = Var_Location::Type::out;
			
			// NOTE: it is easier to keep track of who is supposed to use the unit conversion if we only keep a reference to it on the one that is going to use it.
			//   we only multiply with the unit conversion at the point where we add the flux to the target, so it is only needed on the aggregate.
			agg_var->unit_conversion_tree = std::move(var->unit_conversion_tree);
			
			agg_var->agg_to_compartment = to_compartment;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = as<State_Var::Type::declared>(app->vars[looked_up_by]);
				
				Entity_Id lu_compartment = lu->loc1.first();
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.first();
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the Var_Map2
				
				if(lu->function_tree)
					replace_flagged(lu->function_tree.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->override_tree)
					replace_flagged(lu->override_tree.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
				if(lu->initial_function_tree)
					replace_flagged(lu->initial_function_tree.get(), var_id, agg_id, Identifier_FT::Flags::aggregate);
			}
		}
	}
	
	// TODO: Should we give an error if there is a connection flux on an overridden variable?
	
	for(auto &pair : may_need_connection_target) {
		
		auto &conn_id = pair.first;
		auto &source_id  = pair.second;
		
		auto connection = model->connections[conn_id];
		
		if(connection->type == Connection_Type::directed_tree || connection->type == Connection_Type::directed_graph) {
			
			if(connection->type == Connection_Type::directed_graph) {
				auto find_source = app->find_connection_component(conn_id, app->vars[source_id]->loc1.components[0], false);
				if(find_source && find_source->total_as_source > 0)
					register_connection_agg(app, true, source_id, invalid_entity_id, conn_id, &varname[0]); // Aggregation variable for outgoing fluxes on the connection
			}
			
			for(auto &target_comp : app->connection_components[conn_id].components) {
				
				// If it wasn't actually put as a possible target in the data set, don't bother with it.
				if(target_comp.possible_sources.empty()) continue;

				auto target_loc = app->vars[source_id]->loc1;
				target_loc.components[0] = target_comp.id;
				auto target_id = app->vars.id_of(target_loc);
				if(!is_valid(target_id))   // NOTE: the target may not have that state variable. This can especially happen for dissolvedes.
					continue;
				
				register_connection_agg(app, false, target_id, target_comp.id, conn_id, &varname[0]); // Aggregation variable for incoming fluxes.
			}
		} else if (connection->type == Connection_Type::all_to_all || connection->type == Connection_Type::grid1d) {
			if(connection->components.size() != 1 || app->connection_components[conn_id].components.size() != 1)
				fatal_error(Mobius_Error::internal, "Expected exactly one compartment for this type of connection."); // Should have been detected earlier
			
			// NOTE: all_to_all and grid1d connections can (currently) only go from one state variable to another
			// instance of itself ( the source_id ).
			auto target_comp = app->connection_components[conn_id].components[0].id;
			if(connection->type == Connection_Type::all_to_all)
				register_connection_agg(app, true, source_id, target_comp, conn_id, &varname[0]);
			register_connection_agg(app, false, source_id, target_comp, conn_id, &varname[0]);
			
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection type in compose_and_resolve()");
		}
	}
	
	for(auto &in_flux : in_flux_map) {
		
		auto &key = in_flux.first;
		Var_Id target_id = key.first;
		auto target = as<State_Var::Type::declared>(app->vars[target_id]);
		Entity_Id connection = key.second;
		
		Var_Id in_flux_id = invalid_var;
		if(!is_valid(connection)) {
			sprintf(varname, "in_flux(%s)", target->name.data());
			in_flux_id = register_state_variable<State_Var::Type::in_flux_aggregate>(app, invalid_entity_id, false, varname);
			auto in_flux_var = as<State_Var::Type::in_flux_aggregate>(app->vars[in_flux_id]);
			in_flux_var->in_flux_to = target_id;
			
			app->vars[in_flux_id]->unit = divide(target->unit, app->time_step_unit); //NOTE: In codegen, the components in the sum are rescaled to this unit.
		} else {
			//We don't have to register an aggregate for the connection since that will always have been done for a variable on a connection if it is at all relevant.
			for(auto conn_agg_id : target->conn_target_aggs) {
				if( as<State_Var::Type::connection_aggregate>(app->vars[conn_agg_id])->connection == connection)
					in_flux_id = conn_agg_id;
			}
		}
		
		// NOTE: If in_flux_id is invalid at this point (since the referenced connection aggregate did not exist), replace_flagged will put a 0.0 in place of this value.
		//   TODO: Maybe print a warning?
		
		// TODO: What happens if we use in_flux in initial code or override code??
		for(auto rep_id : in_flux.second) {
			auto var = as<State_Var::Type::declared>(app->vars[rep_id]);
			replace_flagged(var->function_tree.get(), target_id, in_flux_id, Identifier_FT::Flags::in_flux, connection);
		}
	}
	
	
	// See if the code for computing a variable looks up other values that are distributed over index sets that the var is not distributed over.
	// NOTE: This must be done after all calls to replace_flagged, otherwise the references are not correctly in place.
	for(auto var_id : app->vars.all_state_vars()) {
		auto var = app->vars[var_id];
		
		if(var->type != State_Var::Type::declared) continue;
		auto var2 = as<State_Var::Type::declared>(var);
		
		Specific_Var_Location &in_loc = (is_located(var->loc1) ? var->loc1 : var->loc2);
		
		if(var2->function_tree)
			check_valid_distribution_of_dependencies(app, var2->function_tree.get(), in_loc, var->loc2);
		if(var2->initial_function_tree)
			check_valid_distribution_of_dependencies(app, var2->initial_function_tree.get(), in_loc);
		if(var2->override_tree)
			check_valid_distribution_of_dependencies(app, var2->override_tree.get(), in_loc);
	}
	
	for(auto var_id : app->vars.all_state_vars()) {
		auto var = app->vars[var_id];
		app->serial_to_id[app->serialize(var_id)] = var_id;
	}
	for(auto var_id : app->vars.all_series()) {
		auto var = app->vars[var_id];
		app->serial_to_id[app->serialize(var_id)] = var_id;
	}
	
	for(auto module_id : model->modules) {
		auto module = model->modules[module_id];
		for(auto &pair : module->scope.visible_entities) {
			auto &reg = pair.second;
			
			if(reg.is_load_arg && !reg.was_referenced) {
				log_print("Warning: In ");
				reg.source_loc.print_log_header();
				log_print("The module argument '", reg.handle, "' was never referenced.\n");
			}
		}
	}
}




