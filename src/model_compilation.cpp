
#include "model_application.h"
#include "function_tree.h"
#include "model_codegen.h"

#include <string>

inline void
error_print_instruction(Model_Application *app, Model_Instruction *instr) {
	if(instr->type == Model_Instruction::Type::compute_state_var)
		error_print("\"", app->state_vars[instr->var_id]->name, "\"");
	else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
		error_print("(\"", app->state_vars[instr->source_or_target_id]->name, "\" -= \"", app->state_vars[instr->var_id]->name, "\")");
	else if(instr->type == Model_Instruction::Type::add_flux_to_target) {
		error_print("(");
		if(is_valid(instr->connection))
			error_print("connection(");
		error_print("\"", app->state_vars[instr->source_or_target_id]->name, "\"");
		if(is_valid(instr->connection))
			error_print(")");
		error_print(" += \"", app->state_vars[instr->var_id]->name, "\")");
	}
}

inline void
print_partial_dependency_trace(Model_Application *app, Model_Instruction *we, Model_Instruction *dep) {
	error_print_instruction(app, dep);
	error_print(" <-- ");
	error_print_instruction(app, we);
	error_print("\n");
}

bool
topological_sort_instructions_visit(Model_Application *app, int instr_idx, std::vector<int> &push_to, std::vector<Model_Instruction> &instructions, bool initial) {
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
		bool success = topological_sort_instructions_visit(app, dep, push_to, instructions, initial);
		if(!success) {
			print_partial_dependency_trace(app, instr, &instructions[dep]);
			return false;
		}
	}
	instr->visited = true;
	push_to.push_back(instr_idx);
	return true;
}

void
resolve_index_set_dependencies(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	Mobius_Model *model = app->model;
	
	// Collect direct dependencies coming from lookups in the declared functions of the variables.
	// NOTE: we can't just reuse the dependency sets we computed in model_composition, because some of the variables have undergone codegen between then.
	
	for(auto &instr : instructions) {
		
		if(!instr.code) continue;
		
		Dependency_Set code_depends;
		register_dependencies(instr.code, &code_depends);
		
		for(auto par_id : code_depends.on_parameter) {
			auto index_sets = app->parameter_structure.get_index_sets(par_id);
			instr.index_sets.insert(index_sets.begin(), index_sets.end()); //TODO: handle matrix parameters when we make those
		}
		for(auto series_id : code_depends.on_series) {
			auto index_sets = app->series_structure.get_index_sets(series_id);
			instr.index_sets.insert(index_sets.begin(), index_sets.end());
		}
		
		for(auto dep : code_depends.on_state_var) {
			if(dep.type == State_Var_Dependency::Type::none)
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
debug_print_instruction(Model_Application *app, Model_Instruction *instr) {
	if(instr->type == Model_Instruction::Type::compute_state_var)
		warning_print("\"", app->state_vars[instr->var_id]->name, "\"\n");
	else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
		warning_print("\"", app->state_vars[instr->source_or_target_id]->name, "\" -= \"", app->state_vars[instr->var_id]->name, "\"\n");
	else if(instr->type == Model_Instruction::Type::add_flux_to_target) {
		if(is_valid(instr->connection)) warning_print("connection(");
		warning_print("\"", app->state_vars[instr->source_or_target_id]->name, "\"");
		if(is_valid(instr->connection)) warning_print(")");
		warning_print(" += \"", app->state_vars[instr->var_id]->name, "\"\n");
	} else if(instr->type == Model_Instruction::Type::clear_state_var)
		warning_print("\"", app->state_vars[instr->var_id]->name, "\" = 0\n");
	else if(instr->type == Model_Instruction::Type::add_to_aggregate)
		warning_print("\"", app->state_vars[instr->source_or_target_id]->name, "\" += \"", app->state_vars[instr->var_id]->name, "\" * weight\n");
}

void
debug_print_batch_array(Model_Application *app, std::vector<Batch_Array> &arrays, std::vector<Model_Instruction> &instructions) {
	for(auto &pre_batch : arrays) {
		warning_print("\t[");;
		for(auto index_set : pre_batch.index_sets)
			warning_print("\"", app->model->index_sets[index_set]->name, "\" ");
		warning_print("]\n");
		for(auto instr_id : pre_batch.instr_ids) {
			warning_print("\t\t");
			auto instr = &instructions[instr_id];
			debug_print_instruction(app, instr);
		}
	}
}

void
debug_print_batch_structure(Model_Application *app, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions) {
	warning_print("\n**** batch structure ****\n");
	for(auto &batch : batches) {
		if(is_valid(batch.solver))
			warning_print("  solver \"", app->model->solvers[batch.solver]->name, "\" :\n");
		else
			warning_print("  discrete :\n");
		debug_print_batch_array(app, batch.arrays, instructions);
		if(is_valid(batch.solver)) {
			warning_print("\t(ODE):\n");
			debug_print_batch_array(app, batch.arrays_ode, instructions);
		}
	}
	warning_print("\n\n");
}

void
build_batch_arrays(Model_Application *app, std::vector<int> &instrs, std::vector<Model_Instruction> &instructions, std::vector<Batch_Array> &batch_out, bool initial) {
	Mobius_Model *model = app->model;
	
	batch_out.clear();
	
	for(int instr_id : instrs) {
		Model_Instruction *instr = &instructions[instr_id];
		
		int earliest_possible_batch = batch_out.size();
		int earliest_suitable_pos   = batch_out.size();
		
		for(int sub_batch_idx = batch_out.size()-1; sub_batch_idx >= 0 ; --sub_batch_idx) {
			auto sub_batch = &batch_out[sub_batch_idx];
			
			bool blocked = false;
			bool found_dependency = false;
			
			for(auto other_id : sub_batch->instr_ids) {
				if(instr->depends_on_instruction.find(other_id) != instr->depends_on_instruction.end())
					found_dependency = true;
				if(instr->instruction_is_blocking.find(other_id) != instr->instruction_is_blocking.end())
					blocked = true;
			}
			
			if(sub_batch->index_sets == instr->index_sets && !blocked)
				earliest_possible_batch = sub_batch_idx;
			
			if(found_dependency || blocked) break;
			earliest_suitable_pos   = sub_batch_idx;
		}
		if(earliest_possible_batch != batch_out.size()) {
			batch_out[earliest_possible_batch].instr_ids.push_back(instr_id);
		} else {
			Batch_Array sub_batch;
			sub_batch.index_sets = instr->index_sets;
			sub_batch.instr_ids.push_back(instr_id);
			if(earliest_suitable_pos == batch_out.size())
				batch_out.push_back(std::move(sub_batch));
			else
				batch_out.insert(batch_out.begin()+earliest_suitable_pos, std::move(sub_batch));
		}
	}
	
	
	// NOTE: Do more passes to try and group instructions in an optimal way:
#if 0
	bool changed = false;
	for(int it = 0; it < 10; ++it) {
		changed = false;
		
		int batch_idx = 0;
		for(auto &sub_batch : batch_out) {
			int instr_idx = sub_batch.instr_ids.size() - 1;
			while(instr_idx > 0) {
				int instr_id = sub_batch.instr_ids[instr_idx];
				bool cont = false;
				// If another instruction behind us in the same batch depends on us, we are not allowed to move!
				for(int instr_behind_idx = instr_idx+1; instr_behind_idx < sub_batch.instr_ids.size(); ++instr_behind_idx) {
					int behind_id = sub_batch.instr_ids[instr_behind_idx];
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
				
				// We attempt to move instructions to a later batch if we are allowed to move past all the instructions in between.
				int last_suitable_batch_idx = batch_idx;
				for(int batch_behind_idx = batch_idx + 1; batch_behind_idx < batch_out.size(); ++batch_behind_idx) {
					auto &batch_behind = batch_out[batch_behind_idx];
					bool batch_depends_on_us = false;
					bool batch_is_blocked    = false;
					for(int instr_behind_idx = 0; instr_behind_idx < batch_behind.instr_ids.size(); ++instr_behind_idx) {
						auto behind_id = batch_behind.instr_ids[instr_behind_idx];
						auto behind = &instructions[behind_id];
						if(behind->depends_on_instruction.find(instr_id) != behind->depends_on_instruction.end())
							batch_depends_on_us = true;
						if(behind->instruction_is_blocking.find(instr_id) != behind->instruction_is_blocking.end())
							batch_is_blocked = true;
					}
					if(!batch_is_blocked && (batch_behind.index_sets == sub_batch.index_sets))
						last_suitable_batch_idx = batch_behind_idx;
					if(batch_depends_on_us || batch_behind_idx == batch_out.size()-1) {
						if(last_suitable_batch_idx != batch_idx) {
							// We are allowed to move. Move to the beginning of the first other batch that is suitable.
							auto &insert_to = batch_out[last_suitable_batch_idx];
							insert_to.instr_ids.insert(insert_to.instr_ids.begin(), instr_id);
							sub_batch.instr_ids.erase(sub_batch.instr_ids.begin() + instr_idx);
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
		fatal_error(Mobius_Error::internal, "Unable to optimize instruction sub batch grouping in the allotted amount of iterations.");
	
	// Remove batches that were emptied as a result of the step above.
	int batch_idx = batch_out.size()-1;
	while(batch_idx >= 0) {
		auto &sub_batch = batch_out[batch_idx];
		if(sub_batch.instr_ids.empty())
			batch_out.erase(batch_out.begin() + batch_idx);  // NOTE: ok since we are iterating batch_idx backwards.
		--batch_idx;
	}
#endif
	
#if 0
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	debug_print_batch_array(model, batch_out, instructions);
	warning_print("\n\n");
#endif
}


void
create_initial_vars_for_lookups(Model_Application *app, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions) {
	for(auto arg : expr->exprs) create_initial_vars_for_lookups(app, arg, instructions);
	
	if(expr->expr_type == Math_Expr_Type::identifier_chain) {
		auto ident = reinterpret_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var) {
			auto instr = &instructions[ident->state_var.id];
			
			if(instr->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
			
			// This function wants to look up the value of another variable, but it doesn't have initial code. If it has regular code, we can substitute that!
			
			auto var = app->state_vars[ident->state_var];
			//TODO: We have to be careful, because there are things that are allowed in regular code that is not allowed in initial code. We have to vet for it here!
				
			instr->type = Model_Instruction::Type::compute_state_var;
			instr->var_id = ident->state_var;
			if(var->function_tree) {
				instr->code = var->function_tree;
				// Have to do this recursively, since we may already have passed it in the outer loop.
				create_initial_vars_for_lookups(app, instr->code, instructions);
			} else
				instr->code = nullptr;
			
			// If it is an aggregation variable, whatever it aggregates also must be computed.
			if(var->flags & State_Variable::Flags::f_is_aggregate) {
				auto instr2 = &instructions[var->agg.id];
				
				if(instr2->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
				
				auto var2 = app->state_vars[var->agg];
				
				instr2->type = Model_Instruction::Type::compute_state_var;
				instr2->var_id = var->agg;
				if(var2->function_tree) {
					instr2->code = var2->function_tree;
					create_initial_vars_for_lookups(app, instr2->code, instructions);
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
	
	instructions.resize(app->state_vars.count());
	
	for(auto var_id : app->state_vars) {
		auto var = app->state_vars[var_id];
		auto fun = var->function_tree;
		if(initial) fun = var->initial_function_tree;
		if(!fun) {
			if(!initial && var->type != Decl_Type::quantity &&
				!(var->flags & State_Variable::Flags::f_is_aggregate) &&
				!(var->flags & State_Variable::Flags::f_in_flux_connection) &&
				!(var->flags & State_Variable::Flags::f_in_flux) &&
				!(var->flags & State_Variable::Flags::f_dissolved_flux) &&
				!(var->flags & State_Variable::Flags::f_dissolved_conc) &&
				!(var->flags & State_Variable::Flags::f_invalid))
				fatal_error(Mobius_Error::internal, "Somehow we got a state variable \"", var->name, "\" where the function code was unexpectedly not provided. This should have been detected at an earlier stage in model registration.");
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
		for(auto var_id : app->state_vars) {
			auto instr = &instructions[var_id.id];
			if(instr->type == Model_Instruction::Type::invalid) continue;
			if(!instr->code) continue;
			
			create_initial_vars_for_lookups(app, instr->code, instructions);
		}
	}
	
	
	// Put solvers on instructions for computing ODE variables.
	// Solvers are propagated to other instructions later based on need.
	if(!initial) {
		
		// NOTE: We tested the validity of the solve declaration in the model composition already, so we don't do it again here.
		for(auto id : model->solves) {
			auto solve = model->solves[id];
			Var_Id var_id = app->state_vars[solve->loc];
			
			instructions[var_id.id].solver = solve->solver;
		}
		
		for(auto var_id : app->state_vars) {
			auto var = app->state_vars[var_id];
			// Fluxes with an ODE variable as source is given the same solver as it.
			if(var->type == Decl_Type::flux && is_located(var->loc1))
				instructions[var_id.id].solver = instructions[app->state_vars[var->loc1].id].solver;

			// Also set the solver for an aggregation variable for a connection flux.
			if(var->flags & State_Variable::Flags::f_in_flux_connection)
				instructions[var_id.id].solver = instructions[var->connection_agg.id].solver;

			// Dissolved fluxes of fluxes with solvers must be on solvers
			//TODO: make it work with dissolvedes of dissolvedes
			//TODO: what if the source was on another solver than the dissolved? Another reason to force them to be the same (unless overridden)?
			if(var->flags & State_Variable::Flags::f_dissolved_flux)
				instructions[var_id.id].solver = instructions[var->dissolved_flux.id].solver;
			
			// Also for concs. Can be overkill some times, but safer just to do it.
			if(var->flags & State_Variable::Flags::f_dissolved_conc)
				instructions[var_id.id].solver = instructions[var->dissolved_conc.id].solver;
		}
	}
	
	// Generate instructions needed to compute special variables.
	
	for(auto var_id : app->state_vars) {
		auto instr = &instructions[var_id.id];
		
		if(instr->type == Model_Instruction::Type::invalid) continue;
	
		auto var = app->state_vars[var_id];
		
		auto loc1 = var->loc1;
		auto loc2 = var->loc2;
		
		bool is_aggregate  = var->flags & State_Variable::Flags::f_is_aggregate;
		bool has_aggregate = var->flags & State_Variable::Flags::f_has_aggregate;
		
		auto var_solver = instr->solver;
		
		if(has_aggregate) {
			// var (var_id) is now the variable that is being aggregated.
			// instr is the instruction to compute it
			
			//if(initial) {
			//	warning_print("*** *** *** initial agg for ", var->name, "\n");
			//}
			
			auto aggr_var = app->state_vars[var->agg];    // aggr_var is the aggregation variable (the one we sum to).
			
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
			auto agg_to_comp = model->components[aggr_var->agg_to_compartment];
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
				auto target_id = app->state_vars[aggr_var->loc2];
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(var->agg.id);
			}
		}
		
		if(var->flags & State_Variable::Flags::f_in_flux_connection) {
			
			if(initial)
				fatal_error(Mobius_Error::internal, "Got a connection flux in the initial step.");
			if(!is_valid(var_solver))
				fatal_error(Mobius_Error::internal, "Got aggregation variable for connection fluxes without a solver.");
			
			//warning_print("************ Found it\n");
			
			int clear_id = instructions.size();
			instructions.push_back({});
			auto clear_instr       = &instructions[clear_id];
			clear_instr->type = Model_Instruction::Type::clear_state_var;
			clear_instr->solver = var_solver;
			clear_instr->var_id = var_id;     // The var_id of the clear_instr indicates which variable we want to clear.
			clear_instr->inherits_index_sets_from_instruction.insert(var_id.id);
			
			// var is the aggregation variable for the target
			// var->connection_agg  is the quantity state variable for the target.
			
			// Find all the connection fluxes pointing to the target.
			for(auto var_id_flux : app->state_vars) {
				auto var_flux = app->state_vars[var_id_flux];
				
				if(var_flux->type != Decl_Type::flux || !is_valid(var_flux->connection)) continue;
			
				// Create instruction to add the flux to the target aggregate.	
				int add_to_aggr_id = instructions.size();
				instructions.push_back({});
				
				// TODO: Go over and refactor this stuff: !!
				
				auto add_to_aggr_instr = &instructions[add_to_aggr_id];
				
				add_to_aggr_instr->solver = var_solver;
				add_to_aggr_instr->connection = var_flux->connection;
				add_to_aggr_instr->type = Model_Instruction::Type::add_to_aggregate;
				add_to_aggr_instr->var_id = var_id_flux;
				add_to_aggr_instr->source_or_target_id = var_id;
				add_to_aggr_instr->depends_on_instruction.insert(clear_id); // Only start summing up after we cleared to 0.
				add_to_aggr_instr->instruction_is_blocking.insert(clear_id); // This says that the clear_id has to be in a separate for loop from this instruction
				add_to_aggr_instr->depends_on_instruction.insert(var_id_flux.id); // We can only sum the value in after it is computed.
				//add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id_flux.id); // Sum it in each time it is computed. Should no longer be needed with the new dependency system
				
				// NOTE: The source of the flux could be different per target, so even if the value flux itself doesn't have any index set dependencies, it could still be targeted differently depending on the connection data.
				auto source_comp = model->components[var_flux->loc1.components[0]];
				add_to_aggr_instr->index_sets.insert(source_comp->index_sets.begin(), source_comp->index_sets.end()); //TODO: This should instead depend on the index sets involved in the connection relation..
				
				// The aggregate value is not ready before the summing is done. (This is maybe unnecessary since the target is an ODE (?))
				instructions[var_id.id].depends_on_instruction.insert(add_to_aggr_id);
				instructions[var_id.id].inherits_index_sets_from_instruction.insert(var->connection_agg.id);    // Get at least one instance of the aggregation variable per instance of the variable we are aggregating for.
				
				// Since the target could get a different value from the connection depending on its own index, we have to force it to be computed per each of these indexes even if it were not to have an index set dependency on this otherwise.
				auto target_compartment = app->state_vars[var->connection_agg]->loc1.components[0];
				auto target_comp = model->components[target_compartment];
				instructions[var->connection_agg.id].index_sets.insert(target_comp->index_sets.begin(), target_comp->index_sets.end());
				
				// This is not needed because the target is always an ODE:
				//instructions[var->connection_agg.id].depends_on_instruction.insert(var_id.id); // The target must be computed after the aggregation variable.
			}
		}
		
		if(initial || var->type != Decl_Type::flux) continue;
		
		Entity_Id source_solver = invalid_entity_id;
		Var_Id source_id;
		if(is_located(loc1) && !is_aggregate) {
			source_id = app->state_vars[loc1];
			Model_Instruction *source = &instructions[source_id.id];
			source_solver = source->solver;
				
			if(!is_valid(source_solver) && !app->state_vars[source_id]->override_tree) {
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
				// The reason for this is that for discrete fluxes we generate a
				// flux := min(flux, source) in order to not send the source negative.
				// Should no longer be necessary to fix this here with the new dependency system.
				//instructions[var_id.id].inherits_index_sets_from_instruction.insert(source_id.id);
				
				instructions.push_back(std::move(sub_source_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
		bool is_connection = is_valid(var->connection);
		if(is_connection && has_aggregate)
			fatal_error(Mobius_Error::internal, "Somehow a connection flux got an aggregate");
		
		/*
		if(is_connection) {
			auto connection = model->connections[var->connection];
			if(connection->type != Connection_Structure_Type::directed_tree)
				fatal_error(Mobius_Error::internal, "Unsupported connection structure in build_instructions().");
			// NOTE: the source and target id for the connection-flux are the same, but loc2 doesn't record the target in this case, so we use the source_id.
			Model_Instruction *target = &instructions[source_id.id];
			// If we have fluxes of two instances of the same quantity, we have to enforce that it is indexed by by the index set of that connection relation.
			
			//NOTE :temporary!
			// Eventually we could check if there are cross-connections between certain indexes or not (and only enforce dependencies in the cross connections)
			auto conn_comp = model->components[connection->compartments[0]];
			auto conn_idx_set = conn_comp->index_sets[0];
			target->index_sets.insert(conn_idx_set);
		}
		*/
		
		if((is_located(loc2) || is_connection) && !has_aggregate) {
			Var_Id target_id;
			if(is_connection) // TODO: This is outdated. The target id should not be set for connection fluxes (as it could be variable)
				target_id = source_id;
			else
				target_id = app->state_vars[loc2];
			
			Model_Instruction *target = &instructions[target_id.id];
			Entity_Id target_solver = target->solver;
			
			if(!is_valid(target_solver) && !app->state_vars[target_id]->override_tree) {
				Model_Instruction add_target_instr;
				add_target_instr.type   = Model_Instruction::Type::add_flux_to_target;
				add_target_instr.var_id = var_id;
				
				add_target_instr.depends_on_instruction.insert(var_id.id);   // the addition of the flux has to be done after the flux is computed.
				//add_target_instr.inherits_index_sets_from_instruction.insert(var_id.id);  // it also has to be done (at least) once per instance of the flux. Should no longer be needed to be declared explicitly with new dependency system.
				add_target_instr.inherits_index_sets_from_instruction.insert(target_id.id); // it has to be done once per instance of the target.
				add_target_instr.source_or_target_id = target_id;
				if(is_connection) {
					// the index sets already depend on the flux itself, which depends on the source, so we don't have to redeclare that.
					add_target_instr.connection = var->connection;
				}
				
				int add_idx = (int)instructions.size();
				if(!is_connection)
					target->depends_on_instruction.insert(add_idx);
				
				// NOTE: this one is needed because of unit conversions, which could give an extra index set dependency to the add instruction.
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(add_idx);
				
				instructions.push_back(std::move(add_target_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
	}
}



// give all properties the solver if it is "between" quantities or fluxes with that solver in the dependency tree.
bool propagate_solvers(Model_Application *app, int instr_id, Entity_Id solver, std::vector<Model_Instruction> &instructions) {
	auto instr = &instructions[instr_id];
	Decl_Type decl_type = app->state_vars[instr->var_id]->type;
	
	if(instr->solver == solver)
		return true;
	if(instr->visited)
		return false;
	instr->visited = true;
	
	bool found = false;
	for(int dep : instr->depends_on_instruction) {
		if(propagate_solvers(app, dep, solver, instructions))
			found = true;
	}
	if(found) {
		if(is_valid(instr->solver) && instr->solver != solver) {
			// ooops, we already wanted to put it on another solver.
			//TODO: we must give a much better error message here. This is not parseable to regular users.
			// print a dependency trace or something like that!
			fatal_error(Mobius_Error::model_building, "The state variable \"", app->state_vars[instr->var_id]->name, "\" is lodged between multiple ODE solvers.");
		}
		
		if(decl_type != Decl_Type::quantity)
			instr->solver = solver;
	}
	instr->visited = false;
	
	return found;
}

void create_batches(Model_Application *app, std::vector<Batch> &batches_out, std::vector<Model_Instruction> &instructions) {
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
				propagate_solvers(app, dep, instr->solver, instructions);
		}
	}
	
	warning_print("Remove ODE dependencies\n");
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto &instr = instructions[instr_id];
		
		if(instr.type == Model_Instruction::Type::invalid) continue;
		
		if(is_valid(instr.solver)) {
			// Remove dependency of any instruction on an ode variable if they are on the same solver.
			std::vector<int> remove;
			for(int other_id : instr.depends_on_instruction) {
				auto &other_instr = instructions[other_id];
				if(other_instr.solver == instr.solver && app->state_vars[other_instr.var_id]->type == Decl_Type::quantity)
					remove.push_back(other_id);
			}
			for(int rem : remove)
				instr.depends_on_instruction.erase(rem);
		} else if (app->state_vars[instr.var_id]->type == Decl_Type::flux) {
			// Remove dependency of discrete fluxes on their sources. Discrete fluxes are ordered in a specific way, and the final value of the source comes after the flux is subtracted.
			auto var = app->state_vars[instr.var_id];
			if(is_located(var->loc1)) {
				instr.depends_on_instruction.erase(app->state_vars[var->loc1].id);
			}
		}
	}
	
	warning_print("Sorting begin\n");
	std::vector<int> sorted_instructions;
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		if(instructions[instr_id].type == Model_Instruction::Type::invalid) continue;
		
		bool success = topological_sort_instructions_visit(app, instr_id, sorted_instructions, instructions, false);
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
set_up_result_structure(Model_Application *app, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions) {
	if(app->result_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up result structure twice.");
	if(!app->all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up result structure before all index sets received indexes.");
	
	// NOTE: we just copy the batch structure so that it is easier to optimize the run code for cache locality.
	// NOTE: It is crucial that all ode variables from the same batch are stored contiguously, so that part of the setup must be kept no matter what!
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	for(auto &batch : batches) {
		for(auto &array : batch.arrays)      add_array(structure, array, instructions);
		for(auto &array : batch.arrays_ode)  add_array(structure, array, instructions);
	}
	
	app->result_structure.set_up(std::move(structure));
}

void
compose_and_resolve(Model_Application *app);

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
	if(!connection_structure.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before connection data was set up.");
	
	compose_and_resolve(this);
	
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
	for(auto var_id : state_vars) {
		auto &init_idx = initial_instructions[var_id.id].index_sets;
		
		instructions[var_id.id].index_sets.insert(init_idx.begin(), init_idx.end());
	}
	
	resolve_index_set_dependencies(this, instructions, false);
	
	// similarly, the initial state of a varialble has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
	for(auto var_id : state_vars)
		initial_instructions[var_id.id].index_sets = instructions[var_id.id].index_sets;
	
	std::vector<Batch> batches;
	create_batches(this, batches, instructions);
	
	Batch initial_batch;
	initial_batch.solver = invalid_entity_id;
	
	warning_print("Sort initial.\n");
	// Sort the initial instructions too.
	for(int instr_id = 0; instr_id < initial_instructions.size(); ++instr_id) {
		bool success = topological_sort_instructions_visit(this, instr_id, initial_batch.instrs, initial_instructions, true);
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
				auto var_ref = state_vars[instructions[var].var_id];
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
	debug_print_batch_array(this, initial_batch.arrays, initial_instructions);
	debug_print_batch_structure(this, batches, instructions);
	
	set_up_result_structure(this, batches, instructions);
	
	LLVM_Constant_Data constants;
	constants.connection_data       = data.connections.data;
	constants.connection_data_count = connection_structure.total_count;
	
	warning_print("****Connection data is:\n");
	for(int idx = 0; idx < constants.connection_data_count; ++idx)
		warning_print(" ", constants.connection_data[idx]);
	warning_print("\n");
	
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
