
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
					auto dissolved_in = app->state_vars[remove_dissolved(var->loc1)];
						
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
				auto dissolved_in = app->state_vars[remove_dissolved(mass_var->loc1)];
				
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
					Var_Id source_id = app->state_vars[var->loc1];
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
					if(flux_var->type == Decl_Type::flux && !is_valid(flux_var->connection) && is_located(flux_var->loc2) && app->state_vars[flux_var->loc2] == var->in_flux_target) {
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
				auto conn_agg = var->connection_agg; // aggregation variable for values coming from connection fluxes.
				if(is_valid(conn_agg))
					fun = make_state_var_identifier(conn_agg);
				else
					fun = make_literal((double)0.0);
				
				for(Var_Id flux_id : app->state_vars) {
					auto flux = app->state_vars[flux_id];
					if(flux->flags & State_Variable::Flags::f_invalid) continue;
					if(flux->type != Decl_Type::flux) continue;
					
					if(is_located(flux->loc1) && app->state_vars[flux->loc1] == instr.var_id) {
						auto flux_ref = make_state_var_identifier(flux_id);
						fun = make_binop('-', fun, flux_ref);
					}
					
					// TODO: if we explicitly computed an in_flux earlier, we could just refer to it here instead of re-computing it.
					if(is_located(flux->loc2) && !is_valid(flux->connection) && app->state_vars[flux->loc2] == instr.var_id) {
						auto flux_ref = make_state_var_identifier(flux_id);
						// NOTE: the unit conversion applies to what reaches the target.
						if(flux->unit_conversion_tree)
							flux_ref = make_binop('*', flux_ref, flux->unit_conversion_tree);
						fun = make_binop('+', fun, flux_ref);
					}
				}
				instr.code = fun;
			}
			
		} else if (instr.type == Model_Instruction::Type::subtract_flux_from_source) {
			
			instr.code = make_possibly_weighted_var_ident(instr.var_id);
			
		} else if (instr.type == Model_Instruction::Type::add_flux_to_target) {
			
			auto unit_conv = app->state_vars[instr.var_id]->unit_conversion_tree;
			instr.code = make_possibly_weighted_var_ident(instr.var_id, nullptr, unit_conv);
			
		} else if (instr.type == Model_Instruction::Type::add_to_aggregate) {
			
			auto agg_var = app->state_vars[instr.source_or_target_id];
			
			auto weight = agg_var->aggregation_weight_tree;
			if(!weight && !is_valid(instr.connection))  // NOTE: no default weight for connection fluxes.
				fatal_error(Mobius_Error::internal, "Somehow we got an aggregation without code for computing the weight.");
			
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_var_ident(instr.var_id, weight, nullptr);
		}
	}
}

void
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *app, std::vector<Math_Expr_FT *> &index_expr) {
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
add_value_to_agg_var(Model_Application *app, char oper, Math_Expr_FT *value, Var_Id agg_id, std::vector<Math_Expr_FT *> &indexes, Var_Id connection_source = invalid_var, Entity_Id connection_id = invalid_entity_id) {
	// TODO: Maybe refactor this so that it doesn't have code from different use cases mixed this much.

	auto model = app->model;
	Math_Expr_FT *result = nullptr;

	if(is_valid(connection_id)) {
		
		// Hmm, this line looks a bit messy..
		Entity_Id source_compartment = app->state_vars[connection_source]->loc1.components[0];
		
		auto connection = model->connections[connection_id];
		if(connection->type != Connection_Structure_Type::directed_tree)
			fatal_error(Mobius_Error::internal, "Unhandled connection type in add_or_subtract_var_from_agg_var()");
		
		// NOTE: we create the formula to look up the index of the target, but this is stored using the indexes of the source.
		auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, 1}, indexes);	// the 1 is because the target index is stored at info id 1
		auto target_index = new Identifier_FT();
		target_index->variable_type = Variable_Type::connection_info;
		target_index->value_type = Value_Type::integer;
		target_index->exprs.push_back(idx_offset);
		
		auto target_agg = app->state_vars[agg_id];
		// This is also messy...
		auto target_compartment = app->state_vars[target_agg->connection_agg]->loc1.components[0];
		
		//warning_print("*** Codegen for connection ", app->state_vars[connection_source]->name, " to ", app->state_vars[target_agg->connection_agg]->name, " using agg var ", app->state_vars[agg_id]->name, "\n");
		
		auto index_set_target = model->components[target_compartment]->index_sets[0]; //NOTE: temporary!!
		auto cur_idx = indexes[index_set_target.id]; // Store it so that we can restore it later.
		indexes[index_set_target.id] = target_index;
		auto agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		indexes[index_set_target.id] = cur_idx;
		
		// If the target index is negative, this does not connect anywhere, so we have to make code to check for that.
		auto condition = make_binop(Token_Type::geq, target_index, make_literal((s64)0));
		
		if(connection->compartments.size() > 1) {
			// If there can be multiple valid targets compartments for the connection, we have to make code to see if the value should indeed be added to this aggregation variable.
			
			auto idx_offset = app->connection_structure.get_offset_code(Connection_T {connection_id, source_compartment, 0}, indexes);	// the 0 is because the compartment id is stored at info id 0
			auto compartment_id = new Identifier_FT();
			compartment_id->variable_type = Variable_Type::connection_info;
			compartment_id->value_type = Value_Type::integer;
			compartment_id->exprs.push_back(idx_offset);
			
			auto condition2 = make_binop('=', compartment_id, make_literal((s64)target_compartment.id));
			condition = make_binop('&', condition, condition2);
		}
		
		auto if_chain = new Math_Expr_FT(Math_Expr_Type::if_chain);
		if_chain->value_type = Value_Type::none;
		
		if_chain->exprs.push_back(add_value_to_state_var(agg_id, agg_offset, value, oper));
		if_chain->exprs.push_back(condition);
		if_chain->exprs.push_back(make_literal((s64)0));   // NOTE: This is a dummy value that won't be used. We don't support void 'else' clauses at the moment.
		
		result = if_chain;
	} else {
		auto agg_offset = app->result_structure.get_offset_code(agg_id, indexes);
		result = add_value_to_state_var(agg_id, agg_offset, value, oper);
	}
	
	//warning_print("\n\n**** Agg instruction tree is :\n");
	//print_tree(result, 0);
	
	return result;
}

Math_Expr_FT *
create_nested_for_loops(Model_Application *app, Math_Block_FT *top_scope, Batch_Array &array, std::vector<Math_Expr_FT *> &indexes) {
	for(auto &index_set : app->model->index_sets)
		indexes[index_set.id] = nullptr;    //note: just so that it is easy to catch if we somehow use an index we shouldn't
		
	Math_Block_FT *scope = top_scope;
	for(auto &index_set : array.index_sets) {
		auto loop = make_for_loop();
		loop->exprs.push_back(make_literal((s64)app->index_counts[index_set.id].index));
		scope->exprs.push_back(loop);
		
		//NOTE: the scope of this item itself is replaced when it is inserted later.
		// note: this is a reference to the iterator of the for loop.
		indexes[index_set.id] = make_local_var_reference(0, loop->unique_block_id, Value_Type::integer);
		
		scope = loop;
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
	
	std::vector<Math_Expr_FT *> indexes(model->index_sets.count());
	
	for(auto &array : batch->arrays) {
		Math_Expr_FT *scope = create_nested_for_loops(app, top_scope, array, indexes);
		
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			auto fun = instr->code;
			
			if(fun) {
				fun = copy(fun);
			
				//TODO: we should not do excessive lookups. Can instead keep them around as local vars and reference them (although llvm will probably optimize it).
				put_var_lookup_indexes(fun, app, indexes);
			} else if (instr->type != Model_Instruction::Type::clear_state_var)
				continue;
				//TODO: we coud set an explicit no-op flag on the instruction. If the flag is set, we expect a nullptr fun, otherwise a valid one. Then throw error if there is something unexpected.
				
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				
				auto offset_code = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
				
			} else if (instr->type == Model_Instruction::Type::subtract_flux_from_source) {

				auto result = add_value_to_agg_var(app, '-', fun, instr->source_or_target_id, indexes);
				//auto result = add_or_subtract_var_from_agg_var(app, '-', fun, instr->source_or_target_id, indexes);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_flux_to_target) {
				
				// TODO: this is annoying to do... We should store source_id and target_id separately on the instr. Also makes it less likely to make errors later
				Var_Id source_id = invalid_var;
				if(is_valid(instr->connection)) {
					auto source_loc = app->state_vars[instr->var_id]->loc1;
					source_id = app->state_vars[source_loc];
				}
				
				auto result = add_value_to_agg_var(app, '+', fun, instr->source_or_target_id, indexes, source_id, instr->connection);
				//auto result = add_or_subtract_var_from_agg_var(app, '+', fun, instr->source_or_target_id, indexes, instr->connection);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_to_aggregate) {
				
				// TODO: see above!
				Var_Id source_id = invalid_var;
				if(is_valid(instr->connection)) {
					auto source_loc = app->state_vars[instr->var_id]->loc1;
					source_id = app->state_vars[source_loc];
				}
				
				auto result = add_value_to_agg_var(app, '+', fun, instr->source_or_target_id, indexes, source_id, instr->connection);
				//auto result = add_or_subtract_var_from_agg_var(app, '+', fun, instr->source_or_target_id, indexes, instr->connection);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((double)0));
				scope->exprs.push_back(assignment);
				
			}
		}
		
		//NOTE: delete again to not leak (note that if any of these are used, they are copied, so we are free to delete the originals).
		for(auto expr : indexes) if(expr) delete expr;
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
		Math_Expr_FT *scope = create_nested_for_loops(app, top_scope, array, indexes);
				
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
