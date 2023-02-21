
#include "model_codegen.h"
#include "model_application.h"


Math_Expr_FT *
get_index_count_code(Model_Application *app, Entity_Id index_set, Index_Exprs &indexes) {
	auto offset = app->index_counts_structure.get_offset_code(index_set, indexes);
	auto ident = new Identifier_FT();
	ident->value_type = Value_Type::integer;
	ident->variable_type = Variable_Type::index_count;
	ident->exprs.push_back(offset);
	return ident;
}

Math_Expr_FT *
make_possibly_time_scaled_ident(Model_Application *app, Var_Id var_id) {
	auto ident = make_state_var_identifier(var_id);
	auto var = app->state_vars[var_id];
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
			auto var = app->state_vars[instr.var_id];  // var is the mass or volume of the quantity
			if(var->type != State_Var::Type::declared) continue;
			auto var2 = as<State_Var::Type::declared>(var);
			auto conc_id = var2->conc;
			if(!is_valid(conc_id)) continue;
			
			// NOTE: it is easier just to set the generated code for both the mass and conc as we process the mass
			auto conc = as<State_Var::Type::dissolved_conc>(app->state_vars[conc_id]);
			auto &conc_instr = instructions[conc_id.id];
			auto dissolved_in = app->state_vars.id_of(remove_dissolved(var->loc1));
				
			if(var2->initial_is_conc) {
				// conc is given. compute mass
				conc_instr.code = instr.code;
				conc_instr.type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
				instr.code = make_binop('*', make_state_var_identifier(conc_id), make_state_var_identifier(dissolved_in));
				if(conc->unit_conversion != 1.0)
					instr.code = make_binop('/', instr.code, make_literal(conc->unit_conversion));
			} else {
				// mass is given. compute conc.
				// TODO: do we really need the initial conc always though?
				conc_instr.type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
				conc_instr.code = make_safe_divide(make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
				if(conc->unit_conversion != 1.0)
					conc_instr.code = make_binop('*', instr.code, make_literal(conc->unit_conversion));
			}
		}
	}
	
	for(auto &instr : instructions) {
		
		if(!initial && instr.type == Model_Instruction::Type::compute_state_var) {
			auto var = app->state_vars[instr.var_id];
			
			//TODO: For overridden quantities they could just be removed from the solver. Also they shouldn't get any index set dependencies from fluxes connected to them.
			//    not sure about the best way to do it (or where).
			//      -hmm this comment may be outdated. Should be checked.
			
			// Directly override the mass of a quantity
			if(var->type == State_Var::Type::declared) {
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->decl_type == Decl_Type::quantity && var2->override_tree && !var2->override_is_conc)
					instr.code = var2->override_tree;
			}
			
			// Codegen for concs of dissolved variables
			if(var->type == State_Var::Type::dissolved_conc) {
				
				auto conc = as<State_Var::Type::dissolved_conc>(var);
				auto mass_id = conc->conc_of;
				auto mass_var = as<State_Var::Type::declared>(app->state_vars[mass_id]);
				auto dissolved_in = app->state_vars.id_of(remove_dissolved(mass_var->loc1));
				
				if(mass_var->override_tree && mass_var->override_is_conc) {
					instr.code = mass_var->override_tree;

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
				auto conc = as<State_Var::Type::dissolved_conc>(app->state_vars[var2->conc]);
				instr.code = make_binop('*', make_state_var_identifier(var2->conc), make_possibly_time_scaled_ident(app, var2->flux_of_medium));
				// TODO: Here we could also just do the re-computation of the concentration so that we don't get the back-and-forth unit conversion...
				
				if(conc->unit_conversion != 1.0)
					instr.code = make_binop('/', instr.code, make_literal(conc->unit_conversion));
				// Certain types of fluxes are allowed to be negative, in that case we need the concentration to be taken from the target.
				
				// TODO: Allow for other types of fluxes to be bidirectional also
				auto conn_id = connection_of_flux(var);
				if(is_valid(conn_id)) {
					auto conn = model->connections[conn_id];
					if(conn->type == Connection_Type::grid1d) {
						auto condition = make_binop(Token_Type::geq, make_state_var_identifier(var2->flux_of_medium), make_literal(0.0));
						auto conc2 = static_cast<Identifier_FT *>(make_state_var_identifier(var2->conc));
						conc2->flags = Identifier_Data::Flags::below_above;
						conc2->is_above = false;
						conc2->connection = conn_id;
						
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

				Var_Id source_id = app->state_vars.id_of(var->loc1);
				auto source_ref = static_cast<Identifier_FT *>(make_state_var_identifier(source_id));
				instr.code = make_intrinsic_function_call(Value_Type::real, "min", instr.code, source_ref);
			}
			
			// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
			//   Make a lookup accelleration for this?
			
			// Codegen for in_fluxes:
			if(var->type == State_Var::Type::in_flux_aggregate) {
				auto var2 = as<State_Var::Type::in_flux_aggregate>(var);
				Math_Expr_FT *flux_sum = make_literal(0.0);
				//  find all fluxes that has the given target and sum them up.
				for(auto flux_id : app->state_vars) {
					auto flux_var = app->state_vars[flux_id];
					if(!flux_var->is_valid() || !flux_var->is_flux()) continue;
					// NOTE: by design we don't include connection fluxes in the in_flux. May change that later.
					if(is_valid(connection_of_flux(flux_var))) continue;
					if(!is_located(flux_var->loc2) || app->state_vars.id_of(flux_var->loc2) != var2->in_flux_to) continue;
					
					auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
					if(flux_var->unit_conversion_tree)
						flux_ref = make_binop('*', flux_ref, copy(flux_var->unit_conversion_tree)); // NOTE: we need to copy it here since it is also inserted somewhere else
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
					
					for(Var_Id flux_id : app->state_vars) {
						auto flux = app->state_vars[flux_id];
						if(!flux->is_valid() || !flux->is_flux()) continue;
						
						// NOTE: In the case of an all-to-all connection case we have set up an aggregation variable also for the source, so we already subtract using that.
						// TODO: We could consider always having an aggregation variable for the source even when the source is always just one instace just to get rid of all the special cases (?).
						
						auto conn_id = connection_of_flux(flux);
						bool is_bottom      = flux->boundary_type == Boundary_Type::bottom;
						bool is_all_to_all  = is_valid(conn_id) && model->connections[conn_id]->type == Connection_Type::all_to_all;
						
						// NOTE: For bottom fluxes there is a special hack where they are subtracted from the target agg variable. Hopefully we get a better solution soon.
						
						if(is_located(flux->loc1) && app->state_vars.id_of(flux->loc1) == instr.var_id
							&& !is_all_to_all && !is_bottom) {

							auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
							fun = make_binop('-', fun, flux_ref);
						}
						
						if(is_located(flux->loc2) && app->state_vars.id_of(flux->loc2) == instr.var_id
							&& (!is_valid(conn_id) || is_bottom)) {
							
							auto flux_ref = make_possibly_time_scaled_ident(app, flux_id);
							// NOTE: the unit conversion applies to what reaches the target.
							if(flux->unit_conversion_tree)
								flux_ref = make_binop('*', flux_ref, flux->unit_conversion_tree);
							fun = make_binop('+', fun, flux_ref);
						}
					}
					instr.code = fun;
				}
			}
			
		} else if (instr.type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
			
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id);
			
		} else if (instr.type == Model_Instruction::Type::add_discrete_flux_to_target) {
			
			auto unit_conv = app->state_vars[instr.var_id]->unit_conversion_tree;
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id, nullptr, unit_conv);
			
		} else if (instr.type == Model_Instruction::Type::add_to_aggregate) {
			
			auto agg_var = as<State_Var::Type::regular_aggregate>(app->state_vars[instr.target_id]);
			
			auto weight = agg_var->aggregation_weight_tree;
			//if(!weight)
			//	fatal_error(Mobius_Error::internal, "Somehow we got a regular aggregation without code for computing the weight.");
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id, weight, nullptr);
			
		} else if (instr.type == Model_Instruction::Type::add_to_connection_aggregate) {
			
			// Note weights are applied directly inside the codegen for this one. TODO: do that for the others too?
			
			auto agg_var = app->state_vars[instr.target_id];
			instr.code = make_possibly_weighted_var_ident(app, instr.var_id);
			
		}
	}
}

void
get_grid1d_target_indexes(Model_Application *app, std::vector<Math_Expr_FT *> &target_indexes, Entity_Id connection, Index_Exprs &indexes, bool is_above = false, bool is_boundary = false) {
	if(app->model->connections[connection]->type != Connection_Type::grid1d)
		fatal_error(Mobius_Error::internal, "Misuse of get_grid1d_target_indexes().");
	auto &comp = app->connection_components[connection.id][0];
	auto index_set = comp.index_sets[0];
	
	target_indexes.resize(app->model->index_sets.count(), nullptr);
	if(!is_boundary) {
		auto index = copy(indexes.indexes[index_set.id]);
		char oper = is_above ? '-' : '+';
		index = make_binop(oper, index, make_literal((s64)1));
		target_indexes[index_set.id] = index;
	} else {
		if (is_above)
			target_indexes[index_set.id] = make_literal((s64)0);
		else {
			auto count = get_index_count_code(app, index_set, indexes);
			target_indexes[index_set.id] = make_binop('-', count, make_literal((s64)1));
		}
	}
}

void
put_var_lookup_indexes_helper(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, std::vector<Math_Expr_FT *> *provided_target_idx, std::set<std::pair<Entity_Id, bool>> &found_grid1d_target) {
	
	for(auto arg : expr->exprs)
		put_var_lookup_indexes_helper(arg, app, index_expr, provided_target_idx, found_grid1d_target);
	
	if(expr->expr_type != Math_Expr_Type::identifier) return;
	
	auto ident = static_cast<Identifier_FT *>(expr);
	
	std::vector<Math_Expr_FT *> target_indexes;
	
	Connection_Type conn_type;
	if(ident->flags & Identifier_FT::Flags::below_above) {
		auto conn = app->model->connections[ident->connection];
		conn_type = conn->type;
		if(conn_type == Connection_Type::all_to_all) {
			index_expr.transpose();
		} else if (conn_type == Connection_Type::grid1d) {
			found_grid1d_target.insert({ident->connection, ident->is_above});
			
			if(!provided_target_idx) {
				get_grid1d_target_indexes(app, target_indexes, ident->connection, index_expr, ident->is_above);
				provided_target_idx = &target_indexes;
			}
			index_expr.swap(*provided_target_idx);
		} else {
			ident->source_loc.print_error_header();
			fatal_error("Got a 'above' or 'below' directive for a connection \"", conn->name, "\" that is not of type all_to_all or grid1d."); // TODO: above should not be allowed for all_to_all
		}
	}
	if(ident->flags & Identifier_FT::Flags::top_bottom) {
		auto conn = app->model->connections[ident->connection];
		conn_type = conn->type;
		if(conn_type != Connection_Type::grid1d)
			fatal_error("Got a 'top' or 'bottom' directive for a connection \"", conn->name, "\" that is not of type grid1d.");
		
		get_grid1d_target_indexes(app, target_indexes, ident->connection, index_expr, ident->is_above, true);
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
		auto var = app->state_vars[ident->var_id];
		if(var->flags & State_Var::Flags::invalid)
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		offset_code = app->result_structure.get_offset_code(ident->var_id, index_expr);
		back_step = app->result_structure.total_count;
	}
	
	if(ident->flags & Identifier_FT::Flags::below_above) {
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
}

Math_Expr_FT *
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr, std::vector<Math_Expr_FT *> *provided_target_idx = nullptr, 
	Entity_Id always_check_connection = invalid_entity_id, Boundary_Type boundary_type = Boundary_Type::none) {
		
	std::set<std::pair<Entity_Id, bool>> found_grid1d_target;
	put_var_lookup_indexes_helper(expr, app, index_expr, provided_target_idx, found_grid1d_target);
	
	if(is_valid(always_check_connection) && app->model->connections[always_check_connection]->type == Connection_Type::grid1d)
		found_grid1d_target.insert({always_check_connection, false});
	
	if(found_grid1d_target.empty() || boundary_type != Boundary_Type::none)
		return expr;
	
	// Make code to check that we are not at the last index.
	// If we are at the last index relative to a connection we can't compute the target(), hence the entire expression must be invalid (will be evaluated as 0)
	auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
	if_chain->value_type = Value_Type::real;
	if_chain->exprs.push_back(expr);
	
	Math_Expr_FT *condition = nullptr;
	for(auto found : found_grid1d_target) {
		auto conn = found.first;
		bool is_above = found.second;
		
		auto &comp = app->connection_components[conn.id][0];
		auto index_set = comp.index_sets[0];
		
		Math_Expr_FT *index = nullptr;
		if(provided_target_idx)
			index = (*provided_target_idx)[index_set.id];
		else {
			// TODO: This is a bit wasteful, we only need the single index
			std::vector<Math_Expr_FT *> target_indexes;
			get_grid1d_target_indexes(app, target_indexes, conn, index_expr, is_above);
			index = target_indexes[index_set.id];
		}
		
		Math_Expr_FT *ltc = nullptr;
		if(is_above)
			ltc = make_binop(Token_Type::geq, copy(index), make_literal((s64)0));
		else {
			auto index_count = get_index_count_code(app, index_set, index_expr);
			ltc = make_binop('<', copy(index), index_count);
		}
		
		if(!condition)
			condition = ltc;
		else
			condition = make_binop('&', condition, ltc);
	}
	if_chain->exprs.push_back(condition);
	if_chain->exprs.push_back(make_literal(0.0));
	
	return if_chain;
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
add_value_to_tree_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Entity_Id connection_id, Math_Expr_FT *weight) {
	// TODO: Maybe refactor this so that it doesn't have code from different use cases mixed this much.

	auto model = app->model;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->state_vars[agg_id]);
	
	// Hmm, these two lookups are very messy. See also similar in model_compilation a couple of places
	Entity_Id source_compartment = app->state_vars[source_id]->loc1.components[0];
	auto target_compartment = app->state_vars[target_agg->agg_for]->loc1.components[0];
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
			weight = put_var_lookup_indexes(weight, app, indexes, &target_indexes);
		
		for(int idx = 0; idx < target_indexes.size(); ++idx) // NOTE: If they were used, they were copied, so we delete them again now.
			delete target_indexes[idx];
	} else {
		if(weight) weight = put_var_lookup_indexes(weight, app, indexes);
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	}
	
	//warning_print("*** *** Codegen for connection ", app->state_vars[source_id]->name, " to ", app->state_vars[target_agg->connection_agg]->name, " using agg var ", app->state_vars[agg_id]->name, "\n");
	
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
	
	if_chain->exprs.push_back(add_value_to_state_var(agg_id, agg_offset, value, '+'));
	if_chain->exprs.push_back(condition);
	if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
	
	Math_Expr_FT *result = if_chain;
	
	//warning_print("\n\n**** Agg instruction tree is :\n");
	//print_tree(result, 0);
	
	return result;
}

Math_Expr_FT *
add_value_to_all_to_all_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Entity_Id connection_id, Math_Expr_FT *weight) {
	
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->state_vars[agg_id]);

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
		weight = put_var_lookup_indexes(weight, app, indexes, nullptr);
		value = make_binop('*', value, weight);
	}
	
	return add_value_to_state_var(agg_id, agg_offset, value, '+'); // NOTE: it is a + regardless, since the subtraction happens explicitly when we use the value later.
}

Math_Expr_FT *
add_value_to_grid1d_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Entity_Id connection_id, Math_Expr_FT *weight, Boundary_Type boundary_type) {
	
	auto model = app->model;
	auto &comp = app->connection_components[connection_id.id][0];
	auto index_set = comp.index_sets[0];
	
	std::vector<Math_Expr_FT *> target_indexes;
	
	bool is_above = false; // For grid1d the canonical direction for the connection is downwards
	bool is_edge = boundary_type != Boundary_Type::none;
	if(is_edge) {
		is_above = boundary_type == Boundary_Type::top;
	}
	if(boundary_type == Boundary_Type::bottom) // NOTE: This is a bit of a hack. We should maybe have a source aggregate here instead, but that can cause a lot of unnecessary work for the model.
		value = make_unary('-', value);
	
	get_grid1d_target_indexes(app, target_indexes, connection_id, indexes, is_above, is_edge);
	
	indexes.swap(target_indexes);
	auto agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	indexes.swap(target_indexes);
	
	if(weight) {
		weight = put_var_lookup_indexes(weight, app, indexes, &target_indexes);
		value = make_binop('*', value, weight);
	}
	//TODO: We don't free the target_indexes!
	// Same goes for other places where we use get_grid1d_target_indexes() !
	auto result = add_value_to_state_var(agg_id, agg_offset, value, '+');
	
	// Make code to check that we are not at the last index.
	if(boundary_type == Boundary_Type::none) {
		auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
		if_chain->value_type = Value_Type::none;
		if_chain->exprs.push_back(result);
		auto index_count = get_index_count_code(app, index_set, indexes);
		auto index = indexes.indexes[index_set.id];
		if_chain->exprs.push_back(make_binop('<', copy(index), index_count));
		if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
		
		result = if_chain;
	}
	return result;
}

Math_Expr_FT *
add_value_to_connection_agg_var(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Entity_Id connection_id, Boundary_Type boundary_type) {
	auto model = app->model;
	
	auto connection = model->connections[connection_id];
	
	// See if we should apply a weight to the value.
	Math_Expr_FT *weight = nullptr;
	auto target_agg = as<State_Var::Type::connection_aggregate>(app->state_vars[agg_id]);
	for(auto &pair : target_agg->weights) {
		if(pair.first == source_id) {
			weight = pair.second;
			break;
		}
	}
	
	if(connection->type == Connection_Type::directed_tree) {
		
		return add_value_to_tree_agg(app, value, agg_id, source_id, indexes, connection_id, weight);
		
	} else if (connection->type == Connection_Type::all_to_all) {
		
		return add_value_to_all_to_all_agg(app, value, agg_id, indexes, connection_id, weight);
		
	} else if (connection->type == Connection_Type::grid1d) {
		
		return add_value_to_grid1d_agg(app, value, agg_id, indexes, connection_id, weight, boundary_type);
		
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
			
			auto fun = instr->code;
			
			if(fun) {
				fun = copy(fun);
				
				Entity_Id connection = invalid_entity_id;
				Boundary_Type boundary_type = Boundary_Type::none;
				if(instr->type == Model_Instruction::Type::compute_state_var) {
					auto var = app->state_vars[instr->var_id];
					if(var->is_flux()) {
						connection = connection_of_flux(var);
						boundary_type = var->boundary_type;
					}
					//warning_print("**** Generate for computing ", var->name, "\n");
				} //else
					//warning_print("** Generate for other\n");
				
				//TODO: we should not do excessive lookups. Can instead keep them around as local vars and reference them (although llvm will probably optimize it).
				fun = put_var_lookup_indexes(fun, app, indexes, nullptr, connection, boundary_type);
				
			} else if (instr->type != Model_Instruction::Type::clear_state_var) {
				//NOTE: This could happen for discrete quantities since they are instead modified by add/subtract instructions. Same for some aggregation variables that are only modified by other instructions.
				// TODO: Should we try to infer if it is ok that there is no code for this compute_state_var (?). Or maybe have a separate type for it when we expect there to be no actual computation and it is only a placeholder for a value (?). Or maybe have a flag for it on the State_Variable.
				if(instr->type == Model_Instruction::Type::compute_state_var)
					continue;
				fatal_error(Mobius_Error::internal, "Unexpectedly missing code for a model instruction. Type: ", (int)instr->type, ".");
			}
			
				
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				
				auto offset_code = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
				
			} else if (instr->type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->source_id, indexes);
				auto result = add_value_to_state_var(instr->source_id, agg_offset, fun, '-');
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_discrete_flux_to_target) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->target_id, indexes);
				auto result = add_value_to_state_var(instr->target_id, agg_offset, fun, '+');
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_to_aggregate) {
				
				auto agg_offset = app->result_structure.get_offset_code(instr->target_id, indexes);
				auto result = add_value_to_state_var(instr->target_id, agg_offset, fun, '+');
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_to_connection_aggregate) {
				
				auto result = add_value_to_connection_agg_var(app, fun, instr->target_id, instr->source_id, indexes, instr->connection, instr->boundary_type);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((double)0));
				scope->exprs.push_back(assignment);
				
			} else {
				fatal_error(Mobius_Error::internal, "Unimplemented instruction type in code generation.");
			}
		}
		
		indexes.clean();
	}
	
	if(!is_valid(batch->solver) || initial) {
		auto result = prune_tree(top_scope);
		return result;
	}
	
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
			
			fun = put_var_lookup_indexes(fun, app, indexes);
			
			auto offset_var = app->result_structure.get_offset_code(instr->var_id, indexes);
			auto offset_deriv = make_binop('-', offset_var, make_literal(init_pos));
			auto assignment = new Math_Expr_FT(Math_Expr_Type::derivative_assignment);
			assignment->exprs.push_back(offset_deriv);
			assignment->exprs.push_back(fun);
			scope->exprs.push_back(assignment);
		}
		
		indexes.clean();
	}
	
	/*
	warning_print("\nTree after prune:\n");
	std::stringstream ss;
	print_tree(result, ss);
	warning_print(ss.str());
	warning_print("\n");
	*/
	
	auto result = prune_tree(top_scope);
	
	/*
	warning_print("\nTree after prune:\n");
	std::stringstream ss;
	print_tree(result, ss);
	warning_print(ss.str());
	warning_print("\n");
	*/
	
	return result;
}
