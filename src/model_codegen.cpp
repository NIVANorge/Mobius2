
#include "model_codegen.h"
#include "model_application.h"


Math_Expr_FT *
get_index_count_code(Model_Application *app, Entity_Id index_set, Index_Exprs &indexes) {
	
	// If the index count could depend on the state of another index set, we have to look it up dynamically
	if(is_valid(app->model->index_sets[index_set]->sub_indexed_to)) {
		auto offset = app->index_counts_structure.get_offset_code(index_set, indexes);
		auto ident = new Identifier_FT();
		ident->value_type = Value_Type::integer;
		ident->variable_type = Variable_Type::index_count;
		ident->exprs.push_back(offset);
		return ident;
	}
	// Otherwise we can just return the constant.
	return make_literal((s64)app->get_max_index_count(index_set).index);
}

Math_Expr_FT *
make_possibly_time_scaled_ident(Model_Application *app, Var_Id var_id) {
	auto ident = make_state_var_identifier(var_id);
	auto var = app->vars[var_id];
	if(var->is_flux() && var->type == State_Var::Type::declared) {
		auto var2 = as<State_Var::Type::declared>(var);
		if(var2->flux_time_unit_conv != 1.0);
		ident = make_binop('*', ident, make_literal(var2->flux_time_unit_conv));
	}
	return ident;
}

Math_Expr_FT *
make_possibly_weighted_var_ident(Model_Application *app, Var_Id var_id, Math_Expr_FT *weight = nullptr, Math_Expr_FT *unit_conv = nullptr) {
	
	Math_Expr_FT* var_ident = make_possibly_time_scaled_ident(app, var_id);
	
	if(unit_conv)
		var_ident = make_binop('*', var_ident, unit_conv);
	
	if(weight)
		var_ident = make_binop('*', var_ident, weight);
	
	return var_ident;
}



void
instruction_codegen(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	auto model = app->model;
	
	if(initial) {
		for(auto &instr : instructions) {
			
			// Initial values for dissolved quantities
			if(!(instr.type == Model_Instruction::Type::compute_state_var && instr.code && is_valid(instr.var_id))) continue;
			auto var = app->vars[instr.var_id];  // var is the mass or volume of the quantity
			if(var->type != State_Var::Type::declared) continue;
			auto var2 = as<State_Var::Type::declared>(var);
			auto conc_id = var2->conc;
			if(!is_valid(conc_id)) continue;
			
			// NOTE: it is easier just to set the generated code for both the mass and conc as we process the mass
			auto conc = as<State_Var::Type::dissolved_conc>(app->vars[conc_id]);
			auto &conc_instr = instructions[conc_id.id];
			auto dissolved_in = app->vars.id_of(remove_dissolved(var->loc1));
				
			if(var2->initial_is_conc) {
				// conc is given. compute mass
				conc_instr.code = instr.code;
				conc_instr.type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
				instr.code = make_binop('*', make_state_var_identifier(conc_id), make_state_var_identifier(dissolved_in));
				if(conc->unit_conversion != 1.0)
					instr.code = make_binop('/', instr.code, make_literal(conc->unit_conversion));
			} else {
				// mass is given. compute conc.
				conc_instr.type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
				conc_instr.code = make_safe_divide(make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
				if(conc->unit_conversion != 1.0)
					conc_instr.code = make_binop('*', instr.code, make_literal(conc->unit_conversion));
			}
		}
	}
	
	for(auto &instr : instructions) {
		
		if(!initial && instr.type == Model_Instruction::Type::compute_state_var) {
			auto var = app->vars[instr.var_id];
			
			//TODO: For overridden quantities they could just be removed from the solver. Also they shouldn't get any index set dependencies from fluxes connected to them.
			//    not sure about the best way to do it (or where).
			//      -hmm this comment may be outdated. Should be checked.
			
			// Directly override the mass of a quantity
			if(var->type == State_Var::Type::declared) {
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->decl_type == Decl_Type::quantity && var2->override_tree && !var2->override_is_conc)
					instr.code = copy(var2->override_tree.get());
			}
			
			// Codegen for concs of dissolved variables
			if(var->type == State_Var::Type::dissolved_conc) {
				
				auto conc = as<State_Var::Type::dissolved_conc>(var);
				auto mass_id = conc->conc_of;
				auto mass_var = as<State_Var::Type::declared>(app->vars[mass_id]);
				auto dissolved_in = app->vars.id_of(remove_dissolved(mass_var->loc1));
				
				if(mass_var->override_tree && mass_var->override_is_conc) {
					instr.code = copy(mass_var->override_tree.get());

					// If we override the conc, the mass is instead  conc*volume_we_are_dissolved_in .
					auto mass_instr = &instructions[mass_id.id];
					mass_instr->code = make_binop('*', make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
					if(conc->unit_conversion != 1.0)
						mass_instr->code = make_binop('/', mass_instr->code, make_literal(conc->unit_conversion));
					
				} else {
					instr.code = make_safe_divide(make_state_var_identifier(mass_id), make_state_var_identifier(dissolved_in));
					if(conc->unit_conversion != 1.0)
						instr.code = make_binop('*', instr.code, make_literal(conc->unit_conversion));
				}
			}
			
			// Codegen for fluxes of dissolved variables
			if(var->type == State_Var::Type::dissolved_flux) {
				auto var2 = as<State_Var::Type::dissolved_flux>(var);
				auto conc = as<State_Var::Type::dissolved_conc>(app->vars[var2->conc]);
				instr.code = make_binop('*', make_state_var_identifier(var2->conc), make_possibly_time_scaled_ident(app, var2->flux_of_medium));
				// TODO: Here we could also just do the re-computation of the concentration so that we don't get the back-and-forth unit conversion...
				
				if(conc->unit_conversion != 1.0)
					instr.code = make_binop('/', instr.code, make_literal(conc->unit_conversion));
				
				// Certain types of fluxes are allowed to be negative, in that case we need the concentration to be taken from the target.
				// TODO: Allow for other types of fluxes to be bidirectional also
				
				// TODO: The way it is currently set up will probably only work if the restriction is
				// Var_Loc_Restriction::below. Otherwise the concentration is another
				// state variable altogether. If we fix that however, this should also
				// work for more general fluxes.
				auto &restriction = restriction_of_flux(var); // TODO: Should use instr->restriction?
				if(is_valid(restriction.connection_id) && restriction.restriction == Var_Loc_Restriction::below) {
					auto conn = model->connections[restriction.connection_id];
					if(conn->type == Connection_Type::grid1d || conn->type == Connection_Type::all_to_all) {
						auto condition = make_binop(Token_Type::geq, make_state_var_identifier(var2->flux_of_medium), make_literal(0.0));
						auto conc2 = static_cast<Identifier_FT *>(make_state_var_identifier(var2->conc));
						
						conc2->restriction = restriction;
						
						auto altval = make_binop('*', conc2, make_possibly_time_scaled_ident(app, var2->flux_of_medium));
						if(conc->unit_conversion != 1.0)
							altval = make_binop('/', altval, make_literal(conc->unit_conversion));
						
						auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
						if_chain->value_type = Value_Type::real;
						if_chain->exprs.push_back(instr.code);
						if_chain->exprs.push_back(condition);
						if_chain->exprs.push_back(altval);
						instr.code = if_chain;
					}
				}
			}
		
			
			// Restrict discrete fluxes to not overtax their source.
			if(var->is_flux() && !is_valid(instr.solver) && instr.code && is_located(var->loc1)) {
			
				// note: create something like
				//      flux = min(flux, source)
				// NOTE: it is a design decision by the framework to not allow negative discrete fluxes, otherwise the flux would get a much more
				//      complicated relationship with its target. Should maybe just apply a    max(0, ...) to it as well by default?

				Var_Id source_id = app->vars.id_of(var->loc1);
				auto source_ref = make_state_var_identifier(source_id);
				instr.code = make_intrinsic_function_call(Value_Type::real, "min", instr.code, source_ref);
			}
			
			// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
			//   Make a lookup accelleration for this?
			
			// Codegen for in_fluxes (not connection in_flux):
			if(var->type == State_Var::Type::in_flux_aggregate) {
				auto var2 = as<State_Var::Type::in_flux_aggregate>(var);
				Math_Expr_FT *flux_sum = make_literal((double)0.0);
				//  find all fluxes that has the given target and sum them up.
				for(auto flux_id : app->vars.all_fluxes()) {
					auto flux_var = app->vars[flux_id];
					//if(!flux_var->is_valid() || !flux_var->is_flux()) continue;
					if(is_valid(restriction_of_flux(flux_var).connection_id)) continue;
					if(!is_located(flux_var->loc2) || app->vars.id_of(flux_var->loc2) != var2->in_flux_to) continue;
					
					auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
					if(flux_var->unit_conversion_tree)
						flux_ref = make_binop('*', flux_ref, copy(flux_var->unit_conversion_tree.get()));
					flux_sum = make_binop('+', flux_sum, flux_ref);
				}
				instr.code = flux_sum;
			}
			
			// Codegen for the derivative of ODE state variables:
			if(var->type == State_Var::Type::declared) {
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->decl_type == Decl_Type::quantity
					&& is_valid(instr.solver) && !var2->override_tree) {
					Math_Expr_FT *fun = make_literal((double)0.0);
					
					// aggregation variable for values coming from connection fluxes.
					for(auto target_agg : var2->conn_target_aggs)
						fun = make_binop('+', fun, make_state_var_identifier(target_agg));
					
					for(auto source_agg : var2->conn_source_aggs)
						fun = make_binop('-', fun, make_state_var_identifier(source_agg));
					
					for(Var_Id flux_id : app->vars.all_fluxes()) {
						auto flux = app->vars[flux_id];
						
						// NOTE: In the case of an all-to-all or directed_graph connection case we have set up an aggregation variable also for the source, so we already subtract using that.
						// TODO: We could consider always having an aggregation variable for the source even when the source is always just one instace just to get rid of all the special cases (?).
						
						auto &restriction = restriction_of_flux(flux);
						bool is_bottom      = (restriction.restriction == Var_Loc_Restriction::bottom);
						bool has_source_agg = false;
						if(is_valid(restriction.connection_id)) {
							auto type = model->connections[restriction.connection_id]->type;
							has_source_agg = (type == Connection_Type::all_to_all) || (type == Connection_Type::directed_graph);
						}
						
						// NOTE: For bottom fluxes there is a special hack where they are subtracted from the target agg variable. Hopefully we get a better solution.
						
						if(is_located(flux->loc1) && app->vars.id_of(flux->loc1) == instr.var_id
							&& !has_source_agg && !is_bottom) {

							auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
							fun = make_binop('-', fun, flux_ref);
						}
						
						if(is_located(flux->loc2) && app->vars.id_of(flux->loc2) == instr.var_id
							&& (!is_valid(restriction.connection_id) || is_bottom)) {
							
							auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
							// NOTE: the unit conversion applies to what reaches the target.
							if(flux->unit_conversion_tree)
								flux_ref = make_binop('*', flux_ref, copy(flux->unit_conversion_tree.get()));
							fun = make_binop('+', fun, flux_ref);
						}
					}
					instr.code = fun;
				}
			}
			
		} else if (instr.type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
			
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id);
			
		} else if (instr.type == Model_Instruction::Type::add_discrete_flux_to_target) {
			
			auto unit_conv = app->vars[instr.var_id]->unit_conversion_tree.get();
			if(unit_conv)
				unit_conv = copy(unit_conv);
			
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id, nullptr, unit_conv);
			
		} else if (instr.type == Model_Instruction::Type::add_to_aggregate) {
			
			auto agg_var = as<State_Var::Type::regular_aggregate>(app->vars[instr.target_id]);
			
			auto weight = agg_var->aggregation_weight_tree.get();
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id, weight, nullptr);
			
		} else if (instr.type == Model_Instruction::Type::add_to_connection_aggregate) {
			
			Math_Expr_FT *weight = nullptr;
			Math_Expr_FT *unit_conv = nullptr;
			auto target_agg = as<State_Var::Type::connection_aggregate>(app->vars[instr.target_id]);
			for(auto &data : target_agg->conversion_data) {
				if(data.source_id == instr.source_id) {
					weight    = data.weight.get();
					unit_conv = data.unit_conv.get();
					break;
				}
			}
			if(weight) weight = copy(weight);
			if(unit_conv) unit_conv = copy(unit_conv);
			
			auto agg_var = app->vars[instr.target_id];
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id, weight, unit_conv);
			
		} else if(instr.type == Model_Instruction::Type::special_computation) {
			
			// Taken care of elsewhere.
		}
	}
}

void
set_grid1d_target_indexes(Model_Application *app, Index_Exprs &indexes, Single_Restriction &restriction, Math_Expr_FT *specific_target = nullptr) {
	
	if(app->model->connections[restriction.connection_id]->type != Connection_Type::grid1d)
		fatal_error(Mobius_Error::internal, "Misuse of set_grid1d_target_indexes().");

	auto index_set = app->get_single_connection_index_set(restriction.connection_id);
	
	auto prev_index = indexes.indexes[index_set.id];

	if (restriction.restriction == Var_Loc_Restriction::top) {
		indexes.indexes[index_set.id] = make_literal((s64)0);
	} else if(restriction.restriction == Var_Loc_Restriction::bottom) {
		auto count = get_index_count_code(app, index_set, indexes);
		indexes.indexes[index_set.id] = make_binop('-', count, make_literal((s64)1));
	} else if(restriction.restriction == Var_Loc_Restriction::specific) {
		// TODO: Should clamp it betwen 0 and index_count
		if(!specific_target)
			fatal_error(Mobius_Error::internal, "Wanted to set indexes for a specific connection target, but the code was not provided.");
		indexes.indexes[index_set.id] = specific_target;
		
	} else if(restriction.restriction == Var_Loc_Restriction::above || restriction.restriction == Var_Loc_Restriction::below) {
		auto index = copy(prev_index);
		char oper = (restriction.restriction == Var_Loc_Restriction::above) ? '-' : '+';
		index = make_binop(oper, index, make_literal((s64)1));
		indexes.indexes[index_set.id] = index;
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection restriction type.");
	
	delete prev_index;
}

void
set_all_to_all_target_indexes(Model_Application *app, Index_Exprs &indexes, Entity_Id connection_id) {
	if(app->model->connections[connection_id]->type != Connection_Type::all_to_all)
		fatal_error(Mobius_Error::internal, "Misuse of set_all_to_all_target_indexes().");
	
	auto index_set = app->get_single_connection_index_set(connection_id);
	if(!indexes.mat_col || index_set != indexes.mat_index_set) return;
	
	auto tmp = indexes.indexes[index_set.id];
	indexes.indexes[index_set.id] = indexes.mat_col;
	indexes.mat_col = tmp;
}

void
set_graph_target_indexes(Model_Application *app, Index_Exprs &indexes, Entity_Id connection_id, Entity_Id source_compartment, Entity_Id target_compartment) {
	
	auto type = app->model->connections[connection_id]->type;
	if(type != Connection_Type::directed_tree && type != Connection_Type::directed_graph)
		fatal_error(Mobius_Error::internal, "Misuse of set_graph_target_indexes().");
	
	auto model = app->model;

	auto find_target = app->find_connection_component(connection_id, target_compartment);
	
	if(find_target->index_sets.empty()) return;
	
	for(int idx = 0; idx < find_target->index_sets.size(); ++idx) {
		int id = idx+1;
		auto index_set = find_target->index_sets[idx];
		// NOTE: we create the formula to look up the index of the target, but this is stored using the indexes of the source.
		Math_Expr_FT *idx_offset;
		try {
			idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, id}, indexes);
		} catch (...) {
			fatal_error(Mobius_Error::internal, "Unable to find connection data for connection ", app->model->connections[connection_id]->name, 
				" going from ", app->model->components[source_compartment]->name, " to ", app->model->components[target_compartment]->name, " ( and index set ", app->model->index_sets[index_set]->name, " )");
		}
		auto target_index = new Identifier_FT();
		target_index->variable_type = Variable_Type::connection_info;
		target_index->value_type = Value_Type::integer;
		target_index->exprs.push_back(idx_offset);
		indexes.indexes[index_set.id] = target_index;
	}
}

void
put_var_lookup_indexes_basic(Identifier_FT *ident, Model_Application *app, Index_Exprs &index_expr) {
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step = -1;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = app->parameter_structure.get_offset_code(ident->par_id, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = app->series_structure.get_offset_code(ident->var_id, index_expr);
		back_step = app->series_structure.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		auto var = app->vars[ident->var_id];
		if(!var->is_valid())
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		offset_code = app->result_structure.get_offset_code(ident->var_id, index_expr);
		back_step = app->result_structure.total_count;
	}

	if(ident->has_flag(Identifier_FT::last_result)) {
		if(back_step > 0)
			offset_code = make_binop('-', offset_code, make_literal(back_step));
		else
			fatal_error(Mobius_Error::internal, "Received a 'last_result' flag on an identifier that should not have one.");
		ident->remove_flag(Identifier_FT::last_result);
	} 
	
	if(ident->flags) { // NOTE: all flags should have been resolved and removed at this point.
		ident->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Forgot to resolve one or more flags on an identifier.");
	}
	
	ident->exprs.push_back(offset_code);
}

bool
give_the_same_condition(Model_Application *app, Var_Loc_Restriction *a, Var_Loc_Restriction *b) {
	// TODO: Also for the secondary restriction!
	if(!is_valid(a->connection_id) || !is_valid(b->connection_id)) return false; // The function should not be used in this case, probably.
	
	auto model = app->model;
	if(model->connections[a->connection_id]->type != Connection_Type::grid1d || model->connections[b->connection_id]->type != Connection_Type::grid1d)
		return a->connection_id == b->connection_id;
	if(a->restriction != b->restriction) return false;
	return app->get_single_connection_index_set(a->connection_id) == app->get_single_connection_index_set(b->connection_id);
}


Math_Expr_FT *
make_restriction_condition(Model_Application *app, Math_Expr_FT *value, Math_Expr_FT *alt_val, Var_Loc_Restriction restriction, Index_Exprs &index_expr, Entity_Id source_compartment = invalid_entity_id) {
	
	// TOOD: Also for the secondary restriction!
	
	// For grid1d connections, if we look up 'above' or 'below', we can't do it if we are on the first or last index respectively, and so the entire expression must be invalidated.
	// Same if the expression itself is for a flux that is along a grid1d connection and we are at the last index.
	// This function creates the code to compute the boolean condition that the expression should be invalidated.
	auto connection_id = restriction.connection_id;
	
	if(!is_valid(connection_id)) return value;
	
	auto type = app->model->connections[connection_id]->type;

	Math_Expr_FT *new_condition = nullptr;
	
	if(type == Connection_Type::grid1d && (restriction.restriction == Var_Loc_Restriction::above || restriction.restriction == Var_Loc_Restriction::below)) {
		
		auto index_set = app->get_single_connection_index_set(connection_id);
		
		// TODO: This is very wasteful just to get the single index. Could factor out a function
		// for that instead.
		Index_Exprs new_indexes(app->model);
		new_indexes.copy(index_expr);
		set_grid1d_target_indexes(app, new_indexes, restriction);
		Math_Expr_FT *index = new_indexes.indexes[index_set.id];
		
		if(restriction.restriction == Var_Loc_Restriction::above)
			new_condition = make_binop(Token_Type::geq, copy(index), make_literal((s64)0));
		else if(restriction.restriction == Var_Loc_Restriction::below) {
			auto index_count = get_index_count_code(app, index_set, index_expr);
			new_condition = make_binop('<', copy(index), index_count);
		}
	} else if ((type == Connection_Type::directed_tree || type == Connection_Type::directed_graph) && is_valid(source_compartment)) {
		
		auto *comp = app->find_connection_component(connection_id, source_compartment, false);
		if(comp && comp->total_as_source > 0) {
			int max_instances = 1;    //TODO: use app->get_active_instance_count instead ?
			for(auto index_set : comp->index_sets)
				max_instances *= app->get_max_index_count(index_set).index;
			if(comp->total_as_source < max_instances) {
				// If the component some times appears as a source, but not always, we have to check for each instance whether or not to evaluate the flux.
				
				// Code for looking up the id of the target compartment of the current source.
				auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, 0}, index_expr);	// the 0 is because the compartment id is stored at info id 0
				auto compartment_id = new Identifier_FT();
				compartment_id->variable_type = Variable_Type::connection_info;
				compartment_id->value_type = Value_Type::integer;
				compartment_id->exprs.push_back(idx_offset);
				
				// I.e. it is -1 (out) or it is a positive id (located).
				new_condition = make_binop('>', compartment_id, make_literal((s64)-2));
			}
		} else {
			// If this compartment never appears as a source, we can always turn off evaluating this flux
			new_condition = make_literal(false);
		}
	}
	
	if(new_condition) {
		auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
		if_chain->value_type = Value_Type::real;
		if_chain->exprs.push_back(value);
		
		if_chain->exprs.push_back(new_condition);
		if_chain->exprs.push_back(alt_val);
		return if_chain;
	}
	
	return value;
}

Math_Expr_FT *
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, Var_Loc_Restriction *existing_restriction = nullptr) {
	
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = put_var_lookup_indexes(expr->exprs[idx], app, index_expr, existing_restriction);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	if(ident->variable_type != Variable_Type::parameter && ident->variable_type != Variable_Type::series && ident->variable_type != Variable_Type::state_var)
		return expr;
	
	if(!expr->exprs.empty())
		fatal_error(Mobius_Error::internal, "Tried to set var lookup indexes on an expr that already has them.");
	
	//if(is_valid(ident->restriction.connection_id))
	//	found_restriction.insert(ident->restriction);
	
	auto &res = ident->restriction;
	if(res.restriction != Var_Loc_Restriction::none) {
		if(!is_valid(ident->restriction.connection_id))
			fatal_error(Mobius_Error::internal, "Got an identifier with a restriction but without a connection");
		
		auto connection = app->model->connections[res.connection_id];
		
		if(connection->type != Connection_Type::directed_graph) {
			Index_Exprs new_indexes(app->model);
			new_indexes.copy(index_expr);
			
			if(connection->type == Connection_Type::all_to_all) {
				if(res.restriction != Var_Loc_Restriction::below) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Only the 'below' restriction is allowed on an all_to_all connection.");
				}
				set_all_to_all_target_indexes(app, new_indexes, res.connection_id);
			} else if(connection->type == Connection_Type::grid1d) {
				set_grid1d_target_indexes(app, new_indexes, res);
				
			} else if(connection->type == Connection_Type::directed_tree) {
				if(res.restriction != Var_Loc_Restriction::below) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Only the 'below' restriction is allowed in this context.");
				}
				if(!is_valid(res.source_comp) || !is_valid(res.target_comp) || res.source_comp != res.target_comp) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("The 'below' directive is not allowed in this context.");
				}
				set_graph_target_indexes(app, new_indexes, res.connection_id, res.source_comp, res.target_comp);
			}
			
			put_var_lookup_indexes_basic(ident, app, new_indexes);
			
			// If there is an exisiting condition, it will enclose the entire expression, so we don't need to check for it again.
			if(!existing_restriction || !give_the_same_condition(app, existing_restriction, &res)) {
				return make_restriction_condition(app, ident, make_literal((double)0.0), res, index_expr); // Important not to use new_indexes here..						
			}
			
			return ident;
			
		} else if (connection->type == Connection_Type::directed_graph) {
			
			if(ident->variable_type != Variable_Type::series && ident->variable_type != Variable_Type::state_var) {
				ident->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("On a directed_graph connection, 'below' can only be used on a state variable or series.");
			}
			
			auto source_comp = app->vars[ident->var_id]->loc1.first();
			
			if(!is_valid(source_comp))
				fatal_error(Mobius_Error::internal, "Did not properly set the source compartment of the expression.");
			
			// We have to check for each possible target of the graph.
			auto component = app->find_connection_component(res.connection_id, source_comp, false);
			
			if(!component) {
				ident->source_loc.print_error_header(Mobius_Error::internal);
				fatal_error("Did not find the component for this expression. Connection is ", connection->name, ", component is ", app->model->components[source_comp]->name, ".");
			}
			
			auto if_expr = new Math_Expr_FT(Math_Expr_Type::if_chain);
			if_expr->value_type = expr->value_type;
			
			for(auto target_comp : component->possible_targets) {
				
				if(!is_valid(target_comp)) continue; // This happens if it is an 'out'. In that case it will be caught by the "otherwise" clause below (the value is 0).
				
				auto loc = app->vars[ident->var_id]->loc1;
				loc.components[0] = target_comp;
				auto target_id = app->vars.id_of(loc);
				if(!is_valid(target_id)) {
					 // TODO: Maybe we should do this validity check in model_composition already, before we do the codegen.
					 //   Or the value should just default to 0 in this case? That could cause silent errors though.
					ident->source_loc.print_error_header();
					fatal_error("This variable does not exist for the compartment \"", app->model->components[target_comp]->name, "\", and so can't be referenced along every edge starting in this node.");
				}
				auto ident2 = static_cast<Identifier_FT *>(copy(ident));
				ident2->var_id = target_id;
				
				Index_Exprs new_indexes(app->model);
				new_indexes.copy(index_expr);
				set_graph_target_indexes(app, new_indexes, res.connection_id, source_comp, target_comp);
				put_var_lookup_indexes_basic(ident2, app, new_indexes);
				
				if_expr->exprs.push_back(ident2);
				
				// Note here we use the index_expr belonging to the source compartment, not the
				// one belonging to the target compartment
				auto idx_offset = app->connection_structure.get_offset_code(Connection_T {res.connection_id, source_comp, 0}, index_expr);	// the 0 is because the compartment id is stored at info id 0
				auto compartment_id = new Identifier_FT();
				compartment_id->variable_type = Variable_Type::connection_info;
				compartment_id->value_type = Value_Type::integer;
				compartment_id->exprs.push_back(idx_offset);
				
				// The condition that the expression resolves to the ident2 value.
				if_expr->exprs.push_back(make_binop('=', compartment_id, make_literal((s64)target_id.id)));
			}
			
			delete ident;
			
			if_expr->exprs.push_back(make_literal((double)0.0));  // This is if the given target is not on the list.
			
			return if_expr;
		} else
			fatal_error(Mobius_Error::internal, "Unimplemented connection type in put_var_lookup_indexes()");
		
	} //else if(is_valid(ident->restriction.connection_id))
		//fatal_error(Mobius_Error::internal, "Got an identifier with a connection but without a restriction.");

	put_var_lookup_indexes_basic(ident, app, index_expr);
	return ident;
}

Math_Expr_FT *
add_value_to_state_var(Var_Id target_id, Math_Expr_FT *target_offset, Math_Expr_FT *value, char oper) {
	auto target_ident = make_state_var_identifier(target_id);
	target_ident->exprs.push_back(target_offset);
	auto sum_or_difference = make_binop(oper, target_ident, value);
	
	auto assignment = new Assignment_FT(Math_Expr_Type::state_var_assignment, target_id);
	assignment->exprs.push_back(copy(target_offset));
	assignment->exprs.push_back(sum_or_difference);
	assignment->value_type = Value_Type::none;
	return assignment;
}

Math_Expr_FT *
add_value_to_graph_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Var_Loc_Restriction &restriction) {
	
	auto model = app->model;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	
	if(target_agg->is_source) {
		auto agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		return add_value_to_state_var(agg_id, agg_offset, value, '+');
	}
	
	// Hmm, these two lookups are very messy. See also similar in model_compilation a couple of places
	auto source_compartment = app->vars[source_id]->loc1.components[0];
	auto target_compartment = app->vars[target_agg->agg_for]->loc1.components[0];
	
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	set_graph_target_indexes(app, new_indexes, restriction.connection_id, source_compartment, target_compartment);
	
	auto agg_offset = app->result_structure.get_offset_code(agg_id, new_indexes);
	
	//warning_print("*** *** Codegen for connection ", app->vars[source_id]->name, " to ", app->vars[target_agg->connection_agg]->name, " using agg var ", app->vars[agg_id]->name, "\n");
	
	// Code for looking up the id of the target compartment of the current source.
	auto idx_offset = app->connection_structure.get_offset_code(Connection_T {restriction.connection_id, source_compartment, 0}, indexes);	// the 0 is because the compartment id is stored at info id 0
	auto compartment_id = new Identifier_FT();
	compartment_id->variable_type = Variable_Type::connection_info;
	compartment_id->value_type = Value_Type::integer;
	compartment_id->exprs.push_back(idx_offset);
	
	// There can be multiple valid target components for the connection, so we have to make code to see if the value should indeed be added to this aggregation variable.
	// (even if there could only be one valid target compartment, this makes sure that the set target is not -2 or -1 (i.e. nonexistent or 'out')).
	Math_Expr_FT *condition = make_binop('=', compartment_id, make_literal((s64)target_compartment.id));
	
	auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
	if_chain->value_type = Value_Type::none;
	
	if_chain->exprs.push_back(add_value_to_state_var(agg_id, agg_offset, value, '+'));
	if_chain->exprs.push_back(condition);
	//if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
	if_chain->exprs.push_back(make_no_op());
	
	Math_Expr_FT *result = if_chain;
	
	return result;
}

Math_Expr_FT *
add_value_to_all_to_all_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Var_Loc_Restriction &restriction) {
	
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);

	Math_Expr_FT *agg_offset = nullptr;
	
	if(agg_var->is_source)
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	else {
		
		Index_Exprs new_indexes(app->model);
		new_indexes.copy(indexes);
		set_all_to_all_target_indexes(app, new_indexes, restriction.connection_id);
		agg_offset = app->result_structure.get_offset_code(agg_id, new_indexes);
	}
	
	return add_value_to_state_var(agg_id, agg_offset, value, '+'); // NOTE: it is a + regardless, since the subtraction happens explicitly when we use the value later.
}

void
set_grid1d_target_indexes_for_flux(Model_Application *app, Index_Exprs &indexes, Single_Restriction &restriction, Var_Id flux_id) {
	Math_Expr_FT *specific_target = nullptr;
	if(restriction.restriction == Var_Loc_Restriction::specific) {
		if(!app->vars[flux_id]->specific_target.get())
			fatal_error(Mobius_Error::internal, "Did not find @specific code for ", app->vars[flux_id]->name, " even though it was expected.");
		specific_target = copy(app->vars[flux_id]->specific_target.get());
		put_var_lookup_indexes(specific_target, app, indexes);
	}

	set_grid1d_target_indexes(app, indexes, restriction, specific_target);
}

Math_Expr_FT *
add_value_to_grid1d_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id flux_id, Index_Exprs &indexes, Var_Loc_Restriction restriction) {
	
	auto model = app->model;

	// NOTE: This is a bit of a hack. We should maybe have a source aggregate here instead, but that can cause a lot of unnecessary work and memory use for the model.
	if(restriction.restriction == Var_Loc_Restriction::bottom)
		value = make_unary('-', value);
	
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	
	set_grid1d_target_indexes_for_flux(app, new_indexes, restriction, flux_id);
	
	auto agg_offset = app->result_structure.get_offset_code(agg_id, new_indexes);

	auto result = add_value_to_state_var(agg_id, agg_offset, value, '+');
	
	return result;
}

Math_Expr_FT *
add_value_to_connection_agg_var(Model_Application *app, Math_Expr_FT *value, Model_Instruction *instr, Index_Exprs &indexes) {
	auto model = app->model;
	
	auto &restriction = instr->restriction;
	auto agg_id = instr->target_id;
	
	auto connection = model->connections[restriction.connection_id];
	
	// TODO: This is only needed if we have a secondary restriction. A bit wasteful..
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	
	if(!as<State_Var::Type::connection_aggregate>(app->vars[agg_id])->is_source 
		&& restriction.restriction2.restriction != Var_Loc_Restriction::none) {
			
		auto conn2 = model->connections[restriction.restriction2.connection_id];
		if(conn2->type != Connection_Type::grid1d) {
			model->fluxes[as<State_Var::Type::declared>(app->vars[instr->var_id])->decl_id]->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Currently we only support if the type of a connection in a secondary bracket is grid1d.");
		}
		
		//log_print("Got here! ", app->vars[agg_id]->name, " ", app->vars[instr->var_id]->name, "\n");
		//log_print("Got here! ", instr->debug_string(app), "\n");
		
		set_grid1d_target_indexes_for_flux(app, new_indexes, restriction.restriction2, instr->var_id);
	}
	
	if(connection->type == Connection_Type::directed_tree || connection->type == Connection_Type::directed_graph) {
		
		return add_value_to_graph_agg(app, value, agg_id, instr->source_id, new_indexes, restriction);
		
	} else if (connection->type == Connection_Type::all_to_all) {
		
		return add_value_to_all_to_all_agg(app, value, agg_id, new_indexes, restriction);
		
	} else if (connection->type == Connection_Type::grid1d) {
		
		return add_value_to_grid1d_agg(app, value, agg_id, instr->var_id, new_indexes, restriction);
		
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection type in add_value_to_connection_agg_var()");
	
	return nullptr;
}

Math_Expr_FT *
create_nested_for_loops(Math_Block_FT *top_scope, Model_Application *app, std::set<Index_Set_Dependency> &index_sets, Index_Exprs &index_expr) {
	
	auto &indexes = index_expr.indexes;
	
	Math_Block_FT *scope = top_scope;
	auto index_set = index_sets.begin();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		
		auto loop = make_for_loop();
		// NOTE: There is a caveat here: This will only work if the parent index of a sub-indexed index set is always set up first,
		//   and that  *should* work the way we order the Entity_Id's of those right now, but it is a smidge volatile...
		auto index_count = get_index_count_code(app, index_set->id, index_expr);
		loop->exprs.push_back(index_count);
		scope->exprs.push_back(loop);
		
		//NOTE: the scope of this item itself is replaced when it is inserted later.
		// note: this is a reference to the iterator of the for loop.
		indexes[index_set->id.id] = make_local_var_reference(0, loop->unique_block_id, Value_Type::integer);
		
		scope = loop;
		
		if(index_set->order != 1) {
			if(idx != index_sets.size()-1 || index_set->order > 2) {
				fatal_error(Mobius_Error::internal, "Somehow got a higher-order indexing over an index set that was not the last index set dependency, or the order was larger than 2. Order: ", index_set->order);
			}
			auto loop = make_for_loop();
			auto index_count = get_index_count_code(app, index_set->id, index_expr);
			loop->exprs.push_back(index_count);
			index_expr.mat_col = make_local_var_reference(0, loop->unique_block_id, Value_Type::integer);
			index_expr.mat_index_set = index_set->id;
			
		
			// Make it so that it skips when the two indexes of the same index set are the same...
			auto the_scope = new Math_Block_FT();
			the_scope->value_type = Value_Type::none;
			auto condition = make_binop(Token_Type::neq, copy(indexes[index_set->id.id]), copy(index_expr.mat_col));
			auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
			if_chain->value_type = Value_Type::none;
			if_chain->exprs.push_back(the_scope);
			if_chain->exprs.push_back(condition);
			if_chain->exprs.push_back(make_no_op());
			
			auto block = new Math_Block_FT();
			block->value_type = Value_Type::none;
			loop->exprs.push_back(block);
			block->exprs.push_back(if_chain);
			
			scope->exprs.push_back(loop);
			scope = the_scope;
		
			//scope = loop;
			
		}
		index_set++;
	}
	
	auto body = new Math_Block_FT();
	scope->exprs.push_back(body);
	scope = body;
	
	return scope;
}


Math_Expr_FT *
generate_run_code(Model_Application *app, Batch *batch, std::vector<Model_Instruction> &instructions, bool initial) {
	auto model = app->model;
	auto top_scope = new Math_Block_FT();
	
	Index_Exprs indexes(app->model);
	
	for(auto &array : batch->arrays) {
		Math_Expr_FT *scope = create_nested_for_loops(top_scope, app, array.index_sets, indexes);
		
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			Math_Expr_FT *fun = nullptr;
			
			try {
				if(instr->code) {
					fun = copy(instr->code);
					fun = put_var_lookup_indexes(fun, app, indexes, &instr->restriction);
				} else if (instr->type != Model_Instruction::Type::clear_state_var) {
					//NOTE: Some instructions are placeholders that give the order of when a value is 'ready' for use by other instructions, but they are not themselves computing the value they are placeholding for. This for instance happens with aggregation variables that are computed by other add_to_aggregate instructions. So it is OK that their 'fun' is nullptr.
					
					// TODO: Should we try to infer if it is ok that there is no code for this compute_state_var (?). Or maybe have a separate type for it when we expect there to be no actual computation and it is only a placeholder for a value (?). Or maybe have a flag for it on the State_Variable.
					if(instr->type == Model_Instruction::Type::compute_state_var)
						continue;
					fatal_error(Mobius_Error::internal, "Unexpectedly missing code for a model instruction. Type: ", (int)instr->type, ".");
				}
			} catch(int) {
				fatal_error("The error happened when trying to put lookup indexes for the instruction ", instr->debug_string(app), initial ? " during the initial value step." : ".");
			
			}
				
			Math_Expr_FT *result_code = nullptr;
			
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				
				auto offset_code = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Assignment_FT(Math_Expr_Type::state_var_assignment, instr->var_id);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				assignment->value_type = Value_Type::none;
				result_code = assignment;
				
			} else if (instr->type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->source_id, indexes);
				result_code = add_value_to_state_var(instr->source_id, agg_offset, fun, '-');
				
			} else if (instr->type == Model_Instruction::Type::add_discrete_flux_to_target) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->target_id, indexes);
				result_code = add_value_to_state_var(instr->target_id, agg_offset, fun, '+');
				
			} else if (instr->type == Model_Instruction::Type::add_to_aggregate) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->target_id, indexes);
				result_code = add_value_to_state_var(instr->target_id, agg_offset, fun, '+');
				
			} else if (instr->type == Model_Instruction::Type::add_to_connection_aggregate) {
				
				result_code = add_value_to_connection_agg_var(app, fun, instr, indexes);
				
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Assignment_FT(Math_Expr_Type::state_var_assignment, instr->var_id);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((double)0.0));
				result_code = assignment;
				
			} else if (instr->type == Model_Instruction::Type::special_computation) {
				
				auto special = static_cast<Special_Computation_FT *>(instr->code);
				
				Entity_Id source_compartment = invalid_entity_id;
				for(auto &arg : special->arguments) {
					
					// TODO: Again, copy is only necessary if we have a restriction... Make a system to avoid it when not necessary.
					Index_Exprs new_indexes(model);
					new_indexes.copy(indexes);
					
					// TODO: The source_compartment setup is very error prone.
					//      What happens if there are many different or none?
					//      Also, the way it is done now presupposes that the results are declared first.
					Entity_Id compartment = invalid_entity_id;
					if(arg.variable_type == Variable_Type::state_var) {
						compartment = app->vars[arg.var_id]->loc1.components[0];
					} else if(arg.variable_type == Variable_Type::parameter) {
						//TODO
					}
					if(arg.has_flag(Identifier_Data::result))
						source_compartment = compartment;
					
					if(arg.restriction.restriction != Var_Loc_Restriction::none) {
						auto conn_id = arg.restriction.connection_id;
						auto conn = model->connections[conn_id];
						if(conn->type == Connection_Type::directed_graph) {
							set_graph_target_indexes(app, new_indexes, conn_id, source_compartment, compartment);
						} else
							fatal_error(Mobius_Error::internal, "Unimplemented special computation codegen for var loc restriction");
					}
					
					Offset_Stride_Code res;
					if(arg.variable_type == Variable_Type::state_var)
						res = app->result_structure.get_special_offset_stride_code(arg.var_id, new_indexes);
					else if(arg.variable_type == Variable_Type::parameter)
						res = app->parameter_structure.get_special_offset_stride_code(arg.par_id, new_indexes);
					else
						fatal_error(Mobius_Error::internal, "Unrecognized variable type in special computation codegen.");
					special->exprs.push_back(res.offset);
					special->exprs.push_back(res.stride);
					special->exprs.push_back(res.count);
				}
				result_code = special;
				
			} else {
				fatal_error(Mobius_Error::internal, "Unimplemented instruction type in code generation.");
			}
			
			if(is_valid(instr->restriction.connection_id)) {// TODO: check instr->restriction.restriction instead ??
				
				Entity_Id source_comp = invalid_entity_id;
				//TODO: Could this be organized in a better way so that we don't need this lookup?
				//   similar to the problem of why we need to store the source_comp and target_comp in the restriction itself some times.
				if(model->connections[instr->restriction.connection_id]->type == Connection_Type::directed_tree)
					source_comp = app->vars[instr->var_id]->loc1.first();
				
				result_code = make_restriction_condition(app, result_code, make_no_op(), instr->restriction, indexes, source_comp);
			}
			
			scope->exprs.push_back(result_code);
		}
		
		if(scope->exprs.empty()) {
			// This could happen in some rare cases where it just contains placeholders for aggregation variables, and so not additional computation should happen.
			
			// TODO: We could just check first if the block just contains placeholders and skip creating this in the first place.
			top_scope->exprs.pop_back();
			delete scope;
		}
		
		indexes.clean();
	}
	
	if(is_valid(batch->solver)) {
	
		if(batch->arrays_ode.empty() || batch->arrays_ode[0].instr_ids.empty())
			fatal_error(Mobius_Error::internal, "Somehow we got an empty ode batch in a batch that was assigned a solver.");
		
		// NOTE: The way we do things here rely on the fact that all ODEs of the same batch (and time step) are stored contiguously in memory. If that changes, the indexing of derivatives will break!
		//    If we ever want to change it, we have to come up with a separate system for indexing the derivatives. (which should not be a big deal).
		s64 init_pos = app->result_structure.get_offset_base(instructions[batch->arrays_ode[0].instr_ids[0]].var_id);
		for(auto &array : batch->arrays_ode) {
			Math_Expr_FT *scope = create_nested_for_loops(top_scope, app, array.index_sets, indexes);
					
			for(int instr_id : array.instr_ids) {
				auto instr = &instructions[instr_id];
				
				if(instr->type != Model_Instruction::Type::compute_state_var)
					fatal_error(Mobius_Error::internal, "Somehow we got an instruction that is not a state var computation inside an ODE batch.\n");
				
				// NOTE: the code for an ode variable computes the derivative of the state variable, which is all ingoing fluxes minus all outgoing fluxes
				auto fun = instr->code;
				
				if(!fun)
					fatal_error(Mobius_Error::internal, "ODE variables should always be provided with generated code in instruction_codegen, but we got one without.");
				
				fun = copy(fun);
				
				put_var_lookup_indexes(fun, app, indexes);
				
				auto offset_var = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto offset_deriv = make_binop('-', offset_var, make_literal(init_pos));
				auto assignment = new Assignment_FT(Math_Expr_Type::derivative_assignment, instr->var_id);
				assignment->exprs.push_back(offset_deriv);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
			}
			
			indexes.clean();
		}
	
	}
#if 0
	log_print("\nTree before prune:\n");
	std::stringstream ss;
	print_tree(app, top_scope, ss);
	log_print(ss.str());
	log_print("\n");
#endif
	auto result = prune_tree(top_scope);
#if 0
	log_print("\nTree after prune:\n");
	std::stringstream ss;
	print_tree(app, result, ss);
	log_print(ss.str());
	log_print("\n");
#endif
	
	return result;
}
