
#include "model_application.h"
#include "function_tree.h"
#include "model_codegen.h"

#include <string>
#include <sstream>

std::string
Model_Instruction::debug_string(Model_Application *app) {
	std::stringstream ss;
	
	if(type == Model_Instruction::Type::compute_state_var)
		ss << "\"" << app->vars[var_id]->name << "\"";
	else if(type == Model_Instruction::Type::subtract_discrete_flux_from_source)
		ss << "\"" << app->vars[source_id]->name << "\" -= \"" << app->vars[var_id]->name << "\"";
	else if(type == Model_Instruction::Type::add_discrete_flux_to_target)
		ss << "\"" << app->vars[target_id]->name << "\" += \"" << app->vars[var_id]->name << "\"";
	else if(type == Model_Instruction::Type::clear_state_var)
		ss << "\"" << app->vars[var_id]->name << "\" = 0";
	else if(type == Model_Instruction::Type::add_to_connection_aggregate)
		ss << "\"" << app->vars[target_id]->name << "\" += \"" << app->vars[var_id]->name << "\"";
	else if(type == Model_Instruction::Type::add_to_aggregate)
		ss << "\"" << app->vars[target_id]->name << "\" += \"" << app->vars[var_id]->name << "\" * weight";
	else if(type == Model_Instruction::Type::special_computation)
		ss << "(special_computation)";  //TODO: Give the function name.
	
	return ss.str();
}

void
debug_print_batch_array(Model_Application *app, std::vector<Batch_Array> &arrays, std::vector<Model_Instruction> &instructions, std::ostream &os, bool show_dependencies = false) {
	for(auto &array : arrays) {
		os << "\t[";
		for(auto index_set : array.index_sets) {
			os << "\"" << app->model->index_sets[index_set.id]->name << "\"";
			if(index_set.order > 1)
				os << "^" << index_set.order;
			os << " ";
		}
		os << "]\n";
		for(auto instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			os << "\t\t" << instr->debug_string(app) << "\n";
			if(show_dependencies) {
				for(int dep : instr->loose_depends_on_instruction)
					os << "\t\t\t* " << instructions[dep].debug_string(app) << "\n";
				for(int dep : instr->depends_on_instruction)
					os << "\t\t\t** " << instructions[dep].debug_string(app) << "\n";
				for(int dep : instr->instruction_is_blocking)
					os << "\t\t\t| " << instructions[dep].debug_string(app) << "\n";
					
			}
		}
	}
}

void
debug_print_batch_structure(Model_Application *app, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions, std::ostream &os, bool show_dependencies = false) {
	os << "\n**** batch structure ****\n";
	for(auto &batch : batches) {
		if(is_valid(batch.solver))
			os << "  solver \"" << app->model->solvers[batch.solver]->name << "\" :\n";
		else
			os << "  discrete :\n";
		debug_print_batch_array(app, batch.arrays, instructions, os, show_dependencies);
		if(is_valid(batch.solver)) {
			os << "\t(ODE):\n";
			debug_print_batch_array(app, batch.arrays_ode, instructions, os, show_dependencies);
		}
	}
	os << "\n\n";
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
			error_print(instructions[dep].debug_string(app), " <-- ", instr->debug_string(app), "\n");
			return false;
		}
	}
	instr->visited = true;
	push_to.push_back(instr_idx);
	return true;
}

bool
insert_dependency(std::set<Index_Set_Dependency> &dependencies, const Index_Set_Dependency &to_insert) {
	// Returns true if there is a change in dependencies
	auto find = std::find_if(dependencies.begin(), dependencies.end(), [&](const Index_Set_Dependency &dep) -> bool { return dep.id == to_insert.id; });
	
	if(find == dependencies.end()) {
		dependencies.insert(to_insert);
		return true;
	} else if(to_insert.order > find->order) {
		dependencies.erase(find);
		dependencies.insert(to_insert);
		return true;
	}
	return false;
}

void
insert_dependencies(Model_Application *app, std::set<Index_Set_Dependency> &dependencies, const Identifier_Data &dep, int type) {
	
	const std::vector<Entity_Id> *index_sets = nullptr;
	if(type == 0)
		index_sets = &app->parameter_structure.get_index_sets(dep.par_id);
	else if(type == 1)
		index_sets = &app->series_structure.get_index_sets(dep.var_id);
	else
		fatal_error(Mobius_Error::internal, "Misuse of insert_dependencies().");
	
	auto avoid = avoid_index_set_dependency(app, dep.restriction);
	
	int sz = index_sets->size();
	int idx = 0;
	for(auto index_set : *index_sets) {
		
		if(index_set == avoid) continue;
		// NOTE: Specialized logic to handle if a parameter is indexing over the same index set twice.
		//   Currently we only support this for the two last index sets being the same.
		if(idx == sz-2 && index_set == (*index_sets)[sz-1]) {
			insert_dependency(dependencies, {index_set, 2});
			break;
		} else {
			insert_dependency(dependencies, index_set);
		}
		++idx;
	}
}

bool
insert_dependencies(Model_Application *app, std::set<Index_Set_Dependency> &dependencies, std::set<Index_Set_Dependency> &to_insert, const Identifier_Data &dep) {
	auto avoid = avoid_index_set_dependency(app, dep.restriction);
	
	bool changed = false;
	for(auto index_set_dep : to_insert) {
		if(index_set_dep.id == avoid) continue;
		
		changed = changed || insert_dependency(dependencies, index_set_dep);
	}
	return changed;
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
		
		for(auto &dep : code_depends.on_parameter) {
			insert_dependencies(app, instr.index_sets, dep, 0);
		}
		for(auto &dep : code_depends.on_series) {
			insert_dependencies(app, instr.index_sets, dep, 1);
		}
		
		for(auto &dep : code_depends.on_state_var) {
			
			if(dep.restriction.restriction == Var_Loc_Restriction::above || dep.restriction.restriction == Var_Loc_Restriction::below) {
				if(dep.restriction.restriction == Var_Loc_Restriction::above) {
					instr.loose_depends_on_instruction.insert(dep.var_id.id);
				} else {
					instr.instruction_is_blocking.insert(dep.var_id.id);
					instr.depends_on_instruction.insert(dep.var_id.id);
				}
				// Ugh, this is a bit hacky. Could it be improved?
				// TODO: Document why this was needed, or check if it is no longer needed.
				if(instr.type == Model_Instruction::Type::compute_state_var) {
					auto index_set = app->get_single_connection_index_set(dep.restriction.connection_id);
					instr.index_sets.insert(index_set);
				}
			} else if(dep.restriction.restriction == Var_Loc_Restriction::top || dep.restriction.restriction == Var_Loc_Restriction::bottom) {
				instr.depends_on_instruction.insert(dep.var_id.id);
				if(dep.restriction.restriction == Var_Loc_Restriction::bottom)
					instr.instruction_is_blocking.insert(dep.var_id.id);
				
			} else if(!(dep.flags & Identifier_Data::Flags::last_result)) {  // TODO: Shouldn't last_result disqualify more of the strict dependencies in other cases above?
				auto var = app->vars[dep.var_id];
				if(var->type == State_Var::Type::connection_aggregate)
					instr.loose_depends_on_instruction.insert(dep.var_id.id);
				else
					instr.depends_on_instruction.insert(dep.var_id.id);
			}
		}
		instr.inherits_index_sets_from_state_var = code_depends.on_state_var;
	}
	
	// Let index set dependencies propagate from state variable to state variable. (For instance if a looks up the value of b, a needs to be indexed over (at least) the same index sets as b. This could propagate down a long chain of dependencies, so we have to keep iterating until nothing changes.
	bool changed;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto &instr : instructions) {
			
			for(int dep : instr.inherits_index_sets_from_instruction) {
				auto &dep_idx = instructions[dep].index_sets;
				for(auto &dep_idx_set : dep_idx) {
					if(insert_dependency(instr.index_sets, dep_idx_set))
						changed = true;
				}
			}
			for(auto &dep : instr.inherits_index_sets_from_state_var) {
				auto &dep_idx = instructions[dep.var_id.id].index_sets;
				if(insert_dependencies(app, instr.index_sets, dep_idx, dep))
					changed = true;
			}
		}
		
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Failed to resolve state variable index set dependencies in the alotted amount of iterations!");
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
				if(instr->loose_depends_on_instruction.find(other_id) != instr->loose_depends_on_instruction.end())
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
#if 1
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
				// If another instruction in the same batch loose depends on us, we are not allowed to movve.
				for(int instr_behind_idx = 0; instr_behind_idx < sub_batch.instr_ids.size(); ++instr_behind_idx) {
					// It is not necessarily "behind" in this case.
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
						if(behind->loose_depends_on_instruction.find(instr_id) != behind->loose_depends_on_instruction.end())
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
	debug_print_batch_array(model, batch_out, instructions, global_warning_stream);
	warning_print("\n\n");
#endif
}


void
create_initial_vars_for_lookups(Model_Application *app, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions) {
	for(auto arg : expr->exprs) create_initial_vars_for_lookups(app, arg, instructions);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var) {
			auto instr = &instructions[ident->var_id.id];
			
			if(instr->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
			
			// This function wants to look up the value of another variable, but it doesn't have initial code. If it has regular code, we can substitute that!
			
			auto var = app->vars[ident->var_id];
			//TODO: We have to be careful, because there are things that are allowed in regular code that is not allowed in initial code. We have to vet for it here!
				
			instr->type = Model_Instruction::Type::compute_state_var;
			instr->var_id = ident->var_id;
			instr->code = nullptr;
			
			if(var->type == State_Var::Type::declared) {
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->function_tree) {
					instr->code = copy(var2->function_tree.get());
					// Have to do this recursively, since we may already have passed it in the outer loop.
					create_initial_vars_for_lookups(app, instr->code, instructions);
				}
			}
			
			// If it is an aggregation variable, whatever it aggregates also must be computed.
			if(var->type == State_Var::Type::regular_aggregate) {
				auto var2 = as<State_Var::Type::regular_aggregate>(var);
				
				auto instr_agg_of = &instructions[var2->agg_of.id];
				if(instr_agg_of->type != Model_Instruction::Type::invalid) return; // If it is already valid, fine!
				
				auto var_agg_of = app->vars[var2->agg_of];
				
				instr_agg_of->type = Model_Instruction::Type::compute_state_var;
				instr_agg_of->var_id = var2->agg_of;
				instr_agg_of->code = nullptr;
				
				if(var_agg_of->type == State_Var::Type::declared) {
					auto var_agg_of2 = as<State_Var::Type::declared>(var_agg_of);
					if(var_agg_of2->function_tree) {
						instr_agg_of->code = copy(var_agg_of2->function_tree.get());
						create_initial_vars_for_lookups(app, instr_agg_of->code, instructions);
					}
				}
			}
			//TODO: if it is a conc and is not computed, do we need to check if the mass variable has an initial conc?
		}
	}
}


void
build_instructions(Model_Application *app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	auto model = app->model;
	
	instructions.resize(app->vars.count(Var_Id::Type::state_var));
	
	for(auto var_id : app->vars.all_state_vars()) {
		auto var = app->vars[var_id];
		auto &instr = instructions[var_id.id];
		
		/*
		if(!var->is_valid()) {
			instr.type = Model_Instruction::Type::invalid;
			continue;
		}
		*/
		
		// TODO: Couldn't the lookup of the function be moved to instruction_codegen ?
		Math_Expr_FT *fun = nullptr;
		if(var->type == State_Var::Type::declared) {
			auto var2 = as<State_Var::Type::declared>(var);
			
			fun = var2->function_tree.get();
			if(initial) fun = var2->initial_function_tree.get();
			if(!initial && !fun) {
				if(var2->decl_type != Decl_Type::quantity) // NOTE: quantities typically don't have code associated with them directly (except for the initial value)
					fatal_error(Mobius_Error::internal, "Somehow we got a state variable \"", var->name, "\" where the function code was unexpectedly not provided. This should have been detected at an earlier stage in model registration.");
			}
		}
		
		instr.var_id = var_id;
		instr.type = Model_Instruction::Type::compute_state_var;

		if(fun)
			instr.code = copy(fun);
		else if(initial)
			instr.type = Model_Instruction::Type::invalid;
		
		if(var->is_flux())
			instr.restriction = restriction_of_flux(var);

	}
	
	if(initial) {
		for(auto var_id : app->vars.all_state_vars()) {
			auto instr = &instructions[var_id.id];
			if(instr->type == Model_Instruction::Type::invalid) continue;
			if(!instr->code) continue;
			
			create_initial_vars_for_lookups(app, instr->code, instructions);
		}
	}
	
	
	// Put solvers on instructions for computing ODE variables.
	// Solvers are propagated to other instructions later based on need.
	if(!initial) {

		for(auto id : model->solves) {
			auto solve = model->solves[id];
			
			for(auto &loc : solve->locs) {
				Var_Id var_id = app->vars.id_of(loc);
				
				if(!is_valid(var_id)) {
					//error_print_location(this, loc);
					solve->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("This compartment does not have that quantity.");  // TODO: give the handles names in the error message.
				}
				auto hopefully_a_quantity = model->find_entity(loc.last());
				if(hopefully_a_quantity->decl_type != Decl_Type::quantity) {
					solve->source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("Solvers can only be put on quantities.");
				}
				
				instructions[var_id.id].solver = solve->solver;
			}
		}
		
		// Automatically let dissolved substances have the solvers of what they are dissolved in.
		// Do it in stages of number of components so that we propagate them out.
		for(int n_components = 3; n_components <= max_var_loc_components; ++n_components) {
			for(Var_Id var_id : app->vars.all_state_vars()) {
				auto var = app->vars[var_id];
				if(var->type != State_Var::Type::declared) continue;
				auto var2 = as<State_Var::Type::declared>(var);
				if(var2->decl_type != Decl_Type::quantity) continue;
				if(var->loc1.n_components != n_components) continue;

				auto parent_id = app->vars.id_of(remove_dissolved(var->loc1));
				instructions[var_id.id].solver = instructions[parent_id.id].solver;
			}
		}
		
		for(auto var_id : app->vars.all_state_vars()) {
			auto var = app->vars[var_id];
			//if(!var->is_valid()) continue;
			
			// Fluxes with an ODE variable as source is given the same solver as it.
			if(var->is_flux() && is_located(var->loc1))
				instructions[var_id.id].solver = instructions[app->vars.id_of(var->loc1).id].solver;

			// Also set the solver for an aggregation variable for a connection flux.
			if(var->type == State_Var::Type::connection_aggregate) {
				auto var2 = as<State_Var::Type::connection_aggregate>(var);
				
				instructions[var_id.id].solver = instructions[var2->agg_for.id].solver;
				
				if(!is_valid(instructions[var_id.id].solver)) {
					auto conn_var = as<State_Var::Type::declared>(app->vars[var2->agg_for]);
					auto var_decl = model->vars[conn_var->decl_id];
					// TODO: This is not really the location where the problem happens. The error is the direction of the flux along this connection, but right now we can't access the source loc for that from here.
					// TODO: The problem is more complex. We should check that the source and target is on the same solver (maybe - or at least have some strategy for how to handle it)
					auto conn = model->connections[var2->connection];
					var_decl->source_loc.print_error_header(Mobius_Error::model_building);
					error_print("This state variable is the source or target of a connection \"", conn->name, "\", declared here:\n");
					conn->source_loc.print_error();
					fatal_error("but the state variable is not on a solver. This is currently not allowed.");
				}
			}

			// Dissolved fluxes of fluxes with solvers must be on solvers
			//TODO: it seems to work on dissolvedes of dissolvedes, but this is only because they happen to be in the right order in the state_vars array. Should be more robust.
			if(var->type == State_Var::Type::dissolved_flux) {
				auto var2 = as<State_Var::Type::dissolved_flux>(var);
				instructions[var_id.id].solver = instructions[var2->flux_of_medium.id].solver;
			}
			
			// Also for concs. Can be overkill some times, but safer just to do it.
			if(var->type == State_Var::Type::dissolved_conc) {
				auto var2 = as<State_Var::Type::dissolved_conc>(var);
				instructions[var_id.id].solver = instructions[var2->conc_of.id].solver;
			}
		}
		
		// Check if connection fluxes have discrete source (currently not supported)
		// TODO: should also check if an arrow can go between different sources (also will not work
		// correctly), but that is more tricky.
		// TODO: Bug: this doesn't work!
		
		for(auto var_id : app->vars.all_fluxes()) {
			
			// TODO: May need to be fixed for boundary fluxes!
			auto var = app->vars[var_id];
			//if(!var->is_valid() || !var->is_flux()) continue;
			if(!is_located(var->loc1)) continue;
			if(!is_valid(restriction_of_flux(var).connection_id)) continue;
			auto source_id = app->vars.id_of(var->loc1);
			if(!is_valid(instructions[source_id.id].solver)) {
				// Technically not all fluxes may be declared, but if there is an error, it *should* trigger on a declared flux first.
				model->fluxes[as<State_Var::Type::declared>(var)->decl_id]->source_loc.print_error_header();
				//TODO: this is not really enough info to tell the user where the error happened.
				fatal_error("This flux was put on a connection, but the source is a discrete variable. This is currently not supported.");
			}
		}
	}
	
	// Generate instructions needed to compute different types of specialized variables.
	
	for(auto var_id : app->vars.all_state_vars()) {
		auto instr = &instructions[var_id.id];
		
		if(instr->type == Model_Instruction::Type::invalid) continue;
	
		auto var = app->vars[var_id];
		
		bool is_aggregate  = var->type == State_Var::Type::regular_aggregate;
		bool has_aggregate = var->flags & State_Var::Flags::has_aggregate;
		
		auto var_solver = instr->solver;
		
		if(instr->code && instr->code->expr_type == Math_Expr_Type::special_computation) {
			auto special = static_cast<Special_Computation_FT *>(instr->code);
			
			int spec_idx = instructions.size();
			instructions.emplace_back();
			
			auto instr      = &instructions[var_id.id];  // Have to refetch it because of resized array.
			auto spec_instr = &instructions[spec_idx];
			
			spec_instr->code = instr->code;
			instr->code = nullptr;
			spec_instr->type = Model_Instruction::Type::special_computation;
			spec_instr->solver = var_solver;
			spec_instr->var_id = instr->var_id;
			
			instr->depends_on_instruction.insert(spec_idx);
			//TODO: Various index set dependencies.
			
			for(auto &arg : special->arguments) {
				if(arg.variable_type == Variable_Type::state_var) {
					spec_instr->depends_on_instruction.insert(arg.var_id.id);
					spec_instr->instruction_is_blocking.insert(arg.var_id.id);
					
					instr->inherits_index_sets_from_instruction.insert(arg.var_id.id);
				} else if (arg.variable_type == Variable_Type::parameter) {
					insert_dependencies(app, instr->index_sets, arg, 0);
				} else
					fatal_error(Mobius_Error::internal, "Unexpected variable type for special_computation argument.");
			}
		}
		
		if(is_aggregate) {
			// var (var_id) is now the variable that is being aggregated.
			
			//if(initial) warning_print("*** *** *** initial agg for ", var->name, "\n");
			auto var2 = as<State_Var::Type::regular_aggregate>(var);
			auto agg_of = var2->agg_of;
			var_solver = instructions[agg_of.id].solver;
			
			//auto aggr_var = app->vars[var->agg];    // aggr_var is the aggregation variable (the one we sum to).
			
			// We need to clear the aggregation variable to 0 between each time it is needed.
			int clear_idx = instructions.size();
			instructions.emplace_back();
			
			int add_to_aggr_idx = instructions.size();
			instructions.emplace_back();
			
			// The instruction for the var. It compiles to a no-op, but it is kept in the model structure to indicate the location of when this var has its final value. (also used for result storage structure).
			auto agg_instr = &instructions[var_id.id];
			// The instruction for clearing to 0
			auto clear_instr = &instructions[clear_idx];
			// The instruction that takes the value of var and adds it to aggr_var (with a weight)
			auto add_to_aggr_instr = &instructions[add_to_aggr_idx];
			
			// TODO: We may be able to remove a lot of explicit dependency declarations here now:
			
			// Since we generate one aggregation variable per target compartment, we have to give it the full index set dependencies of that compartment
			// TODO: we could generate one per variable that looks it up and prune them later if they have the same index set dependencies (?)
			auto agg_to_comp = model->components[var2->agg_to_compartment];
			//agg_instr->inherits_index_sets_from_instruction.clear(); // Should be unnecessary. We just constructed it.
			
			agg_instr->index_sets.insert(agg_to_comp->index_sets.begin(), agg_to_comp->index_sets.end());
			agg_instr->solver = var_solver;
			agg_instr->depends_on_instruction.insert(add_to_aggr_idx); // The value of the aggregate is only done after we have finished summing to it.
			
			// Build the clear instruction
			add_to_aggr_instr->depends_on_instruction.insert(clear_idx);  // We can only sum to the aggregation after the clear.
			clear_instr->type = Model_Instruction::Type::clear_state_var;
			clear_instr->solver = var_solver;
			clear_instr->var_id = var_id;     // The var_id of the clear_instr indicates which variable we want to clear.
			clear_instr->index_sets.insert(agg_to_comp->index_sets.begin(), agg_to_comp->index_sets.end());

			add_to_aggr_instr->type = Model_Instruction::Type::add_to_aggregate;
			add_to_aggr_instr->var_id = agg_of;
			add_to_aggr_instr->target_id = var_id;
			add_to_aggr_instr->depends_on_instruction.insert(agg_of.id); // We can only sum the value in after it is computed.
			//add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id.id); // Sum it in each time it is computed. Unnecessary to declare since it is handled by new dependency system.
			add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id.id); // We need to look it up every time we sum to it.
			add_to_aggr_instr->solver = var_solver;
			
			if(var->is_flux() && is_located(var->loc2)) {
				// If the aggregated variable is a flux, we potentially may have to tell it to get the right index sets.
				
				//note: this is only ok for auto-generated aggregations. Not if somebody called aggregate() on a flux explicitly, because then it is not necessarily the target of the flux that wants to know the aggregate.
				// But I guess you can't explicitly reference fluxes in code any way. We have to keep in mind that if that is implemented though.
				auto target_id = app->vars.id_of(var->loc2);
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(var_id.id);
			}
			
		} else if(var->type == State_Var::Type::connection_aggregate) {
			
			if(initial)
				fatal_error(Mobius_Error::internal, "Got a connection flux in the initial step.");
			if(!is_valid(var_solver))
				fatal_error(Mobius_Error::internal, "Got aggregation variable for connection fluxes without a solver.");
			
			//warning_print("************ Found it\n");
			
			int clear_id = instructions.size();
			instructions.emplace_back();
			auto clear_instr       = &instructions[clear_id];
			clear_instr->type = Model_Instruction::Type::clear_state_var;
			clear_instr->solver = var_solver;
			clear_instr->var_id = var_id;     // The var_id of the clear_instr indicates which variable we want to clear.
			clear_instr->inherits_index_sets_from_instruction.insert(var_id.id);
			
			// Find all the connection fluxes pointing to the target (or going out from source)
			
			auto var2 = as<State_Var::Type::connection_aggregate>(var);
			if(!is_valid(var2->agg_for))
				fatal_error(Mobius_Error::internal, "Something went wrong with setting up connection aggregates");
			
			// var is the aggregation variable for the target (or source)
			// agg_for  is the id of the quantity state variable for the target (or source).
			
			for(auto var_id_flux : app->vars.all_fluxes()) {
				auto var_flux = app->vars[var_id_flux];
				//if(!var_flux->is_valid()) continue;
				auto &restriction = restriction_of_flux(var_flux);
				if(/*!var_flux->is_flux() ||*/ restriction.connection_id != var2->connection) continue;
				
				auto conn_type = model->connections[var2->connection]->type;
				
				Var_Location flux_loc = var_flux->loc1;
				if(restriction.restriction == Var_Loc_Restriction::top)
					flux_loc = var_flux->loc2;
				
				//if(var_flux->boundary_type == Boundary_Type::bottom && !is_located(var_flux->loc2))
				//	continue;
				
				if(var2->is_source) {
					if(!is_located(var_flux->loc1) || app->vars.id_of(var_flux->loc1) != var2->agg_for)
						continue;
				} else {
					// Ouch, these tests are super super awkward. Is there no better way to set up the data??
					
					// See if this flux has a loc that is the same quantity (chain) as the target variable.
					auto loc = flux_loc;
					Var_Location target_loc = app->vars[var2->agg_for]->loc1;
					loc.components[0] = invalid_entity_id;
					target_loc.components[0] = invalid_entity_id;
					if(loc != target_loc) continue;
					
					// Also test if there is actually an arrow for that connection in the specific data we are setting up for now.
					if(conn_type == Connection_Type::directed_tree) {
						Entity_Id source_comp_id = var_flux->loc1.components[0];
						auto *find_source = app->find_connection_component(var2->connection, source_comp_id);
						if(!find_source->can_be_source) continue;
					}
				}
				
				// Create instruction to add the flux to the target aggregate.
				int add_to_aggr_id = instructions.size();
				instructions.emplace_back();
				auto add_to_aggr_instr = &instructions[add_to_aggr_id];
				
				add_to_aggr_instr->solver = var_solver;
				
				add_to_aggr_instr->restriction = restriction;
	
				add_to_aggr_instr->type = Model_Instruction::Type::add_to_connection_aggregate;
				add_to_aggr_instr->var_id = var_id_flux;
				if(conn_type == Connection_Type::directed_tree)
					add_to_aggr_instr->source_id = app->vars.id_of(var_flux->loc1);
				add_to_aggr_instr->target_id = var_id;
				add_to_aggr_instr->depends_on_instruction.insert(clear_id); // Only start summing up after we cleared to 0.
				add_to_aggr_instr->instruction_is_blocking.insert(clear_id); // This says that the clear_id has to be in a separate for loop from this instruction
				add_to_aggr_instr->depends_on_instruction.insert(var_id_flux.id); // We can only sum the value in after it is computed.
				//add_to_aggr_instr->inherits_index_sets_from_instruction.insert(var_id_flux.id); // Sum it in each time it is computed. Should no longer be needed with the new dependency system
				
				
				// The aggregate value is not ready before the summing is done. (This is maybe unnecessary since the target is an ODE (?))
				instructions[var_id.id].depends_on_instruction.insert(add_to_aggr_id);
				instructions[var_id.id].inherits_index_sets_from_instruction.insert(var2->agg_for.id);    // Get at least one instance of the aggregation variable per instance of the variable we are aggregating for.
				
				// TODO: We need something like this because otherwise there may be additional index sets that are not accounted for. But we need to skip the particular index set(s) belonging to the connection (otherwise there are problems with matrix indexing for all_to_all etc.)
				//instructions[var_id.id].inherits_index_sets_from_instruction.insert(add_to_aggr_id);
				
				// Hmm, not that nice that we have to do have knowledge about the specific types here, but maybe unavoidable.
				if(conn_type == Connection_Type::directed_tree) {
					// Hmm, could have kept find_source from above?
					Entity_Id source_comp_id = var_flux->loc1.components[0];
					Entity_Id target_comp_id = app->vars[var2->agg_for]->loc1.components[0];
					auto *find_source = app->find_connection_component(var2->connection, source_comp_id);
					auto *find_target = app->find_connection_component(var2->connection, target_comp_id);
					
					// If the target compartment (not just what the connection indexes over) has an index set shared with the source connection, we must index the target variable over that.
					auto target_comp = model->components[target_comp_id];
					auto target_index_sets = find_target->index_sets; // vector copy;
					for(auto index_set : find_source->index_sets) {
						if(std::find(target_comp->index_sets.begin(), target_comp->index_sets.end(), index_set) != target_comp->index_sets.end())
							target_index_sets.push_back(index_set);
					}
					
					// NOTE: The target of the flux could be different per source, so even if the value flux itself doesn't have any index set dependencies, it could still be targeted differently depending on the connection data.
					add_to_aggr_instr->index_sets.insert(find_source->index_sets.begin(), find_source->index_sets.end());
					
					// Since the target could get a different value from the connection depending on its own index, we have to force it to be computed per each of these indexes even if it were not to have an index set dependency on this otherwise.
					instructions[var2->agg_for.id].index_sets.insert(target_index_sets.begin(), target_index_sets.end());
					
				} else if(conn_type == Connection_Type::all_to_all || conn_type == Connection_Type::grid1d) {
					
					auto &components = app->connection_components[var2->connection.id];
					auto source_comp = components[0].id;
					
					bool found = false;
					for(int idx = 0; idx < flux_loc.n_components; ++idx)
						if(flux_loc.components[idx] == source_comp) found = true;
					if(components.size() != 1 || !found)
						// TODO: It seems like this check is not performed anywhere else (?). It should be checked earlier.
						fatal_error(Mobius_Error::internal, "Got an all_to_all or grid1d connection for a state var that the connection is not supported for.");
					
					auto index_set = app->get_single_connection_index_set(var2->connection);
					if(conn_type == Connection_Type::all_to_all)
						add_to_aggr_instr->index_sets.insert({index_set, 2}); // The summation to the aggregate must always be per pair of indexes.
					else if(conn_type == Connection_Type::grid1d) {
						instructions[var_id_flux.id].restriction = add_to_aggr_instr->restriction; // TODO: This one should be set first, then used to set the aggr instr restriction.
						
						if(var_flux->loc2.restriction == Var_Loc_Restriction::below) {    // TODO: Is this the right way to finally check it?
							add_to_aggr_instr->index_sets.insert(index_set);
							instructions[var_id_flux.id].index_sets.insert(index_set); // This is because we have to check per index if the value should be computed at all or be set to the bottom boundary (which is 0 by default).
						} //else {
							//add_to_aggr_instr->excluded_index_sets.insert(index_set);
							//instructions[var_id_flux.id].excluded_index_sets.insert(index_set);
						//}
					}
					instructions[var2->agg_for.id].index_sets.insert(index_set);
				} else
					fatal_error(Mobius_Error::internal, "Unhandled connection type in build_instructions()");
				
				// This is not needed because the target is always an ODE:
				//instructions[var->connection_agg.id].depends_on_instruction.insert(var_id.id); // The target must be computed after the aggregation variable.
			}
		}
		
		if(initial || !var->is_flux()) continue;
		
		var = app->vars[var_id];
		auto loc1 = var->loc1;
		auto loc2 = var->loc2;
		
		std::vector<int> sub_add_instrs;
		
		Entity_Id source_solver = invalid_entity_id;
		Var_Id source_id;
		if(is_located(loc1) && !is_aggregate) {
			source_id = app->vars.id_of(loc1);
			source_solver = instructions[source_id.id].solver;
			auto source_var = as<State_Var::Type::declared>(app->vars[source_id]);
			
			// If the source is not an ODE variable, generate an instruction to subtract the flux from the source.
			
			if(!is_valid(source_solver) && !source_var->override_tree) {
				int sub_idx = (int)instructions.size();
				instructions.emplace_back();
				
				auto &sub_source_instr = instructions.back();
				sub_source_instr.type = Model_Instruction::Type::subtract_discrete_flux_from_source;
				sub_source_instr.var_id = var_id;
				
				sub_source_instr.depends_on_instruction.insert(var_id.id);     // the subtraction of the flux has to be done after the flux is computed.
				sub_source_instr.inherits_index_sets_from_instruction.insert(source_id.id); // and it has to be done per instance of the source.
				
				sub_source_instr.source_id = source_id;
				
				//NOTE: the "compute state var" of the source "happens" after the flux has been subtracted. In the discrete case it will not generate any code, but it is useful to keep it as a stub so that other vars that depend on it happen after it (and we don't have to make them depend on all the fluxes from the var instead).
				instructions[source_id.id].depends_on_instruction.insert(sub_idx);
				
				sub_add_instrs.push_back(sub_idx);
				//instructions[var_id.id].loose_depends_on_instruction.insert(sub_idx);
			}
		}
		bool is_connection = is_valid(restriction_of_flux(var).connection_id);
		if(is_connection && has_aggregate)
			fatal_error(Mobius_Error::internal, "Somehow a connection flux got an aggregate");
		
		if((is_located(loc2) /*|| is_connection*/) && !has_aggregate) {
			Var_Id target_id = app->vars.id_of(loc2);
			
			Entity_Id target_solver = instructions[target_id.id].solver;
			auto target_var = as<State_Var::Type::declared>(app->vars[target_id]);
			
			if(!is_valid(target_solver) && !target_var->override_tree) {
				int add_idx = (int)instructions.size();
				instructions.emplace_back();
				
				Model_Instruction &add_target_instr = instructions.back();
				add_target_instr.type   = Model_Instruction::Type::add_discrete_flux_to_target;
				add_target_instr.var_id = var_id;
				
				add_target_instr.depends_on_instruction.insert(var_id.id);   // the addition of the flux has to be done after the flux is computed.
				add_target_instr.inherits_index_sets_from_instruction.insert(target_id.id); // it has to be done once per instance of the target.
				add_target_instr.target_id = target_id;
				
				sub_add_instrs.push_back(add_idx);
				
				instructions[target_id.id].depends_on_instruction.insert(add_idx);
			
				//instructions[var_id.id].loose_depends_on_instruction.insert(add_idx);
				
				// NOTE: this one is needed because of unit conversions, which could give an extra index set dependency to the add instruction.
				instructions[target_id.id].inherits_index_sets_from_instruction.insert(add_idx);
			}
		}
		
		// TODO: make an error or warning if an ODE flux is given a discrete order. Maybe also if a discrete flux is not given one.
		
		// TODO: may want to do somehting similar if it is dissolved (look up the decl of the parent flux).
		if(var->type == State_Var::Type::declared) {
			auto flux_decl_id = as<State_Var::Type::declared>(var)->decl_id;
			auto flux_decl = model->fluxes[flux_decl_id];
			if(is_valid(flux_decl->discrete_order)) {
				auto discrete_order = model->discrete_orders[flux_decl->discrete_order];
				
				bool after = false;
				if(discrete_order) {
					// If the flux is tied to a discrete order, make all fluxes after it depend on the subtraction add addition instructions of this one.
					
					for(auto flux_id : discrete_order->fluxes) {
						if(after) {
							// TODO: Ugh, we have to do this just to find the single state variable corresponding to a given flux declaration.
							// should have a lookup structure for it!
							for(auto var_id_2 : app->vars.all_fluxes()) {
								auto var2 = app->vars[var_id_2];
								if(var2->type != State_Var::Type::declared) continue;
								if(as<State_Var::Type::declared>(var2)->decl_id == flux_id)
									instructions[var_id_2.id].depends_on_instruction.insert(sub_add_instrs.begin(), sub_add_instrs.end());
							}
						}
						if(flux_id == flux_decl_id)
							after = true;
					}
				}
			}
		}
		
	}
}



// give all properties the solver if it is "between" quantities or fluxes with that solver in the dependency tree.
bool propagate_solvers(Model_Application *app, int instr_id, Entity_Id solver, std::vector<Model_Instruction> &instructions) {
	auto instr = &instructions[instr_id];

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
	for(int dep : instr->loose_depends_on_instruction) {
		if(propagate_solvers(app, dep, solver, instructions))
			found = true;
	}
	if(found) {
		if(is_valid(instr->solver) && instr->solver != solver) {
			// ooops, we already wanted to put it on another solver.
			//TODO: we must give a much better error message here. This is not parseable to regular users.
			// print a dependency trace or something like that!
			fatal_error(Mobius_Error::model_building, "The state variable \"", app->vars[instr->var_id]->name, "\" is lodged between multiple ODE solvers.");
		}
		
		auto var = app->vars[instr->var_id];
		if(var->type != State_Var::Type::declared || as<State_Var::Type::declared>(var)->decl_type != Decl_Type::quantity)
			instr->solver = solver;
	}
	instr->visited = false;
	
	return found;
}

struct Pre_Batch {
	Entity_Id solver = invalid_entity_id;
	std::vector<int> instructions;
	std::set<int>    depends_on;
	bool visited = false;
	bool temp_visited = false;
};

void
topological_sort_pre_batch_visit(int idx, std::vector<int> &push_to, std::vector<Pre_Batch> &pre_batches) {
	Pre_Batch *pre_batch = &pre_batches[idx];
	if(pre_batch->visited) return;
	if(pre_batch->temp_visited)
		fatal_error(Mobius_Error::internal, "Unable to sort pre batches. Should not be possible if solvers are correctly propagated.");
	pre_batch->temp_visited = true;
	for(int dep : pre_batch->depends_on)
		topological_sort_pre_batch_visit(dep, push_to, pre_batches);
	pre_batch->visited = true;
	push_to.push_back(idx);
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
	
	//warning_print("Propagate solvers\n");
	// TODO: We need to make some guard to check that this is a sufficient amount of iterations!
	for(int idx = 0; idx < 10; ++idx) {
		for(auto &instr : instructions) instr.visited = false;
		for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
			auto instr = &instructions[instr_id];
			
			if(instr->type == Model_Instruction::Type::invalid) continue;
			
			if(is_valid(instr->solver)) {
				for(int dep : instr->depends_on_instruction)
					propagate_solvers(app, dep, instr->solver, instructions);
				for(int dep : instr->loose_depends_on_instruction)
					propagate_solvers(app, dep, instr->solver, instructions);
			}
		}
	}
	
	//warning_print("Remove ODE dependencies\n");
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto &instr = instructions[instr_id];
		
		if(instr.type == Model_Instruction::Type::invalid) continue;
		
		if(is_valid(instr.solver)) {
			// Remove dependency of any instruction on an ode variable if they are on the same solver.
			std::vector<int> remove;
			for(int other_id : instr.depends_on_instruction) {
				auto &other_instr = instructions[other_id];
				if(!is_valid(other_instr.var_id)) continue;
				auto other_var = app->vars[other_instr.var_id];
				bool is_quantity = other_var->type == State_Var::Type::declared &&
					as<State_Var::Type::declared>(other_var)->decl_type == Decl_Type::quantity;
				if(other_instr.solver == instr.solver && is_quantity)
					remove.push_back(other_id);
			}
			for(int rem : remove)
				instr.depends_on_instruction.erase(rem);
		} else if (app->vars[instr.var_id]->is_flux()) {
			// Remove dependency of discrete fluxes on their sources. Discrete fluxes are ordered in a specific way, and the final value of the source comes after the flux is subtracted.
			auto var = app->vars[instr.var_id];
			if(is_located(var->loc1))
				instr.depends_on_instruction.erase(app->vars.id_of(var->loc1).id);
		}
	}
	
	//warning_print("Sorting begin\n");
	std::vector<int> sorted_instructions;
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		if(instructions[instr_id].type == Model_Instruction::Type::invalid) continue;
		
		bool success = topological_sort_instructions_visit(app, instr_id, sorted_instructions, instructions, false);
		if(!success) mobius_error_exit();
	}
	
	//warning_print("Create batches\n");
	
	std::vector<Pre_Batch> pre_batches;
	std::vector<int> pre_batch_of_solver(app->model->solvers.count(), -1);
	std::vector<int> pre_batch_of_instr(instructions.size(), -1);
	
	for(int instr_id : sorted_instructions) {
		auto &instr = instructions[instr_id];
		int batch_id = -1;
		if(is_valid(instr.solver))
			batch_id = pre_batch_of_solver[instr.solver.id];
		if(batch_id < 0) {
			pre_batches.resize(pre_batches.size()+1);
			batch_id = pre_batches.size()-1;
		}
		auto &pre_batch = pre_batches[batch_id];
		pre_batch.solver = instr.solver;
		pre_batch.instructions.push_back(instr_id);
		pre_batch_of_instr[instr_id] = batch_id;
		for(int dep : instr.depends_on_instruction) {
			int dep_id = pre_batch_of_instr[dep];
			if(dep_id != batch_id)
				pre_batch.depends_on.insert(dep_id); // It should be ok to do this in the same pass because we already sorted the instructions.
		}
		if(is_valid(instr.solver))
			pre_batch_of_solver[instr.solver.id] = batch_id;
	}
	
	std::vector<int> sorted_pre_batches;
	for(int idx = 0; idx < pre_batches.size(); ++idx)
		topological_sort_pre_batch_visit(idx, sorted_pre_batches, pre_batches);
	
	// Now group discrete equations into single pre_batches.
	std::vector<Pre_Batch> grouped_pre_batches;
	//Entity_Id prev_solver = invalid_entity_id;
	for(int order : sorted_pre_batches) {
		auto &pre_batch = pre_batches[order];
		int insertion_point = -1;
		if(!is_valid(pre_batch.solver)) {
			for(int compare_idx = (int)grouped_pre_batches.size()-1; compare_idx >= 0; --compare_idx) {
				auto &compare = grouped_pre_batches[compare_idx];
				if(!is_valid(compare.solver))
					insertion_point = compare_idx;
				if(compare.depends_on.find(order) != compare.depends_on.end()) {
					if(!is_valid(compare.solver))
						insertion_point = compare_idx;
					break;
				}
			}
		}
		if(insertion_point < 0) {
			grouped_pre_batches.resize(grouped_pre_batches.size()+1);
			insertion_point = grouped_pre_batches.size()-1;
		}
		auto &insertion_batch = grouped_pre_batches[insertion_point];
		insertion_batch.solver = pre_batch.solver;
		insertion_batch.instructions.insert(insertion_batch.instructions.end(), pre_batch.instructions.begin(), pre_batch.instructions.end());
		insertion_batch.depends_on.insert(pre_batch.depends_on.begin(), pre_batch.depends_on.end());
		
		/*
		if(is_valid(prev_solver) || is_valid(pre_batch.solver) || grouped_pre_batches.empty())
			grouped_pre_batches.resize(grouped_pre_batches.size()+1);
		
		auto &new_batch = grouped_pre_batches.back();
		new_batch.instructions.insert(new_batch.instructions.end(), pre_batch.instructions.begin(), pre_batch.instructions.end());
		new_batch.solver = pre_batch.solver;
		prev_solver = pre_batch.solver;
		*/
	}
	
	
	// Try to move instructions to as late a batch as possible. Can some times improve the structure by eliminating unnecessary discrete batches.
	bool changed = false;
	for(int it = 0; it < 10; ++it) {
		changed = false;
	
		for(int batch_idx = 0; batch_idx < grouped_pre_batches.size(); ++batch_idx) {
			auto &pre_batch = grouped_pre_batches[batch_idx];
			if(is_valid(pre_batch.solver)) continue;
			for(int instr_idx = pre_batch.instructions.size()-1; instr_idx > 0; --instr_idx) {
				int instr_id = pre_batch.instructions[instr_idx];
				
				int last_suitable = -1;
				for(int batch_ahead_idx = batch_idx; batch_ahead_idx < grouped_pre_batches.size(); ++batch_ahead_idx) {
					int start_at = 0;
					//if(batch_ahead_idx == batch_idx) start_at = instr_idx+1;
					auto &batch_ahead = grouped_pre_batches[batch_ahead_idx];
					
					bool someone_ahead_in_this_batch_depends_on_us = false;
					for(int ahead_idx = start_at; ahead_idx < batch_ahead.instructions.size(); ++ahead_idx) {
						int ahead_id = batch_ahead.instructions[ahead_idx];
						auto &ahead = instructions[ahead_id];
						if(std::find(ahead.depends_on_instruction.begin(), ahead.depends_on_instruction.end(), instr_id) != ahead.depends_on_instruction.end()) {
							someone_ahead_in_this_batch_depends_on_us = true;
							break;
						}
						if(std::find(ahead.loose_depends_on_instruction.begin(), ahead.loose_depends_on_instruction.end(), instr_id) != ahead.loose_depends_on_instruction.end()) {
							someone_ahead_in_this_batch_depends_on_us = true;
							break;
						}
					}
					if(batch_ahead_idx != batch_idx && !is_valid(batch_ahead.solver))
						last_suitable = batch_ahead_idx;
					if(someone_ahead_in_this_batch_depends_on_us) break;
				}
				if(last_suitable > 0) {
					// We are allowed to move. Move to the beginning of the first other batch that is suitable.
					auto &insert_to = grouped_pre_batches[last_suitable];
					insert_to.instructions.insert(insert_to.instructions.begin(), instr_id);
					pre_batch.instructions.erase(pre_batch.instructions.begin()+instr_idx); // NOTE: it is safe to do this since we are iterating instr_idx from the end to the beginning
					changed = true;
				}
			}
		}
		if(!changed) break; // If we can't move anything, there is no point to continue trying.
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Unable to optimize instruction batch grouping in the allotted amount of iterations.");
	
	batches_out.clear();
	
	for(auto &pre_batch : grouped_pre_batches) {
		if(pre_batch.instructions.empty()) continue; // Can happen as a result of the previous step where we move them around.
		
		Batch batch;
		batch.instrs = pre_batch.instructions;
		batch.solver = pre_batch.solver;
		batches_out.push_back(std::move(batch));
	}
	
#if 0
	warning_print("*** Batches before internal structuring: ***\n");
	for(auto &batch : batches_out) {
		if(is_valid(batch.solver))
			warning_print("  solver(\"", model->solvers[batch.solver]->name, "\") :\n");
		else
			warning_print("  discrete :\n");
		for(int instr_id : batch.instrs) {
			warning_print("\t\t", instructions[instr_id]->debug_string(app), "\n");
		}
	}
	warning_print("\n\n");
#endif
}


void
add_array(std::vector<Multi_Array_Structure<Var_Id>> &structure, Batch_Array &array, std::vector<Model_Instruction> &instructions) {
	std::vector<Entity_Id> index_sets;
	for(auto &index_set : array.index_sets)
		for(int order = 0; order < index_set.order; ++order)
			index_sets.push_back(index_set.id);
	
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
Model_Application::compile(bool store_code_strings) {
	
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
	
	//warning_print("Create instruction arrays\n");
	std::vector<Model_Instruction> initial_instructions;
	std::vector<Model_Instruction> instructions;
	build_instructions(this, initial_instructions, true);
	build_instructions(this, instructions, false);
	
	//warning_print("Instruction codegen\n");
	
	// We can't check it like this anymore, because we can now invalidate instructions due to state variables being invalidated.
	//for(auto &instr : instructions)
	//	if(instr.type == Model_Instruction::Type::invalid)
	//		fatal_error(Mobius_Error::internal, "Did not set up instruction types properly.");
	
	instruction_codegen(this, initial_instructions, true);
	instruction_codegen(this, instructions, false);

	//warning_print("Resolve index sets dependencies begin.\n");
	resolve_index_set_dependencies(this, initial_instructions, true);
	
	// NOTE: state var inherits all index set dependencies from its initial code.
	for(auto var_id : vars.all_state_vars()) {
		auto &init_idx = initial_instructions[var_id.id].index_sets;
		
		instructions[var_id.id].index_sets.insert(init_idx.begin(), init_idx.end());
	}
	
	resolve_index_set_dependencies(this, instructions, false);
	
	// similarly, the initial state of a varialble has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
	for(auto var_id : vars.all_state_vars())
		initial_instructions[var_id.id].index_sets = instructions[var_id.id].index_sets;
	
	std::vector<Batch> batches;
	create_batches(this, batches, instructions);
	
	Batch initial_batch;
	initial_batch.solver = invalid_entity_id;
	
	//warning_print("Sort initial.\n");
	// Sort the initial instructions too.
	for(int instr_id = 0; instr_id < initial_instructions.size(); ++instr_id) {
		bool success = topological_sort_instructions_visit(this, instr_id, initial_batch.instrs, initial_instructions, true);
		if(!success) mobius_error_exit();
	}
	
	//warning_print("Build pre batches.\n");
	build_batch_arrays(this, initial_batch.instrs, initial_instructions, initial_batch.arrays, true);
	
	for(auto &batch : batches) {
		if(!is_valid(batch.solver))
			build_batch_arrays(this, batch.instrs, instructions, batch.arrays, false);
		else {
			std::vector<int> vars;
			std::vector<int> vars_ode;
			for(int var : batch.instrs) {
				auto var_ref = this->vars[instructions[var].var_id];
				// NOTE: if we override the conc or value of var, we instead compute the mass from the conc.
				bool is_ode = false;
				if(instructions[var].type == Model_Instruction::Type::compute_state_var && var_ref->type == State_Var::Type::declared) {
					auto var2 = as<State_Var::Type::declared>(var_ref);
					is_ode = (var2->decl_type == Decl_Type::quantity) && !var2->override_tree;
				}
				if(is_ode)
					vars_ode.push_back(var);
				else
					vars.push_back(var);
			}
			build_batch_arrays(this, vars,     instructions, batch.arrays,     false);
			build_batch_arrays(this, vars_ode, instructions, batch.arrays_ode, false);
		}
	}
	
	set_up_result_structure(this, batches, instructions);
	
	LLVM_Constant_Data constants;
	constants.connection_data        = data.connections.data;
	constants.connection_data_count  = connection_structure.total_count;
	constants.index_count_data       = data.index_counts.data;
	constants.index_count_data_count = index_counts_structure.total_count;
	
	/*
	warning_print("****Connection data is:\n");
	for(int idx = 0; idx < constants.connection_data_count; ++idx)
		warning_print(" ", constants.connection_data[idx]);
	warning_print("\n");
	*/
	
	jit_add_global_data(llvm_data, &constants);
	
	//warning_print("Generate inital run code\n");
	this->initial_batch.run_code = generate_run_code(this, &initial_batch, initial_instructions, true);
	jit_add_batch(this->initial_batch.run_code, "initial_values", llvm_data);
	
	//warning_print("Generate main run code\n");
	
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
	
	std::string *ir_string = nullptr;
	if(store_code_strings) {
		
		bool show_dependencies = false;
		
		std::stringstream ss;
		ss << "**** initial batch:\n";
		debug_print_batch_array(this, initial_batch.arrays, initial_instructions, ss, show_dependencies);
		debug_print_batch_structure(this, batches, instructions, ss, show_dependencies);
		this->batch_structure = ss.str();
		
		ss.str("");
		ss << "**** initial batch:\n";
		print_tree(this, this->initial_batch.run_code, ss);
		for(auto &batch : this->batches) {
			ss << "\n**** batch:\n";   //TODO: print whether discrete or solver
			print_tree(this, batch.run_code, ss);
		}
		this->batch_code = ss.str();
		
		ir_string = &this->llvm_ir;
	}
	
	jit_compile_module(llvm_data, ir_string);
	
	this->initial_batch.compiled_code = get_jitted_batch_function("initial_values");
	batch_idx = 0;
	for(auto &batch : this->batches) {
		std::string function_name = std::string("batch_function_") + std::to_string(batch_idx);
		batch.compiled_code = get_jitted_batch_function(function_name);
		++batch_idx;
	}
	
	is_compiled = true;

/*
	std::stringstream ss;
	for(auto &instr : instructions) {
		if(instr.code) {
			print_tree(this, instr.code, ss);
			ss << "\n";
		}
	}
	warning_print(ss.str());
*/

	// NOTE: For some reason it doesn't work to have the deletion in the destructor of the Model_Instruction ..
	//    Has to do with the resizing of the instructions vector where instructions are moved, and it is tricky
	//    to get that to work.
	for(auto &instr : initial_instructions) {
		if(instr.code)
			delete instr.code;
	}
	
	for(auto &instr : instructions) {
		if(instr.code)
			delete instr.code;
	}
	
#ifndef MOBIUS_EMULATE
	
	delete initial_batch.run_code;
	initial_batch.run_code = nullptr;
	for(auto &batch : batches) {
		delete batch.run_code;
		batch.run_code = nullptr;
	}
	
#endif
	
}
