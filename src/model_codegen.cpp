
#include "model_codegen.h"
#include "model_application.h"


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

Math_Expr_FT *
make_possibly_weighted_par_ident(Model_Application *app, Entity_Id par_id, Math_Expr_FT *weight = nullptr) {
	
	Math_Expr_FT *par_ident = make_cast(make_parameter_identifier(app->model, par_id), Value_Type::real);
	
	if(weight)
		par_ident = make_binop('*', par_ident, weight);
	
	return par_ident;
}

Math_Expr_FT *
make_connection_target_identifier(Model_Application *app, Index_Exprs &indexes, Entity_Id connection_id, Entity_Id source_comp) {
	// the 0 is because the compartment id is stored at info id 0
	auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_comp, 0}, indexes);
	auto compartment_id = new Identifier_FT();
	compartment_id->variable_type = Variable_Type::connection_info;
	compartment_id->value_type = Value_Type::integer;
	compartment_id->exprs.push_back(idx_offset);
	return compartment_id;
}

Math_Expr_FT *
make_connection_target_check(Model_Application *app, Index_Exprs &indexes, Entity_Id connection_id, Entity_Id source_comp, Entity_Id target_comp) {
	auto compartment_id = make_connection_target_identifier(app, indexes, connection_id, source_comp);
	return make_binop('=', compartment_id, make_literal((s64)target_comp.id));
}


Math_Expr_FT *
make_flux_conc(Model_Application *app, Specific_Var_Location &loc, int n_levels) {
	// This function is only for use inside make_dissolved_flux_code
	
	auto id = app->vars.id_of(loc);
	Identifier_FT *mass_ident = static_cast<Identifier_FT *>(make_state_var_identifier(id));
	mass_ident->restriction = loc;
	
	Specific_Var_Location loc_above = loc;
	loc_above.n_components -= n_levels;    // TODO: Would like to have a remove_dissolved() that works with Specific_Var_Location

	auto above_id = app->vars.id_of(loc_above);
	Identifier_FT *medium_ident = static_cast<Identifier_FT *>(make_state_var_identifier(above_id));
	medium_ident->restriction = loc_above;
	
	return make_safe_divide(mass_ident, medium_ident);
}

Math_Expr_FT *
make_dissolved_flux_code(Model_Application *app, Var_Id flux_id) {
	
	auto var2 = as<State_Var::Type::dissolved_flux>(app->vars[flux_id]);
	
	auto base_id = app->find_base_flux(flux_id);
	auto base = app->vars[base_id];
	int n_levels = var2->loc1.n_components - base->loc1.n_components;
	if(n_levels < 1)
		fatal_error(Mobius_Error::internal, "Something is strange with Var_Location of a dissolved flux");
	
	// NOTE: We re-compute the concentration instead of just referencing the concentration variable for a few reasons:
	//   - We don't have to unwind the unit conversion of the concentration
	//   - If there is a chain of dissolvedes it is cleaner if each of them just reference the base flux instead of one another.
	//   - For 'mixing' fluxes to work correctly they have to reference the base flux.
	
	auto conc_code = make_flux_conc(app, var2->loc1, n_levels);
	
	if(var2->bidirectional || var2->mixing) {
		// In this case we have to also look up the concentration in the target of the flux.
		
		Math_Expr_FT *conc2 = nullptr;
		
		// TODO: This is very annoying, should be streamlined somehow.. We probably redo the same work several places.
		if(var2->loc2.type == Var_Location::Type::connection) {
			Specific_Var_Location loc = var2->loc1;
			static_cast<Var_Loc_Restriction &>(loc) = static_cast<Var_Loc_Restriction &>(var2->loc2);
			conc2 = make_flux_conc(app, loc, n_levels);
		} else if(!is_located(var2->loc2)) {
			fatal_error(Mobius_Error::internal, "Unsupported bidirectional or mixing flux.");
		} else {
			conc2 = make_flux_conc(app, var2->loc2, n_levels);
		}
		
		if(var2->bidirectional) {
			// This says that if the flux is positive, use the concentration in the source, else use the concentration in the target.
			auto condition = make_binop(Token_Type::geq, make_state_var_identifier(base_id), make_literal(0.0));
			
			auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
			if_chain->value_type = Value_Type::real;
			if_chain->exprs.push_back(conc_code);
			if_chain->exprs.push_back(condition);
			if_chain->exprs.push_back(conc2);
			conc_code = if_chain;
		} else if(var2->mixing) {
			conc_code = make_binop('-', conc_code, conc2);
		}
	}
	
	auto *result_code = make_binop('*', conc_code, make_possibly_time_scaled_ident(app, base_id));
	
	return result_code;
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
			auto dissolved_in = conc->conc_in;//app->vars.id_of(remove_dissolved(var->loc1));
				
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
			
			// Directly override the mass of a quantity
			if(var->type == State_Var::Type::declared) {
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->decl_type == Decl_Type::quantity && var2->function_tree && !var2->override_is_conc)
					instr.code = copy(var2->function_tree.get());
			}
			
			// Codegen for concs of dissolved variables
			if(var->type == State_Var::Type::dissolved_conc) {
				
				auto conc = as<State_Var::Type::dissolved_conc>(var);
				auto mass_id = conc->conc_of;
				auto mass_var = as<State_Var::Type::declared>(app->vars[mass_id]);
				auto dissolved_in = conc->conc_in;
				
				if(mass_var->function_tree && mass_var->override_is_conc) {
					instr.code = copy(mass_var->function_tree.get());

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
				instr.code = make_dissolved_flux_code(app, instr.var_id);
			}
			
			// Restrict discrete fluxes to not overtax their source.
			if(var->is_flux() && !is_valid(instr.solver) && instr.code && is_located(var->loc1)) {
			
				// TODO It is a design decision by the framework to not allow negative discrete
				// fluxes, but this is currently not enforced or checked for.
				
				// Note: create something like
				//      flux = min(flux, source)
				// so that a discrete flux can never overtax its target.
				
				Var_Id source_id = app->vars.id_of(var->loc1);
				auto source_ref = make_state_var_identifier(source_id);
				instr.code = make_intrinsic_function_call(Value_Type::real, "min", instr.code, source_ref);
			}
			
			// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
			//   Make a lookup accelleration for this?
			
			// Codegen for regular in_flux (not connection in_flux):
			if(var->type == State_Var::Type::in_flux_aggregate) {
				auto var2 = as<State_Var::Type::in_flux_aggregate>(var);
				Math_Expr_FT *flux_sum = make_literal((double)0.0);
				//  find all fluxes that has the given target and sum them up.
				for(auto flux_id : app->vars.all_fluxes()) {
					auto flux_var = app->vars[flux_id];
					
					if(flux_var->mixing_base || flux_var->loc2.r1.type != Restriction::none || !is_located(flux_var->loc2) || app->vars.id_of(flux_var->loc2) != var2->in_flux_to) continue;
					
					auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
					if(flux_var->unit_conversion_tree)
						flux_ref = make_binop('*', flux_ref, copy(flux_var->unit_conversion_tree.get()));
					flux_sum = make_binop('+', flux_sum, flux_ref);
				}
				instr.code = flux_sum;
			}
			
			// Codegen for the derivative of ODE state variables:
			if(var->is_mass_balance_quantity() && is_valid(instr.solver)) {
				auto var2 = as<State_Var::Type::declared>(var);
					
				Math_Expr_FT *fun = make_literal((double)0.0);
				
				// aggregation variable for values coming from connection fluxes.
				for(auto target_agg : var2->conn_target_aggs)
					fun = make_binop('+', fun, make_state_var_identifier(target_agg));
				
				for(auto source_agg : var2->conn_source_aggs)
					fun = make_binop('-', fun, make_state_var_identifier(source_agg));
				
				for(Var_Id flux_id : app->vars.all_fluxes()) {
					auto flux = app->vars[flux_id];
					
					if(flux->mixing_base) continue; // Mixing fluxes are not subtracted or added to the base quantity, only the dissolved fluxes count.
					
					if(is_located(flux->loc1) && app->vars.id_of(flux->loc1) == instr.var_id) {
						
						// Subtract the flux from the source unless it was separately subtracted via a source aggregate.
						bool omit = false;
						if(flux->loc2.r1.type != Restriction::none) {
							auto conn_id = flux->loc2.r1.connection_id;
							auto type = model->connections[conn_id]->type;
							if(type == Connection_Type::directed_graph) {
								auto find_source = app->find_connection_component(conn_id, flux->loc1.first(), false);
								// NOTE: Could instead be find_source->max_outgoing_per_node > 1, but it is tricky wrt index set dependencies for the flux.
								omit = find_source && find_source->is_edge_indexed;
							}
						}
						if(flux->loc1.r1.type != Restriction::none) {
							// This is only the case where the source is e.g. .top of a grid1d connection. In that case there is a hack where it is subtracted from the target aggregate of that variable.
							omit = true;
						}
						if(!omit) {
							auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
							fun = make_binop('-', fun, flux_ref);
						}
					}
					
					if(is_located(flux->loc2) && app->vars.id_of(flux->loc2) == instr.var_id && flux->loc2.r1.type == Restriction::none) {
						auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
						// NOTE: the unit conversion applies to what reaches the target.
						if(flux->unit_conversion_tree)
							flux_ref = make_binop('*', flux_ref, copy(flux->unit_conversion_tree.get()));
						fun = make_binop('+', fun, flux_ref);
					}
				}
				instr.code = fun;
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
		} else if (instr.type == Model_Instruction::Type::add_to_parameter_aggregate) {
			
			auto agg_var = as<State_Var::Type::parameter_aggregate>(app->vars[instr.target_id]);
			auto weight = agg_var->aggregation_weight_tree.get();
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_par_ident(app, instr.par_id, weight);
			
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
			
		}
		// Other instr.types are handled differently.
	}
}

Math_Expr_FT *
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, Model_Instruction *context_instr = nullptr);

void
set_grid1d_target_indexes(Model_Application *app, Index_Exprs &indexes, Restriction &res, Math_Expr_FT *specific_target = nullptr) {
	
	auto conn = app->model->connections[res.connection_id];
	if(conn->type != Connection_Type::grid1d)
		fatal_error(Mobius_Error::internal, "Misuse of set_grid1d_target_indexes().");

	auto index_set = conn->node_index_set;

	if (res.type == Restriction::top) {
		indexes.set_index(index_set, make_literal((s64)0));
	} else if(res.type == Restriction::bottom) {
		auto count = app->get_index_count_code(index_set, indexes);
		indexes.set_index(index_set, make_binop('-', count, make_literal((s64)1)));
	} else if(res.type == Restriction::specific) {
		if(!specific_target)
			fatal_error(Mobius_Error::internal, "Wanted to set indexes for a specific connection target, but the code was not provided.");
		indexes.set_index(index_set, specific_target);
		
	} else if(res.type == Restriction::above || res.type == Restriction::below) {
		auto index = indexes.get_index(app, index_set);
		char oper = (res.type == Restriction::above) ? '-' : '+';
		index = make_binop(oper, index, make_literal((s64)1));
		indexes.set_index(index_set, index);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection restriction type.");
}

void
set_grid1d_target_indexes_with_possible_specific(Model_Application *app, Index_Exprs &indexes, Restriction &restriction, Var_Id var_id) {
	// TODO: The specific_target should be stored on the Restriction instead so that we don't need this separate function.
	Math_Expr_FT *specific_target = nullptr;
	if(restriction.type == Restriction::specific) {
		if(!is_valid(var_id) || !app->vars[var_id]->specific_target)
			fatal_error(Mobius_Error::internal, "Did not find @specific code for ", app->vars[var_id]->name, " even though it was expected.");
		
		specific_target = copy(app->vars[var_id]->specific_target.get());
		
		auto index_set = app->model->connections[restriction.connection_id]->node_index_set;
		auto high = make_binop('-', app->get_index_count_code(index_set, indexes), make_literal((s64)1));
		specific_target = make_clamp(specific_target, make_literal((s64)0), high);
		
		
		put_var_lookup_indexes(specific_target, app, indexes);
	}

	set_grid1d_target_indexes(app, indexes, restriction, specific_target);
}

void
set_graph_target_indexes(Model_Application *app, Index_Exprs &indexes, Entity_Id connection_id, Entity_Id source_compartment, Entity_Id target_compartment) {
	
	auto type = app->model->connections[connection_id]->type;
	if(type != Connection_Type::directed_graph)
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
		indexes.set_index(index_set, target_index);
	}
}

Math_Expr_FT *
maybe_apply_back_step(Model_Application *app, Identifier_Data *ident, Math_Expr_FT *offset_code) {
	if(!ident->has_flag(Identifier_FT::last_result)) return offset_code;
	s64 back_step = -1;
	if(ident->variable_type == Variable_Type::series)
		back_step = app->series_structure.total_count;
	else if(ident->variable_type == Variable_Type::state_var && ident->var_id.type == Var_Id::Type::state_var)
		back_step = app->result_structure.total_count;
	
	ident->remove_flag(Identifier_FT::last_result);
	
	if(back_step > 0)
		return make_binop('-', offset_code, make_literal(back_step));
	
	return offset_code;
	// Actually, no, because if this was a state var with @no_store (it is a temp_var) we should still allow it.
	//else
	//	fatal_error(Mobius_Error::internal, "Received a 'last_result' flag on an identifier that should not have one.");
	
}

void
put_var_lookup_indexes_basic(Identifier_FT *ident, Model_Application *app, Index_Exprs &index_expr) {
	Math_Expr_FT *offset_code = nullptr;
	
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = app->parameter_structure.get_offset_code(ident->par_id, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = app->series_structure.get_offset_code(ident->var_id, index_expr);
	} else if(ident->variable_type == Variable_Type::state_var) {
		auto var = app->vars[ident->var_id];
		if(!var->is_valid())
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		if(ident->var_id.type == Var_Id::Type::state_var)
			offset_code = app->result_structure.get_offset_code(ident->var_id, index_expr);
		else
			offset_code = app->temp_result_structure.get_offset_code(ident->var_id, index_expr);
	}
	
	offset_code = maybe_apply_back_step(app, ident, offset_code);
	
	if(ident->flags) { // NOTE: all flags should have been resolved and removed at this point.
		ident->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Forgot to resolve one or more flags on an identifier.");
	}
	
	ident->exprs.push_back(offset_code);
}

bool
give_the_same_condition(Model_Application *app, Var_Loc_Restriction *a, Var_Loc_Restriction *b) {
	// TODO: Also for the secondary restriction r2!
	if(!is_valid(a->r1.connection_id) || !is_valid(b->r1.connection_id)) return false; // The function should not be used in this case, probably.
	
	auto model = app->model;
	if(model->connections[a->r1.connection_id]->type != Connection_Type::grid1d || model->connections[b->r1.connection_id]->type != Connection_Type::grid1d)
		return a->r1.connection_id == b->r1.connection_id;
	if(a->r1.type != b->r1.type) return false;
	return app->model->connections[a->r1.connection_id]->node_index_set == app->model->connections[b->r1.connection_id]->node_index_set;
}


Math_Expr_FT *
make_restriction_condition(Model_Application *app, Math_Expr_FT *value, Math_Expr_FT *alt_val, Var_Loc_Restriction restriction, Index_Exprs &index_expr, Entity_Id source_compartment = invalid_entity_id) {
	
	// TOOD: Also for the secondary restriction r2!
	
	// For grid1d connections, if we look up 'above' or 'below', we can't do it if we are on the first or last index respectively, and so the entire expression must be invalidated.
	// Same if the expression itself is for a flux that is along a grid1d connection and we are at the last index.
	// This function creates the code to compute the boolean condition that the expression should be invalidated.
	auto connection_id = restriction.r1.connection_id;
	
	if(!is_valid(connection_id)) return value;
	
	auto conn = app->model->connections[connection_id];
	auto type = conn->type;

	Math_Expr_FT *new_condition = nullptr;
	
	if(type == Connection_Type::grid1d && (restriction.r1.type == Restriction::above || restriction.r1.type == Restriction::below)) {
		
		auto index_set = conn->node_index_set;
		
		// TODO: This is very wasteful just to get the single index. Could factor out a function
		// for that instead.
		Index_Exprs new_indexes(app->model);
		new_indexes.copy(index_expr);
		set_grid1d_target_indexes(app, new_indexes, restriction.r1);
		Math_Expr_FT *index = new_indexes.get_index(app, index_set);
		
		if(restriction.r1.type == Restriction::above)
			new_condition = make_binop(Token_Type::geq, index, make_literal((s64)0));
		else if(restriction.r1.type == Restriction::below) {
			auto index_count = app->get_index_count_code(index_set, index_expr);
			new_condition = make_binop('<', index, index_count);
		}
	} else if (type == Connection_Type::directed_graph && is_valid(source_compartment)) {
		
		auto *comp = app->find_connection_component(connection_id, source_compartment, false);
		if(comp && comp->is_edge_indexed) {
			// In this case we don't need to check if the flux should be computed, since that is taken care of by the iteration over the arrows.
			new_condition = nullptr;
		} else if(comp && comp->total_as_source > 0) {
			
			auto max_instances = app->index_data.get_instance_count(comp->index_sets);
			if(comp->total_as_source < max_instances) {
				// If the component some times appears as a source, but not always, we have to check for each instance whether or not to evaluate the flux.
				
				auto compartment_id = make_connection_target_identifier(app, index_expr, connection_id, source_compartment);
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
process_is_at(Model_Application *app, Identifier_FT *ident, Index_Exprs &indexes) {
	auto &res = ident->restriction;
	auto index_set = app->model->connections[res.r1.connection_id]->node_index_set;
	
	Math_Expr_FT *check_against = nullptr;
	if(res.r1.type == Restriction::top) {
		check_against = make_literal((s64)0);
	} else if (res.r1.type == Restriction::bottom) {
		check_against = make_binop('-', app->get_index_count_code(index_set, indexes), make_literal((s64)1));  // count-1
	} else {
		ident->source_loc.print_error_header(Mobius_Error::internal);
		fatal_error("Unimplemented codegen for 'is_at' check.");
	}
	
	delete ident;
	
	auto index = indexes.get_index(app, index_set);
	if(!index)
		fatal_error(Mobius_Error::internal, "Missing index set ", app->model->index_sets[index_set]->name, " for is_at instruction.");
	
	return make_binop('=', index, check_against);
}

Math_Expr_FT *
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, Model_Instruction *context_instr) {
	
	for(int idx = 0; idx < expr->exprs.size(); ++idx)
		expr->exprs[idx] = put_var_lookup_indexes(expr->exprs[idx], app, index_expr, context_instr);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return expr;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	if(ident->variable_type == Variable_Type::is_at)
		return process_is_at(app, ident, index_expr);
	
	if(ident->variable_type != Variable_Type::parameter && ident->variable_type != Variable_Type::series && ident->variable_type != Variable_Type::state_var)
		return expr;
	
	if(!expr->exprs.empty())
		fatal_error(Mobius_Error::internal, "Tried to set var lookup indexes on an expr that already has them.");
	
	auto &res = ident->restriction;
	if(res.r1.type != Restriction::none) {
		if(!is_valid(res.r1.connection_id))
			fatal_error(Mobius_Error::internal, "Got an identifier with a restriction but without a connection");
		
		auto connection = app->model->connections[res.r1.connection_id];
		
		if(connection->type == Connection_Type::grid1d) {
			Index_Exprs new_indexes(app->model);
			new_indexes.copy(index_expr);
				
			set_grid1d_target_indexes(app, new_indexes, res.r1);

			put_var_lookup_indexes_basic(ident, app, new_indexes);
			
			// If there is an existing condition, it will enclose the entire expression, so we don't need to check for it again.	
			if(!context_instr || !give_the_same_condition(app, &context_instr->restriction, &res)) {
				return make_restriction_condition(app, ident, make_literal((double)0.0), res, index_expr); // Important not to use new_indexes here..						
			}
			
			return ident;
			
		} else if (connection->type == Connection_Type::directed_graph) {
			
			if(ident->variable_type == Variable_Type::parameter) {
				
				bool valid = false;
				if(context_instr && context_instr->type == Model_Instruction::Type::add_to_connection_aggregate) {
					auto flux_var = app->vars[context_instr->var_id];
					auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[context_instr->target_id]);
					auto agg_for = app->vars[agg_var->agg_for];
					
					if(!agg_var->is_source && flux_var->loc1 == agg_for->loc1) {
						valid = true;
						
						Entity_Id target_comp = agg_for->loc1.first();
						Index_Exprs new_indexes(app->model);
						new_indexes.copy(index_expr);
						set_graph_target_indexes(app, new_indexes, res.r1.connection_id, target_comp, target_comp);
						
						put_var_lookup_indexes_basic(ident, app, new_indexes);
					}
				}
				if(!valid) {
					// TODO: Also allow it for graphs in general as long as it only has one component type (but in that case it must be 0-ed on an 'out'.
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("A 'below' indexing can only be applied to a parameter in a context where it can be guaranteed that the source and target of the flux are the same node type of the graph.");
				}
				return ident;
					
			} else {
			
				if(ident->variable_type != Variable_Type::series && ident->variable_type != Variable_Type::state_var) {
					ident->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("On a directed_graph connection, 'below' can only be used on a state variable, series or parameter.");
				}
				
				bool is_conc;
				auto loc0 = app->get_primary_location(ident->var_id, is_conc);
				
				auto source_comp = loc0.first();
				
				if(!is_valid(source_comp))
					fatal_error(Mobius_Error::internal, "Did not properly set the source compartment of the expression.");
				
				// We have to check for each possible target of the graph.
				auto component = app->find_connection_component(res.r1.connection_id, source_comp, false);
				
				if(!component) {
					ident->source_loc.print_error_header(Mobius_Error::internal);
					fatal_error("Did not find the component for this expression. Connection is ", connection->name, ", component is ", app->model->components[source_comp]->name, ".");
				}
				
				auto if_expr = new Math_Expr_FT(Math_Expr_Type::if_chain);
				if_expr->value_type = expr->value_type;
				
				// TODO: Should maybe move the unit match check to model_composition.
				// There is something tricky wrt. conc() lookups for bidirectional fluxes also, esp. if there is a unit_conversion or an aggregation_weight.
				auto unit = &app->vars[ident->var_id]->unit.standard_form;
				
				for(auto target_comp : component->possible_targets) {
					
					if(!is_valid(target_comp)) continue; // This happens if it is an 'out'. In that case it will be caught by the "otherwise" clause below (the value is 0).
					
					auto target_id = app->get_connection_target_variable(loc0, target_comp, is_conc);
					if(!is_valid(target_id)) {
						// TODO: Having this give an error is annoying. But maybe make it give a warning in developer mode?
						// TODO: Maybe we should do this validity check in model_composition already, before we do the codegen.
						
						/*
						ident->source_loc.print_error_header();
						fatal_error("The variable \"", app->vars[ident->var_id]->name, "\"does not exist for the compartment \"", app->model->components[target_comp]->name, "\", and so can't be referenced along every edge starting in this node.");
						*/
						continue;
					}
					
					auto target_var = app->vars[target_id];
					auto unit2 = &target_var->unit.standard_form;
					if(!match_exact(unit, unit2)) {
						ident->source_loc.print_error_header();
						fatal_error("The unit of this identifier does not match the unit of the variable \"", target_var->name, "\" which this 'below' lookup could point at over the 'directed_graph'.");
					}
						
					// TODO: Is there any reason to copy it instead of just modifying it?
					auto ident2 = static_cast<Identifier_FT *>(copy(ident));
					ident2->var_id = target_id;
					
					Index_Exprs new_indexes(app->model);
					new_indexes.copy(index_expr);
					set_graph_target_indexes(app, new_indexes, res.r1.connection_id, source_comp, target_comp);
					if(res.r2.type != Restriction::none) {
						auto connection = app->model->connections[res.r2.connection_id];
						if(connection->type != Connection_Type::grid1d)
							fatal_error(Mobius_Error::internal, "Got a secondary restriction for an identifier that was not a grid1d.");
						auto context_id = context_instr ? context_instr->var_id : invalid_var;
						set_grid1d_target_indexes_with_possible_specific(app, new_indexes, res.r2, context_id); // TODO: The specific code should be stored directly on the restriction itself so that we don't have to pass the context var.
					}
					put_var_lookup_indexes_basic(ident2, app, new_indexes);
					
					if_expr->exprs.push_back(ident2);
					
					// The condition that the expression resolves to the ident2 value.
					// Note here we use the index_expr belonging to the source compartment, not the
					// one belonging to the target compartment
					auto condition = make_connection_target_check(app, index_expr, res.r1.connection_id, source_comp, target_comp);
					if_expr->exprs.push_back(condition);
				}
				
				delete ident;
				
				// This is the 'otherwise' case which happens if the graph points at 'out' or doesn't point anywhere, or if the variable doesn't exist in the target.
				if_expr->exprs.push_back(make_literal((double)0.0));
				
				return if_expr;
			}
		} else
			fatal_error(Mobius_Error::internal, "Unimplemented connection type in put_var_lookup_indexes()");
		
	}

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
add_value_to_graph_agg(Model_Application *app, Math_Expr_FT *value, Var_Id var_id, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Restriction &restriction, Restriction &r2) {
	
	auto model = app->model;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	
	if(target_agg->is_source) {
		auto agg_offset = app->get_storage_structure(agg_id.type).get_offset_code(agg_id, indexes);
		return add_value_to_state_var(agg_id, agg_offset, value, '+');
	}
	
	// Hmm, these two lookups are very messy. See also similar in model_compilation a couple of places
	auto source_compartment = app->vars[source_id]->loc1.components[0];
	auto target_compartment = app->vars[target_agg->agg_for]->loc1.components[0];
	
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	set_graph_target_indexes(app, new_indexes, restriction.connection_id, source_compartment, target_compartment);
	
	if(r2.type != Restriction::none && !target_agg->is_source) {
			
		// TODO: Should be checked at an earlier stage instead of here.
		auto conn2 = model->connections[r2.connection_id];
		if(conn2->type != Connection_Type::grid1d) {
			model->fluxes[as<State_Var::Type::declared>(app->vars[var_id])->decl_id]->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Currently we only support if the type of a connection in a secondary bracket is grid1d.");
		}

		set_grid1d_target_indexes_with_possible_specific(app, new_indexes, r2, var_id);
	}
	
	auto agg_offset = app->get_storage_structure(agg_id.type).get_offset_code(agg_id, new_indexes);
	
	auto condition = make_connection_target_check(app, indexes, restriction.connection_id, source_compartment, target_compartment);
	
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
add_value_to_grid1d_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id flux_id, bool subtract, Index_Exprs &indexes, Restriction restriction) {
	
	auto model = app->model;

	// NOTE: This is a bit of a hack. We should maybe have a source aggregate here instead, but that can cause a lot of unnecessary work and memory use for the model.
	char oper = subtract ? '-' : '+';
	
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	
	set_grid1d_target_indexes_with_possible_specific(app, new_indexes, restriction, flux_id);
	
	auto agg_offset = app->get_storage_structure(agg_id.type).get_offset_code(agg_id, new_indexes);

	auto result = add_value_to_state_var(agg_id, agg_offset, value, oper);
	
	return result;
}

Math_Expr_FT *
add_value_to_connection_agg_var(Model_Application *app, Math_Expr_FT *value, Model_Instruction *instr, Index_Exprs &indexes) {
	auto model = app->model;
	
	auto &restriction = instr->restriction;
	auto agg_id = instr->target_id;
	
	// TODO: This is only needed if we have a secondary restriction. A bit wasteful..
	Index_Exprs new_indexes(model);
	new_indexes.copy(indexes);
	
	auto type = model->connections[restriction.r1.connection_id]->type;
	
	if(type == Connection_Type::directed_graph) {
		
		return add_value_to_graph_agg(app, value, instr->var_id, agg_id, instr->source_id, new_indexes, restriction.r1, restriction.r2);
		
	} else if (type == Connection_Type::grid1d) {
		
		return add_value_to_grid1d_agg(app, value, agg_id, instr->var_id, instr->subtract, new_indexes, restriction.r1);
		
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection type in add_value_to_connection_agg_var()");
	
	return nullptr;
}

Math_Expr_FT *
create_nested_for_loops(Math_Block_FT *top_scope, Model_Application *app, Index_Set_Tuple &index_sets, Index_Exprs &indexes) {
	
	Math_Block_FT *scope = top_scope;

	for(auto index_set : index_sets) {
		
		auto loop = make_for_loop();
		// NOTE: There is a caveat here: This will only work if the parent index of a sub-indexed index set is always set up first,
		//   and that  *should* work the way we order the Entity_Id's of those right now, but it is a smidge volatile...
		auto index_count = app->get_index_count_code(index_set, indexes);
		loop->exprs.push_back(index_count);
		scope->exprs.push_back(loop);
		
		// NOTE: this is a reference to the iterator of the for loop.
		indexes.set_index(index_set, make_local_var_reference(0, loop->unique_block_id, Value_Type::integer));
		
		scope = loop;
	}
	
	auto body = new Math_Block_FT();
	scope->exprs.push_back(body);
	scope = body;
	
	return scope;
}

Math_Expr_FT*
generate_external_computation_code(Model_Application *app, External_Computation_FT *ext, Index_Exprs &indexes, bool copy = false,
	Entity_Id connection_id = invalid_entity_id, Entity_Id source_compartment = invalid_entity_id, Entity_Id target_compartment = invalid_entity_id) {
	
	auto model = app->model;
	auto external = ext;
	if(copy)
		external = static_cast<External_Computation_FT *>(::copy(ext));
	
	for(auto &arg : external->arguments) {
		
		// TODO: Again, copy is only necessary if we have a restriction... Make a system to avoid it when not necessary.
		Index_Exprs new_indexes(model);
		new_indexes.copy(indexes);
		
		Var_Id target_id = arg.var_id;
		
		if(arg.restriction.r1.type != Restriction::none) {
			if(
				!is_valid(source_compartment) || 
				!is_valid(target_compartment) ||
				arg.restriction.r1.type != Restriction::below || 
				arg.restriction.r1.connection_id != connection_id ||
				arg.variable_type != Variable_Type::state_var
			)
				fatal_error(Mobius_Error::internal, "Something went wrong with configuring an external_computation over a connection.");
			
			auto conn = model->connections[connection_id];
			if(conn->type == Connection_Type::directed_graph)
				set_graph_target_indexes(app, new_indexes, connection_id, source_compartment, target_compartment);
			else
				fatal_error(Mobius_Error::internal, "Unimplemented external computation codegen for var loc restriction that is not directed_graph.");
			
			target_id = app->get_connection_target_variable(target_id, connection_id, target_compartment);
			if(!is_valid(target_id)) {
				ext->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("Unable to access the variable \"", app->vars[arg.var_id]->name, "\" through the connection \"", model->connections[connection_id]->name, "\".");
			}
			//TODO: Not sure if this is safe or if we should store it separately: This is to make a check in llvm_jit correct.
			arg.var_id = target_id;
		}
		
		Offset_Stride_Code res;
		if(arg.variable_type == Variable_Type::state_var) {
			res = app->get_storage_structure(target_id.type).get_special_offset_stride_code(target_id, new_indexes);
		} else if(arg.variable_type == Variable_Type::parameter)
			res = app->parameter_structure.get_special_offset_stride_code(arg.par_id, new_indexes);
		else
			fatal_error(Mobius_Error::internal, "Unrecognized variable type in external computation codegen.");
		
		res.offset = maybe_apply_back_step(app, &arg, res.offset);
		
		// Ooops, can't do this since we don't remove 'result' (I think ?)
		/*
		if(arg.flags) { // NOTE: all flags should have been resolved and removed at this point.
			ext->source_loc.print_error_header(Mobius_Error::internal);
			fatal_error("Forgot to resolve one or more flags on an identifier.");
		}
		*/
		
		external->exprs.push_back(res.offset);
		external->exprs.push_back(res.stride);
		external->exprs.push_back(res.count);
	}
	
	return external;
}



Math_Expr_FT *
generate_run_code(Model_Application *app, Batch *batch, std::vector<Model_Instruction> &instructions, bool initial) {
	auto model = app->model;
	auto top_scope = new Math_Block_FT();
	
	Index_Exprs indexes(app->model);
	
	for(auto &array : batch->arrays) {
		
		Math_Expr_FT *scope = create_nested_for_loops(top_scope, app, array.index_sets, indexes);
		
		for(int instr_id : array.instr_ids) {
			
			auto &instr = instructions[instr_id];
			
			Math_Expr_FT *fun = nullptr;
			
			try {
				if(instr.code) {
					fun = copy(instr.code);
					fun = put_var_lookup_indexes(fun, app, indexes, &instr);
				} else if (instr.type != Model_Instruction::Type::clear_state_var) {
					//NOTE: Some instructions are placeholders that give the order of when a value is 'ready' for use by other instructions, but they are not themselves computing the value they are placeholding for. This for instance happens with aggregation variables that are computed by other add_to_aggregate instructions. So it is OK that their 'fun' is nullptr.
					
					// TODO: Should we try to infer if it is ok that there is no code for this compute_state_var (?). Or maybe have a separate type for it when we expect there to be no actual computation and it is only a placeholder for a value (?). Or maybe have a flag for it on the State_Variable.
					if(instr.type == Model_Instruction::Type::compute_state_var)
						continue;
					fatal_error(Mobius_Error::internal, "Unexpectedly missing code for a model instruction. Type: ", (int)instr.type, ".");
				}
			} catch(int) {
				fatal_error("The error happened when trying to put lookup indexes for the instruction ", instr.debug_string(app), initial ? " during the initial value step." : ".");
			}
				
			Math_Expr_FT *result_code = nullptr;
			
			if(instr.type == Model_Instruction::Type::compute_state_var) {
				/*auto structure = app->get_storage_structure(instr.var_id.type);
				if(structure.handle_is_in_array.find(instr.var_id) == structure.handle_is_in_array.end()) {
					begin_error(Mobius_Error::internal);
					error_print(instr.debug_string(app), " was not registered in the storage structure. initial: ", initial, ". The following are in the structure: \n");
					for(auto &pair : structure.handle_is_in_array) {
						error_print(app->vars[pair.first]->name, "\n");
					}
					mobius_error_exit();
				}*/
				
				auto offset_code = app->get_storage_structure(instr.var_id.type).get_offset_code(instr.var_id, indexes);
				auto assignment = new Assignment_FT(Math_Expr_Type::state_var_assignment, instr.var_id);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				assignment->value_type = Value_Type::none;
				result_code = assignment;
				
			} else if (instr.type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
				
				// TODO: Just make an app->get_offset_code(Var_Id, Index_Exprs &)
				auto agg_offset = app->get_storage_structure(instr.source_id.type).get_offset_code(instr.source_id, indexes);
				result_code = add_value_to_state_var(instr.source_id, agg_offset, fun, '-');
				
			} else if (instr.type == Model_Instruction::Type::add_discrete_flux_to_target) {
				
				auto agg_offset = app->get_storage_structure(instr.target_id.type).get_offset_code(instr.target_id, indexes);
				result_code = add_value_to_state_var(instr.target_id, agg_offset, fun, '+');
				
			} else if (instr.type == Model_Instruction::Type::add_to_aggregate
				|| instr.type == Model_Instruction::Type::add_to_parameter_aggregate) {
				
				auto agg_offset = app->get_storage_structure(instr.target_id.type).get_offset_code(instr.target_id, indexes);
				result_code = add_value_to_state_var(instr.target_id, agg_offset, fun, '+');
				
			} else if (instr.type == Model_Instruction::Type::add_to_connection_aggregate) {
				
				result_code = add_value_to_connection_agg_var(app, fun, &instr, indexes);
				
			} else if (instr.type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = app->get_storage_structure(instr.var_id.type).get_offset_code(instr.var_id, indexes);
				auto assignment = new Assignment_FT(Math_Expr_Type::state_var_assignment, instr.var_id);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((double)0.0));
				result_code = assignment;
				
			} else if (instr.type == Model_Instruction::Type::external_computation) {
				
				auto external = static_cast<External_Computation_FT *>(fun);

				if(is_valid(external->connection)) {
					auto source_comp = app->find_connection_component(external->connection, external->connection_component, false);
					
					if(!source_comp || !source_comp->can_be_located_source) {
						log_print("Disabling external computation \"", external->function_name, "\" due to a connection lookup over a non-existing edge.\n");
						log_print("The source compartmet was ", model->components[external->connection_component]->name, "\n");
						delete fun;
						result_code = make_no_op();
					} else {
						
						auto if_expr = new Math_Expr_FT(Math_Expr_Type::if_chain);
						if_expr->value_type = Value_Type::none;
						
						for(auto target : source_comp->possible_targets) {
							if(!is_valid(target)) continue;
							
							auto target_case = generate_external_computation_code(app, external, indexes, true, external->connection, external->connection_component, target);
							auto target_condition = make_connection_target_check(app, indexes, external->connection, external->connection_component, target);
							if_expr->exprs.push_back(target_case);
							if_expr->exprs.push_back(target_condition);
						}
						if_expr->exprs.push_back(make_no_op());
						
						delete fun; // We copied it above.
						result_code = if_expr;
					}
				} else {
					generate_external_computation_code(app, external, indexes);
					result_code = external;
				}
				
			} else {
				fatal_error(Mobius_Error::internal, "Unimplemented instruction type in code generation.");
			}
			
			if(is_valid(instr.restriction.r1.connection_id)) {
				
				Entity_Id source_comp = invalid_entity_id;
				//TODO: Could this be organized in a better way so that we don't need this lookup?
				if(model->connections[instr.restriction.r1.connection_id]->type == Connection_Type::directed_graph)
					source_comp = app->vars[instr.var_id]->loc1.first();
				
				result_code = make_restriction_condition(app, result_code, make_no_op(), instr.restriction, indexes, source_comp);
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
				auto &instr = instructions[instr_id];
				
				if(instr.type != Model_Instruction::Type::compute_state_var)
					fatal_error(Mobius_Error::internal, "Somehow we got an instruction that is not a state var computation inside an ODE batch.\n");
				
				// NOTE: the code for an ode variable computes the derivative of the state variable, which is all ingoing fluxes minus all outgoing fluxes
				auto fun = instr.code;
				
				if(!fun)
					fatal_error(Mobius_Error::internal, "ODE variables should always be provided with generated code in instruction_codegen, but we got one without.");
				
				fun = copy(fun);
				
				put_var_lookup_indexes(fun, app, indexes);
				
				auto offset_var = app->result_structure.get_offset_code(instr.var_id, indexes);
				auto offset_deriv = make_binop('-', offset_var, make_literal(init_pos));
				auto assignment = new Assignment_FT(Math_Expr_Type::derivative_assignment, instr.var_id);
				assignment->exprs.push_back(offset_deriv);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
			}
			
			indexes.clean();
		}
	
	}
#if 0
	std::stringstream ss;
	//ss << "\nTree before prune:\n";
	//print_tree(app, top_scope, ss);
	//ss << "\n";
	//auto result = top_scope;
	auto result = prune_tree(top_scope);
	ss << "\nTree after prune:\n";
	print_tree(app, result, ss);
	ss << "\n";
	log_print(ss.str());
#else
	//auto result = top_scope;
	auto result = prune_tree(top_scope);
#endif
	
	return result;
}
