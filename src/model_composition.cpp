

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
//#include <bitset>

void
prelim_compose(Model_Application *app);

Model_Application::Model_Application(Mobius_Model *model) : 
	model(model), parameter_structure(this), series_structure(this), result_structure(this), connection_structure(this), 
	additional_series_structure(this), data_set(nullptr), data(this), llvm_data(nullptr) {
	
	index_counts.resize(model->index_sets.count());
	index_names_map.resize(model->index_sets.count());
	index_names.resize(model->index_sets.count());
	
	for(auto index_set : model->index_sets) {
		index_counts[index_set.id].index_set = index_set;
		index_counts[index_set.id].index = 0;
	}
	
	// TODO: make time step size configurable.
	time_step_size.unit      = Time_Step_Size::second;
	time_step_size.magnitude = 86400;
	
	initialize_llvm();
	llvm_data = create_llvm_module();
	
	prelim_compose(this);
}

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

Var_Id
register_state_variable(Model_Application *app, Decl_Type type, Entity_Id id, bool is_series, const std::string &given_name = "") {
	
	State_Variable var = {};
	var.type           = type;
	var.entity_id      = id;
	
	auto model = app->model;
	
	Var_Location loc = invalid_var_location;
	
	if(is_valid(id)) {
		if(type == Decl_Type::has) {
			auto has = model->hases[id];
			loc = has->var_location;
			var.loc1 = loc;
			check_if_var_loc_is_well_formed(model, var.loc1, has->source_loc);
			var.type = model->components[loc.last()]->decl_type;
			if(is_valid(has->unit)) 
				var.unit = model->units[has->unit]->data;
		} else if (type == Decl_Type::flux) {
			auto flux = model->fluxes[id];
			var.loc1 = flux->source;
			var.loc2 = flux->target;
			// These may not be needed, since we would check if the locations exist in any case (and the source location is confusing as it stands here).
			// check_if_loc_is_well_formed(model, var.loc1, flux->source_loc);
			// check_if_loc_is_well_formed(model, var.loc2, flux->source_loc);
			if(is_valid(flux->connection_target)) {
				if(!is_located(var.loc1)) {
					flux->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("You can't have a flux from nowhere to a connection.\n");
				}
				var.connection = flux->connection_target;
				var.loc2 = var.loc1;
			}
			// TODO: the flux unit should always be (unit of what is transported) / (time step unit)
		} else
			fatal_error(Mobius_Error::internal, "Unhandled type in register_state_variable().");
	} else if (type == Decl_Type::has)
		var.type = Decl_Type::property; // TODO: should be a separate type, or it could cause difficulty later.
	
	auto name = given_name;
	if(name.empty() && is_valid(id)) {
		if(type == Decl_Type::flux)
			name = model->fluxes[id]->name;
		else
			name = model->hases[id]->var_name;
	} 
	if(name.empty() && type == Decl_Type::has) //TODO: this is a pretty poor stopgap. We could generate "Compartmentname Quantityname" or something like that instead.
		name = model->find_entity(loc.last())->name;
		
	var.name = name;
	if(var.name.empty())
		fatal_error(Mobius_Error::internal, "Variable was somehow registered without a name.");
	
	//warning_print("**** register ", var.name, is_series ? " as series" : " as state var", "\n");
	
	if(is_series)
		return app->series.register_var(var, loc);
	else
		return app->state_vars.register_var(var, loc);
	
	return invalid_var;
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
	
	Var_Id var_id = app->state_vars[loc];
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
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var && (ident->flags & ident_flags_last_result)) {
			if(make_error) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
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
				expr->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Did not expect an in_flux() in an initial value function.");
			}
			in_fluxes[ident->state_var.id].push_back(looked_up_by);
		}
		if(ident->flags & ident_flags_aggregate) {
			if(!is_valid(lookup_compartment)) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
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
replace_conc(Model_Application *app, Math_Expr_FT *expr) {
	for(auto arg : expr->exprs) replace_conc(app, arg);
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if((ident->variable_type == Variable_Type::state_var) && (ident->flags & ident_flags_conc)) {
			
			auto var = app->state_vars[ident->state_var];
			if(!is_valid(var->dissolved_conc)) {
				expr->source_loc.print_error_header(Mobius_Error::model_building);
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
			expr->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("The function body for a ", name(decl_type), " declaration is only allowed to look up parameters, no other types of state variables.");
		} else if (ident->variable_type == Variable_Type::parameter)
			parameter_refs.insert(ident->parameter);
	}
}

bool
location_indexes_below_location(Mobius_Model *model, Var_Location &loc, Var_Location &below_loc) {
	
	if(!is_located(loc) || !is_located(below_loc))
		fatal_error(Mobius_Error::internal, "Got a non-located location to a location_indexes_below_location() call.");
	
	for(int idx1 = 0; idx1 < loc.n_components; ++idx1) {
		for(auto index_set : model->components[loc.components[idx1]]->index_sets) {
			bool found = false;
			for(auto idx2 = 0; idx2 < below_loc.n_components; ++idx2) {
				for(auto index_set2 : model->components[below_loc.components[idx2]]->index_sets) {
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
	/*
	auto comp       = model->components[loc.first()]; //NOTE: right now we are only guaranteed that loc.first() is valid. See note in parameter_indexes_below_location.
	auto below_comp = model->components[below_loc.first()];
	
	for(auto index_set : comp->index_sets) {
		if(std::find(below_comp->index_sets.begin(), below_comp->index_sets.end(), index_set) == below_comp->index_sets.end())
			return false;
	}
	*/
	return true;
}

bool
parameter_indexes_below_location(Mobius_Model *model, Entity_Id par_id, Var_Location &below_loc) {
	auto par = model->parameters[par_id];
	auto par_comp_id = model->par_groups[par->par_group]->component;
	// TODO: invalid component should only happen for the "System" par group, and in that case we should probably not have referenced that parameter, but it is a bit out of scope for this function to handle it.
	if(!is_valid(par_comp_id)) return false;
	
	// NOTE: This is a bit of a hack that allows us to reuse location_indexes_below_location. We have to monitor that it doesn't break.
	Var_Location loc;
	loc.type = Var_Location::Type::located;
	loc.n_components = 1;
	loc.components[0] = par_comp_id;
	
	return location_indexes_below_location(model, loc, below_loc);
}

void
check_valid_distribution_of_dependencies(Model_Application *app, Math_Expr_FT *function, State_Variable *var, bool initial) {
	Dependency_Set code_depends;
	register_dependencies(function, &code_depends);
	
	// NOTE: We should not have undergone codegen yet, so the source location of the top node of the function should be valid.
	Source_Location source_loc = function->source_loc;
	
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
	
	// TODO: in this error messages we should really print out the two tuples of index sets.
	for(auto par_id : code_depends.on_parameter) {
		if(!parameter_indexes_below_location(app->model, par_id, loc)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the parameter \"", app->model->parameters[par_id]->name, "\". This parameter belongs to a component that is distributed over a higher number of index sets than the the state variable.");
		}
	}
	for(auto series_id : code_depends.on_series) {
		if(!location_indexes_below_location(app->model, app->series[series_id]->loc1, loc)) {
			source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error(Mobius_Error::model_building, err_begin, var->name, "\" looks up the input series \"", app->series[series_id]->name, "\". This series has a location that is distributed over a higher number of index sets than the state variable.");
		}
	}
	
	for(auto dep : code_depends.on_state_var) {
		auto dep_var = app->state_vars[dep.var_id];
		
		// For generated in_flux aggregation variables we are instead interested in the variable that is the target of the fluxes.
		if(dep_var->flags & State_Variable::Flags::f_in_flux)
			dep_var = app->state_vars[dep_var->in_flux_target];
		
		// If it is an aggregate, index set dependencies will be generated to be correct.
		if(dep_var->flags & State_Variable::Flags::f_is_aggregate)
			continue;
		
		// If it is a conc, check vs the mass instead.
		if(dep_var->flags & State_Variable::Flags::f_dissolved_conc)
			dep_var = app->state_vars[dep_var->dissolved_conc];
		
		if(dep_var->type == Decl_Type::flux || !is_located(dep_var->loc1))
			fatal_error(Mobius_Error::internal, "Somehow a direct lookup of a flux or unlocated variable \"", dep_var->name, "\" in code tested with check_valid_distribution_of_dependencies().");
		
		if(!location_indexes_below_location(app->model, dep_var->loc1, loc)) {
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
prelim_compose(Model_Application *app) {
	warning_print("Compose begin\n");
	
	auto model = app->model;
	
	for(auto conn_id : model->connections) {
		auto conn = model->connections[conn_id];
		if(conn->type == Connection_Type::unrecognized) {
			conn->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("This connection never received a type. Declare it as connection(name, type) somewhere.");
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
				Var_Location above_loc = remove_dissolved(has->var_location);
				
				auto above_var = app->state_vars[above_loc];
				if(!is_valid(above_var)) {
					has->source_loc.print_error_header(Mobius_Error::model_building);
					//error_print_location(this, above_loc);
					fatal_error("The located quantity that this 'has' declaration is assigned to has itself not been created using a 'has' declaration.");
				}
			}
			
			Decl_Type type = model->find_entity(has->var_location.last())->decl_type;
			auto find = has_location.find(has->var_location);
			if(find == has_location.end()) {
				// No declaration provided code for this series, so it is an input series.
				
				// If was already registered by another module, we don't need to re-register it.
				// TODO: still need to check for conflicts (unit, name) (ideally bake this check into the check where we build the has_location data.
				if(is_valid(app->series[has->var_location])) continue; 
				
				if(has->var_location.is_dissolved()) {
					has->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("For now we don't support input series with chained locations.");
				}
				register_state_variable(app, Decl_Type::has, id, true);
			} else if (id == find->second) {
				// This is the particular has declaration that provided the code, so we register a state variable using this one.
				Var_Id var_id = register_state_variable(app, Decl_Type::has, id, false);
				if(has->var_location.is_dissolved() && type == Decl_Type::quantity)
					dissolvedes.push_back(var_id);
			}
		}
	}
	
	for(Entity_Id id : model->fluxes) {
		auto flux = model->fluxes[id];

		auto scope = model->get_scope(flux->code_scope);
		check_flux_location(app, scope, flux->source_loc, flux->source);
		check_flux_location(app, scope, flux->source_loc, flux->target); //TODO: The scope may not be correct if the flux was redirected!!!
		
		register_state_variable(app, Decl_Type::flux, id, false);
	}
	
	warning_print("Generate fluxes and concentrations for dissolved quantities.\n");
	
	//TODO: make better name generation system!
	char varname[1024];
	
	//NOTE: not that clean to have this part here, but it is just much easier if it is done before function resolution.
	for(auto var_id : dissolvedes) {
		auto var = app->state_vars[var_id];
		
		auto above_loc = remove_dissolved(var->loc1);
		std::vector<Var_Id> generate;
		for(auto flux_id : app->state_vars) {
			auto flux = app->state_vars[flux_id];
			if(flux->type != Decl_Type::flux) continue;
			if(!(flux->loc1 == above_loc)) continue;
			if(is_located(flux->loc2)) {
				// If the target of the flux is a specific location, we can only send the dissolved quantity if it exists in that location.
				auto below_loc = add_dissolved(flux->loc2, var->loc1.last());
				auto below_var = app->state_vars[below_loc];
				if(!is_valid(below_var)) continue;
			}
			// See if it was declared that this flux should not carry this quantity (using a no_carry declaration)
			if(is_valid(flux->entity_id)) { // If this flux was itself generated, it won't have a valid entity id.
				auto flux_reg = model->fluxes[flux->entity_id];
				if(flux_reg->no_carry_by_default) continue;
				if(std::find(flux_reg->no_carry.begin(), flux_reg->no_carry.end(), var->loc1) != flux_reg->no_carry.end()) continue;
			}
			
			// TODO: need system to exclude some fluxes for this quantity (e.g. evapotranspiration should not carry DOC).
			generate.push_back(flux_id);
		}
		
		Var_Location source  = var->loc1; // Note we have to copy it since we start registering new state variables, invalidating our pointer to var.
		std::string var_name = var->name; // Same: Have to copy, not take reference.
		auto dissolved_in_id = app->state_vars[above_loc];
		auto dissolved_in = app->state_vars[dissolved_in_id];
		
		sprintf(varname, "concentration(%s, %s)", var_name.data(), dissolved_in->name.data());
		Var_Id gen_conc_id = register_state_variable(app, Decl_Type::has, invalid_entity_id, false, varname);
		auto conc_var = app->state_vars[gen_conc_id];
		conc_var->flags = State_Variable::Flags::f_dissolved_conc;
		conc_var->dissolved_conc = var_id;
		app->state_vars[var_id]->dissolved_conc = gen_conc_id;
		
		for(auto flux_id : generate) {
			std::string &flux_name = app->state_vars[flux_id]->name;
			sprintf(varname, "dissolved_flux(%s, %s)", var_name.data(), flux_name.data());
			Var_Id gen_flux_id = register_state_variable(app, Decl_Type::flux, invalid_entity_id, false, varname);
			auto gen_flux = app->state_vars[gen_flux_id];
			auto flux = app->state_vars[flux_id];
			gen_flux->flags = State_Variable::Flags::f_dissolved_flux;
			gen_flux->dissolved_flux = flux_id;
			gen_flux->dissolved_conc = gen_conc_id;
			gen_flux->loc1 = source;
			gen_flux->loc2.type = flux->loc2.type;
			if(is_located(flux->loc2))
				gen_flux->loc2 = add_dissolved(flux->loc2, source.last());
			if(is_valid(flux->connection))
				gen_flux->connection = flux->connection;
		}
	}
}

void
register_connection_agg(Model_Application *app, bool is_source, Var_Id var_id, Entity_Id conn_id, char *varname) {
	
	auto connection = app->model->connections[conn_id];
	
	auto existing_agg = is_source ? app->state_vars[var_id]->connection_source_agg : app->state_vars[var_id]->connection_target_agg;
	if(is_valid(existing_agg)) {
		if(app->state_vars[existing_agg]->connection != conn_id)
			fatal_error(Mobius_Error::internal, "Currently we only support one connection targeting or being sourced in the same state variable.");
		return;
	}
	
	if(is_source)
		sprintf(varname, "in_flux_connection_source(%s, %s)", connection->name.data(), app->state_vars[var_id]->name.data());
	else
		sprintf(varname, "in_flux_connection_target(%s, %s)", connection->name.data(), app->state_vars[var_id]->name.data());
	
	Var_Id agg_id = register_state_variable(app, Decl_Type::has, invalid_entity_id, false, varname);
	auto agg_var = app->state_vars[agg_id];
	agg_var->flags = State_Variable::Flags::f_connection_agg;
	if(is_source) {
		app->state_vars[var_id]->connection_source_agg = agg_id;
		agg_var->connection_source_agg = var_id;
	} else {
		app->state_vars[var_id]->connection_target_agg = agg_id;
		agg_var->connection_target_agg = var_id;
	}
	agg_var->connection = conn_id;
}



void
compose_and_resolve(Model_Application *app) {
	
	auto model = app->model;
	
	for(auto par_id : model->parameters) {
		auto par = model->parameters[par_id];
		if(par->decl_type == Decl_Type::par_bool || par->decl_type == Decl_Type::par_enum) {
			s64 count = app->parameter_structure.instance_count(par_id);
			if(app->parameter_structure.instance_count(par_id) == 1) {// TODO: Would like to get rid of this requirement, but without it we may end up needing a separate code tree per index tuple for a given state variable :(
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
	//Var_Map2 needs_aggregate_initial;
	
	std::set<std::pair<Entity_Id, Var_Id>> may_need_connection_target;
	
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		
		Math_Expr_AST *ast = nullptr;
		Math_Expr_AST *init_ast = nullptr;
		Math_Expr_AST *unit_conv_ast = nullptr;
		Math_Expr_AST *override_ast = nullptr;
		bool override_is_conc = false;
		bool initial_is_conc = false;
		
		//TODO: it would probably be better to default in_loc to be loc1 regardless (except when loc1 is not located).
		Var_Location in_loc;
		in_loc.type = Var_Location::Type::nowhere;
		Entity_Id from_compartment = invalid_entity_id;
		
		Decl_Scope *code_scope       = nullptr;
		Decl_Scope *other_code_scope = nullptr;
		Decl_Scope *unit_conv_scope  = nullptr;
		
		if(var->type == Decl_Type::flux) {
			bool target_was_out = false;
			if(is_valid(var->entity_id)) {
				auto flux_decl = model->fluxes[var->entity_id];
				target_was_out = flux_decl->target_was_out;
				ast = flux_decl->code;
				code_scope = model->get_scope(flux_decl->code_scope);
			}
			bool target_is_located = is_located(var->loc2) && !target_was_out; // Note: the target could have been re-directed by the model. In this setting we only care about how it was declared originally.
			if(is_located(var->loc1)) {
				from_compartment = var->loc1.first();
				//if(!target_is_located || var->loc1 == var->loc2)    // Hmm, it seems to make more sense to always let the source be the context if it is located.
				in_loc = var->loc1;
			} else if(target_is_located)
				in_loc = var->loc2;
			
			if(!is_valid(from_compartment)) from_compartment = in_loc.first();
			
			if(is_valid(var->connection)) {
				auto conn = model->connections[var->connection];
				Var_Id source_id = app->state_vars[var->loc1];
				may_need_connection_target.insert({var->connection, source_id});
			}
			
			if(is_located(var->loc1)) {
				for(auto &unit_conv : model->components[var->loc1.first()]->unit_convs) {
					if(var->loc1 == unit_conv.source && var->loc2 == unit_conv.target) {
						unit_conv_ast   = unit_conv.code;
						unit_conv_scope = model->get_scope(unit_conv.code_scope);
					}
				}
			}
		} else if((var->type == Decl_Type::property || var->type == Decl_Type::quantity) && is_valid(var->entity_id)) {
			auto has = model->hases[var->entity_id];
			ast      = has->code;
			init_ast = has->initial_code;
			override_ast = has->override_code;
			override_is_conc = has->override_is_conc;
			initial_is_conc  = has->initial_is_conc;
			code_scope = model->get_scope(has->code_scope);
			other_code_scope = code_scope;
			
			if(!ast) {
				auto comp = model->components[has->var_location.last()];
				ast = comp->default_code;
				if(ast)
					code_scope = model->get_scope(comp->code_scope);
			}
			
			if(override_ast && (var->type != Decl_Type::quantity || (override_is_conc && !var->loc1.is_dissolved()))) {
				override_ast->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Either got an '.override' block on a property or a '.override_conc' block on a non-dissolved variable.");
			}
			in_loc = has->var_location;
			from_compartment = in_loc.first();
		}
		
		// TODO: instead of passing the in_compartment, we could just pass the var_id and give the function resolution more to work with.
		Function_Resolve_Data res_data = { app, code_scope, in_loc, &app->baked_parameters };
		if(ast) {
			var->function_tree = make_cast(resolve_function_tree(ast, &res_data), Value_Type::real);
			replace_conc(app, var->function_tree); // Replace explicit conc() calls by pointing them to the conc variable.
			find_other_flags(var->function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, false);
		} else
			var->function_tree = nullptr; // NOTE: this is for substances. They are computed a different way.
		
		res_data.scope = other_code_scope;
		if(init_ast) {
			//TODO: handle initial_is_conc!
			var->initial_function_tree = make_cast(resolve_function_tree(init_ast, &res_data), Value_Type::real);
			remove_lasts(var->initial_function_tree, true);
			replace_conc(app, var->initial_function_tree);  // Replace explicit conc() calls by pointing them to the conc variable
			find_other_flags(var->initial_function_tree, in_flux_map, needs_aggregate, var_id, from_compartment, true);
			var->initial_is_conc = initial_is_conc;
			
			if(initial_is_conc && (var->type != Decl_Type::quantity || !var->loc1.is_dissolved())) {
				init_ast->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Got an \"initial_conc\" block for a non-dissolved variable");
			}
		} else
			var->initial_function_tree = nullptr;
		
		if(unit_conv_ast) {
			auto res_data2 = res_data;
			res_data2.scope = unit_conv_scope;
			var->unit_conversion_tree = make_cast(resolve_function_tree(unit_conv_ast, &res_data2), Value_Type::real);
			std::set<Entity_Id> parameter_refs;
			restrictive_lookups(var->unit_conversion_tree, Decl_Type::unit_conversion, parameter_refs);

			for(auto par_id : parameter_refs) {
				bool ok = parameter_indexes_below_location(model, par_id, var->loc1);
				if(!ok) {
					unit_conv_ast->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("The parameter \"", (*unit_conv_scope)[par_id], "\" belongs to a compartment that is distributed over index sets that the source compartment of the unit conversion is not distributed over.");
				}
			}
		} else
			var->unit_conversion_tree = nullptr;
		
		if(override_ast) {
			auto override_tree = prune_tree(resolve_function_tree(override_ast, &res_data));
			bool no_override = false;
			if(override_tree->expr_type == Math_Expr_Type::identifier_chain) {
				auto ident = reinterpret_cast<Identifier_FT *>(override_tree);
				no_override = (ident->variable_type == Variable_Type::no_override);
			}
			if(no_override)
				var->override_tree = nullptr;
			else {
				var->override_tree = make_cast(override_tree, Value_Type::real);
				var->override_is_conc = override_is_conc;
				replace_conc(app, var->override_tree);
				find_other_flags(var->override_tree, in_flux_map, needs_aggregate, var_id, from_compartment, false);
				// TODO: Should audit this one for flags also!
			}
		} else
			var->override_tree = nullptr;
	}
	
	// Invalidate dissolved fluxes if both source and target is overridden.
	
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_dissolved_flux) {
			bool valid_source = true;
			if(is_located(var->loc1)) {
				auto source = app->state_vars[app->state_vars[var->loc1]];
				if(source->override_tree)
					valid_source = false;
			} else
				valid_source = false;
			bool valid_target = true;
			if(is_located(var->loc2)) {
				auto target = app->state_vars[app->state_vars[var->loc2]];
				if(target->override_tree)
					valid_target = false;
			} else
				valid_target = false;
			if(!valid_source && !valid_target)
				var->flags = (State_Variable::Flags)(var->flags | State_Variable::Flags::f_invalid);
		}
	}
	
	// TODO: We could check if any of the so-far declared aggregates are not going to be needed and should be thrown out(?)
	// TODO: interaction between in_flux and aggregate declarations (i.e. we have something like an explicit aggregate(in_flux(soil.water)) in the code.
	// Or what about last(aggregate(in_flux(bla))) ?
	
	// note: We always generate an aggregate if the source compartment of a flux has more indexes than the target compartment.
	//    TODO: We could have an optimization in the model app that removes it again in the case where the source variable is actually indexed with fewer and the weight is trivial
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_invalid) continue;
		if(var->type != Decl_Type::flux) continue;
		if(!is_located(var->loc1) || !is_located(var->loc2)) continue;
		
		if(!location_indexes_below_location(model, var->loc1, var->loc2))
			needs_aggregate[var_id.id].first.insert(var->loc2.first());
	}
	
	warning_print("Generate state vars for aggregates.\n");
		
	for(auto &need_agg : needs_aggregate) {
		auto var_id = Var_Id {Var_Id::Type::state_var, need_agg.first};
		auto var = app->state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_invalid) continue;
		
		auto loc1 = var->loc1;
		if(var->flags & State_Variable::Flags::f_dissolved_conc)
			loc1 = app->state_vars[var->dissolved_conc]->loc1;
		
		auto source = model->components[loc1.first()];
		
		for(auto to_compartment : need_agg.second.first) {
			
			Math_Expr_FT *agg_weight = nullptr;
			for(auto &agg : source->aggregations) {
				if(agg.to_compartment == to_compartment) {
					// Note: the module id is always 0 here since aggregation_weight should only be declared in model scope.
					auto scope = model->get_scope(agg.code_scope);
					Function_Resolve_Data res_data = { app, scope, {}, &app->baked_parameters };
					agg_weight = make_cast(resolve_function_tree(agg.code, &res_data), Value_Type::real);
					std::set<Entity_Id> parameter_refs;
					restrictive_lookups(agg_weight, Decl_Type::aggregation_weight, parameter_refs);
					
					for(auto par_id : parameter_refs) {
						bool ok = parameter_indexes_below_location(model, par_id, loc1);
						if(!ok) {
							agg.code->source_loc.print_error_header(Mobius_Error::model_building);
							fatal_error("The parameter \"", (*scope)[par_id], "\" belongs to a compartment that is distributed over index sets that the source compartment of the aggregation weight is not distributed over.");
						}
					}
					break;
				}
			}
			if(!agg_weight) {
				auto target = model->components[to_compartment];
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
			
			sprintf(varname, "aggregate(%s)", var->name.data());
			Var_Id agg_id = register_state_variable(app, var->type, invalid_entity_id, false, varname);
			
			auto agg_var = app->state_vars[agg_id];
			agg_var->flags = (State_Variable::Flags)(agg_var->flags | State_Variable::f_is_aggregate);
			agg_var->agg = var_id;
			agg_var->aggregation_weight_tree = agg_weight;
			
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
			app->state_vars[var_id]->agg = agg_id;
			
			for(auto looked_up_by : need_agg.second.second) {
				auto lu = app->state_vars[looked_up_by];
				
				Entity_Id lu_compartment = lu->loc1.first();
				if(!is_located(lu->loc1)) {
					lu_compartment = lu->loc2.first();
					if(!is_located(lu->loc2))
						fatal_error(Mobius_Error::internal, "We somehow allowed a non-located state variable to look up an aggregate.");
				}

				if(lu_compartment != to_compartment) continue;    //TODO: we could instead group these by the compartment in the Var_Map2
				
				if(lu->function_tree)
					replace_flagged(lu->function_tree, var_id, agg_id, ident_flags_aggregate);
				if(lu->override_tree)
					replace_flagged(lu->override_tree, var_id, agg_id, ident_flags_aggregate);
				if(lu->initial_function_tree)
					replace_flagged(lu->initial_function_tree, var_id, agg_id, ident_flags_aggregate);
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
		
			for(auto target_compartment : connection->components) {
				auto target_loc = app->state_vars[source_id]->loc1;
				target_loc.components[0] = target_compartment;
				auto target_id = app->state_vars[target_loc];
				if(!is_valid(target_id))   // NOTE: the target may not have that state variable. This can especially happen for dissolvedes.
					continue;
				auto *conn_comp = app->find_connection_component(conn_id, target_compartment, false);
				if(!conn_comp)
					continue;
				// TODO: Even if it is present in the connection data, it may not appear as a target, only as a source. Should also be checked eventually.
				
				register_connection_agg(app, false, target_id, conn_id, &varname[0]);
			}
		} else if (connection->type == Connection_Type::all_to_all) {
			if(connection->components.size() != 1)
				fatal_error(Mobius_Error::internal, "Expected exactly one compartment for all_to_all connection."); // Should have been detected earlier
			
			// NOTE: all_to_all connections can (currently) only go from one state variable to another
			// instance of itself ( the source_id ).
			register_connection_agg(app, true, source_id, conn_id, &varname[0]);
			register_connection_agg(app, false, source_id, conn_id, &varname[0]);
			
		} else {
			fatal_error(Mobius_Error::internal, "Unsupported connection type in compose_and_resolve()");
		}
	}
	
	warning_print("Generate state vars for in_flux.\n");
	for(auto &in_flux : in_flux_map) {
		Var_Id target_id = {Var_Id::Type::state_var, in_flux.first}; //TODO: Not good way to do it!
		
		Var_Id in_flux_id = register_state_variable(app, Decl_Type::has, invalid_entity_id, false, "in_flux");   //TODO: generate a better name
		auto in_flux_var = app->state_vars[in_flux_id];
		
		in_flux_var->in_flux_target        = target_id;
		in_flux_var->function_tree         = nullptr; // This is instead generated in model_compilation
		in_flux_var->initial_function_tree = nullptr;
		in_flux_var->flags = (State_Variable::Flags)(in_flux_var->flags | State_Variable::f_in_flux);
		
		for(auto rep_id : in_flux.second)
			replace_flagged(app->state_vars[rep_id]->function_tree, target_id, in_flux_id, ident_flags_in_flux);
	}

	// See if the code for computing a variable looks up other values that are distributed over index sets that the var is not distributed over.
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		if(var->flags & State_Variable::Flags::f_invalid) continue;
		
		if(var->function_tree)
			check_valid_distribution_of_dependencies(app, var->function_tree,         var, false);
		if(var->initial_function_tree)
			check_valid_distribution_of_dependencies(app, var->initial_function_tree, var, true);
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




