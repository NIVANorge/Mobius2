
#include "model_codegen.h"
#include "model_application.h"

Math_Expr_FT *
make_possibly_weighted_var_ident(Var_Id var_id, Math_Expr_FT *weight = nullptr, Math_Expr_FT *unit_conv = nullptr) {
	
	auto var_ident = make_state_var_identifier(var_id);
	
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
			if(instr.type == Model_Instruction::Type::compute_state_var && instr.code && is_valid(instr.var_id)) {
				
				auto var = app->state_vars[instr.var_id];  // var is the mass/volume of the quantity
				
				auto conc = var->dissolved_conc;
				// NOTE: it is easier just to set it for both the mass and conc as we process the mass
				if(is_valid(conc) && !(var->flags & State_Variable::Flags::f_dissolved_conc) && !(var->flags & State_Variable::Flags::f_dissolved_flux)) {
					auto dissolved_in = app->state_vars.id_of(remove_dissolved(var->loc1));
						
					if(var->initial_is_conc) {
						// conc is given. compute mass
						instructions[conc.id].code = instr.code;
						instructions[conc.id].type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
						instr.code = make_binop('*', make_state_var_identifier(conc), make_state_var_identifier(dissolved_in));
					} else {
						// mass is given. compute conc.
						// TODO: do we really need the initial conc always though?
						instructions[conc.id].code = make_safe_divide(make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
						instructions[conc.id].type = Model_Instruction::Type::compute_state_var; //NOTE: it was probably declared invalid before since it by default had no code.
					}
				}
			}
		}
	}
	
	for(auto &instr : instructions) {
		
		if(!initial && instr.type == Model_Instruction::Type::compute_state_var) {
			auto var = app->state_vars[instr.var_id];
			
			//TODO: For overridden quantities they could just be removed from the solver. Also they shouldn't get any index set dependencies from fluxes connected to them.
			//    not sure about the best way to do it (or where).
			
			// Directly override the mass of a quantity
			if(var->type == Decl_Type::quantity && var->override_tree && !var->override_is_conc) {
				instr.code = var->override_tree;
			}
			
			// Codegen for concs of dissolved variables
			if(var->type == Decl_Type::property && (var->flags & State_Variable::Flags::f_dissolved_conc) ) {
				
				auto mass = var->dissolved_conc;
				auto mass_var = app->state_vars[mass];
				auto dissolved_in = app->state_vars.id_of(remove_dissolved(mass_var->loc1));
				
				if(mass_var->override_tree && mass_var->override_is_conc) {
					instr.code = mass_var->override_tree;

					// If we override the conc, the mass is instead  conc*volume_we_are_dissolved_in .
					auto mass_instr = &instructions[mass.id];
					mass_instr->code = make_binop('*', make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
				} else
					instr.code = make_safe_divide(make_state_var_identifier(mass), make_state_var_identifier(dissolved_in));
			}
			
			// Codegen for fluxes of dissolved variables
			
			if(var->type == Decl_Type::flux && (var->flags & State_Variable::Flags::f_dissolved_flux) ) {
				instr.code = make_binop('*', make_state_var_identifier(var->dissolved_conc), make_state_var_identifier(var->dissolved_flux));
			}
			
			// Restrict discrete fluxes to not overtax their source.
			
			if(var->type == Decl_Type::flux && !is_valid(instr.solver) && instr.code) {
			
				// note: create something like
				// 		flux = min(flux, source)
				// NOTE: it is a design decision by the framework to not allow negative fluxes, otherwise the flux would get a much more
				//      complicated relationship with its target. Should maybe just apply a    max(0, ...) to it as well by default?
				// NOTE: this will not be tripped by aggregates since they don't have their own function tree... but TODO: maybe make it a bit nicer still?
				
				if(is_located(var->loc1)) {   // This should always be true if the flux has a solver at this stage, but no reason not to be safe.
					Var_Id source_id = app->state_vars.id_of(var->loc1);
					auto source_ref = reinterpret_cast<Identifier_FT *>(make_state_var_identifier(source_id));
					instr.code = make_intrinsic_function_call(Value_Type::real, "min", instr.code, source_ref);
				}
			}
			
			// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
			//   Make a lookup accelleration for this?
			
			// Codegen for in_fluxes:
			if(var->flags & State_Variable::Flags::f_in_flux) {
				Math_Expr_FT *flux_sum = make_literal(0.0);
				for(auto flux_id : app->state_vars) {
					auto flux_var = app->state_vars[flux_id];
					if(flux_var->flags & State_Variable::Flags::f_invalid) continue;
					// NOTE: by design we don't include connection fluxes in the in_flux. May change that later.
					if(flux_var->type == Decl_Type::flux && !is_valid(flux_var->connection) && is_located(flux_var->loc2) && app->state_vars.id_of(flux_var->loc2) == var->in_flux_target) {
						auto flux_ref = make_state_var_identifier(flux_id);
						if(flux_var->unit_conversion_tree)
							flux_ref = make_binop('*', flux_ref, copy(flux_var->unit_conversion_tree)); // NOTE: we need to copy it here since it is also inserted somewhere else
						flux_sum = make_binop('+', flux_sum, flux_ref);
					}
				}
				instr.code = flux_sum;
			}
			
			// Codegen for the derivative of state variables:
			if(var->type == Decl_Type::quantity && is_valid(instr.solver) && !var->override_tree) {
				Math_Expr_FT *fun;
				// aggregation variable for values coming from connection fluxes.
				if(is_valid(var->connection_target_agg))
					fun = make_state_var_identifier(var->connection_target_agg);
				else
					fun = make_literal((double)0.0);
				
				if(is_valid(var->connection_source_agg))
					fun = make_binop('-', fun, make_state_var_identifier(var->connection_source_agg));
				
				for(Var_Id flux_id : app->state_vars) {
					auto flux = app->state_vars[flux_id];
					if(flux->flags & State_Variable::Flags::f_invalid) continue;
					if(flux->type != Decl_Type::flux) continue;
					
					if(is_located(flux->loc1) && app->state_vars.id_of(flux->loc1) == instr.var_id) {
						// TODO: We could consider always having an aggregation variable for the source even when the source is always just one instace just to get rid of all the special cases (?).
						if(is_valid(flux->connection)) {
							auto conn = model->connections[flux->connection];
							if(conn->type == Connection_Type::all_to_all)
								continue;   // NOTE: In this case we have set up an aggregation variable also for the source, so we should skip it.
						}
						
						auto flux_ref = make_state_var_identifier(flux_id);
						fun = make_binop('-', fun, flux_ref);
					}
					
					// TODO: if we explicitly computed an in_flux earlier, we could just refer to it here instead of re-computing it.
					//   maybe compicates code unnecessarily though.
					if(is_located(flux->loc2) && !is_valid(flux->connection) && app->state_vars.id_of(flux->loc2) == instr.var_id) {
						auto flux_ref = make_state_var_identifier(flux_id);
						// NOTE: the unit conversion applies to what reaches the target.
						if(flux->unit_conversion_tree)
							flux_ref = make_binop('*', flux_ref, flux->unit_conversion_tree);
						fun = make_binop('+', fun, flux_ref);
					}
				}
				instr.code = fun;
			}
			
		} else if (instr.type == Model_Instruction::Type::subtract_discrete_flux_from_source) {
			
			instr.code = make_possibly_weighted_var_ident(instr.var_id);
			
		} else if (instr.type == Model_Instruction::Type::add_discrete_flux_to_target) {
			
			auto unit_conv = app->state_vars[instr.var_id]->unit_conversion_tree;
			instr.code = make_possibly_weighted_var_ident(instr.var_id, nullptr, unit_conv);
			
		} else if (instr.type == Model_Instruction::Type::add_to_aggregate) {
			
			auto agg_var = app->state_vars[instr.target_id];
			
			auto weight = agg_var->aggregation_weight_tree;
			if(!weight && !is_valid(instr.connection))  // NOTE: no default weight for connection fluxes.
				fatal_error(Mobius_Error::internal, "Somehow we got an aggregation without code for computing the weight.");
			
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_var_ident(instr.var_id, weight, nullptr);
		} else if (instr.type == Model_Instruction::Type::add_to_connection_aggregate) {
			
			// TODO: may need weights and unit conversions here too eventually.
			
			auto agg_var = app->state_vars[instr.target_id];
			instr.code = make_possibly_weighted_var_ident(instr.var_id);
			
		}
	}
}

void
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, Index_Exprs &index_expr) {
	for(auto arg : expr->exprs)
		put_var_lookup_indexes(arg, app, index_expr);
	
	if(expr->expr_type != Math_Expr_Type::identifier_chain) return;
	
	auto ident = reinterpret_cast<Identifier_FT *>(expr);
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = app->parameter_structure.get_offset_code(ident->parameter, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = app->series_structure.get_offset_code(ident->series, index_expr);
		back_step = app->series_structure.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		auto var = app->state_vars[ident->state_var];
		if(var->flags & State_Variable::Flags::f_invalid)
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		offset_code = app->result_structure.get_offset_code(ident->state_var, index_expr);
		back_step = app->result_structure.total_count;
	}
	
	//TODO: Should check that we are not at the initial step
	if(offset_code && ident->variable_type != Variable_Type::parameter && (ident->flags & ident_flags_last_result)) {
		offset_code = make_binop('-', offset_code, make_literal(back_step));
	}
	
	if(offset_code)
		expr->exprs.push_back(offset_code);
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
add_value_to_tree_connection(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Entity_Id connection_id) {
	// TODO: Maybe refactor this so that it doesn't have code from different use cases mixed this much.

	auto model = app->model;
	
	// Hmm, this line looks a bit messy..
	Entity_Id source_compartment = app->state_vars[source_id]->loc1.components[0];
	auto target_agg = app->state_vars[agg_id];
	// This is also messy...
	auto target_compartment = app->state_vars[target_agg->connection_target_agg]->loc1.components[0];
	
	Math_Expr_FT *agg_offset = nullptr;
	
	auto find_target = app->find_connection_component(connection_id, target_compartment);
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
		
		for(int idx = 0; idx < target_indexes.size(); ++idx) // NOTE: If they were used, they were copied, so we delete them again now.
			delete target_indexes[idx];
	} else
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	
	//warning_print("*** *** Codegen for connection ", app->state_vars[source_id]->name, " to ", app->state_vars[target_agg->connection_agg]->name, " using agg var ", app->state_vars[agg_id]->name, "\n");
	
	// Code for looking up the id of the target compartment of the current source.
	auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, 0}, indexes);	// the 0 is because the compartment id is stored at info id 0
	auto compartment_id = new Identifier_FT();
	compartment_id->variable_type = Variable_Type::connection_info;
	compartment_id->value_type = Value_Type::integer;
	compartment_id->exprs.push_back(idx_offset);
	
	// If the target compartment is negative, this source does not connect anywhere, so we have to make code to check for that.
	auto condition = make_binop(Token_Type::geq, compartment_id, make_literal((s64)0));
	if(app->connection_components[connection_id.id].size() > 1) {
		// If there can be multiple valid target components for the connection, we have to make code to see if the value should indeed be added to this aggregation variable.
		
		// TODO: We could optimize to see if there are multiple valid targets for this particular source.
		auto condition2 = make_binop('=', compartment_id, make_literal((s64)target_compartment.id));
		condition = make_binop('&', condition, condition2);
	}
	
	auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
	if_chain->value_type = Value_Type::none;
	
	if_chain->exprs.push_back(add_value_to_state_var(agg_id, agg_offset, value, '+'));
	if_chain->exprs.push_back(condition);
	if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
	
	Math_Expr_FT *result = if_chain;
	
	//warning_print("\n\n**** Agg instruction tree is :\n");
	//print_tree(result, 0);
	
	return result;
}

Math_Expr_FT *
add_value_to_all_to_all_agg(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Index_Exprs &indexes, Entity_Id connection_id) {
	
	bool is_source = true;
	auto agg_var = app->state_vars[agg_id];
	if(is_valid(agg_var->connection_source_agg))
		is_source = true;
	else if(is_valid(agg_var->connection_target_agg))
		is_source = false;
	else
		fatal_error(Mobius_Error::internal, "Incorrect setup of all_to_all aggregation variables.");
	
	Math_Expr_FT *agg_offset = nullptr;
	
	if(is_source)
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
	else {
		// We have to "transpose the matrix" so that we add this to the target instead corresponding to the index pair instead of the source.
		indexes.transpose();
		agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		indexes.transpose();
	}
	
	return add_value_to_state_var(agg_id, agg_offset, value, '+'); // NOTE: it is a + regardless, since the subtraction happens explicitly when we use the value later.
}

Math_Expr_FT *
add_value_to_connection_agg_var(Model_Application *app, Math_Expr_FT *value, Var_Id agg_id, Var_Id source_id, Index_Exprs &indexes, Entity_Id connection_id) {
	auto model = app->model;
	
	auto connection = model->connections[connection_id];
	if(connection->type == Connection_Type::directed_tree) {
		
		return add_value_to_tree_connection(app, value, agg_id, source_id, indexes, connection_id);
		
	} else if (connection->type == Connection_Type::all_to_all) {
		
		return add_value_to_all_to_all_agg(app, value, agg_id, indexes, connection_id);
		
	} else
		fatal_error(Mobius_Error::internal, "Unhandled connection type in add_value_to_connection_agg_var()");
	
	return nullptr;
}

// TODO: This could maybe be a member method of Index_Exprs
Math_Expr_FT *
create_nested_for_loops(Math_Block_FT *top_scope, std::vector<Index_T> index_counts, std::set<Index_Set_Dependency> &index_sets, Index_Exprs &index_expr) {
	
	auto &indexes = index_expr.indexes;
	
	Math_Block_FT *scope = top_scope;
	auto index_set = index_sets.begin();
	for(int idx = 0; idx < index_sets.size(); ++idx) {
		
		auto loop = make_for_loop();
		loop->exprs.push_back(make_literal((s64)index_counts[index_set->id.id].index));
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
			loop->exprs.push_back(make_literal((s64)index_counts[index_set->id.id].index));
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
		Math_Expr_FT *scope = create_nested_for_loops(top_scope, app->index_counts, array.index_sets, indexes);
		
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			auto fun = instr->code;
			
			if(fun) {
				fun = copy(fun);
			
				//TODO: we should not do excessive lookups. Can instead keep them around as local vars and reference them (although llvm will probably optimize it).
				put_var_lookup_indexes(fun, app, indexes);
				
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
				
				auto result = add_value_to_connection_agg_var(app, fun, instr->target_id, instr->source_id, indexes, instr->connection);
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
		Math_Expr_FT *scope = create_nested_for_loops(top_scope, app->index_counts, array.index_sets, indexes);
				
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
	
	//warning_print("\nTree before prune:\n");
	//print_tree(top_scope, 0);
	//warning_print("\n");
	
	auto result = prune_tree(top_scope);
	
	//warning_print("\nTree after prune:\n");
	//print_tree(result, 0);
	//warning_print("\n");
	
	return result;
}
