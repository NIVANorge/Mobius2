
/*
	This is the file that does all the really difficult stuff.
	
	TODO: Many functions here need a proper refactoring and cleanup!
*/


#include "model_application.h"
#include "function_tree.h"
#include "model_codegen.h"
#include "grouped_topological_sort.h"

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
	else if(type == Model_Instruction::Type::external_computation)
		ss << "external_computation(" << app->vars[var_id]->name << ")";
	
	return ss.str();
}

void
debug_print_batch_array(Model_Application *app, std::vector<Batch_Array> &arrays, std::vector<Model_Instruction> &instructions, std::ostream &os, bool show_dependencies = false) {
	for(auto &array : arrays) {
		os << "\t[";
		for(auto index_set : array.index_sets)
			os << "\"" << app->model->index_sets[index_set]->name << "\" ";
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
				for(int dep : instr->inherits_index_sets_from_instruction)
					os << "\t\t\t- " << instructions[dep].debug_string(app) << "\n";
				for(auto &dep : instr->inherits_index_sets_from_state_var)
					os << "\t\t\t-- " << instructions[dep.var_id.id].debug_string(app) << "\n";
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
insert_dependency_base(Model_Application *app, Model_Instruction *instr, Entity_Id to_insert) {
	
	// Returns true if there is a change in dependencies
	
	auto model = app->model;
	
	std::set<Entity_Id> *maximal_index_sets = nullptr;

	if(instr->type == Model_Instruction::Type::compute_state_var || instr->type == Model_Instruction::Type::add_to_connection_aggregate) {
		auto var = app->vars[instr->var_id];
		if(var->type == State_Var::Type::declared)
			maximal_index_sets = &as<State_Var::Type::declared>(var)->maximal_allowed_index_sets;
		else if(var->type == State_Var::Type::dissolved_flux) {
			// could happen if it gets a dependency inserted from a connection aggregation.
			// Other dependencies should be fine automatically.
			maximal_index_sets = &as<State_Var::Type::dissolved_flux>(var)->maximal_allowed_index_sets;
		}
	}
	
	auto &dependencies = instr->index_sets;
	
	if(!is_valid(to_insert))
		fatal_error(Mobius_Error::internal, "Tried to insert an invalid id as an index set dependency.");

	auto find = std::find(dependencies.begin(), dependencies.end(), to_insert);
	
	if(find != dependencies.end())
		return false;
	
	auto set = model->index_sets[to_insert];
	
	// What do we do with higher order dependencies in either of the special cases? Maybe easiest to disallow double dependencies on union sets for now?
	
	// If we insert a union index set and there is already one of the union members there, we should ignore it.
	if(!set->union_of.empty()) {
		
		Entity_Id union_member_allowed = invalid_entity_id;
		for(auto ui_id : set->union_of) {
			auto find2 = std::find(dependencies.begin(), dependencies.end(), ui_id);
			if(find2 != dependencies.end())
				return false;
			if(maximal_index_sets) {
				if(maximal_index_sets->find(ui_id) != maximal_index_sets->end())
					union_member_allowed = ui_id;
			}
		}
		// If the reference var location could only depend on a union member, we should insert the union member rather than the union.
		// (note that if the union member was already a dependency, we have exited already, so this insertion is indeed new).
		if(is_valid(union_member_allowed)) {
			
			dependencies.insert(union_member_allowed);
			return true;
		}
	}
	
	if(maximal_index_sets) {
		if(maximal_index_sets->find(to_insert) == maximal_index_sets->end())
			fatal_error(Mobius_Error::internal, "Inserting a banned index set dependency ", model->index_sets[to_insert]->name, " for ", instr->debug_string(app), "\n");
	}

	// TODO: If any of the existing index sets in dependencies is a union and we try to insert a union member, that should overwrite the union?
	//   Hmm, however, this should not really happen as a Var_Location should not be able to have such a double dependency in the first place.
	//   We should monitor how this works out.
	
	dependencies.insert(to_insert);
	return true;
}

bool
insert_dependency(Model_Application *app, Model_Instruction *instr, Entity_Id to_insert) {
	// Returns true if there is a change in dependencies
	
	bool changed = false;
	auto sub_indexed_to = app->model->index_sets[to_insert]->sub_indexed_to;
	if(is_valid(sub_indexed_to))
		changed = insert_dependency_base(app, instr, sub_indexed_to);
	bool changed2 = insert_dependency_base(app, instr, to_insert);
	
	return changed || changed2;
}

void
insert_dependencies(Model_Application *app, Model_Instruction *instr, const Identifier_Data &dep) {
	
	const std::vector<Entity_Id> *index_sets = nullptr;
	if(dep.variable_type == Variable_Type::parameter)
		index_sets = &app->parameter_structure.get_index_sets(dep.par_id);
	else if(dep.variable_type == Variable_Type::series)
		index_sets = &app->series_structure.get_index_sets(dep.var_id);
	else
		fatal_error(Mobius_Error::internal, "Misuse of insert_dependencies().");
	
	auto avoid = avoid_index_set_dependency(app, dep.restriction);
	
	int sz = index_sets->size();
	int idx = 0;
	for(auto index_set : *index_sets) {
		
		if(index_set == avoid) continue;
		
		insert_dependency(app, instr, index_set);

		++idx;
	}
}

bool
insert_dependencies(Model_Application *app, Model_Instruction *instr, std::set<Entity_Id> &to_insert, const Identifier_Data &dep) {
	auto avoid = avoid_index_set_dependency(app, dep.restriction);
	
	bool changed = false;
	for(auto index_set : to_insert) {
		if(index_set == avoid) continue;
		
		bool changed2 = insert_dependency(app, instr, index_set);
		changed = changed || changed2;
	}
	return changed;
}

void
resolve_basic_dependencies(Model_Application *app, std::vector<Model_Instruction> &instructions) {
	
	auto model = app->model;
	
	for(auto &instr : instructions) {
			
		if(!instr.code) continue;
		
		std::set<Identifier_Data> code_depends;
		register_dependencies(instr.code, &code_depends);
		if(instr.type == Model_Instruction::Type::compute_state_var) {
			auto var = app->vars[instr.var_id];
			if(var->specific_target.get())
				register_dependencies(var->specific_target.get(), &code_depends);
		}
		
		for(auto &dep : code_depends) {
			if(dep.variable_type == Variable_Type::parameter || dep.variable_type == Variable_Type::series) {
				
				insert_dependencies(app, &instr, dep);
			
			} else if(dep.variable_type == Variable_Type::is_at) {
				
				auto index_set = app->model->connections[dep.restriction.r1.connection_id]->node_index_set;
				insert_dependency(app, &instr, index_set);
				
			} else if(dep.variable_type == Variable_Type::state_var) {
				
				instr.inherits_index_sets_from_state_var.insert(dep);
				
				// TODO: Secondary restriction r2 also?
				auto &res = dep.restriction.r1;
				
				if(res.type == Restriction::above || res.type == Restriction::below) {
					if(dep.restriction.r1.type == Restriction::above) {
						instr.loose_depends_on_instruction.insert(dep.var_id.id);
					} else {
						instr.instruction_is_blocking.insert(dep.var_id.id);
						instr.depends_on_instruction.insert(dep.var_id.id);
					}
					// NOTE: The following is needed (e.g. NIVAFjord breaks without it), but I would like to figure out why and then document it here with a comment.
						// It is probably that if it doesn't get this dependency at all, it tries to add or subtract from a nullptr index.
					// TODO: However if we could determine that the reference is constant over that index set, we could allow that and just omit adding to that index in codegen.

					if(instr.type == Model_Instruction::Type::compute_state_var) {
						auto conn = model->connections[res.connection_id];
						if(conn->type == Connection_Type::directed_graph) {
							auto comp = app->find_connection_component(res.connection_id, app->vars[dep.var_id]->loc1.components[0], false);
							if(comp && comp->is_edge_indexed)
								insert_dependency(app, &instr, conn->edge_index_set);
						} else if(conn->type == Connection_Type::grid1d) {
							auto index_set = conn->node_index_set;
							insert_dependency(app, &instr, index_set);
						} else
							fatal_error(Mobius_Error::internal, "Got a 'below' dependency for something that should not have it.");
					}
					
				} else if(res.type == Restriction::top || res.type == Restriction::bottom) {
					instr.depends_on_instruction.insert(dep.var_id.id);
					if(res.type == Restriction::bottom)
						instr.instruction_is_blocking.insert(dep.var_id.id);
					
					auto index_set = app->model->connections[res.connection_id]->node_index_set;
					auto parent = app->model->index_sets[index_set]->sub_indexed_to;
					if(is_valid(parent))
						insert_dependency(app, &instr, parent);
					
				} else if(!(dep.flags & Identifier_Data::Flags::last_result)) {  // TODO: Shouldn't last_result disqualify more of the strict dependencies in other cases above?
					auto var = app->vars[dep.var_id];
					if(var->type == State_Var::Type::connection_aggregate)
						instr.loose_depends_on_instruction.insert(dep.var_id.id);
					else
						instr.depends_on_instruction.insert(dep.var_id.id);
				}
			}
		}
	}
}

bool
propagate_index_set_dependencies(Model_Application *app, std::vector<Model_Instruction> &instructions) {
	
	Mobius_Model *model = app->model;
	
	// Let index set dependencies propagate from state variable to state variable. (For instance if a looks up the value of b, a needs to be indexed over (at least) the same index sets as b. This could propagate down a long chain of dependencies, so we have to keep iterating until nothing changes.
	bool changed;
	bool changed_at_all = false;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto &instr : instructions) {
		
			for(int dep : instr.inherits_index_sets_from_instruction) {
				auto &dep_idx = instructions[dep].index_sets;
				for(auto &dep_idx_set : dep_idx) {
					if(insert_dependency(app, &instr, dep_idx_set))
						changed = true;
				}
			}
			for(auto &dep : instr.inherits_index_sets_from_state_var) {
				auto &dep_idx = instructions[dep.var_id.id].index_sets;
				if(insert_dependencies(app, &instr, dep_idx, dep))
					changed = true;
			}
		}
		
		if(changed)
			changed_at_all = true;
		
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Failed to resolve state variable index set dependencies in the alotted amount of iterations!");
	
	return changed_at_all;
}

// TODO: We should probably just always store index set dependencies packed like this.
//   Will optimize a lot of the dependency checking code.

inline u64
pack_index_sets(std::set<Entity_Id> &index_sets) {
	u64 result = 0;
	for(auto id : index_sets)
		result |= ((u64)1 << (id.id));
	return result;
}

inline void
unpack_index_sets(u64 pack, std::set<Entity_Id> &index_sets) {
	for(s16 id = 0; id < 64; ++id) {
		if(pack & ((u64)1 << id))
			index_sets.insert(Entity_Id {Reg_Type::index_set, id});
	}
}

/*
struct
Instruction_Array_Grouping_Predicate {
	std::vector<Model_Instruction> *instructions;
	
	bool depends(int node, int on_node) {
		auto &dep = (*instructions)[node].depends_on_instruction;
		if(dep.find(on_node) != dep.end()) return true;
		auto &dep2 = (*instructions)[node].loose_depends_on_instruction;
		if(dep2.find(on_node) != dep2.end()) return true;
		return false;
	}
	inline bool blocks(int node, int other_node) {
		auto &blk = (*instructions)[node].instruction_is_blocking;
		return (blk.find(other_node) != blk.end());
	}
	inline u64 label(int node) {  return pack_index_sets((*instructions)[node].index_sets);  }
	inline bool allow_move(u64 label) { return true; }  //NOTE: There is no a priori reason we can't move an instruction out of a group in this application.
};
*/

void
check_for_special_weak_cycles(Model_Application *app, std::vector<Model_Instruction> &instructions, std::vector<Constraint_Cycle<u64>> &cycles) {
	
	auto model = app->model;
	
	for(auto &cycle : cycles) {
		if(cycle.nodes.size() == 1) continue;
		
		for(int node : cycle.nodes) {
			auto &instr = instructions[node];
			if(instr.type != Model_Instruction::Type::compute_state_var) continue;
			auto var = app->vars[instr.var_id];
			if(var->type != State_Var::Type::connection_aggregate) continue;
			auto var2 = as<State_Var::Type::connection_aggregate>(var);
			if(var2->is_source) continue; // Not sure if this could even happen.
			auto conn = model->connections[var2->connection];
			if(conn->type != Connection_Type::directed_graph) continue;
			if(conn->no_cycles) continue;
			/*
				Note: The reasoning for this is as follows. If in_flux(connection, something) is weakly dependent on itself, it is dependent on values of itself from within the same 'for loop'.
				This means that the for loop has to be ordered so that indexes that are 'earlier' in the connection graph come before later ones, but then there can be no cycle within the graph. We force the user to declare @no_cycles so that its validity can be checked at another stage without making the compiler check it here (that would complicate the code).
				
				Note: Don't confuse the term 'cycle' within the weak+strong model instruction dependencies with a 'cycle' within the connection graph itself.
				If there is a *cycle* among the connection aggregate instructions, it means that the indexes must be ordered along the graph arrows, and so there can be *no cycle* in the connection graph.
			*/
			conn->source_loc.print_error_header(Mobius_Error::model_building);
			error_print("Regarding the connection \"", conn->name, "\": there is a circular dependency of the aggregation variable in_flux(", model->get_symbol(var2->connection), ", ");
			error_print_location(model, app->vars[var2->agg_for]->loc1);
			error_print(") with itself. This is only allowed if the connection is marked as @no_cycles, but it is not.\nThe circular dependency also involves the following declared state variables:\n");
			for(int other_node : cycle.nodes) {
				if(other_node == node) continue;
				auto &other_instr = instructions[other_node];
				if(other_instr.type != Model_Instruction::Type::compute_state_var) continue;
				auto other_var = app->vars[other_instr.var_id];
				if(other_var->type != State_Var::Type::declared) continue;
				error_print_location(model, other_var->loc1);
				error_print("\n");
			}
			mobius_error_exit();
			// ( Note: It is probably safe to assume that it includes declared state variables at all (?). There should be no other way such a cycle could happen.)
		}
	}
}

void
build_batch_arrays(Model_Application *app, std::vector<int> &instrs, std::vector<Model_Instruction> &instructions, std::vector<Batch_Array> &arrays_out, bool initial) {
	Mobius_Model *model = app->model;
	
	if(model->index_sets.count() > 64)
		fatal_error(Mobius_Error::internal, "There is an implementation restriction so that you can't currently have more than 64 index_sets in the same model.");
	
	struct Instruction_Array_Grouping_Predicate {
		std::vector<Model_Instruction> *instructions;
		std::vector<u8>                *participates_;
		
		inline bool participates(int node) { return (*participates_)[node]; }
		inline const std::set<int> &edges(int node) { return (*instructions)[node].depends_on_instruction; }
		inline const std::set<int> &weak_edges(int node) { return (*instructions)[node].loose_depends_on_instruction; }
		inline const std::set<int> &blocks(int node) { return (*instructions)[node].instruction_is_blocking; }
		
		inline u64 label(int node) {  return pack_index_sets((*instructions)[node].index_sets);  }
	};
	
	std::vector<u8> participates(instructions.size());
	for(int instr : instrs)
		participates[instr] = true;
	
	Instruction_Array_Grouping_Predicate predicate { &instructions, &participates };
	
	constexpr int max_iter = 10;
	std::vector<Node_Group<u64>> groups;
	std::vector<Constraint_Cycle<u64>> max_cycles;
	bool success = label_grouped_topological_sort_additional_weak_constraint(predicate, groups, max_cycles, instructions.size(), max_iter);
	
	// TODO: Have better error reporting from the above function.
	if(!success)
		fatal_error(Mobius_Error::internal, "Something went wrong with the instruction grouping (", max_iter, ").");
	
	if(!initial)
		check_for_special_weak_cycles(app, instructions, max_cycles);
	
	arrays_out.clear();
	
	for(auto &group : groups) {
		Batch_Array array;
		array.instr_ids = std::move(group.nodes);
		unpack_index_sets(group.label, array.index_sets);
		arrays_out.push_back(std::move(array));
	}
	
#if 0
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	debug_print_batch_array(model, batch_out, instructions, global_warning_stream);
	warning_print("\n\n");
#endif
}


void
create_initial_vars_for_lookups(Model_Application *app, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions);

void
ensure_has_intial_value(Model_Application *app, Var_Id var_id, std::vector<Model_Instruction> &instructions) {
	
	auto instr = &instructions[var_id.id];
			
	if(instr->is_valid()) return; // If it already exists, fine!
	
	// Some other variable wants to look up the value of this one in the initial step, but it doesn't have initial code. If it has regular code, we can substitute that!
	
	auto var = app->vars[var_id];
	//TODO: We have to be careful, because there are things that are allowed in regular code that is not allowed in initial code. We have to vet for it here!
		
	instr->type = Model_Instruction::Type::compute_state_var;
	instr->var_id = var_id;
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
		if(instr_agg_of->is_valid()) return; // If it is already valid, fine!
		
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
}

void
create_initial_vars_for_lookups(Model_Application *app, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions) {
	for(auto arg : expr->exprs) create_initial_vars_for_lookups(app, arg, instructions);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->variable_type == Variable_Type::state_var) {
			
			ensure_has_intial_value(app, ident->var_id, instructions);
		}
	}
}

int
make_clear_instr(std::vector<Model_Instruction> &instructions, Var_Id var_id, Entity_Id solver_id) {
	// Make an instruction that clears the given var_id to 0.
	
	int clear_idx = instructions.size();
	instructions.emplace_back();
	auto &clear_instr = instructions[clear_idx]; 
	
	clear_instr.type = Model_Instruction::Type::clear_state_var;
	clear_instr.solver = solver_id;
	clear_instr.var_id = var_id;     // The var_id of the clear_instr indicates which variable we want to clear.
	clear_instr.inherits_index_sets_from_instruction.insert(var_id.id);
	
	instructions[var_id.id].clear_instr = clear_idx;
	
	return clear_idx;
}

int
make_add_to_aggregate_instr(Model_Application *app, std::vector<Model_Instruction> &instructions, Entity_Id solver_id, Var_Id agg_var, Var_Id agg_of, int clear_id, Var_Id source_id = invalid_var, Var_Loc_Restriction *restriction = nullptr) {
	
	// Make an instruction that adds  the value of agg_of to the value of agg_var .

	int add_to_aggr_id = instructions.size();
	instructions.emplace_back();
	auto &add_to_aggr_instr = instructions[add_to_aggr_id];
	
	add_to_aggr_instr.depends_on_instruction.insert(clear_id);  // We can only sum to the aggregation variable after the variable is cleared.
	add_to_aggr_instr.instruction_is_blocking.insert(clear_id); // This says that the clear_id has to be in a separate for loop from this instruction. Not strictly needed for non-connection aggregate, but probably doesn't hurt...
	
	add_to_aggr_instr.solver = solver_id;
	add_to_aggr_instr.target_id = agg_var;
	add_to_aggr_instr.var_id = agg_of;
	
	if(restriction) {
		add_to_aggr_instr.restriction = *restriction;
		add_to_aggr_instr.type = Model_Instruction::Type::add_to_connection_aggregate;
		add_to_aggr_instr.source_id = source_id;
	} else {
		add_to_aggr_instr.type = Model_Instruction::Type::add_to_aggregate;
		add_to_aggr_instr.inherits_index_sets_from_instruction.insert(agg_var.id);
	}
	
	// The variable that we aggregate to is only "ready" after the summing is done.
	instructions[agg_var.id].depends_on_instruction.insert(add_to_aggr_id);
	instructions[agg_var.id].solver = solver_id;  // Strictly only needed for regular aggregate, since for connection aggregate it will have been done already
	
	return add_to_aggr_id;
}

void
process_grid1d_connection_aggregation(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id agg_id, Var_Id flux_id, int clear_id, bool is_first) {
	
	auto model = app->model;
	
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	auto flux_var =  app->vars[flux_id];
	
	Specific_Var_Location loc = is_first ? flux_var->loc1 : flux_var->loc2;
	auto &res = loc.r1;
	
	Var_Location source = loc;
	if(!is_first && res.type == Restriction::below && !is_located(source))
		source = flux_var->loc1;
	
	if(app->vars.id_of(source) != agg_var->agg_for) return;
	
	auto conn = model->connections[agg_var->connection];
	auto var_solver = instructions[agg_id.id].solver;
	
	Var_Id source_id = invalid_var;
	if(!is_first && is_located(flux_var->loc1))
		source_id = app->vars.id_of(flux_var->loc1);
	
	int add_to_aggr_id = make_add_to_aggregate_instr(app, instructions, var_solver, agg_id, flux_id, clear_id, source_id, &loc);
		
	auto add_to_aggr_instr = &instructions[add_to_aggr_id];
	if(is_first)
		add_to_aggr_instr->subtract = true;
		
	// Get at least one instance of the aggregation variable per instance of the variable we are aggregating for
	instructions[agg_id.id].inherits_index_sets_from_instruction.insert(agg_var->agg_for.id);
	
	auto &components = app->connection_components[agg_var->connection].components;
	auto source_comp = components[0].id;
	
	bool found = false;
	for(int idx = 0; idx < source.n_components; ++idx)
		if(source.components[idx] == source_comp) found = true;
	if(components.size() != 1 || !found)
		// NOTE: This should already have been checked in model_compilation, this is just a safeguard.
		fatal_error(Mobius_Error::internal, "Got an grid1d connection for a state var that the connection is not supported for.");
	
	auto index_set = conn->node_index_set;
	
	if(!is_first)
		instructions[flux_id.id].restriction = loc;
	
	auto type = res.type;
	
	if(type == Restriction::below) {
		insert_dependency(app, add_to_aggr_instr, index_set);
		
		// This is because we have to check per index if the value should be computed at all (no if we are at the bottom).
		// TODO: Shouldn't an index count lookup give a direct index set dependency in the dependency system instead?
		insert_dependency(app, &instructions[flux_id.id], index_set);
		
		// TODO: Is this still needed: ?
		add_to_aggr_instr->inherits_index_sets_from_instruction.insert(agg_id.id);
		
	} else if (type == Restriction::top || type == Restriction::bottom) {
		
		auto parent = model->index_sets[index_set]->sub_indexed_to;
		if(is_valid(parent))
			insert_dependency(app, add_to_aggr_instr, parent);
		
	} else {
		fatal_error(Mobius_Error::internal, "Should not have got this type of restriction for a grid1d flux, ", app->vars[flux_id]->name, ".");
	}
	
	insert_dependency(app, &instructions[agg_var->agg_for.id], index_set);
}

void
process_graph_connection_aggregation(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id agg_id, Var_Id flux_id, int clear_id) {
	
	auto model = app->model;
	
	auto agg_var = as<State_Var::Type::connection_aggregate>(app->vars[agg_id]);
	auto flux_var =  app->vars[flux_id];

	// TODO: Some of this must be updated if we allow graphs to be over quantity components, not just compartment components.
	
	auto source_comp_id = flux_var->loc1.components[0];
	auto find_source = app->find_connection_component(agg_var->connection, source_comp_id, false);
	if(!find_source) return; // Can happen if it is not given in the graph data (if this is a graph connection).  .. although isn't the flux already disabled then?
	if(!find_source->can_be_located_source) return;
	
	Entity_Id target_comp_id = invalid_entity_id;
	Sub_Indexed_Component *find_target = nullptr;
	
	if(agg_var->is_source) {
		if(agg_var->agg_for != app->vars.id_of(flux_var->loc1)) return;
	} else {
		// Check if this flux could point at the given aggregate location.
		Var_Location loc = flux_var->loc1;
		Var_Location target_loc = app->vars[agg_var->agg_for]->loc1;
		loc.components[0] = invalid_entity_id;
		target_loc.components[0] = invalid_entity_id;
		if(loc != target_loc) return;
		
		// Also test if there is actually an arrow for that connection in the specific data we are setting up for now.
		target_comp_id = app->vars[agg_var->agg_for]->loc1.components[0];
		find_target = app->find_connection_component(agg_var->connection, target_comp_id);
		auto find = find_target->possible_sources.find(source_comp_id);
		if(find == find_target->possible_sources.end()) return;
	}
			
	auto conn = model->connections[agg_var->connection];
	auto var_solver = instructions[agg_id.id].solver;
	
	instructions[flux_id.id].restriction = flux_var->loc2;
	
	auto source_id = app->vars.id_of(flux_var->loc1);
	int add_to_aggr_id = make_add_to_aggregate_instr(app, instructions, var_solver, agg_id, flux_id, clear_id, source_id, &flux_var->loc2);
		
	auto add_to_aggr_instr = &instructions[add_to_aggr_id];
		
	instructions[agg_id.id].inherits_index_sets_from_instruction.insert(agg_var->agg_for.id); 
	
	for(auto index_set : find_source->index_sets)
		insert_dependency(app, add_to_aggr_instr, index_set);
		
	if(find_source->is_edge_indexed) {
		insert_dependency(app, add_to_aggr_instr, conn->edge_index_set);
		insert_dependency(app, &instructions[flux_id.id], conn->edge_index_set);
	}
	
	if(!agg_var->is_source) { // TODO: should (something like) this also be done for the source aggregate in directed_graph?
		
		// TODO: Make a better explanation of what is going on in this block and why it is needed (What is the failure case otherwise).
		
		// If the target compartment (not just what the connection indexes over) has an index set shared with the source compartment, we must index the target variable over that.
		auto target_comp = model->components[target_comp_id];
		auto target_index_sets = find_target->index_sets; // vector copy;
		for(auto index_set : find_source->index_sets) {
			if(std::find(target_comp->index_sets.begin(), target_comp->index_sets.end(), index_set) != target_comp->index_sets.end())
				target_index_sets.push_back(index_set);
		}
		
		// Since the target could get a different value from the connection depending on its own index, we have to force it to be computed per each of these indexes even if it were not to have an index set dependency on this otherwise.
		for(auto index_set : target_index_sets)
			insert_dependency(app, &instructions[agg_var->agg_for.id], index_set);
	}
}


void
set_up_connection_aggregation(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id agg_id) {
	
	auto model = app->model;
	
	auto var_solver = instructions[agg_id.id].solver;
	// Make an instruction for clearing the aggregation variable to 0 before we start adding values to it.
	int clear_id = make_clear_instr(instructions, agg_id, var_solver);
	
	auto var  = app->vars[agg_id];
	auto var2 = as<State_Var::Type::connection_aggregate>(var);
	if(!is_valid(var2->agg_for))
		fatal_error(Mobius_Error::internal, "Something went wrong with setting up connection aggregates");
	
	// var is the aggregation variable for the target (or source)
	// agg_for  is the id of the quantity state variable for the target (or source).
	
	// Find all the connection fluxes pointing to the target (or going out from source)
	for(auto flux_id : app->vars.all_fluxes()) {
		
		auto flux_var = app->vars[flux_id];
		auto conn = model->connections[var2->connection];
		
		bool is_flux_source = false;
		if(flux_var->loc1.r1.connection_id == var2->connection) {
			if(conn->type != Connection_Type::grid1d)
				fatal_error(Mobius_Error::internal, "Connection in the source of a flux only allowed for grid1d.");
			process_grid1d_connection_aggregation(app, instructions, agg_id, flux_id, clear_id, true);
		}
		if(flux_var->loc2.r1.connection_id == var2->connection) {
			if(conn->type == Connection_Type::grid1d)
				process_grid1d_connection_aggregation(app, instructions, agg_id, flux_id, clear_id, false);
			else if(conn->type == Connection_Type::directed_graph)
				process_graph_connection_aggregation(app, instructions, agg_id, flux_id, clear_id);
			else
				fatal_error(Mobius_Error::internal, "Unimplemented connection type for set_up_connection_aggregation");
		}
		
	}
}

void
basic_instruction_solver_configuration(Model_Application *app, std::vector<Model_Instruction> &instructions) {
	
	auto model = app->model;
	
	for(auto solver_id : model->solvers) {
		auto solver = model->solvers[solver_id];
		
		for(auto &pair : solver->locs) {
			auto &loc = pair.first;
			auto &source_loc = pair.second;
			
			Var_Id var_id = app->vars.id_of(loc);
			auto comp = model->components[loc.last()];
			if(comp->decl_type != Decl_Type::quantity) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The variable \"", app->vars[var_id], "\" is not a quantity, and so it can not be put on a solver directly.");
			}
			if(loc.is_dissolved()) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("A solver was specified for \"", app->vars[var_id], "\". For now we don't allow specifying solvers for dissolved substances. Instead they are given the solver of the variable they are dissolved in.");
			}
			if(is_valid(instructions[var_id.id].solver)) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The quantity \"", app->vars[var_id]->name, "\" was put on a solver more than one time.");
			}
			if(!app->vars[var_id]->store_series) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The quantity \"", app->vars[var_id]->name, "\" was specified as @no_store, and so can not be put on a solver.");
			}
			instructions[var_id.id].solver = solver_id;
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
		
		// Fluxes with an ODE variable as source is given the same solver as this variable.
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
	
	// Currently we don't support connections for fluxes that are not on solvers (except if the source is 'out')
	for(auto var_id : app->vars.all_fluxes()) {
		
		auto var = app->vars[var_id];
		if(!is_located(var->loc1)) continue;
		if(var->loc1.r1.type == Restriction::none && var->loc2.r1.type == Restriction::none) continue;
		auto source_id = app->vars.id_of(var->loc1);
		if(!is_valid(instructions[source_id.id].solver)) {
			// Technically not all fluxes may be declared, but if there is an error, it *should* trigger on a declared flux first.
				// TODO: This is not all that robust.
			model->fluxes[as<State_Var::Type::declared>(var)->decl_id]->source_loc.print_error_header();
			//TODO: this is not really enough info to tell the user where the error happened.
			fatal_error("This flux was put on a connection, but the source is a discrete variable. This is currently not supported.");
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
		
		// TODO: Couldn't the lookup of the function be moved to instruction_codegen ?
		Math_Expr_FT *fun = nullptr;
		if(var->type == State_Var::Type::declared) {
			auto var2 = as<State_Var::Type::declared>(var);
			
			fun = var2->function_tree.get();
			if(initial) fun = var2->initial_function_tree.get();
			if(!initial && !fun && !is_valid(var2->external_computation)) {
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
	}
	
	if(initial) {
		for(auto var_id : app->vars.all_state_vars()) {
			auto instr = &instructions[var_id.id];
			if(!instr->is_valid()) continue;
			if(!instr->code) continue;
			
			create_initial_vars_for_lookups(app, instr->code, instructions);
		}
	} else
		basic_instruction_solver_configuration(app, instructions);

	
	// Generate instructions needed to compute different types of specialized variables.
	
	for(auto var_id : app->vars.all_state_vars()) {
		auto instr = &instructions[var_id.id];
		
		if(!instr->is_valid()) continue;
	
		auto var = app->vars[var_id];
		
		bool is_aggregate  = var->type == State_Var::Type::regular_aggregate;
		bool has_aggregate = var->has_flag(State_Var::has_aggregate);
		
		if(var->type == State_Var::Type::external_computation) {
			
			instr->type = Model_Instruction::Type::external_computation;
			
			auto var2 = as<State_Var::Type::external_computation>(var);
			auto external = model->external_computations[var2->decl_id];
			
			std::vector<Entity_Id> index_sets;
			if(is_valid(external->component))
				index_sets = model->components[external->component]->index_sets;
			
			// TODO: Check for solver conflicts.
			for(auto target_id : var2->targets) {
				auto &target = instructions[target_id.id];
				instr->solver = target.solver;
				target.depends_on_instruction.insert(var_id.id);
				
				// TODO: Check that every target could index over the computation index sets at all
				
				// NOTE: A target of a external_computation should always index over as many index sets as it can.
				auto &loc = app->vars[target_id]->loc1;
				for(int idx = 0; idx < loc.n_components; ++idx) {
					auto comp = model->components[loc.components[idx]];
					for(auto index_set : comp->index_sets)
						insert_dependency(app, &target, index_set);
				}
			}
			
			instr->code = copy(var2->code.get());
			
			for(auto index_set : index_sets)
				insert_dependency(app, instr, index_set);
			
			auto code = static_cast<External_Computation_FT *>(instr->code);
			
			for(auto &arg : code->arguments) {
				if(arg.has_flag(Identifier_Data::result)) continue;
				
				if(arg.variable_type == Variable_Type::state_var) {
					instr->depends_on_instruction.insert(arg.var_id.id);
					instr->instruction_is_blocking.insert(arg.var_id.id);
				} else if (arg.variable_type == Variable_Type::parameter) {
					
				} else
					fatal_error(Mobius_Error::internal, "Unexpected variable type for external_computation argument.");
			}
			
			continue;
		}
		
		if(is_aggregate) {
			
			// NOTE: We have to look up the solver here. This is because we can't necessarily set it up before this point.
			//    although TODO: Couldn't we though?
			auto var2 = as<State_Var::Type::regular_aggregate>(var);
			auto agg_of = var2->agg_of;
			auto var_solver = instructions[agg_of.id].solver;
			
			int clear_id = make_clear_instr(instructions, var_id, var_solver);
			make_add_to_aggregate_instr(app, instructions, var_solver, var_id, agg_of, clear_id);
			
			// The instruction for the var. It compiles to a no-op, but it is kept in the model structure to indicate the location of when this var has its final value. (also used for result storage structure).
			// Since we generate one aggregation variable per target compartment, we have to give it the full index set dependencies of that compartment
			// TODO: we could generate one per variable that looks it up and prune them later if they have the same index set dependencies (?)
			// TODO: we could also check if this is necessary at all any more?
			auto agg_instr = &instructions[var_id.id];
			auto agg_to_comp = model->components[var2->agg_to_compartment];
			
			for(auto index_set : agg_to_comp->index_sets)
				insert_dependency(app, agg_instr, index_set);
			
		} else if(var->type == State_Var::Type::connection_aggregate) {
			
			if(initial)
				fatal_error(Mobius_Error::internal, "Got a connection flux in the initial step.");
			if(!is_valid(instr->solver))
				fatal_error(Mobius_Error::internal, "Got aggregation variable for connection fluxes without a solver.");
			
			set_up_connection_aggregation(app, instructions, var_id);
		}
		
		if(initial || !var->is_flux()) continue;
		
		
		// The rest is dealing with instructions for updating quantities based on discrete fluxes.
		
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
		bool is_connection = (var->loc1.r1.type != Restriction::none || var->loc2.r2.type != Restriction::none);
		if(is_connection && has_aggregate)
			fatal_error(Mobius_Error::internal, "Somehow a connection flux got a regular aggregate: ", var->name);
		
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
		
		// TODO: make an error or warning if an ODE flux is given a discrete order. Maybe also if a discrete flux is not given one (but maybe not).
		
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
bool
propagate_solvers(Model_Application *app, int instr_id, Entity_Id solver, std::vector<Model_Instruction> &instructions) {
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
		if(instr->clear_instr >= 0)  // The clear instruction would otherwise not be visited since it doesn't depend on anything (only the other way around).
			instructions[instr->clear_instr].solver = solver;
	}
	instr->visited = false;
	
	return found;
}

struct
Pre_Batch {
	Entity_Id solver = invalid_entity_id;
	std::vector<int> instructions;
	std::set<int>    depends_on;
};

struct
Instruction_Sort_Predicate {
	std::vector<Model_Instruction> *instructions;
	inline bool participates(int node) {  return (*instructions)[node].is_valid(); }
	inline const std::set<int>& edges(int node) { return (*instructions)[node].depends_on_instruction; }
};

void
report_instruction_cycle(Model_Application *app, std::vector<Model_Instruction> &instructions, std::vector<int> &cycle, bool initial = false) {
	begin_error(Mobius_Error::model_building);
	error_print("There is a circular dependency among the ", initial ? "initial-value " : "", "model instructions:\n");
	bool first = true;
	for(int instr_id : cycle) {
		if(first) error_print("    ");
		else      error_print("--> ");
		error_print(instructions[instr_id].debug_string(app), '\n');
		first = false;
	}
	fatal_error("\n");
}

void create_batches(Model_Application *app, std::vector<Batch> &batches_out, std::vector<Model_Instruction> &instructions) {

	struct
	Pre_Batch_Sort_Predicate {
		std::vector<Pre_Batch> *pre_batches;
		inline bool participates(int node) { return true; }
		inline const std::set<int> &edges(int node) { return (*pre_batches)[node].depends_on; }
	};

	struct
	Instruction_Solver_Grouping_Predicate {
		std::vector<Model_Instruction> *instructions;
		
		bool depends(int node, int on_node) {
			auto &dep = (*instructions)[node].depends_on_instruction;
			if(dep.find(on_node) != dep.end()) return true;
			auto &dep2 = (*instructions)[node].loose_depends_on_instruction;
			if(dep2.find(on_node) != dep2.end()) return true;
			return false;
		}
		inline bool blocks(int node, int other_node) { return false; }  // NOTE: Not needed for solver grouping.
		inline Entity_Id label(int node) {  return (*instructions)[node].solver;  }
		inline bool allow_move(Entity_Id label) { return !is_valid(label); } // NOTE: In this specific application, we can't move an instruction out of its solver batch if it has a solver.
	};
	
	// Propagate solvers in a way so that instructions that are (dependency-) sandwiched between other instructions that are on the same ODE solver are also put in that ODE function.
	// TODO: We need to make some guard to check that this is a sufficient amount of iterations!
	for(int idx = 0; idx < 10; ++idx) {
		for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
			auto instr = &instructions[instr_id];
			
			if(!instr->is_valid()) continue;
			
			if(is_valid(instr->solver)) {
				for(int dep : instr->depends_on_instruction)
					propagate_solvers(app, dep, instr->solver, instructions);
				for(int dep : instr->loose_depends_on_instruction)
					propagate_solvers(app, dep, instr->solver, instructions);
			}
		}
	}
	
	// Some auto-gathered dependencies are not relevant for (and detrimental to) the instruction sorting. We remove them.
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto &instr = instructions[instr_id];
		
		if(!instr.is_valid()) continue;
		
		if(is_valid(instr.solver)) {
			// Remove dependency of any instruction on an ODE variable if they are on the same solver.
			// This is because ODE instructions don't need to (and should not) participate in the sorting of instructions.
			std::vector<int> remove;
			for(int other_id : instr.depends_on_instruction) {
				auto &other_instr = instructions[other_id];
				if(other_instr.type != Model_Instruction::Type::compute_state_var || !is_valid(other_instr.var_id)) continue;
				auto other_var = app->vars[other_instr.var_id];
				bool is_quantity = other_var->type == State_Var::Type::declared &&
					as<State_Var::Type::declared>(other_var)->decl_type == Decl_Type::quantity;
				if(other_instr.solver == instr.solver && is_quantity)
					remove.push_back(other_id);
			}
			for(int rem : remove)
				instr.depends_on_instruction.erase(rem);
		} else if (app->vars[instr.var_id]->is_flux()) {
			// Remove dependency of discrete fluxes on their sources. Discrete fluxes are ordered in a specific way, and the final value of the source is "ready" after the flux is subtracted. If the flux references its source, that will be a temporary value by design.
			auto var = app->vars[instr.var_id];
			if(is_located(var->loc1))
				instr.depends_on_instruction.erase(app->vars.id_of(var->loc1).id);
		}
	}
	
	// Make a 'naive' sorting of instructions by dependencies. This makes it easier to work with them later.
	std::vector<int> sorted_instructions;
	std::vector<int> maybe_cycle;
	
	Instruction_Sort_Predicate predicate { &instructions };
	bool success = topological_sort(predicate, sorted_instructions, instructions.size(), maybe_cycle);
	
	if(!success)
		report_instruction_cycle(app, instructions, maybe_cycle);
	
	std::vector<Pre_Batch> pre_batches;
	std::vector<int> pre_batch_of_solver(app->model->solvers.count(), -1);
	std::vector<int> pre_batch_of_instr(instructions.size(), -1);
	
	// Put all solver equations in the same pre batch, then put every non-solver equation into its own pre_batch.
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
	Pre_Batch_Sort_Predicate predicate2 { &pre_batches };
	success = topological_sort(predicate2, sorted_pre_batches, pre_batches.size(), maybe_cycle);
	if(!success)
		fatal_error(Mobius_Error::internal, "Unable to sort pre batches. This should not be possible if solvers are correctly propagated.");
	
	
#if 0
	log_print("**** Pre batches before grouping.");
	for(int id : sorted_pre_batches) {
		auto &batch = pre_batches[id];
		if(is_valid(batch.solver))
			log_print("* Solver : ", app->model->solvers[batch.solver]->name, "\n");
		else
			log_print(instructions[batch.instructions[0]].debug_string(app), "\n");
	}
	log_print("\n\n");
#endif
	
	// Make preliminary instruction groups by merging discrete equations into single groups.
	std::vector<Node_Group<Entity_Id>> grouped_batches;
	std::vector<std::vector<int>>      group_consists_of;
	
	// TODO: This is a lot like label_grouped_sort_first_pass . Could we reuse that somehow (maybe need an insertion predicate).
	for(int pre_batch_idx : sorted_pre_batches) {
		auto &pre_batch = pre_batches[pre_batch_idx];
		int insertion_idx = -1;
		if(!is_valid(pre_batch.solver)) {
			// If it is not on a solver, try to group it with the previous non-solver instruction *if possible* (e.g. this instruction doesn't depend on another instruction in between.)
			for(int compare_idx = (int)grouped_batches.size()-1; compare_idx >= 0; --compare_idx) {
				auto &compare = grouped_batches[compare_idx];
				if(!is_valid(compare.label)) insertion_idx = compare_idx;
				bool found_dependency = false;
				for(int other : group_consists_of[compare_idx]) {
					if(pre_batch.depends_on.find(other) != pre_batch.depends_on.end()) {
						if(!is_valid(compare.label)) insertion_idx = compare_idx; // Must to this again since we exit the loop.
						found_dependency = true;
						break;
					}
				}
				if(found_dependency) break;
			}
		}
		if(insertion_idx < 0) {
			insertion_idx = grouped_batches.size();
			grouped_batches.resize(insertion_idx+1);
			group_consists_of.resize(insertion_idx+1);
		}
		auto &insertion_group = grouped_batches[insertion_idx];
		insertion_group.nodes.insert(insertion_group.nodes.end(), pre_batch.instructions.begin(), pre_batch.instructions.end());
		insertion_group.label = pre_batch.solver;
		group_consists_of[insertion_idx].push_back(pre_batch_idx);
	}
	
	constexpr int max_iter = 10;
	Instruction_Solver_Grouping_Predicate predicate3 { &instructions };
	// TODO: This is actually a bit unoptimal right now because we are never allowed to move from solver groups (there is one per solver).
	//    Maybe include an allow_move in the predicate.
	success = optimize_label_group_packing(predicate3, grouped_batches, max_iter);
	if(!success)
		fatal_error(Mobius_Error::internal, "Unable to optimize instruction solver batch grouping in the allotted amount of iterations (", max_iter, ").");
	
	// Unpack to different structure needed for further processing.
	for(auto &group : grouped_batches) {
		Batch batch;
		batch.instrs = group.nodes;
		batch.solver = group.label;
		batches_out.push_back(std::move(batch));
	}
	
#if 0
	log_print("*** Batches before internal structuring: ***\n");
	for(auto &batch : batches_out) {
		if(is_valid(batch.solver))
			log_print("  solver(\"", app->model->solvers[batch.solver]->name, "\") :\n");
		else
			log_print("  discrete :\n");
		for(int instr_id : batch.instrs) {
			log_print("\t\t", instructions[instr_id].debug_string(app), "\n");
		}
	}
	log_print("\n\n");
#endif
}


void
add_array(
	std::vector<Multi_Array_Structure<Var_Id>> &result_structure, 
	std::vector<Multi_Array_Structure<Var_Id>> &temp_result_structure, 
	Batch_Array &array, 
	std::vector<Model_Instruction> &instructions
) {
	std::vector<Entity_Id> index_sets;
	for(auto &index_set : array.index_sets)
		index_sets.push_back(index_set);
	std::vector<Entity_Id> index_sets2 = index_sets;
	
	std::vector<Var_Id>    result_handles;
	std::vector<Var_Id>    temp_result_handles;
	for(int instr_id : array.instr_ids) {
		auto instr = &instructions[instr_id];
		if(instr->type == Model_Instruction::Type::compute_state_var) {
			if(instr->var_id.type == Var_Id::Type::state_var)
				result_handles.push_back(instr->var_id);
			else
				temp_result_handles.push_back(instr->var_id);
		}
	}
	if(!result_handles.empty()) {
		Multi_Array_Structure<Var_Id> arr(std::move(index_sets), std::move(result_handles));
		result_structure.push_back(std::move(arr));
	}
	if(!temp_result_handles.empty()) {
		Multi_Array_Structure<Var_Id> arr(std::move(index_sets2), std::move(temp_result_handles));
		temp_result_structure.push_back(std::move(arr));
	}
}

void
set_up_result_structure(Model_Application *app, std::vector<Batch> &batches, std::vector<Model_Instruction> &instructions) {
	if(app->result_structure.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up result structure twice.");
	if(!app->all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up result structure before all index sets received indexes.");
	
	// NOTE: we just copy the batch structure so that it is easier to optimize the run code for cache locality.
	// NOTE: It is crucial that all ode variables from the same batch are stored contiguously, so that part of the setup must be kept no matter what!
	std::vector<Multi_Array_Structure<Var_Id>> result_structure;
	std::vector<Multi_Array_Structure<Var_Id>> temp_result_structure;
	for(auto &batch : batches) {
		for(auto &array : batch.arrays)      add_array(result_structure, temp_result_structure, array, instructions);
		for(auto &array : batch.arrays_ode)  add_array(result_structure, temp_result_structure, array, instructions); // The ODEs will never be added to temp results in reality.
	}
	
	app->result_structure.set_up(std::move(result_structure));
	app->temp_result_structure.set_up(std::move(temp_result_structure));
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

	std::vector<Model_Instruction> initial_instructions;
	std::vector<Model_Instruction> instructions;
	build_instructions(this, initial_instructions, true);
	build_instructions(this, instructions, false);
	
	instruction_codegen(this, initial_instructions, true);
	instruction_codegen(this, instructions, false);



	resolve_basic_dependencies(this, initial_instructions);
	resolve_basic_dependencies(this, instructions);

	propagate_index_set_dependencies(this, initial_instructions);
	
	// NOTE: A state var inherits all index set dependencies from the code that computes its initial value.
	for(auto var_id : vars.all_state_vars()) {
		auto &init_idx = initial_instructions[var_id.id].index_sets;
		
		for(auto index_set : init_idx)
			insert_dependency(this, &instructions[var_id.id], index_set);
	}
	
	propagate_index_set_dependencies(this, instructions);
	
	bool changed = false;
	for(int it = 0; it < 10; ++it) {
		// TODO: It would be a bit nicer if the two dependency sets were just stored as one, however
		//   it is a bit tricky because this is only the case for the entities that are state variables.
		
		changed = false;
		
		// similarly, the initial state of a varialble has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
		for(auto var_id : vars.all_state_vars()) {
			if(initial_instructions[var_id.id].index_sets != instructions[var_id.id].index_sets) {
				changed = true;
				initial_instructions[var_id.id].index_sets = instructions[var_id.id].index_sets;
			}
		}
		if(!changed) break;
		// We have to do it again because we may need to propagate more internal dependencies now that we got more dependencies here.
		if(propagate_index_set_dependencies(this, initial_instructions))
			changed = true;
		if(!changed) break;
		
		for(auto var_id : vars.all_state_vars()) {
			if(initial_instructions[var_id.id].index_sets != instructions[var_id.id].index_sets) {
				changed = true;
				instructions[var_id.id].index_sets = initial_instructions[var_id.id].index_sets;
			}
		}
		if(!changed) break;
		
		if(propagate_index_set_dependencies(this, instructions))
			changed = true;
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Failed to resolve all dependencies in the alotted amount of iterations.");
	
	std::vector<Batch> batches;
	create_batches(this, batches, instructions);
	
	Batch initial_batch;
	initial_batch.solver = invalid_entity_id;
	
	// Sort the initial instructions too.
	std::vector<int> maybe_cycle;
	Instruction_Sort_Predicate predicate { &initial_instructions };
	bool success = topological_sort(predicate, initial_batch.instrs, initial_instructions.size(), maybe_cycle);
	if(!success)
		report_instruction_cycle(this, initial_instructions, maybe_cycle, true);
	
	build_batch_arrays(this, initial_batch.instrs, initial_instructions, initial_batch.arrays, true);
	
	for(auto &batch : batches) {
		if(!is_valid(batch.solver))
			build_batch_arrays(this, batch.instrs, instructions, batch.arrays, false);
		else {
			std::vector<int> vars;
			std::vector<int> vars_ode;
			for(int var : batch.instrs) {
				auto var_ref = this->vars[instructions[var].var_id];
				// NOTE: if we override the conc or value of var, it is not treated as an ODE variable, it is just computed directly
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
	
	//debug_print_batch_array(this, initial_batch.arrays, initial_instructions, global_log_stream, false);
	//debug_print_batch_structure(this, batches, instructions, global_log_stream, false);
	
	set_up_result_structure(this, batches, instructions);
	
	LLVM_Constant_Data constants;
	constants.connection_data        = data.connections.data;
	constants.connection_data_count  = connection_structure.total_count;
	constants.index_count_data       = data.index_counts.data;
	constants.index_count_data_count = index_counts_structure.total_count;
	
#if 0
	log_print("****Connection data is:\n");
	for(int idx = 0; idx < constants.connection_data_count; ++idx)
		log_print(" ", constants.connection_data[idx]);
	log_print("\n");
	
	log_print("****Index count data is:\n");
	for(int idx = 0; idx < constants.index_count_data_count; ++idx)
		log_print(" ", constants.index_count_data[idx]);
	log_print("\n");
#endif
	jit_add_global_data(llvm_data, &constants);
	
	this->initial_batch.run_code = generate_run_code(this, &initial_batch, initial_instructions, true);
	jit_add_batch(this->initial_batch.run_code, "initial_values", llvm_data);

	int batch_idx = 0;
	for(auto &batch : batches) {
		Run_Batch new_batch;
		new_batch.run_code = generate_run_code(this, &batch, instructions, false);
		if(is_valid(batch.solver)) {
			new_batch.solver_id    = batch.solver;

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

#if 0
	std::stringstream ss;
	for(auto &instr : instructions) {
		if(instr.code) {
			print_tree(this, instr.code, ss);
			ss << "\n";
		}
	}
	log_print(ss.str());
#endif

	// **** We don't need the tree representations of the code now that it is compiled into proper functions, so we can free them.

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
