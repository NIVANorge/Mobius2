
#include "model_application.h"
#include "function_tree.h"

#include <string>


struct
Model_Instruction {
	enum class
	Type {
		invalid,
		compute_state_var,
		subtract_flux_from_source,
		add_flux_to_target,
		clear_state_var,
		add_to_aggregate,
	}                   type;
	
	Var_Id              var_id;
	Var_Id              source_or_target_id;
	Entity_Id           neighbor;
	
	Entity_Id           solver;
	
	std::set<Entity_Id> index_sets;
	
	std::set<int> depends_on_instruction; // Instructions that must be executed before this one.
	std::set<int> instruction_is_blocking; // Instructions that can not go in the same for loop as this one
	std::set<int> inherits_index_sets_from_instruction;
	
	Math_Expr_FT *code;
	
	bool visited;
	bool temp_visited;
	
	Model_Instruction() : visited(false), temp_visited(false), var_id(invalid_var), solver(invalid_entity_id), neighbor(invalid_entity_id), type(Type::invalid), code(nullptr) {};
};

struct
Batch_Array {
	std::vector<int>         instr_ids;
	std::set<Entity_Id>      index_sets;
};

struct Batch {
	Entity_Id solver;
	std::vector<int> instrs;
	std::vector<Batch_Array> arrays;
	std::vector<Batch_Array> arrays_ode;
	
	Batch() : solver(invalid_entity_id) {}
};


inline void
error_print_instruction(Mobius_Model *model, Model_Instruction *instr) {
	if(instr->type == Model_Instruction::Type::compute_state_var)
		error_print("\"", model->state_vars[instr->var_id]->name, "\"");
	else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
		error_print("(\"", model->state_vars[instr->source_or_target_id]->name, "\" -= \"", model->state_vars[instr->var_id]->name, "\")");
	else if(instr->type == Model_Instruction::Type::add_flux_to_target) {
		error_print("(");
		if(is_valid(instr->neighbor))
			error_print("neighbor(");
		error_print("\"", model->state_vars[instr->source_or_target_id]->name, "\"");
		if(is_valid(instr->neighbor))
			error_print(")");
		error_print(" += \"", model->state_vars[instr->var_id]->name, "\")");
	}
}

inline void
print_partial_dependency_trace(Mobius_Model *model, Model_Instruction *we, Model_Instruction *dep) {
	error_print_instruction(model, dep);
	error_print(" <-- ");
	error_print_instruction(model, we);
	error_print("\n");
}

bool
topological_sort_instructions_visit(Mobius_Model *model, int instr_idx, std::vector<int> &push_to, std::vector<Model_Instruction> &instructions, bool initial) {
	Model_Instruction *instr = &instructions[instr_idx];
	
	if(instr->type == Model_Instruction::Type::invalid) return true;
	
	if(instr->visited) return true;
	if(instr->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the");
		if(initial) error_print(" initial value of the");
		error_print(" state variables:\n");
		return false;
	}
	instr->temp_visited = true;
	for(int dep : instr->depends_on_instruction) {
		bool success = topological_sort_instructions_visit(model, dep, push_to, instructions, initial);
		if(!success) {
			print_partial_dependency_trace(model, instr, &instructions[dep]);
			return false;
		}
	}
	instr->visited = true;
	push_to.push_back(instr_idx);
	return true;
}


void
put_var_lookup_indexes(Math_Expr_FT *expr, Model_Application *model_app, std::vector<Math_Expr_FT *> &index_expr) {
	for(auto arg : expr->exprs)
		put_var_lookup_indexes(arg, model_app, index_expr);
	
	if(expr->expr_type != Math_Expr_Type::identifier_chain) return;
	
	auto ident = reinterpret_cast<Identifier_FT *>(expr);
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = model_app->parameter_structure.get_offset_code(ident->parameter, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = model_app->series_structure.get_offset_code(ident->series, index_expr);
		back_step = model_app->series_structure.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		auto var = model_app->model->state_vars[ident->state_var];
		if(var->flags & State_Variable::Flags::f_invalid)
			fatal_error(Mobius_Error::internal, "put_var_lookup_indexes() Tried to look up the value of an invalid variable \"", var->name, "\".");
		
		offset_code = model_app->result_structure.get_offset_code(ident->state_var, index_expr);
		back_step = model_app->result_structure.total_count;
	}
	
	//TODO: Should check that we are not at the initial step
	if(offset_code && ident->variable_type != Variable_Type::parameter && (ident->flags & ident_flags_last_result)) {
		offset_code = make_binop('-', offset_code, make_literal(back_step));
	}
	
	if(offset_code)
		expr->exprs.push_back(offset_code);
}


Math_Expr_FT *
make_possibly_weighted_var_ident(Var_Id var_id, Math_Expr_FT *weight = nullptr, Math_Expr_FT *unit_conv = nullptr) {
	
	auto var_ident = make_state_var_identifier(var_id);
	
	if(unit_conv)
		var_ident = make_binop('*', var_ident, unit_conv);
	
	if(weight)
		var_ident = make_binop('*', var_ident, weight);
	
	return var_ident;
}

Math_Expr_FT *
add_or_subtract_var_from_agg_var(Model_Application *model_app, char oper, Math_Expr_FT *var_ident, Var_Id var_id_agg, std::vector<Math_Expr_FT *> &indexes, Entity_Id neighbor_id = invalid_entity_id) {

	Math_Expr_FT *offset_code_agg;
	Math_Expr_FT *index_ref = nullptr;
	
	if(is_valid(neighbor_id)) {
		// The aggregation was pointed at a neighboring index, not the same index as the current one.
		auto neighbor = model_app->model->neighbors[neighbor_id];
		
		auto cur_idx = indexes[neighbor->index_set.id];
		
		// TODO: for directed_trees, if the index count is 1, we know that this can't possibly go anywhere, and can be omitted, so we should just return a no-op.
		if(neighbor->type != Neighbor_Structure_Type::directed_tree)
			fatal_error(Mobius_Error::internal, "Unhandled neighbor type in add_or_subtract_flux_from_var()");
		
		auto index_offset = model_app->neighbor_structure.get_offset_code({neighbor_id, 0}, indexes); // NOTE: the 0 signifies that this is "data point" 0, and directed trees only have one.
		auto index = new Identifier_FT();
		index->variable_type = Variable_Type::neighbor_info;
		index->value_type = Value_Type::integer;
		index->exprs.push_back(index_offset);
		
		indexes[neighbor->index_set.id] = index;
		offset_code_agg = model_app->result_structure.get_offset_code(var_id_agg, indexes);
		indexes[neighbor->index_set.id] = cur_idx;  // Reset it for use by others;
		index_ref = index;
	} else
		offset_code_agg = model_app->result_structure.get_offset_code(var_id_agg, indexes);
	
	auto agg_ident = make_state_var_identifier(var_id_agg);
	agg_ident->exprs.push_back(offset_code_agg);
	
	auto sum_or_difference = make_binop(oper, agg_ident, var_ident);
	
	auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
	assignment->exprs.push_back(copy(offset_code_agg));
	assignment->exprs.push_back(sum_or_difference);
	assignment->value_type = Value_Type::none;
	
	if(index_ref) {
		// We have to check that the index is not negative (meaning there are no neighbors)
		// NOTE: we don't have to copy the index_ref here, because it should have been copied in its previous use.
		
		// TODO: Instead of having the branch we could have the index point at some random existing value, then multiply with a weight which is 0 if there is no target, and 1 otherwise.
		auto condition = make_binop(Token_Type::geq, index_ref, make_literal((s64)0));
		auto if_chain = new Math_Expr_FT();
		if_chain->expr_type = Math_Expr_Type::if_chain;
		if_chain->exprs.push_back(assignment);
		if_chain->exprs.push_back(condition);
		if_chain->exprs.push_back(make_literal((s64)0)); // The last "else" value is a dummy. We could make llvm and the emulator accept an if statement that doesn't have an else (?). Would also need to make the prune_tree understand it.
		if_chain->value_type = Value_Type::none;
		return if_chain;
	}
	
	return assignment;
}

Math_Expr_FT *
create_nested_for_loops(Model_Application *model_app, Math_Block_FT *top_scope, Batch_Array &array, std::vector<Math_Expr_FT *> &indexes) {
	for(auto &index_set : model_app->model->index_sets)
		indexes[index_set.id] = nullptr;    //note: just so that it is easy to catch if we somehow use an index we shouldn't
		
	Math_Block_FT *scope = top_scope;
	for(auto &index_set : array.index_sets) {
		auto loop = make_for_loop();
		loop->exprs.push_back(make_literal((s64)model_app->index_counts[index_set.id].index));
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
generate_run_code(Model_Application *model_app, Batch *batch, std::vector<Model_Instruction> &instructions, bool initial) {
	auto model = model_app->model;
	auto top_scope = new Math_Block_FT();
	
	std::vector<Math_Expr_FT *> indexes(model->index_sets.count());
	
	for(auto &array : batch->arrays) {
		Math_Expr_FT *scope = create_nested_for_loops(model_app, top_scope, array, indexes);
		
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			auto fun = instr->code;
			
			if(fun) {
				fun = copy(fun);
			
				//TODO: we should not do excessive lookups. Can instead keep them around as local vars and reference them (although llvm will probably optimize it).
				put_var_lookup_indexes(fun, model_app, indexes);
			} else if (instr->type != Model_Instruction::Type::clear_state_var)
				continue;
				//TODO: we coud set an explicit no-op flag on the instruction. If the flag is set, we expect a nullptr fun, otherwise a valid one.
				
				//else if(var->type != Decl_Type::quantity)
					//fatal_error(Mobius_Error::internal, "Some variable \"", var->name, "\" unexpectedly did not get a function tree before generate_run_code(). This should have been detected at an earlier stage.");
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				
				auto offset_code = model_app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				scope->exprs.push_back(assignment);
				
			} else if (instr->type == Model_Instruction::Type::subtract_flux_from_source) {
				
				auto result = add_or_subtract_var_from_agg_var(model_app, '-', fun, instr->source_or_target_id, indexes);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_flux_to_target) {
				
				auto result = add_or_subtract_var_from_agg_var(model_app, '+', fun, instr->source_or_target_id, indexes, instr->neighbor);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::add_to_aggregate) {
				
				auto result = add_or_subtract_var_from_agg_var(model_app, '+', fun, instr->source_or_target_id, indexes, instr->neighbor);
				scope->exprs.push_back(result);
				
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				
				auto offset = model_app->result_structure.get_offset_code(instr->var_id, indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((s64)0));
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
	s64 init_pos = model_app->result_structure.get_offset_base(instructions[batch->arrays_ode[0].instr_ids[0]].var_id);
	for(auto &array : batch->arrays_ode) {
		Math_Expr_FT *scope = create_nested_for_loops(model_app, top_scope, array, indexes);
				
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			if(instr->type != Model_Instruction::Type::compute_state_var)
				fatal_error(Mobius_Error::internal, "Somehow we got an instruction that is not a state var computation inside an ODE batch.\n");
			
			// NOTE: the code for an ode variable computes the derivative of the state variable, which is all ingoing fluxes minus all outgoing fluxes
			auto fun = instr->code;
			
			if(!fun)
				fatal_error(Mobius_Error::internal, "ODE variables should always be provided with generated code in instruction_codegen, but we got one without.");
			
			fun = copy(fun);
			
			put_var_lookup_indexes(fun, model_app, indexes);
			
			auto offset_var = model_app->result_structure.get_offset_code(instr->var_id, indexes);
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


void
resolve_index_set_dependencies(Model_Application *model_app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	Mobius_Model *model = model_app->model;
	
	// Collect direct dependencies coming from lookups in the declared functions of the variables.
	// NOTE: we can't just reuse the dependency sets we computed in model_composition, because some of the variables have undergone codegen between then.
	
	for(auto &instr : instructions) {
		
		if(!instr.code) continue;
		
		Dependency_Set code_depends;
		register_dependencies(instr.code, &code_depends);
		
		for(auto par_id : code_depends.on_parameter) {
			auto index_sets = model_app->parameter_structure.get_index_sets(par_id);
			instr.index_sets.insert(index_sets.begin(), index_sets.end()); //TODO: handle matrix parameters when we make those
		}
		for(auto series_id : code_depends.on_series) {
			auto index_sets = model_app->series_structure.get_index_sets(series_id);
			instr.index_sets.insert(index_sets.begin(), index_sets.end());
		}
		
		for(auto dep : code_depends.on_state_var) {
			if(dep.type == dep_type_none)
				instr.depends_on_instruction.insert(dep.var_id.id);
			instr.inherits_index_sets_from_instruction.insert(dep.var_id.id);
		}
	}
	
	// Let index set dependencies propagate from state variable to state variable. (For instance if a looks up the value of b, a needs to be indexed over (at least) the same index sets as b. This could propagate down a long chain of dependencies, so we have to keep iterating until nothing changes.
	bool changed;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto &instr : instructions) {
			
			int before = instr.index_sets.size();
			for(int dep : instr.inherits_index_sets_from_instruction) {
				auto dep_idx = &instructions[dep].index_sets;
				instr.index_sets.insert(dep_idx->begin(), dep_idx->end());
			}
			int after = instr.index_sets.size();
			if(before != after) changed = true;
		}
		
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Failed to resolve state variable index set dependencies in the alotted amount of iterations!");
}

void
debug_print_instruction(Mobius_Model *model, Model_Instruction *instr) {
	if(instr->type == Model_Instruction::Type::compute_state_var)
		warning_print("\"", model->state_vars[instr->var_id]->name, "\"\n");
	else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
		warning_print("\"", model->state_vars[instr->source_or_target_id]->name, "\" -= \"", model->state_vars[instr->var_id]->name, "\"\n");
	else if(instr->type == Model_Instruction::Type::add_flux_to_target) {
		if(is_valid(instr->neighbor)) warning_print("neighbor(");
		warning_print("\"", model->state_vars[instr->source_or_target_id]->name, "\"");
		if(is_valid(instr->neighbor)) warning_print(")");
		warning_print(" += \"", model->state_vars[instr->var_id]->name, "\"\n");
	} else if(instr->type == Model_Instruction::Type::clear_state_var)
		warning_print("\"", model->state_vars[instr->var_id]->name, "\" = 0\n");
	else if(instr->type == Model_Instruction::Type::add_to_aggregate)
		warning_print("\"", model->state_vars[instr->source_or_target_id]->name, "\" += \"", model->state_vars[instr->var_id]->name, "\" * weight\n");
}

void
debug_print_batch_array(Mobius_Model *model, std::vector<Batch_Array> &arrays, std::vector<Model_Instruction> &instructions) {
	for(auto &pre_batch : arrays) {
		warning_print("\t[");;
		for(auto index_set : pre_batch.index_sets)
			warning_print("\"", model->index_sets[index_set]->name, "\" ");
		warning_print("]\n");
		for(auto instr_id : pre_batch.instr_ids) {
			warning_print("\t\t");
			auto instr = &instructions[instr_id];
			debug_print_instruction(model, instr);
		}
	}
}

void
debug_print_batch_structure(Mobius_Model *model, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions) {
	warning_print("\n**** batch structure ****\n");
	for(auto &batch : batches) {
		if(is_valid(batch.solver))
			warning_print("  solver \"", model->solvers[batch.solver]->name, "\" :\n");
		else
			warning_print("  discrete :\n");
		debug_print_batch_array(model, batch.arrays, instructions);
		if(is_valid(batch.solver)) {
			warning_print("\t(ODE):\n");
			debug_print_batch_array(model, batch.arrays_ode, instructions);
		}
	}
	warning_print("\n\n");
}

void
build_batch_arrays(Model_Application *model_app, std::vector<int> &instrs, std::vector<Model_Instruction> &instructions, std::vector<Batch_Array> &batch_out, bool initial) {
	Mobius_Model *model = model_app->model;
	
	batch_out.clear();
	
	for(int instr_id : instrs) {
		Model_Instruction *instr = &instructions[instr_id];
		
		int earliest_possible_batch = batch_out.size();
		int earliest_suitable_pos   = batch_out.size();
		
		for(int sub_batch_idx = batch_out.size()-1; sub_batch_idx >= 0 ; --sub_batch_idx) {
			auto array = &batch_out[sub_batch_idx];
			
			bool blocked = false;
			bool found_dependency = false;
			
			for(auto other_id : array->instr_ids) {
				if(instr->depends_on_instruction.find(other_id) != instr->depends_on_instruction.end())
					found_dependency = true;
				if(instr->instruction_is_blocking.find(other_id) != instr->instruction_is_blocking.end())
					blocked = true;
			}
			
			if(array->index_sets == instr->index_sets && !blocked)
				earliest_possible_batch = sub_batch_idx;
			
			if(found_dependency || blocked) break;
			earliest_suitable_pos   = sub_batch_idx;
		}
		if(earliest_possible_batch != batch_out.size()) {
			batch_out[earliest_possible_batch].instr_ids.push_back(instr_id);
		} else {
			Batch_Array pre_batch;
			pre_batch.index_sets = instr->index_sets;
			pre_batch.instr_ids.push_back(instr_id);
			if(earliest_suitable_pos == batch_out.size())
				batch_out.push_back(std::move(pre_batch));
			else
				batch_out.insert(batch_out.begin()+earliest_suitable_pos, std::move(pre_batch));
		}
	}
	
#if 0
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	debug_print_batch_array(model, batch_out, instructions);
	warning_print("\n\n");
#endif
	
	// TODO: more passes!
}


void
create_initial_vars_for_lookups(Mobius_Model *model, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions) {
	for(auto arg : expr->exprs) create_initial_vars_for_lookups(model, arg, instructions);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var) {
			auto instr = &instructions[ident->state_var.id];
			
			if(instr->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
			
			// This function wants to look up the value of another variable, but it doesn't have initial code. If it has regular code, we can substitute that!
			
			auto var = model->state_vars[ident->state_var];
			//TODO: We have to be careful, because there are things that are allowed in regular code that is not allowed in initial code. We have to vet for it here!
				
			instr->type = Model_Instruction::Type::compute_state_var;
			instr->var_id = ident->state_var;
			if(var->function_tree) {
				instr->code = var->function_tree;
				// Have to do this recursively, since we may already have passed it in the outer loop.
				create_initial_vars_for_lookups(model, instr->code, instructions);
			} else
				instr->code = nullptr;
			
			// If it is an aggregation variable, whatever it aggregates also must be computed.
			if(var->flags & State_Variable::Flags::f_is_aggregate) {
				auto instr2 = &instructions[var->agg.id];
				
				if(instr2->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
				
				auto var2 = model->state_vars[var->agg];
				
				instr2->type = Model_Instruction::Type::compute_state_var;
				instr2->var_id = var->agg;
				if(var2->function_tree) {
					instr2->code = var2->function_tree;
					create_initial_vars_for_lookups(model, instr2->code, instructions);
				} else
					instr2->code = nullptr;
			}
			//TODO: if it is a conc and is not computed, do we need to check if the mass variable has an initial conc?
		}
	}
}


void
build_instructions(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	auto model = app->model;
	
	instructions.resize(model->state_vars.count());
	
	for(auto var_id : model->state_vars) {
		auto var = model->state_vars[var_id];
		auto fun = var->function_tree;
		if(initial) fun = var->initial_function_tree;
		if(!fun) {
			if(!initial && var->type != Decl_Type::quantity && 
				!(var->flags & State_Variable::Flags::f_is_aggregate) && 
				!(var->flags & State_Variable::Flags::f_in_flux_neighbor) &&
				!(var->flags & State_Variable::Flags::f_in_flux) &&
				!(var->flags & State_Variable::Flags::f_dissolved_flux) &&
				!(var->flags & State_Variable::Flags::f_dissolved_conc) &&
				!(var->flags & State_Variable::Flags::f_invalid))
				fatal_error(Mobius_Error::internal, "Somehow we got a state variable \"", var->name, "\" where the function code was unexpectedly not provided. This should have been detected at an earlier stage in model registration.");
			//if(initial)
			//	continue;     //TODO: we should reproduce the functionality from Mobius1 where the function_tree can act as the initial_function_tree (but only if it is referenced by another state var). But for now we just skip it.
		}
		
		Model_Instruction instr;
		instr.type = Model_Instruction::Type::compute_state_var;
		
		if(var->flags & State_Variable::Flags::f_invalid)
			instr.type = Model_Instruction::Type::invalid;
		
		if(!fun && initial)
			instr.type = Model_Instruction::Type::invalid;
		instr.var_id = var_id;
		instr.code = fun;
		
		instructions[var_id.id] = std::move(instr);
	}
	
	if(initial) {
		for(auto var_id : model->state_vars) {
			auto instr = &instructions[var_id.id];
			if(instr->type == Model_Instruction::Type::invalid) continue;
			if(!instr->code) continue;
			
			create_initial_vars_for_lookups(model, instr->code, instructions);
		}
	}
	
	
	// Put solvers on instructions for computing ODE variables.
	// Solvers are propagated to other instructions later based on need.
	if(!initial) {
		
		// NOTE: We tested the validity of the solve declaration in the model composition already, so we don't do it again here.
		for(auto id : model->solves) {
			auto solve = model->solves[id];
			Var_Id var_id = model->state_vars[solve->loc];
			
			instructions[var_id.id].solver = solve->solver;
		}
		
		for(auto var_id : model->state_vars) {
			auto var = model->state_vars[var_id];
			// Fluxes with an ODE variable as source is given the same solver as it.
			if(var->type == Decl_Type::flux && is_located(var->loc1)) {
				instructions[var_id.id].solver = instructions[model->state_vars[var->loc1].id].solver;
			}
			// Also set the solver for an aggregation variable for a neighbor flux.
			if(var->flags & State_Variable::Flags::f_in_flux_neighbor) {
				instructions[var_id.id].solver = instructions[var->neighbor_agg.id].solver;
			}
			// Dissolved fluxes of fluxes with solvers must be on solvers
			//TODO: make it work with dissolvedes of dissolvedes
			//TODO: what if the source was on another solver than the dissolved? Another reason to force them to be the same (unless overridden)?
			if(var->flags & State_Variable::Flags::f_dissolved_flux) {
				instructions[var_id.id].solver = instructions[var->dissolved_flux.id].solver;
			}
			
			// Also for concs. Can be overkill some times, but safer just to do it.
			if(var->flags & State_Variable::Flags::f_dissolved_conc) {
				instructions[var_id.id].solver = instructions[var->dissolved_conc.id].solver;
			}
		}
	}
	
	// Generate instructions needed to compute special variables.
	
	for(auto var_id : model->state_vars) {
		auto instr = &instructions[var_id.id];
		
		if(instr->type == Model_Instruction::Type::invalid) continue;
	
		auto var = model->state_vars[var_id];
		
		auto loc1 = var->loc1;
		auto loc2 = var->loc2;
		
		bool is_aggregate  = var->flags & State_Variable::Flags::f_is_aggregate;
		bool has_aggregate = var->flags & State_Variable::Flags::f_has_aggregate;
		
		auto var_solver = instr->solver;
		
		if(has_aggregate) {
			// var (var_id) is now the variable that is being aggregated.
			// instr is the instruction to compute it
			
			if(initial) {
				warning_print("*** *** *** initial agg for ", var->name, "\n");
			}
			
			auto aggr_var = model->state_vars[var->agg];    // aggr_var is the aggregation variable (the one we sum to).
			
			// We need to clear the aggregation variable to 0 between each time it is needed.
			int clear_idx = instructions.size();
			instructions.push_back({});
			
			int add_to_aggr_idx = instructions.size();
			instructions.push_back({});
			
			// The instruction for the aggr_var. It compiles to a no-op, but it is kept in the model structure to indicate the location of when this var has its final value. (also used for result storage structure).
			auto agg_instr = &instructions[var->agg.id];  
			
			// The instruction for clearing to 0
			auto clear_instr = &instructions[clear_idx];
			
			// The instruction that takes the value of var and adds it to aggr_var (with a weight)
			auto add_to_aggr_instr = &instructions[add_to_aggr_idx];
			
			// TODO: We may be able to remove a lot of explicit dependency declarations here now:
			
			// Since we generate one aggregation variable per target compartment, we have to give it the full index set dependencies of that compartment
			// TODO: we could generate one per variable that looks it up and prune them later if they have the same index set dependencies (?)
			auto agg_to_comp = model->compartments[aggr_var->agg_to_compartment];
			//agg_instr->inherits_index_sets_from_instruction.clear(); // Should be unnecessary. We just constructed it.
			
			agg_instr->index_sets.insert(agg_to_comp->index_sets.begin(), agg_to_comp->index_sets.end());
			agg_instr->solver = var_solver;
			agg_instr->depends_on_instruction.insert(add_to_aggr_idx); // The value of the aggregate is only done after we have finished summing to it.
			
			// Build the clear instruction
			add_to_aggr_instr->depends_on_instruction.insert(clear_idx);  // We can only sum to the aggregation after the clear.
			clear_instr->type = Model_Instruction::Type::clear_state_var;
			clear_instr->solver = var_solver;
			clear_instr->var_id = var->agg;     // The var_id of the clear_instr indicates which variable we want to clear.
			clear_instr->index_sets.insert(agg_to_comp->index_sets.begin(), agg_to_comp->index_sets.end());

			
			add_to_aggr_instr->type = Model_Instruction::Type::add_to_aggregate;
			add_to_aggr_instr->var_id = var_id;
			add_to_aggr_instr->source_or_target_id = var->agg;
			add_to_aggr_instr->depends_on_instruction.insert(var_id.id); // We can only sum the value in after it is computed.
			//add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id.id); // Sum it in each time it is computed. Unnecessary to declare since it is handled by new dependency system.
			add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var->agg.id); // We need to look it up every time we sum to it.
			add_to_aggr_instr->solver = var_solver;
			
			if(var->type == Decl_Type::flux && is_located(aggr_var->loc2)) {
				// If the aggregated variable is a flux, we potentially may have to tell it to get the right index sets.
				
				//note: this is only ok for auto-generated aggregations. Not if somebody called aggregate() on a flux explicitly, because then it is not necessarily the target of the flux that wants to know the aggregate.
				// But I guess you can't explicitly reference fluxes in code any way. We have to keep in mind that if that is implemented though.
				auto target_id = model->state_vars[aggr_var->loc2];
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(var->agg.id);
			}
		}
		
		if(var->flags & State_Variable::Flags::f_in_flux_neighbor) {
			
			if(initial)
				fatal_error(Mobius_Error::internal, "Got a neighbor flux in the initial step.");
			if(!is_valid(var_solver))
				fatal_error(Mobius_Error::internal, "Got aggregation variable for neighbor fluxes without a solver.");
			
			//warning_print("************ Found it\n");
			
			// var is the target aggregate
			// var->neighbor_agg  is the final quantity state variable for the target.
			
			// Find all the neighbor fluxes pointing to the target.
			for(auto var_id_flux : model->state_vars) {
				auto var_flux = model->state_vars[var_id_flux];
				
				if(var_flux->type != Decl_Type::flux || !is_valid(var_flux->neighbor)) continue;
				if(model->state_vars[var_flux->loc1] != var->neighbor_agg) continue;
				
				// Create instruction to add the in flux to the target aggregate.
				int clear_id = instructions.size();
				instructions.push_back({});
				int add_to_aggr_id = instructions.size();
				instructions.push_back({});
				
				auto clear_instr       = &instructions[clear_id];
				
				clear_instr->type = Model_Instruction::Type::clear_state_var;
				clear_instr->solver = var_solver;
				clear_instr->var_id = var_id;     // The var_id of the clear_instr indicates which variable we want to clear.
				clear_instr->inherits_index_sets_from_instruction.insert(add_to_aggr_id);
				
				auto add_to_aggr_instr = &instructions[add_to_aggr_id];
				
				add_to_aggr_instr->type = Model_Instruction::Type::add_to_aggregate;
				add_to_aggr_instr->var_id = var_id_flux;
				add_to_aggr_instr->source_or_target_id = var_id;
				add_to_aggr_instr->depends_on_instruction.insert(clear_id); // Only start summing up after we cleared to 0.
				add_to_aggr_instr->depends_on_instruction.insert(var_id_flux.id); // We can only sum the value in after it is computed.
				//add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id_flux.id); // Sum it in each time it is computed. Should no longer be needed with the new dependency system
				add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id.id); // We need one per target of the neighbor fluxes.
				add_to_aggr_instr->solver = var_solver;
				add_to_aggr_instr->neighbor = var_flux->neighbor;
				
				// This says that the clear_id has to be in a separate for loop from this instruction
				add_to_aggr_instr->instruction_is_blocking.insert(clear_id);
				
				instructions[var_id.id].depends_on_instruction.insert(add_to_aggr_id);
				instructions[var_id.id].inherits_index_sets_from_instruction.insert(var->neighbor_agg.id); // TODO: Is this necessary, or do we just have to insert the particular index set of the neighbor relation?
				// This is not needed because the target is always an ODE:
				//instructions[var->neighbor_agg.id].depends_on_instruction.insert(var_id.id); // The target must be computed after the aggregation variable.
			}
		}
		
		if(initial || var->type != Decl_Type::flux) continue;
		
		Entity_Id source_solver = invalid_entity_id;
		Var_Id source_id;
		if(is_located(loc1) && !is_aggregate) {
			source_id = model->state_vars[loc1];
			Model_Instruction *source = &instructions[source_id.id];
			source_solver = source->solver;
				
			if(!is_valid(source_solver) && !model->state_vars[source_id]->override_tree) {
				Model_Instruction sub_source_instr;
				sub_source_instr.type = Model_Instruction::Type::subtract_flux_from_source;
				sub_source_instr.var_id = var_id;
				
				sub_source_instr.depends_on_instruction.insert(var_id.id);     // the subtraction of the flux has to be done after the flux is computed.
				//sub_source_instr.inherits_index_sets_from_instruction.insert(var_id.id); // it also has to be done once per instance of the flux. Should no longer be needed with new dependency system
				sub_source_instr.inherits_index_sets_from_instruction.insert(source_id.id); // and it has to be done per instance of the source.
				
				sub_source_instr.source_or_target_id = source_id;
				
				//NOTE: the "compute state var" of the source "happens" after the flux has been subtracted. In the discrete case it will not generate any code, but it is useful to keep it as a stub so that other vars that depend on it happen after it (and we don't have to make them depend on all the fluxes from the var instead).
				int sub_idx = (int)instructions.size();
				source->depends_on_instruction.insert(sub_idx);
								
				// The flux itself has to be computed once per instance of the source.
				// 		The reason for this is that for discrete fluxes we generate a   flux := min(flux, source) in order to not send the source negative.
				// Should no longer be necessary to fix this here with the new dependency system.
				//instructions[var_id.id].inherits_index_sets_from_instruction.insert(source_id.id);
				
				instructions.push_back(std::move(sub_source_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
		bool is_neighbor = is_valid(var->neighbor);
		if(is_neighbor && has_aggregate)
			fatal_error(Mobius_Error::internal, "Somehow a neighbor flux got an aggregate");
		
		if(is_neighbor) {
			auto neighbor = model->neighbors[var->neighbor];
			if(neighbor->type != Neighbor_Structure_Type::directed_tree)
				fatal_error(Mobius_Error::internal, "Unsupported neighbor structure in build_instructions().");
			// NOTE: the source and target id for the neighbor-flux are the same, but loc2 doesn't record the target in this case, so we use the source_id.
			Model_Instruction *target = &instructions[source_id.id];
			// If we have fluxes of two instances of the same quantity, we have to enforce that it is indexed by by the index set of that neighbor relation.
			target->index_sets.insert(neighbor->index_set);
		}
		
		if((is_located(loc2) || is_neighbor) && !has_aggregate) {
			Var_Id target_id;
			if(is_neighbor)
				target_id = source_id;	
			else
				target_id = model->state_vars[loc2];
			
			Model_Instruction *target = &instructions[target_id.id];
			Entity_Id target_solver = target->solver;
			
			if(!is_valid(target_solver) && !model->state_vars[target_id]->override_tree) {
				Model_Instruction add_target_instr;
				add_target_instr.type   = Model_Instruction::Type::add_flux_to_target;
				add_target_instr.var_id = var_id;
				
				add_target_instr.depends_on_instruction.insert(var_id.id);   // the addition of the flux has to be done after the flux is computed.
				//add_target_instr.inherits_index_sets_from_instruction.insert(var_id.id);  // it also has to be done (at least) once per instance of the flux. Should no longer be needed to be declared explicitly with new dependency system.
				add_target_instr.inherits_index_sets_from_instruction.insert(target_id.id); // it has to be done once per instance of the target.
				add_target_instr.source_or_target_id = target_id;
				if(is_neighbor) {
					// the index sets already depend on the flux itself, which depends on the source, so we don't have to redeclare that.
					add_target_instr.neighbor = var->neighbor;
				}
				
				int add_idx = (int)instructions.size();
				if(!is_neighbor)
					target->depends_on_instruction.insert(add_idx);
				
				// NOTE: this one is needed because of unit conversions, which could give an extra index set dependency to the add instruction.
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(add_idx);
				
				instructions.push_back(std::move(add_target_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
	}
}



// give all properties the solver if it is "between" quantities or fluxes with that solver in the dependency tree.
bool propagate_solvers(Mobius_Model *model, int instr_id, Entity_Id solver, std::vector<Model_Instruction> &instructions) {
	auto instr = &instructions[instr_id];
	Decl_Type decl_type = model->state_vars[instr->var_id]->type;
	
	if(instr->solver == solver)
		return true;
	if(instr->visited)
		return false;
	instr->visited = true;
	
	bool found = false;
	for(int dep : instr->depends_on_instruction) {
		if(propagate_solvers(model, dep, solver, instructions))
			found = true;
	}
	if(found) {
		if(is_valid(instr->solver) && instr->solver != solver) {
			// ooops, we already wanted to put it on another solver.
			//TODO: we must give a much better error message here. This is not parseable to regular users.
			// print a dependency trace or something like that!
			fatal_error(Mobius_Error::model_building, "The state variable \"", model->state_vars[instr->var_id]->name, "\" is lodged between multiple ODE solvers.");
		}
		
		if(decl_type != Decl_Type::quantity)
			instr->solver = solver;
	}
	instr->visited = false;
	
	return found;
}

void create_batches(Mobius_Model *model, std::vector<Batch> &batches_out, std::vector<Model_Instruction> &instructions) {
	// create one batch per solver
	// if a quantity has a solver, it goes in that batch.
	// a flux goes in the batch of its source always, (same with the subtraction of that flux).
	// addition of flux goes in the batch of its target.
	// batches are ordered by dependence
	// properties:
	//	- if it is "between" other vars from the same batch, it goes in that batch.
	// discrete batches:
	// 	- can be multiple of these. 
	
	// TODO : this may no longer work, and we may have to have one more outer loop. This is because they are not sorted first.
	//   but we can also not sort them first because we have to remove some dependencies after adding solvers and before sorting...
	warning_print("Propagate solvers\n");
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto instr = &instructions[instr_id];
		
		if(instr->type == Model_Instruction::Type::invalid) continue;
		
		if(is_valid(instr->solver)) {
			for(int dep : instr->depends_on_instruction)
				propagate_solvers(model, dep, instr->solver, instructions);
		}
	}
	
	warning_print("Remove ode dependencies\n");
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto &instr = instructions[instr_id];
		
		if(instr.type == Model_Instruction::Type::invalid) continue;
		
		if(is_valid(instr.solver)) {
			// Remove dependency of any instruction on an ode variable if they are on the same solver.
			std::vector<int> remove;
			for(int other_id : instr.depends_on_instruction) {
				auto &other_instr = instructions[other_id];
				if(other_instr.solver == instr.solver && model->state_vars[other_instr.var_id]->type == Decl_Type::quantity)
					remove.push_back(other_id);
			}
			for(int rem : remove)
				instr.depends_on_instruction.erase(rem);
		} else if (model->state_vars[instr.var_id]->type == Decl_Type::flux) {
			// Remove dependency of discrete fluxes on their sources. Discrete fluxes are ordered in a specific way, and the final value of the source comes after the flux is subtracted.
			auto var = model->state_vars[instr.var_id];
			if(is_located(var->loc1)) {
				instr.depends_on_instruction.erase(model->state_vars[var->loc1].id);
			}
		}
	}
	
	warning_print("Sorting begin\n");
	std::vector<int> sorted_instructions;
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		if(instructions[instr_id].type == Model_Instruction::Type::invalid) continue;
		
		bool success = topological_sort_instructions_visit(model, instr_id, sorted_instructions, instructions, false);
		if(!success) mobius_error_exit();
	}
	
	batches_out.clear();
	
	warning_print("Create batches\n");
	// TODO: this algorithm is repeated only with slightly different checks (solver vs. index sets). Is there a way to unify the code?
	for(int instr_id : sorted_instructions) {
		auto instr = &instructions[instr_id];
		int first_suitable_batch = batches_out.size();
		int first_suitable_location = batches_out.size();
		
		for(int batch_idx = batches_out.size()-1; batch_idx >= 0; --batch_idx) {
			auto batch = &batches_out[batch_idx];
			if(is_valid(batch->solver) && batch->solver == instr->solver) {
				first_suitable_batch = batch_idx;
				break;   // we ever only want one batch per solver, so we can stop looking now.
			} else if (!is_valid(batch->solver) && !is_valid(instr->solver)) {
				first_suitable_batch = batch_idx;
			}
			
			// note : we can't place ourselves earlier than another instruction that we depend on.
			bool found_dependency = false;
			for(int other_id : batch->instrs) {
				if(instr->depends_on_instruction.find(other_id) != instr->depends_on_instruction.end()) {
					found_dependency = true;
					break;
				}
			}
			if(found_dependency)
				break;
			first_suitable_location = batch_idx;
		}
		if(first_suitable_batch != batches_out.size()) {
			batches_out[first_suitable_batch].instrs.push_back(instr_id);
		} else {
			Batch batch;
			batch.instrs.push_back(instr_id);
			batch.solver = instr->solver;
			if(first_suitable_location == batches_out.size())
				batches_out.push_back(std::move(batch));
			else
				batches_out.insert(batches_out.begin() + first_suitable_location, std::move(batch));
		}
	}
	
	//NOTE: we do more passes to try and group instructions in an optimal way.
	
	bool changed = false;
	for(int it = 0; it < 10; ++it) {
		changed = false;
		
		int batch_idx = 0;
		for(auto &batch : batches_out) {
			int instr_idx = batch.instrs.size() - 1;
			while(instr_idx >= 0) {
				int instr_id = batch.instrs[instr_idx];
				
				bool cont = false;
				// If another instruction behind us in the same batch depends on us, we are not allowed to move!
				for(int instr_behind_idx = instr_idx+1; instr_behind_idx < batch.instrs.size(); ++instr_behind_idx) {
					int behind_id = batch.instrs[instr_behind_idx];
					auto behind = &instructions[behind_id];
					if(behind->depends_on_instruction.find(instr_id) != behind->depends_on_instruction.end()) {
						cont = true;
						break;
					}
				}
				if(cont) {
					--instr_idx;
					continue;
				}
				
				int last_suitable_batch_idx = batch_idx;
				for(int batch_behind_idx = batch_idx + 1; batch_behind_idx < batches_out.size(); ++batch_behind_idx) {
					Batch &batch_behind = batches_out[batch_behind_idx];
					if((batch_behind.solver == batch.solver) || (!is_valid(batch_behind.solver) && !is_valid(batch.solver)))
						last_suitable_batch_idx = batch_behind_idx;
					bool batch_depends_on_us = false;
					for(int instr_behind_idx = 0; instr_behind_idx < batch_behind.instrs.size(); ++instr_behind_idx) {
						int behind_id = batch_behind.instrs[instr_behind_idx];
						auto behind = &instructions[behind_id];
						if(behind->depends_on_instruction.find(instr_id) != behind->depends_on_instruction.end()) {
							batch_depends_on_us = true;
							break;
						}
					}
					if(batch_depends_on_us || batch_behind_idx == batches_out.size()-1) {
						if(last_suitable_batch_idx != batch_idx) {
							// We are allowed to move. Move to the beginning of the first other batch that is suitable.
							Batch &insert_to = batches_out[last_suitable_batch_idx];
							insert_to.instrs.insert(insert_to.instrs.begin(), instr_id);
							batch.instrs.erase(batch.instrs.begin()+instr_idx); // NOTE: it is safe to do this since we are iterating instr_idx from the end to the beginning
							changed = true;
						}
						break;
					}
				}
				--instr_idx;
			}
			++batch_idx;
		}
		
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Unable to optimize instruction batch grouping in the allotted amount of iterations.");
	
	// Remove batches that were emptied as a result of the step above.
	int batch_idx = batches_out.size()-1;
	while(batch_idx >= 0) {
		Batch &batch = batches_out[batch_idx];
		if(batch.instrs.empty())
			batches_out.erase(batches_out.begin() + batch_idx);  // NOTE: ok since we are iterating batch_idx backwards.
		--batch_idx;
	}
	
	// TODO: We need to verify that each solver is only given one batch!! Ideally we should just group instructions by solver initially and then sort the groups (where each discrete instruction is just given its own group)?
	
#if 0
	warning_print("*** Batches before internal structuring: ***\n");
	for(auto &batch : batches_out) {
		if(is_valid(batch.solver))
			warning_print("  solver(\"", model->solvers[batch.solver]->name, "\") :\n");
		else
			warning_print("  discrete :\n");
		for(int instr_id : batch.instrs) {
			warning_print("\t\t");
			debug_print_instruction(model, &instructions[instr_id]);
		}
	}
	warning_print("\n\n");
#endif
}


void
add_array(std::vector<Multi_Array_Structure<Var_Id>> &structure, Batch_Array &array, std::vector<Model_Instruction> &instructions) {
	std::vector<Entity_Id> index_sets(array.index_sets.begin(), array.index_sets.end()); // TODO: eventually has to be more sophisticated if we have multiple-dependencies on the same index set.
	std::vector<Var_Id>    handles;
	for(int instr_id : array.instr_ids) {
		auto instr = &instructions[instr_id];
		if(instr->type == Model_Instruction::Type::compute_state_var)
			handles.push_back(instr->var_id);
	}
	Multi_Array_Structure<Var_Id> arr(std::move(index_sets), std::move(handles));
	structure.push_back(std::move(arr));
}

void
set_up_result_structure(Model_Application *model_app, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions) {
	if(model_app->result_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up result structure twice.");
	if(!model_app->all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up result structure before all index sets received indexes.");
	
	// NOTE: we just copy the batch structure so that it is easier to optimize the run code for cache locality.
	// NOTE: It is crucial that all ode variables from the same batch are stored contiguously, so that part of the setup must be kept no matter what!
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	for(auto &batch : batches) {
		for(auto &array : batch.arrays)      add_array(structure, array, instructions);
		for(auto &array : batch.arrays_ode)  add_array(structure, array, instructions);
	}
	
	model_app->result_structure.set_up(std::move(structure));
}

void
instruction_codegen(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	auto model = app->model;
	
	if(initial) {
		for(auto &instr : instructions) {
			
			// Initial values for dissolved quantities
			if(instr.type == Model_Instruction::Type::compute_state_var && instr.code && is_valid(instr.var_id)) {
				
				auto var = model->state_vars[instr.var_id];  // var is the mass/volume of the quantity
				
				auto conc = var->dissolved_conc;
				// NOTE: it is easier just to set it for both the mass and conc as we process the mass
				if(is_valid(conc) && !(var->flags & State_Variable::Flags::f_dissolved_conc) && !(var->flags & State_Variable::Flags::f_dissolved_flux)) {
					auto dissolved_in = model->state_vars[remove_dissolved(var->loc1)];
						
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
			auto var = model->state_vars[instr.var_id];
			
			//TODO: For overridden quantities they could just be removed from the solver. Also they shouldn't get any index set dependencies from fluxes connected to them.
			//    not sure about the best way to do it (or where).
			
			// Directly override the mass of a quantity
			if(var->type == Decl_Type::quantity && var->override_tree && !var->override_is_conc) {
				instr.code = var->override_tree;
			}
			
			// Codegen for concs of dissolved variables
			if(var->type == Decl_Type::property && (var->flags & State_Variable::Flags::f_dissolved_conc) ) {
				
				auto mass = var->dissolved_conc;
				auto mass_var = model->state_vars[mass];
				auto dissolved_in = model->state_vars[remove_dissolved(mass_var->loc1)];
				
				if(mass_var->override_tree && mass_var->override_is_conc) {
					instr.code = mass_var->override_tree;

					// If we override the conc, the mass is instead  conc*volume_we_are_dissolved_in .
					auto mass_instr = &instructions[mass.id];
					mass_instr->code = make_binop('*', make_state_var_identifier(instr.var_id), make_state_var_identifier(dissolved_in));
				} else {
					instr.code = make_safe_divide(make_state_var_identifier(mass), make_state_var_identifier(dissolved_in));
				}
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
					Var_Id source_id = model->state_vars[var->loc1];
					auto source_ref = reinterpret_cast<Identifier_FT *>(make_state_var_identifier(source_id));
					instr.code = make_intrinsic_function_call(Value_Type::real, "min", instr.code, source_ref);
				}
			}
			
			// TODO: same problem as elsewhere: O(n) operation to look up all fluxes to or from a given state variable.
			//   Make a lookup accelleration for this?
			
			// Codegen for in_fluxes:
			if(var->flags & State_Variable::Flags::f_in_flux) {
				Math_Expr_FT *flux_sum = make_literal(0.0);
				for(auto flux_id : model->state_vars) {
					auto flux_var = model->state_vars[flux_id];
					if(flux_var->flags & State_Variable::Flags::f_invalid) continue;
					// NOTE: by design we don't include neighbor fluxes in the in_flux. May change that later.
					if(flux_var->type == Decl_Type::flux && !is_valid(flux_var->neighbor) && is_located(flux_var->loc2) && model->state_vars[flux_var->loc2] == var->in_flux_target) {
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
				auto neigh_agg = var->neighbor_agg; // aggregation variable for values coming from neighbor fluxes.
				if(is_valid(neigh_agg))
					fun = make_state_var_identifier(neigh_agg);
				else
					fun = make_literal((double)0.0);
				
				for(Var_Id flux_id : model->state_vars) {
					auto flux = model->state_vars[flux_id];
					if(flux->flags & State_Variable::Flags::f_invalid) continue;
					if(flux->type != Decl_Type::flux) continue;
					
					if(is_located(flux->loc1) && model->state_vars[flux->loc1] == instr.var_id) {
						auto flux_ref = make_state_var_identifier(flux_id);
						fun = make_binop('-', fun, flux_ref);
					}
					
					// TODO: if we explicitly computed an in_flux earlier, we could just refer to it here instead of re-computing it.
					if(is_located(flux->loc2) && !is_valid(flux->neighbor) && model->state_vars[flux->loc2] == instr.var_id) {
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
			
			auto unit_conv = model->state_vars[instr.var_id]->unit_conversion_tree;
			instr.code = make_possibly_weighted_var_ident(instr.var_id, nullptr, unit_conv);
			
		} else if (instr.type == Model_Instruction::Type::add_to_aggregate) {
			
			auto agg_var = model->state_vars[instr.source_or_target_id];
			
			auto weight = agg_var->aggregation_weight_tree;
			if(!weight && !is_valid(instr.neighbor))  // NOTE: no default weight for neighbor fluxes.
				fatal_error(Mobius_Error::internal, "Somehow we got an aggregation without code for computing the weight.");
			
			if(weight)
				weight = copy(weight);
			
			instr.code = make_possibly_weighted_var_ident(instr.var_id, weight, nullptr);
		}
	}
}

void
Model_Application::compile() {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application twice.");
	
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before all index sets had received indexes.");
	
	if(!series_structure.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before input series data was set up.");
	if(!parameter_structure.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before parameter data was set up.");
	if(!neighbor_structure.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before neighbor data was set up.");
	
	warning_print("Create instruction arrays\n");
	std::vector<Model_Instruction> initial_instructions;
	std::vector<Model_Instruction> instructions;
	build_instructions(this, initial_instructions, true);
	build_instructions(this, instructions, false);
	
	warning_print("Instruction codegen\n");
	// We can't check it like this anymore, because we can now invalidate instructions due to state variables being invalidated.
	//for(auto &instr : instructions)
	//	if(instr.type == Model_Instruction::Type::invalid)
	//		fatal_error(Mobius_Error::internal, "Did not set up instruction types properly.");
	
	instruction_codegen(this, initial_instructions, true);
	instruction_codegen(this, instructions, false);

	warning_print("Resolve index sets dependencies begin.\n");
	resolve_index_set_dependencies(this, initial_instructions, true);
	
	// NOTE: state var inherits all index set dependencies from its initial code.
	for(auto var_id : model->state_vars) {
		auto &init_idx = initial_instructions[var_id.id].index_sets;
		
		instructions[var_id.id].index_sets.insert(init_idx.begin(), init_idx.end());
	}
	
	resolve_index_set_dependencies(this, instructions, false);
	
	// similarly, the initial state of a varialble has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
	for(auto var_id : model->state_vars)
		initial_instructions[var_id.id].index_sets = instructions[var_id.id].index_sets;
	
	std::vector<Batch> batches;
	create_batches(model, batches, instructions);
	
	Batch initial_batch;
	initial_batch.solver = invalid_entity_id;
	
	warning_print("Sort initial.\n");
	// Sort the initial instructions too.
	for(int instr_id = 0; instr_id < initial_instructions.size(); ++instr_id) {
		bool success = topological_sort_instructions_visit(model, instr_id, initial_batch.instrs, initial_instructions, true);
		if(!success) mobius_error_exit();
	}
	
	warning_print("Build pre batches.\n");
	build_batch_arrays(this, initial_batch.instrs, initial_instructions, initial_batch.arrays, true);
	
	for(auto &batch : batches) {
		if(!is_valid(batch.solver))
			build_batch_arrays(this, batch.instrs, instructions, batch.arrays, false);
		else {
			std::vector<int> vars;
			std::vector<int> vars_ode;
			for(int var : batch.instrs) {
				auto var_ref = model->state_vars[instructions[var].var_id];
				// NOTE: if we override the conc or value of var, we instead compute the mass from the conc.
				if((var_ref->type == Decl_Type::quantity) && (instructions[var].type == Model_Instruction::Type::compute_state_var) && !var_ref->override_tree)
					vars_ode.push_back(var);
				else
					vars.push_back(var);
			}
			build_batch_arrays(this, vars,     instructions, batch.arrays,     false);
			build_batch_arrays(this, vars_ode, instructions, batch.arrays_ode, false);
		}
	}
	
	warning_print("**** initial batch:\n");
	debug_print_batch_array(model, initial_batch.arrays, initial_instructions);
	
	debug_print_batch_structure(model, batches, instructions);
	
	set_up_result_structure(this, batches, instructions);
	
	//TODO: add back in!
	
	LLVM_Constant_Data constants;
	constants.neighbor_data       = data.neighbors.data;
	constants.neighbor_data_count = neighbor_structure.total_count;
	
	jit_add_global_data(llvm_data, &constants);
	
	warning_print("Generate inital run code\n");
	this->initial_batch.run_code = generate_run_code(this, &initial_batch, initial_instructions, true);
	jit_add_batch(this->initial_batch.run_code, "initial_values", llvm_data);
	
	warning_print("Generate main run code\n");
	
	int batch_idx = 0;
	for(auto &batch : batches) {
		Run_Batch new_batch;
		new_batch.run_code = generate_run_code(this, &batch, instructions, false);
		if(is_valid(batch.solver)) {
			auto solver = model->solvers[batch.solver];
			new_batch.solver_fun = solver->solver_fun;
			new_batch.h          = solver->h;
			new_batch.hmin       = solver->hmin;
			// NOTE: same as noted above, all ODEs have to be stored contiguously.
			new_batch.first_ode_offset = result_structure.get_offset_base(instructions[batch.arrays_ode[0].instr_ids[0]].var_id);
			new_batch.n_ode = 0;
			for(auto &array : batch.arrays_ode) {         // Hmm, this is a bit inefficient, but oh well.
				for(int instr_id : array.instr_ids)
					new_batch.n_ode += result_structure.instance_count(instructions[instr_id].var_id);
			}
		}
		
		std::string function_name = std::string("batch_function_") + std::to_string(batch_idx);
		jit_add_batch(new_batch.run_code, function_name, llvm_data);
		
		this->batches.push_back(new_batch);
		
		++batch_idx;
	}
	
	jit_compile_module(llvm_data);
	
	this->initial_batch.compiled_code = get_jitted_batch_function("initial_values");
	batch_idx = 0;
	for(auto &batch : this->batches) {
		std::string function_name = std::string("batch_function_") + std::to_string(batch_idx);
		batch.compiled_code = get_jitted_batch_function(function_name);
		++batch_idx;
	}
	
	is_compiled = true;
}
