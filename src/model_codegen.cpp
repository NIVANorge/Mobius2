
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
				Math_Expr_FT *flux_sum = make_literal(0.0);
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
			
			// Codegen for the derivative of state variables:
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
						//if(!flux->is_valid() || !flux->is_flux()) continue;
						
						// NOTE: In the case of an all-to-all connection case we have set up an aggregation variable also for the source, so we already subtract using that.
						// TODO: We could consider always having an aggregation variable for the source even when the source is always just one instace just to get rid of all the special cases (?).
						
						auto &restriction = restriction_of_flux(flux);
						bool is_bottom      = (restriction.restriction == Var_Loc_Restriction::bottom);
						bool is_all_to_all  = is_valid(restriction.connection_id) && (model->connections[restriction.connection_id]->type == Connection_Type::all_to_all);
						
						// NOTE: For bottom fluxes there is a special hack where they are subtracted from the target agg variable. Hopefully we get a better solution.
						
						if(is_located(flux->loc1) && app->vars.id_of(flux->loc1) == instr.var_id
							&& !is_all_to_all && !is_bottom) {

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
			
			// Note weights are applied directly inside the codegen for this one. TODO: do that for the others too?
			
			auto agg_var = app->vars[instr.target_id];
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id);
			
		} else if(instr.type == Model_Instruction::Type::special_computation) {
			
			// Taken care of elsewhere.
		}
	}
}

void
get_grid1d_target_indexes(Model_Application *app, std::vector<Math_Expr_FT *> &target_indexes, Index_Exprs &indexes, Var_Loc_Restriction restriction) {
	if(app->model->connections[restriction.connection_id]->type != Connection_Type::grid1d)
		fatal_error(Mobius_Error::internal, "Misuse of get_grid1d_target_indexes().");
	auto &comp = app->connection_components[restriction.connection_id.id][0];
	auto index_set = comp.index_sets[0];
	
	bool is_boundary = (restriction.restriction == Var_Loc_Restriction::top || restriction.restriction == Var_Loc_Restriction::bottom);
	bool is_above = (restriction.restriction == Var_Loc_Restriction::top || restriction.restriction == Var_Loc_Restriction::above);
	
	target_indexes.resize(app->model->index_sets.count(), nullptr);
	
	if(is_boundary) {
		if (is_above)
			target_indexes[index_set.id] = make_literal((s64)0);
		else {
			auto count = get_index_count_code(app, index_set, indexes);
			target_indexes[index_set.id] = make_binop('-', count, make_literal((s64)1));
		}
	} else {
		auto index = copy(indexes.indexes[index_set.id]);
		char oper = is_above ? '-' : '+';
		index = make_binop(oper, index, make_literal((s64)1));
		target_indexes[index_set.id] = index;
	}
}

void
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, std::vector<Math_Expr_FT *> *provided_target_idx, std::set<Var_Loc_Restriction> &found_restriction) {
	
	for(auto arg : expr->exprs)
		put_var_lookup_indexes(arg, app, index_expr, provided_target_idx, found_restriction);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	std::vector<Math_Expr_FT *> target_indexes;
	
	found_restriction.insert(ident->restriction);
	
	// TODO: Should factor out the swapping.
	// TODO: Swapping back and forth is very bug prone. Should maybe just make a copy that is modified?
	
	Connection_Type conn_type;
	if(ident->restriction.restriction == Var_Loc_Restriction::above || ident->restriction.restriction == Var_Loc_Restriction::below) {
		auto conn = app->model->connections[ident->restriction.connection_id];
		conn_type = conn->type;
		if(conn_type == Connection_Type::all_to_all) {
			index_expr.transpose();
			if(ident->restriction.restriction == Var_Loc_Restriction::above) {
				ident->source_loc.print_error_header();
				fatal_error("The 'above' directive is not allowed on a all_to_all connection");
			}
		} else if (conn_type == Connection_Type::grid1d) {
			if(!provided_target_idx) {
				get_grid1d_target_indexes(app, target_indexes, index_expr, ident->restriction);
				provided_target_idx = &target_indexes;
			}
			index_expr.swap(*provided_target_idx);
		} else {
			ident->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Got a 'above' or 'below' directive for a connection \"", conn->name, "\" that is not of type all_to_all or grid1d.");
		}
	} else if(ident->restriction.restriction == Var_Loc_Restriction::top || ident->restriction.restriction == Var_Loc_Restriction::bottom) {
		auto conn = app->model->connections[ident->restriction.connection_id];
		conn_type = conn->type;
		if(conn_type != Connection_Type::grid1d) {
			ident->source_loc.print_error_header(Mobius_Error::model_building);
			fatal_error("Got a 'top' or 'bottom' directive for a connection \"", conn->name, "\" that is not of type grid1d.");
		}
		
		// TODO: Why isn't this swapped back??
		
		get_grid1d_target_indexes(app, target_indexes, index_expr, ident->restriction);
		provided_target_idx = &target_indexes;
		index_expr.swap(*provided_target_idx);
	}
	
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = app->parameter_structure.get_offset_code(ident->par_id, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = app->series_structure.get_offset_code(ident->var_id, index_expr);
		back_step = app->series_structure.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		auto var = app->vars[ident->var_id];
		if(var->flags & State_Var::Flags::invalid)
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		offset_code = app->result_structure.get_offset_code(ident->var_id, index_expr);
		back_step = app->result_structure.total_count;
	}
	
	if(ident->restriction.restriction == Var_Loc_Restriction::below || ident->restriction.restriction == Var_Loc_Restriction::above) {
		if(conn_type == Connection_Type::all_to_all)
			index_expr.transpose();
		else
			index_expr.swap(*provided_target_idx);
	}
	
	if(offset_code && ident->variable_type != Variable_Type::parameter && (ident->flags & Identifier_FT::Flags::last_result)) {
		offset_code = make_binop('-', offset_code, make_literal(back_step));
	}
	
	if(offset_code)
		expr->exprs.push_back(offset_code);
	
	return;
}

void
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, std::vector<Math_Expr_FT *> *provided_target_idx = nullptr) {
		
	std::set<Var_Loc_Restriction> found_restriction;
	put_var_lookup_indexes(expr, app, index_expr, provided_target_idx, found_restriction);
}

Math_Expr_FT *
make_restriction_condition(Model_Application *app, Var_Loc_Restriction restriction, Math_Expr_FT *existing_condition, Index_Exprs &index_expr) {
	
	// For grid1d connections, if we look up 'above' or 'below', we can't do it if we are on the first or last index respecitively, and so the entire expression must be invalidated.
	// Same if the expression itself is for a flux that is along a grid1d connection.
	// This function creates the code to compute the boolean condition that the expression should be invalidated.
	
	if(!is_valid(restriction.connection_id) || app->model->connections[restriction.connection_id]->type != Connection_Type::grid1d)
		return existing_condition;
	if(restriction.restriction != Var_Loc_Restriction::above && restriction.restriction != Var_Loc_Restriction::below)
		return existing_condition;
	
	auto index_set = app->get_single_connection_index_set(restriction.connection_id);
	
	std::vector<Math_Expr_FT *> target_indexes;
	get_grid1d_target_indexes(app, target_indexes, index_expr, restriction);
	Math_Expr_FT *index = target_indexes[index_set.id];
	
	Math_Expr_FT *ltc = nullptr;
	if(restriction.restriction == Var_Loc_Restriction::above)
		ltc = make_binop(Token_Type::geq, copy(index), make_literal((s64)0));
	else if(restriction.restriction == Var_Loc_Restriction::below) {
		auto index_count = get_index_count_code(app, index_set, index_expr);
		ltc = make_binop('<', copy(index), index_count);
	}
	
	if(!existing_condition)
		return ltc;
	else
		return make_binop('&', existing_condition, ltc);
}

Math_Expr_FT *
add_value_to_state_var(Var_Id target_id, Math_Expr_FT *target_offset, Math_Expr_FT *value, char oper) {
	auto target_ident = make_state_var_identifier(target_id);
	target_ident->exprs.push_back(target_offset);
	auto sum_or_difference = make_binop(oper, target_ident, value);
	
	auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
	assignment->exprs.push_back(copy(target_offset));
	assignment->exprs.push_back(sum_or_difference);
	assignment->value_type = Value_Type::none;
	return assignment;
}

Math_Expr_FT *
add_value_to_tree_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Entity_Id connection_id, Math_Expr_FT *weight, Math_Expr_FT *unit_conv) {
	// TODO: Maybe refactor this so that it doesn't have code from different use cases mixed this much.

	auto model = app->model;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	
	// Hmm, these two lookups are very messy. See also similar in model_compilation a couple of places
	Entity_Id source_compartment = app->vars[source_id]->loc1.components[0];
	auto target_compartment = app->vars[target_agg->agg_for]->loc1.components[0];
	auto find_target = app->find_connection_component(connection_id, target_compartment);
	
	Math_Expr_FT *agg_offset = nullptr;
	
	if(find_target->index_sets.size() > 0) {
		std::vector<Math_Expr_FT *> target_indexes(model->index_sets.count(), nullptr);
		for(int idx = 0; idx < find_target->index_sets.size(); ++idx) {
			int id = idx+1;
			auto index_set = find_target->index_sets[idx];
			// NOTE: we create the formula to look up the index of the target, but this is stored using the indexes of the source.
			auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, id}, indexes);
			auto target_index = new Identifier_FT();
			target_index->variable_type = Variable_Type::connection_info;
			target_index->value_type = Value_Type::integer;
			target_index->exprs.push_back(idx_offset);
			target_indexes[index_set.id] = target_index;
		}
		indexes.swap(target_indexes); // Set the indexes of the target compartment for looking up the target
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		indexes.swap(target_indexes); // Swap back in the ones we had before.
		
		if(weight)
			put_var_lookup_indexes(weight, app, indexes, &target_indexes);
		
		for(int idx = 0; idx < target_indexes.size(); ++idx) // NOTE: If they were used, they were copied, so we delete them again now.
			delete target_indexes[idx];
	} else {
		if(weight) put_var_lookup_indexes(weight, app, indexes);
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	}
	
	if(unit_conv)
		put_var_lookup_indexes(unit_conv, app, indexes);
	
	//warning_print("*** *** Codegen for connection ", app->vars[source_id]->name, " to ", app->vars[target_agg->connection_agg]->name, " using agg var ", app->vars[agg_id]->name, "\n");
	
	// Code for looking up the id of the target compartment of the current source.
	auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, 0}, indexes);	// the 0 is because the compartment id is stored at info id 0
	auto compartment_id = new Identifier_FT();
	compartment_id->variable_type = Variable_Type::connection_info;
	compartment_id->value_type = Value_Type::integer;
	compartment_id->exprs.push_back(idx_offset);
	
	// There can be multiple valid target components for the connection, so we have to make code to see if the value should indeed be added to this aggregation variable.
	// (even if there could only be one valid target compartment, this makes sure that the set target is not -1 (i.e. nowhere)).
	Math_Expr_FT *condition = make_binop('=', compartment_id, make_literal((s64)target_compartment.id));
	
	auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
	if_chain->value_type = Value_Type::none;
	
	if(weight)
		value = make_binop('*', value, weight);
	if(unit_conv)
		value = make_binop('*', value, unit_conv);
	
	if_chain->exprs.push_back(add_value_to_state_var(agg_id, agg_offset, value, '+'));
	if_chain->exprs.push_back(condition);
	if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
	
	Math_Expr_FT *result = if_chain;
	
	return result;
}

Math_Expr_FT *
add_value_to_all_to_all_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Entity_Id connection_id, Math_Expr_FT *weight) {
	
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);

	Math_Expr_FT *agg_offset = nullptr;
	
	if(agg_var->is_source)
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	else {
		// We have to "transpose the matrix" so that we add this to the target instead corresponding to the index pair instead of the source.
		indexes.transpose();
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		indexes.transpose();
	}
	
	if(weight) {
		put_var_lookup_indexes(weight, app, indexes, nullptr);
		value = make_binop('*', value, weight);
	}
	
	return add_value_to_state_var(agg_id, agg_offset, value, '+'); // NOTE: it is a + regardless, since the subtraction happens explicitly when we use the value later.
}

Math_Expr_FT *
add_value_to_grid1d_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Math_Expr_FT *weight, Var_Loc_Restriction restriction) {
	
	auto model = app->model;
	
	std::vector<Math_Expr_FT *> target_indexes;
	// NOTE: This is a bit of a hack. We should maybe have a source aggregate here instead, but that can cause a lot of unnecessary work and memory use for the model.
	if(restriction.restriction == Var_Loc_Restriction::bottom)
		value = make_unary('-', value);
	
	get_grid1d_target_indexes(app, target_indexes, indexes, restriction);
	
	indexes.swap(target_indexes);
	auto agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	indexes.swap(target_indexes);
	
	if(weight) {
		put_var_lookup_indexes(weight, app, indexes, &target_indexes);
		value = make_binop('*', value, weight);
	}
	//TODO: We don't free the target_indexes!
	// Same goes for other places where we use get_grid1d_target_indexes() !
	auto result = add_value_to_state_var(agg_id, agg_offset, value, '+');
	
	return result;
}

Math_Expr_FT *
add_value_to_connection_agg_var(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Var_Loc_Restriction restriction) {
	auto model = app->model;
	
	auto connection = model->connections[restriction.connection_id];
	
	// See if we should apply a weight and/or unit conversion to the value.
	
	Math_Expr_FT *weight = nullptr;
	Math_Expr_FT *unit_conv = nullptr;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	for(auto &data : target_agg->conversion_data) {
		if(data.source_id == source_id) {
			weight    = data.weight.get();
			unit_conv = data.unit_conv.get();
			break;
		}
	}
	if(weight) weight = copy(weight);
	if(unit_conv) unit_conv = copy(unit_conv);
	
	if(connection->type == Connection_Type::directed_tree) {
		
		return add_value_to_tree_agg(app, value, agg_id, source_id, indexes, restriction.connection_id, weight, unit_conv);
		
	} else if (connection->type == Connection_Type::all_to_all) {
		
		return add_value_to_all_to_all_agg(app, value, agg_id, indexes, restriction.connection_id, weight);
		
	} else if (connection->type == Connection_Type::grid1d) {
		
		return add_value_to_grid1d_agg(app, value, agg_id, indexes, weight, restriction);
		
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection type in add_value_to_connection_agg_var()");
	
	return nullptr;
}

// TODO: This could maybe be a member method of Index_Exprs
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
			scope->exprs.push_back(loop);
			index_expr.mat_col = make_local_var_reference(0, loop->unique_block_id, Value_Type::integer);
			index_expr.mat_index_set = index_set->id;
			
			scope = loop;
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
			
			std::set<Var_Loc_Restriction> restrictions;
			restrictions.insert(instr->restriction);
			
			if(instr->code) {
				fun = copy(instr->code);
				
				put_var_lookup_indexes(fun, app, indexes, nullptr, restrictions);
				
			} else if (instr->type != Model_Instruction::Type::clear_state_var) {
				//NOTE: Some instructions are placeholders that give the order of when a value is 'ready' for use by other instructions, but they are not themselves computing the value they are placeholding for. This for instance happens with aggregation variables that are computed by other add_to_aggregate instructions. So it is OK that their 'fun' is nullptr.
				
				// TODO: Should we try to infer if it is ok that there is no code for this compute_state_var (?). Or maybe have a separate type for it when we expect there to be no actual computation and it is only a placeholder for a value (?). Or maybe have a flag for it on the State_Variable.
				if(instr->type == Model_Instruction::Type::compute_state_var)
					continue;
				fatal_error(Mobius_Error::internal, "Unexpectedly missing code for a model instruction. Type: ", (int)instr->type, ".");
			}
			
			
			Math_Expr_FT *result_code = nullptr;
			
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				
				auto offset_code = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
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
				
				result_code = add_value_to_connection_agg_var(app, fun, instr->target_id, instr->source_id, indexes, instr->restriction);
				
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((double)0));
				result_code = assignment;
				
			} else if (instr->type == Model_Instruction::Type::special_computation) {
				
				auto special = static_cast<Special_Computation_FT *>(instr->code);
				// TODO: Have to set strides and indexing information eventually
				special->exprs.push_back(make_literal(app->result_structure.get_offset_base(special->target)));
				special->exprs.push_back(make_literal(app->result_structure.get_stride(special->target)));
				// TODO: If when we specifically index over something, we should let this be a index count, not the instance count
				special->exprs.push_back(make_literal(app->result_structure.instance_count(special->target)));
				
				for(auto &arg : special->arguments) {
					if(arg.variable_type == Variable_Type::state_var) {
						special->exprs.push_back(make_literal(app->result_structure.get_offset_base(arg.var_id)));
						special->exprs.push_back(make_literal(app->result_structure.get_stride(arg.var_id)));
						special->exprs.push_back(make_literal(app->result_structure.instance_count(arg.var_id)));
					} else if(arg.variable_type == Variable_Type::parameter) {
						special->exprs.push_back(make_literal(app->parameter_structure.get_offset_base(arg.par_id)));
						special->exprs.push_back(make_literal(app->parameter_structure.get_stride(arg.par_id)));
						special->exprs.push_back(make_literal(app->parameter_structure.instance_count(arg.par_id)));
					}
				}
				result_code = special;
				
			} else {
				fatal_error(Mobius_Error::internal, "Unimplemented instruction type in code generation.");
			}
			
			// TODO: In some cases there are duplicates of a restriction that produces the same condition. How does this happen?
			Math_Expr_FT *condition = nullptr;
			for(auto &restriction : restrictions)
				condition = make_restriction_condition(app, restriction, condition, indexes);
			
			if(condition) {
				auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
				if_chain->value_type = Value_Type::real;
				if_chain->exprs.push_back(result_code);
				
				if_chain->exprs.push_back(condition);
				if_chain->exprs.push_back(make_literal(0.0));
				
				result_code = if_chain;
			}
			
			scope->exprs.push_back(result_code);
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
				auto assignment = new Math_Expr_FT(Math_Expr_Type::derivative_assignment);
				assignment->exprs.push_back(offset_deriv);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
			}
			
			indexes.clean();
		}
	
	}
	/*
	warning_print("\nTree before prune:\n");
	std::stringstream ss;
	print_tree(app, top_scope, ss);
	warning_print(ss.str());
	warning_print("\n");
	*/
	auto result = prune_tree(top_scope);
	/*
	warning_print("\nTree after prune:\n");
	std::stringstream ss;
	print_tree(app, result, ss);
	warning_print(ss.str());
	warning_print("\n");
	*/
	
	return result;
}
