
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
Model_Instruction::debug_string(Model_Application *app) const {
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
	else if(type == Model_Instruction::Type::add_to_parameter_aggregate)
		ss << "\"" << app->vars[target_id]->name << "\" += \"" << app->model->parameters[par_id]->name << "\" * weight";
	else if(type == Model_Instruction::Type::add_to_aggregate)
		ss << "\"" << app->vars[target_id]->name << "\" += \"" << app->vars[var_id]->name << "\" * weight";
	else if(type == Model_Instruction::Type::external_computation)
		ss << "external_computation(" << app->vars[var_id]->name << ")";
	else if(type == Model_Instruction::Type::compute_assertion)
		ss << "assert(" << app->vars[var_id]->name << ")";
	else
		fatal_error(Mobius_Error::internal, "Unimplemented debug_string for model instruction type.");
	
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
	
	Index_Set_Tuple *allowed_index_sets = nullptr;

	if(instr->type == Model_Instruction::Type::compute_state_var || instr->type == Model_Instruction::Type::add_to_connection_aggregate) {
		auto var = app->vars[instr->var_id];
		if(var->type == State_Var::Type::declared)
			allowed_index_sets = &as<State_Var::Type::declared>(var)->allowed_index_sets;
		else if(var->type == State_Var::Type::dissolved_flux) {
			// could happen if it gets a dependency inserted from a connection aggregation.
			// Other dependencies should be fine automatically.
			allowed_index_sets = &as<State_Var::Type::dissolved_flux>(var)->allowed_index_sets;
		}
	}
	
	auto &dependencies = instr->index_sets;
	
	if(!is_valid(to_insert))
		fatal_error(Mobius_Error::internal, "Tried to insert an invalid id as an index set dependency.");

	if(dependencies.has(to_insert))
		return false;
	
	auto set = model->index_sets[to_insert];
	
	// What do we do with higher order dependencies in either of the special cases? Maybe easiest to disallow double dependencies on union sets for now?
	
	// If we insert a union index set and there is already one of the union members there, we should ignore it.
	if(!set->union_of.empty()) {
		
		Entity_Id union_member_allowed = invalid_entity_id;
		for(auto ui_id : set->union_of) {
			if(dependencies.has(ui_id))
				return false;
			if(allowed_index_sets && allowed_index_sets->has(ui_id))
				union_member_allowed = ui_id;
		}
		// If the reference var location could only depend on a union member, we should insert the union member rather than the union.
		// (note that if the union member was already a dependency, we have exited already, so this insertion is indeed new).
		if(is_valid(union_member_allowed)) {
			dependencies.insert(union_member_allowed);
			return true;
		}
	}
	
	if(allowed_index_sets && !allowed_index_sets->has(to_insert))
		fatal_error(Mobius_Error::internal, "Inserting a banned index set dependency \"", model->index_sets[to_insert]->name, "\" for ", instr->debug_string(app), "\n");

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
insert_dependecies(Model_Application *app, Model_Instruction *instr, Index_Set_Tuple &to_insert) {
	bool changed = false;
	for(auto index_set : to_insert) {
		bool changed2 = insert_dependency(app, instr, index_set);
		changed = changed || changed2;
	}
	return changed;
}

bool
insert_dependencies(Model_Application *app, Model_Instruction *instr, Index_Set_Tuple &to_insert, const Identifier_Data &dep) {
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
insert_var_index_set_dependencies(Model_Application *app, Model_Instruction *instr, const Identifier_Data &dep) {
	auto model = app->model;
	
	if(dep.variable_type == Variable_Type::parameter || dep.is_input_series()) {
				
		insert_dependencies(app, instr, dep);

	} else if(dep.variable_type == Variable_Type::is_at) {
		
		auto index_set = app->model->connections[dep.restriction.r1.connection_id]->node_index_set;
		insert_dependency(app, instr, index_set);
		
	} else if(dep.is_computed_series()) {
		
		if(!is_valid(dep.var_id)) {
			fatal_error(Mobius_Error::internal, "Found a dependency on an invalid Var_Id for instruction ", instr->debug_string(app), ".");
		}
		
		instr->inherits_index_sets_from_state_var.insert(dep);
		
		auto &res = dep.restriction.r1;
		
		if(res.type == Restriction::above || res.type == Restriction::below) {
			
			// NOTE: The following is needed (e.g. NIVAFjord breaks without it), but I would like to figure out why and then document it here with a comment.
				// It is probably that if it doesn't get this dependency at all, it tries to add or subtract from a nullptr index.
			// TODO: However if we could determine that the reference is constant over that index set, we could allow that and just omit adding to that index in codegen.

			if(instr->type == Model_Instruction::Type::compute_state_var) {
				auto conn = model->connections[res.connection_id];
				if(conn->type == Connection_Type::directed_graph) {
					auto comp = app->find_connection_component(res.connection_id, app->vars[dep.var_id]->loc1.components[0], false);
					if(comp && comp->is_edge_indexed)
						insert_dependency(app, instr, conn->edge_index_set);
				} else if(conn->type == Connection_Type::grid1d) {
					auto index_set = conn->node_index_set;
					insert_dependency(app, instr, index_set);
				} else
					fatal_error(Mobius_Error::internal, "Got a 'below' dependency for something that should not have it.");
			}
			
		} else if(res.type == Restriction::top || res.type == Restriction::bottom) {
			
			auto index_set = app->model->connections[res.connection_id]->node_index_set;
			auto parent = app->model->index_sets[index_set]->sub_indexed_to;
			if(is_valid(parent))
				insert_dependency(app, instr, parent);
			
		}
	}
}

// TODO: This function should be reused in model_codegen probably.
void
get_possible_target_ids(Model_Application *app, const Identifier_Data &ident, std::vector<Var_Id> &ids_out) {
	
	auto model = app->model;
	auto &res = ident.restriction.r1;
	
	if(!ident.is_computed_series() || !is_valid(res.connection_id))
		fatal_error(Mobius_Error::internal, "Misuse of get_possible_target_ids");
	
	auto conn = model->connections[res.connection_id];
	if(conn->type != Connection_Type::directed_graph) return;
	auto comp = app->find_connection_component(res.connection_id, app->vars[ident.var_id]->loc1.components[0], false);
	if(!comp) return;
	
	for(auto pot_target : comp->possible_targets) {
		Var_Location loc = app->vars[ident.var_id]->loc1;
		loc.components[0] = pot_target;
		auto target_id = app->vars.id_of(loc);
		if(is_valid(target_id))
			ids_out.push_back(target_id);
	}
}

void
insert_var_order_depencencies(Model_Application *app, Model_Instruction *instr, const Identifier_Data &dep) {
	
	if(!dep.is_computed_series()) return;
	
	auto model = app->model;

	if(!is_valid(dep.var_id)) {
		fatal_error(Mobius_Error::internal, "Found a dependency on an invalid Var_Id for instruction ", instr->debug_string(app), ".");
	}
	
	if(dep.has_flag(Identifier_Data::last_result)) return;
	
	auto &res = dep.restriction.r1;
	
	if(res.type == Restriction::above || res.type == Restriction::below) {
		
		if(dep.restriction.r1.type == Restriction::above) {
			instr->loose_depends_on_instruction.insert(dep.var_id.id);
		} else {
			instr->instruction_is_blocking.insert(dep.var_id.id);
			instr->depends_on_instruction.insert(dep.var_id.id);
		}
	
		if(instr->type == Model_Instruction::Type::compute_state_var || instr->type == Model_Instruction::Type::external_computation) {
			// If it is a 'below' lookup in a graph with multiple components, we have to add a dependency for this for every possible var_id it could actually access.
			std::vector<Var_Id> potential_targets;
			get_possible_target_ids(app, dep, potential_targets);
			for(auto target_id : potential_targets) {
				instr->instruction_is_blocking.insert(target_id.id);
				instr->depends_on_instruction.insert(target_id.id);
			}
		}
		
	} else if(res.type == Restriction::top || res.type == Restriction::bottom) {
		
		instr->depends_on_instruction.insert(dep.var_id.id);
		if(res.type == Restriction::bottom)
			instr->instruction_is_blocking.insert(dep.var_id.id);
		
	} else {  

		auto var = app->vars[dep.var_id];
		if(var->type == State_Var::Type::connection_aggregate)
			instr->loose_depends_on_instruction.insert(dep.var_id.id);
		else
			instr->depends_on_instruction.insert(dep.var_id.id);
	}
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
			insert_var_index_set_dependencies(app, &instr, dep);
			insert_var_order_depencencies(app, &instr, dep);
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
				for(auto dep_idx_set : dep_idx) {
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

void
check_for_special_weak_cycles(Model_Application *app, std::vector<Model_Instruction> &instructions, std::vector<Strongly_Connected_Component<u64>> &max_components) {
	
	auto model = app->model;
	
	for(auto &cycle : max_components) {
		if(cycle.nodes.size() == 1) continue;
		
		for(int node : cycle.nodes) {
			auto &instr = instructions[node];
			if(instr.type != Model_Instruction::Type::compute_state_var) continue;
			auto var = app->vars[instr.var_id];
			if(var->type != State_Var::Type::connection_aggregate) continue;
			auto var2 = as<State_Var::Type::connection_aggregate>(var);
			if(var2->is_out) continue; // Not sure if this could even happen.
			auto conn = model->connections[var2->connection];
			if(conn->type != Connection_Type::directed_graph) continue;
			if(conn->no_cycles) continue;
			/*
				Note: The reasoning for this is as follows. If in_flux(connection, something) is (possibly indirectly) weakly/loosely dependent on itself, it is dependent on (differently indexed) values of itself from within the same 'for loop'.
				This means that the for loop has to be ordered so that indexes that are 'earlier' in the connection graph come before later ones, but then there can be no cycle within the graph. We force the user to declare @no_cycles so that its validity can be checked at another stage without making the compiler check for no cycles (that would complicate the code).
				
				Note: Don't confuse the term 'cycle' within the model instruction dependencies with a 'cycle' within the connection graph itself.
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
				error_print(other_var->name, "\n");
			}
			mobius_error_exit();
			// ( Note: It is probably safe to assume that it includes declared state variables at all (?). There should be no other way such a cycle could happen.)
		}
	}
}

void
build_batch_arrays(Model_Application *app, std::vector<int> &instrs, std::vector<Model_Instruction> &instructions, std::vector<Batch_Array> &arrays_out, bool initial) {
	Mobius_Model *model = app->model;
	
	struct Instruction_Array_Grouping_Predicate {
		std::vector<Model_Instruction> *instructions;
		std::vector<u8>                *participates_;
		
		inline bool participates(int node) { return (*participates_)[node]; }
		inline const std::set<int> &edges(int node) { return (*instructions)[node].depends_on_instruction; }
		inline const std::set<int> &weak_edges(int node) { return (*instructions)[node].loose_depends_on_instruction; }
		inline const std::set<int> &blocks(int node) { return (*instructions)[node].instruction_is_blocking; }
		
		inline u64 label(int node) {  return (*instructions)[node].index_sets.bits;  }
	};
	
	std::vector<u8> participates(instructions.size());
	for(int instr : instrs)
		participates[instr] = true;
	
	Instruction_Array_Grouping_Predicate predicate { &instructions, &participates };
	
	constexpr int max_iter = 10;
	std::vector<Node_Group<u64>> groups;
	std::vector<Strongly_Connected_Component<u64>> max_cycles;
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
		array.index_sets = Index_Set_Tuple { group.label }; // TODO: Maybe the label should be the tuple instead of just the bits.
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
make_safe_for_initial(Model_Application *app, Math_Expr_FT *expr) {
	// If this is initial code that was copied from main code, there could be some thing that are allowed in main code that is not allowed in initial code.
	// So we have to modify it to be safe in initial code (or give an error).
	
	for(auto child : expr->exprs)
		make_safe_for_initial(app, child);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->has_flag(Identifier_FT::last_result))
			ident->remove_flag(Identifier_FT::last_result);
		
		if(ident->is_computed_series()) {
			auto var = app->vars[ident->var_id];
			if(var->type == State_Var::Type::connection_aggregate || var->type == State_Var::Type::in_flux_aggregate) {
				ident->source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("This code needs to be evaluated during the initial step, but 'in_flux' can't be accessed during the initial step. So a separate @initial block is needed for this variable.");
			}
		}
	}
}

void
ensure_has_initial_value(Model_Application *app, Var_Id var_id, std::vector<Model_Instruction> &instructions) {
	
	auto instr = &instructions[var_id.id];
			
	if(instr->is_valid()) return; // If it already exists, fine!
	
	// Some other variable wants to look up the value of this one in the initial step, but it doesn't have @initial or @initial_conc code.
	// If it has a main code block, we also substitute that for the initial step!
	// NOTE: If it doesn't have code at all the data storage will have been cleared to 0, and
	// that will then be the initial value (as by design) without us writing anything to it.
	
	auto var = app->vars[var_id];
		
	instr->type = Model_Instruction::Type::compute_state_var;
	instr->var_id = var_id;
	instr->code = nullptr;
	
	if(var->type == State_Var::Type::declared) {
		auto var2 = as<State_Var::Type::declared>(var);
		auto fun = var2->function_tree.get();
		
		if(fun) {			
			//instr->code = copy(fun);
			instr->code = prune_tree(copy(fun));
			make_safe_for_initial(app, instr->code);
			
			// Have to do this recursively, since we may already have passed it in the outer loop.
			create_initial_vars_for_lookups(app, instr->code, instructions);
		}
		
		if(var2->initial_is_conc) {
			// If the initial is given as a concentration, we have to multiply it with the value of what it is dissolved in, and so that must also be given an initial value.
			auto loc = remove_dissolved(var->loc1);
			ensure_has_initial_value(app, app->vars.id_of(loc), instructions);
		}
		
		// This should no longer be an issue, since in model_composition, if a var lacks an initial tree but has an override tree, the override tree is set as the initial tree, with initial_is_conc = override_is_conc.
		/*
		if(var2->function_tree && var2->override_is_conc)
			fatal_error(Mobius_Error::internal, "Wanted to generate initial code for variable \"", var->name, "\", but it only has @override_conc code. This is not yet handled. (for now, you have to manually put @initial_conc on it.");
		*/
	}
	
	// If it is an aggregation variable, whatever it aggregates must also be computed.
	if(var->type == State_Var::Type::regular_aggregate) {
		auto var2 = as<State_Var::Type::regular_aggregate>(var);
		
		ensure_has_initial_value(app, var2->agg_of, instructions);
	}
}

void
create_initial_vars_for_lookups(Model_Application *app, Math_Expr_FT *expr, std::vector<Model_Instruction> &instructions) {
	for(auto arg : expr->exprs) create_initial_vars_for_lookups(app, arg, instructions);
	
	if(expr->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(expr);
		if(ident->is_computed_series())
			ensure_has_initial_value(app, ident->var_id, instructions);
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
	add_to_aggr_instr.instruction_is_blocking.insert(clear_id); // This says that the clear_id has to be in a separate for loop from this instruction. Not strictly needed for non-connection aggregates, but probably doesn't hurt...
	
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

int
make_add_to_par_aggregate_instr(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id agg_var, Entity_Id agg_of) {
	
	auto model = app->model;
	
	int add_to_aggr_id = instructions.size();
	instructions.emplace_back();
	auto &add_to_aggr_instr = instructions[add_to_aggr_id];
	
	add_to_aggr_instr.type = Model_Instruction::Type::add_to_parameter_aggregate;
	add_to_aggr_instr.target_id = agg_var;
	add_to_aggr_instr.par_id = agg_of;
	
	//add_to_aggr_instr.inherits_index_sets_from_instruction.insert(agg_var.id); // Can't do this, because then the union member could be added after the union.
	auto agg_to_comp = app->model->components[as<State_Var::Type::parameter_aggregate>(app->vars[agg_var])->agg_to_compartment];
	// NOTE: This is necessary in case some union index sets in the par_group need to be reduced to union member index sets:
	for(auto index_set : agg_to_comp->index_sets)
		insert_dependency(app, &add_to_aggr_instr, index_set);
	
	auto par = model->parameters[agg_of];
	auto group = model->par_groups[par->scope_id];
	
	// NOTE: We insert the maximal dependencies rather than the parameter as a dependency, otherwise if the parameter is given fewer index sets in the user data the outcome could be unexpected.
	for(auto id : group->max_index_sets)
		insert_dependency(app, &add_to_aggr_instr, id);
	
	instructions[agg_var.id].depends_on_instruction.insert(add_to_aggr_id);
	
	return add_to_aggr_id;
}

int
make_assertion_instruction(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id var_id) {
	
	auto assert_var = as<State_Var::Type::declared>(app->vars[var_id]);
	
	if(assert_var->decl_id.reg_type != Reg_Type::assert)
		fatal_error(Mobius_Error::internal, "Tried to set a regular variable as an assertion variable?");
	
	int instr_id = instructions.size();
	instructions.emplace_back();
	
	auto &instr = instructions[instr_id];
	instr.var_id = var_id;
	instr.code = copy(assert_var->function_tree.get());
	instr.type = Model_Instruction::Type::compute_assertion;
	
	return instr_id;
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
	
	} else if (type == Restriction::specific) {
		// Do nothing
		
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
	
	if(agg_var->is_out) {
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
	
	if(flux_var->type == State_Var::Type::declared) // For dissolved substances it isn't necessary to have this additional branch as the parent flux will already have been checked.
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
	
	if(!agg_var->is_out) { // TODO: should (something like) this also be done for the source aggregate in directed_graph?
		
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
		if(flux_var->mixing_base) continue;
		
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
			/*
			if(comp->decl_type != Decl_Type::quantity) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The variable \"", app->vars[var_id], "\" is not a quantity, and so it can not be put on a solver directly.");
			}*/
			if(comp->decl_type == Decl_Type::quantity && loc.is_dissolved()) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("A solver was specified for \"", app->vars[var_id], "\". For now we don't allow specifying solvers for dissolved substances. Instead they are given the solver of the variable they are dissolved in.");
			}
			if(is_valid(instructions[var_id.id].solver)) {
				if(instructions[var_id.id].solver != solver_id) {
					source_loc.print_error_header(Mobius_Error::model_building);
					fatal_error("The quantity \"", app->vars[var_id]->name, "\" was put on two different solvers.");
				}
				continue;
			}
			if(!app->vars[var_id]->store_series) {
				source_loc.print_error_header(Mobius_Error::model_building);
				fatal_error("The variable \"", app->vars[var_id]->name, "\" was specified as @no_store, and so can not be put on a solver.");
			}
			instructions[var_id.id].solver = solver_id;
		}
	}
	
	// Automatically let dissolved substances have the solvers of what they are dissolved in.
	// Do it in stages of number of components so that we propagate them out.
	for(int n_components = 3; n_components <= max_var_loc_components; ++n_components) {
		for(Var_Id var_id : app->vars.all_state_vars()) {
			if(is_valid(instructions[var_id.id].solver)) continue;
			
			auto var = app->vars[var_id];
			if(var->type != State_Var::Type::declared) continue;
			auto var2 = as<State_Var::Type::declared>(var);
			if(var2->decl_type != Decl_Type::quantity) continue;
			if(var2->function_tree) continue; // We may not necessarily need to put 'override' variables on solvers.
			if(var->loc1.n_components != n_components) continue;
			
			Var_Location loc = var->loc1;
			do {
				loc = remove_dissolved(loc);
				auto parent_id = app->vars.id_of(loc);
				auto parent_sol = instructions[parent_id.id].solver;
				if(is_valid(parent_sol)) {
					instructions[var_id.id].solver = parent_sol;
					break;
				}
			} while(loc.n_components > 2);
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
		if(as<State_Var::Type::declared>(app->vars[source_id])->function_tree) continue; // If it is 'override' it is not an issue.
		auto s_instr = &instructions[source_id.id];
		if(!is_valid(s_instr->solver)) {
			// Technically not all fluxes may be declared, but if there is an error, it *should* trigger on a declared flux first.
				// TODO: This is not all that robust.
			model->fluxes[as<State_Var::Type::declared>(var)->decl_id]->source_loc.print_error_header();
			//TODO: this is not really enough info to let the user know the combined causes of the problem.
			fatal_error("This flux was put on a connection, but the source is a discrete variable. This is currently not supported.");
		}
	}
}

void
maybe_ensure_initial_vars_for_external_computation_arguments(Model_Application *app, Var_Id var_id, std::vector<Model_Instruction> &instructions) {
	
	// TODO: It is debatable if we should do a similiar thing for all code?
	auto model = app->model;
	
	auto var = app->vars[var_id];
	if(var->type != State_Var::Type::external_computation) return;
	
	auto var2 = as<State_Var::Type::external_computation>(var);
	auto code = static_cast<External_Computation_FT *>(var2->code.get());
	
	for(auto &arg : code->arguments) {
		if(!arg.has_flag(Identifier_FT::last_result)) continue;
		
		auto &res = arg.restriction.r1;
		
		if(is_valid(res.connection_id)) {
			std::vector<Var_Id> potential_targets;
			get_possible_target_ids(app, arg, potential_targets);
			for(auto target_id : potential_targets)
				ensure_has_initial_value(app, target_id, instructions);
		} else
			ensure_has_initial_value(app, arg.var_id, instructions);
	}
}

void
set_up_external_computation_instruction(Model_Application *app, Var_Id var_id, std::vector<Model_Instruction> &instructions, bool initial) {
	
	auto instr = &instructions[var_id.id];
	
	auto model = app->model;
	
	instr->type = Model_Instruction::Type::external_computation;
			
	auto var2 = as<State_Var::Type::external_computation>(app->vars[instr->var_id]);
	auto external = model->external_computations[var2->decl_id];
	
	std::vector<Entity_Id> index_sets;
	if(is_valid(external->component))
		index_sets = model->components[external->component]->index_sets;
	
	auto targets = &var2->targets;
	if(initial)
		targets = &var2->initial_targets; //TODO: Remember to build this list!
	
	// TODO: Check for solver conflicts.
	for(auto target_id : *targets) {
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
	
	if(!initial)
		instr->code = copy(var2->code.get());
	else
		instr->code = copy(var2->initial_code.get());
	
	for(auto index_set : index_sets)
		insert_dependency(app, instr, index_set);
	
	auto code = static_cast<External_Computation_FT *>(instr->code);
	
	for(auto &arg : code->arguments) {
		if(arg.has_flag(Identifier_Data::result)) continue;
		
		if(arg.is_computed_series()) {
			insert_var_order_depencencies(app, instr, arg);
			instr->instruction_is_blocking.insert(arg.var_id.id);
			
		} else if (arg.variable_type == Variable_Type::parameter) {
			
		} else
			fatal_error(Mobius_Error::internal, "Unexpected variable type for external_computation argument.");
	}
}

void
maybe_process_discrete_flux(Model_Application *app, std::vector<Model_Instruction> &instructions, Var_Id var_id) {
	
	// Generate instructions for updating quantities based on discrete fluxes.
	
	auto model = app->model;
	auto var = app->vars[var_id];
	
	if(!var->is_flux()) return;
	
	auto loc1 = var->loc1;
	auto loc2 = var->loc2;
	
	bool is_aggregate  = var->type == State_Var::Type::regular_aggregate;
	bool has_aggregate = var->has_flag(State_Var::has_aggregate);
	
	std::vector<int> sub_add_instrs;
	
	bool is_discrete = false;
	
	Entity_Id source_solver = invalid_entity_id;
	Var_Id source_id;
	if(is_located(loc1) && !is_aggregate) {
		source_id = app->vars.id_of(loc1);
		source_solver = instructions[source_id.id].solver;
		auto source_var = as<State_Var::Type::declared>(app->vars[source_id]);
		
		// If the source is not an ODE variable, and is not overridden, generate an instruction to subtract the flux from the source.
		
		if(!is_valid(source_solver) && !source_var->function_tree) {
			
			is_discrete = true;
			
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
	
	// Similarly generate the instruction to add the flux to the target.
	
	if((is_located(loc2) /*|| is_connection*/) && !has_aggregate) {
		Var_Id target_id = app->vars.id_of(loc2);
		
		Entity_Id target_solver = instructions[target_id.id].solver;
		auto target_var = as<State_Var::Type::declared>(app->vars[target_id]);
		
		if(!is_valid(target_solver) && !target_var->function_tree) {
			
			is_discrete = true;
			
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
	
	// TODO: Does it matter if target is discrete or only source?
	// TODO: We could try to make a more nuanced system where we order dissolved components the same as what they are dissolved in so that people can use discrete transport fluxes.
	if(is_discrete && var->type == State_Var::Type::dissolved_flux) {
		auto parent_id = app->find_base_flux(var_id);
		auto parent = as<State_Var::Type::declared>(app->vars[parent_id]);
		auto parent_decl = model->fluxes[parent->decl_id];
		auto transp_id = is_located(loc1) ? loc1.last() : loc2.last();
		auto transp = model->components[transp_id];
		parent_decl->source_loc.print_error_header(Mobius_Error::model_building);
		fatal_error("The flux \"", parent->name, "\" transports a quantity that is not on a solver (it is a discrete flux). At the same time this quantity has a dissolved quantity \"", transp->name, "\", which would create a transport flux for the dissolved quantity. However, due to complexities with instruction ordering, this is currently not supported.");
	}
	
	// If the flux is tied to a discrete order, make all fluxes after it depend on the subtraction add addition instructions of this one.
	// TODO: make an error or warning if an ODE flux is given a discrete order. Maybe also if a discrete flux is not given one (but maybe not).
	if(!is_discrete || var->type != State_Var::Type::declared) return;
	auto flux_decl_id = as<State_Var::Type::declared>(var)->decl_id;
	auto flux_decl = model->fluxes[flux_decl_id];
	if(!is_valid(flux_decl->discrete_order)) return;
	auto discrete_order = model->discrete_orders[flux_decl->discrete_order];
	//if(!discrete_order) return; // Hmm, this should not be possible?
	
	bool after = false;
	for(auto flux_id : discrete_order->fluxes) {
		if(after) {
			auto var_id_2 = app->find_flux_var_of_decl(flux_id);
			
			if(is_valid(var_id_2))
				instructions[var_id_2.id].depends_on_instruction.insert(sub_add_instrs.begin(), sub_add_instrs.end());
		}
		if(flux_id == flux_decl_id)
			after = true;
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
			
			if(initial) fun = var2->initial_function_tree.get();
			else        fun = var2->function_tree.get();
			
			if(!initial && var2->override_is_conc) // In this case the function tree is for the concentration variable, not for the main variable.
				fun = nullptr;
			
			// NOTE: I commented out the below, becuse then people can add properties that only have an @initial value, but where it doesn't change over time. Useful for computed values that only need to be computed at startup but don't change over time. We should maybe have some kind of check, but that should allow for this case.
			
			// NOTE: quantities typically don't have code associated with them directly (except for the initial value)
			//if(!initial && !fun && !is_valid(var2->external_computation) && (var2->decl_type != Decl_Type::quantity))
			//	fatal_error(Mobius_Error::internal, "Somehow we got a state variable \"", var->name, "\" where the function code was unexpectedly not provided. This should have been detected at an earlier stage in model registration.");
		}
		
		instr.var_id = var_id;
		instr.type = Model_Instruction::Type::compute_state_var;

		if(fun)
			instr.code = prune_tree(copy(fun));
		else if(initial && var->type == State_Var::Type::external_computation) {
			auto var2 = as<State_Var::Type::external_computation>(var);
			if(!var2->initial_code)
				instr.type = Model_Instruction::Type::invalid;
		} else if(initial && var->type != State_Var::Type::parameter_aggregate)
			instr.type = Model_Instruction::Type::invalid;
		
		if(var->type == State_Var::Type::step_resolution)    // These are treated separately, should not be a part of the instruction generation.
			instr.type = Model_Instruction::Type::invalid;
	}
	
	if(initial) {
		
		for(auto var_id : app->vars.all_asserts()) {
			int instr_id = make_assertion_instruction(app, instructions, var_id);
			
			auto &instr = instructions[instr_id];
			create_initial_vars_for_lookups(app, instr.code, instructions);
		}
		
		for(auto var_id : app->vars.all_state_vars()) {
			
			maybe_ensure_initial_vars_for_external_computation_arguments(app, var_id, instructions);
			
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
		
		if(var->type == State_Var::Type::external_computation) {
			set_up_external_computation_instruction(app, var_id, instructions, initial);
			continue;
		}
		
		if(var->type == State_Var::Type::regular_aggregate) {
			
			// NOTE: We have to look up the solver here. This is because we can't necessarily set it up before this point.
			//    although TODO: Couldn't we though?
			auto var2 = as<State_Var::Type::regular_aggregate>(var);
			auto agg_of = var2->agg_of;
			auto var_solver = instructions[agg_of.id].solver;
			
			// TODO: If we are in the initial step, we would not need to clear it.
			int clear_id = make_clear_instr(instructions, var_id, var_solver);
			make_add_to_aggregate_instr(app, instructions, var_solver, var_id, agg_of, clear_id);

			
			// The instruction for the var. It compiles to a no-op, but it is kept in the model structure to indicate the location of when this var has its final value. (also used for result storage structure).
			// Since we generate one aggregation variable per target compartment, we have to give it the full index set dependencies of that compartment
			// TODO: we could generate one per variable that looks it up and prune them later if they have the same index set dependencies (?)
			// TODO: we could also check if this is necessary at all any more?
			auto agg_instr = &instructions[var_id.id];
			//auto agg_to_comp = model->components[var2->agg_to_compartment];
			
			//for(auto index_set : agg_to_comp->index_sets)
			//	insert_dependency(app, agg_instr, index_set);
			insert_dependecies(app, agg_instr, var2->agg_to_index_sets);
			
		} else if(var->type == State_Var::Type::connection_aggregate) {
			
			if(initial)
				fatal_error(Mobius_Error::internal, "Got a connection flux in the initial step.");
			if(!is_valid(instr->solver))
				fatal_error(Mobius_Error::internal, "Got aggregation variable for connection fluxes without a solver.");
			
			set_up_connection_aggregation(app, instructions, var_id);
			
		} else if(initial && var->type == State_Var::Type::parameter_aggregate) { // NOTE: These can be computed in the initial step, and don't need updating after that
			
			auto var2 = as<State_Var::Type::parameter_aggregate>(var);
			auto agg_of = var2->agg_of;
			// Don't need to clear it, it is only computed in the initial step.
			
			make_add_to_par_aggregate_instr(app, instructions, var_id, var2->agg_of);
			
			auto agg_instr = &instructions[var_id.id];
			auto agg_to_comp = model->components[var2->agg_to_compartment];
			//log_print("*** Agg to compartment is ", agg_to_comp->name, " with ", agg_to_comp->index_sets.size(), " index sets. \n");
			for(auto index_set : agg_to_comp->index_sets)
				insert_dependency(app, agg_instr, index_set);
		}
		
		if(initial) continue;
		
		maybe_process_discrete_flux(app, instructions, var_id);
		
	}
}



// give all properties the solver if it is "between" quantities or fluxes with that solver in the dependency tree.
bool
propagate_solvers(Model_Application *app, int instr_id, Entity_Id solver, std::vector<Model_Instruction> &instructions, std::vector<u8> &visited, bool *changed) {
	auto instr = &instructions[instr_id];
	
	bool visited_before = visited[instr_id];
	visited[instr_id] = true;
	if(instr->solver == solver)
		return true;
	else if(visited_before)
		return false;

	bool found = false;
	for(int dep : instr->depends_on_instruction) {
		if(propagate_solvers(app, dep, solver, instructions, visited, changed))
			found = true;
	}
	for(int dep : instr->loose_depends_on_instruction) {
		if(propagate_solvers(app, dep, solver, instructions, visited, changed))
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
		
		if(!var->is_mass_balance_quantity()
		//var->type != State_Var::Type::declared || as<State_Var::Type::declared>(var)->decl_type != Decl_Type::quantity
		) {
			instr->solver = solver;
			*changed = true;
		}
		// TODO: Shouldn't we give an error here if we found a mass balance quantity??
		
		// Note: It may look like we set the solver of the clear instr at construction, but some times the solver is not yet available at that point and must be set here instead.
		// The clear instruction would otherwise not be visited since it doesn't depend on anything (only the other way around).
		if(instr->clear_instr >= 0) {
			auto &clear_instr = instructions[instr->clear_instr];
			if(!is_valid(clear_instr.solver)) {
				clear_instr.solver = solver;
				*changed = true;
			}
		}
	}
	
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
report_instruction_cycle(Model_Application *app, std::vector<Model_Instruction> &instructions, const std::vector<int> &cycle, bool initial = false) {
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

void
remove_ode_dependencies(std::set<int> &remove_from, Entity_Id solver, Model_Application *app, std::vector<Model_Instruction> &instructions) {
	std::vector<int> remove;
	for(int other_id : remove_from) {
		auto &other_instr = instructions[other_id];
		if(other_instr.type != Model_Instruction::Type::compute_state_var || !is_valid(other_instr.var_id)) continue;
		auto other_var = app->vars[other_instr.var_id];
		if(other_instr.solver == solver && other_var->is_mass_balance_quantity())
			remove.push_back(other_id);
	}
	for(int rem : remove)
		remove_from.erase(rem);
}

bool
has_fractional_step_access(Math_Expr_FT *code) {
	for(auto expr : code->exprs) {
		if(has_fractional_step_access(expr)) return true;
	}
	
	if(code->expr_type == Math_Expr_Type::identifier) {
		auto ident = static_cast<Identifier_FT *>(code);
		if(ident->variable_type == Variable_Type::time_fractional_step)
			return true;
	}
	
	return false;
}


void
create_batches(Model_Application *app, std::vector<Batch> &batches_out, std::vector<Model_Instruction> &instructions) {

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
	
	// Propagate solvers in a way so that instructions that are (dependency-) sandwiched between other instructions that are on the same ODE solver are also put in that ODE batch.
	{
		bool changed = false;
		std::vector<u8> visited(instructions.size());
		int idx = 0;
		for(; idx < 10; ++idx) {
			
			for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
				auto instr = &instructions[instr_id];
				
				if(!instr->is_valid()) continue;
				
				auto solver_id = instr->solver;
				if(is_valid(solver_id)) {
					for(auto &v : visited) v = false;
					
					for(int dep : instr->depends_on_instruction)
						propagate_solvers(app, dep, solver_id, instructions, visited, &changed);
					for(int dep : instr->loose_depends_on_instruction)
						propagate_solvers(app, dep, solver_id, instructions, visited, &changed);
				}
			}
			if(!changed) break;
		}
		if(idx == 9 && changed)
			fatal_error(Mobius_Error::internal, "Failed to propagate solvers in the alotted amount of iterations.");
	}
	
	// Check that instructions that are not on a solver don't access time.fractional_step	
	{
		for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
			auto instr = &instructions[instr_id];
			if(!instr->is_valid()) continue;
			if(instr->type != Model_Instruction::Type::compute_state_var) continue;
			if(is_valid(instr->solver)) continue;
			if(!instr->code) continue;
			bool frac = has_fractional_step_access(instr->code);
			if(frac) {
				// TODO: Fetch the source location...
				fatal_error(Mobius_Error::model_building, "The variable \"", instr->debug_string(app), "\" accesses 'time.fractional_step', but it is not on a 'solver'.");
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
			remove_ode_dependencies(instr.depends_on_instruction, instr.solver, app, instructions);
			remove_ode_dependencies(instr.loose_depends_on_instruction, instr.solver, app, instructions);
		} else if (app->vars[instr.var_id]->is_flux()) {
			// Remove dependency of discrete fluxes on their sources. Discrete fluxes are ordered in a specific way, and the final value of the source is "ready" after the flux is subtracted. If the flux references its source, that will be a temporary value by design.
			auto var = app->vars[instr.var_id];
			if(is_located(var->loc1))
				instr.depends_on_instruction.erase(app->vars.id_of(var->loc1).id);
		}
	}
	
	// If an instruction has a weak dependency on something that is on a different solver (or one of them is not on a solver), it must be treated as a strong dependency, because they could not be put in the same for loop unless they could be grouped in the same batch.
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto &instr = instructions[instr_id];
		
		if(!instr.is_valid()) continue;
		
		for(int dep : instr.loose_depends_on_instruction) {
			auto &dep_instr = instructions[dep];
			if(dep_instr.solver != instr.solver) {
				instr.loose_depends_on_instruction.erase(dep);
				instr.depends_on_instruction.insert(dep);
			}
		}
	}
	
	// Make a 'naive' sorting of instructions by dependencies. This makes it easier to work with them later.
	std::vector<int> sorted_instructions;
	
	Instruction_Sort_Predicate predicate { &instructions };
	topological_sort<Instruction_Sort_Predicate, int>(predicate, sorted_instructions, instructions.size(), [&](const std::vector<int> &cycle) {
		report_instruction_cycle(app, instructions, cycle);
	});
	
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
	topological_sort<Pre_Batch_Sort_Predicate, int>(predicate2, sorted_pre_batches, pre_batches.size(), [&](const std::vector<int> &cycle) {
		fatal_error(Mobius_Error::internal, "Unable to sort pre batches. This should not be possible if solvers are correctly propagated.");
	});
	
	
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
	bool success = optimize_label_group_packing(predicate3, grouped_batches, max_iter);
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
	for(auto index_set : array.index_sets)
		index_sets.push_back(index_set);
	
	std::vector<Var_Id>    result_handles;
	std::vector<Var_Id>    temp_result_handles;
	for(int instr_id : array.instr_ids) {
		auto instr = &instructions[instr_id];
		if(instr->type == Model_Instruction::Type::compute_state_var) {
			if(instr->var_id.type == Var_Id::Type::state_var)
				result_handles.push_back(instr->var_id);
			else if(instr->var_id.type == Var_Id::Type::temp_var)
				temp_result_handles.push_back(instr->var_id);
			else
				fatal_error(Mobius_Error::internal, "Unexpected var type for compute_state_var.");
		}
	}
	if(!result_handles.empty()) {
		std::vector<Entity_Id> index_sets2 = index_sets;
		Multi_Array_Structure<Var_Id> arr(std::move(index_sets2), std::move(result_handles));
		result_structure.push_back(std::move(arr));
	}
	if(!temp_result_handles.empty()) {
		std::vector<Entity_Id> index_sets2 = index_sets;
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
		for(auto &array : batch.arrays_ode)  add_array(result_structure, temp_result_structure, array, instructions); // The ODEs will never be added to temp results or asserts in reality, but this is easier for code reuse. A bit inefficient though.
	}
	
	{
		std::vector<Var_Id> resolution_vars;
		for(auto var_id : app->vars.all_state_vars()) {
			auto var = app->vars[var_id];
			if(var->type == State_Var::Type::step_resolution)
				resolution_vars.push_back(var_id);
		}
		Multi_Array_Structure<Var_Id> arr({}, std::move(resolution_vars));
		result_structure.push_back(std::move(arr));
	}
	
	app->result_structure.set_up(std::move(result_structure));
	app->temp_result_structure.set_up(std::move(temp_result_structure));
}

void
set_up_assert_structure(Model_Application *app, Batch &initial_batch, std::vector<Model_Instruction> &initial_instructions) {
	std::vector<Multi_Array_Structure<Var_Id>> assert_structure;
	for(auto &array : initial_batch.arrays) {
		std::vector<Entity_Id> index_sets;
		for(auto index_set : array.index_sets)
			index_sets.push_back(index_set);
		std::vector<Var_Id> handles;
		for(int instr_id : array.instr_ids) {
			auto instr = &initial_instructions[instr_id];
			if(instr->type != Model_Instruction::Type::compute_assertion) continue;
			handles.push_back(instr->var_id);
		}
		Multi_Array_Structure<Var_Id> arr(std::move(index_sets), std::move(handles));
		assert_structure.push_back(std::move(arr));
	}
	app->assert_structure.set_up(std::move(assert_structure));
}

void
validate_batch_structure(Model_Application *app, const std::vector<Batch> &batches, const std::vector<Model_Instruction> &instructions, bool initial = false) {
	
	const char *init = initial ? "(Initial structure): " : "";
	
	for(int batch_idx = 0; batch_idx < batches.size(); ++batch_idx) {
		const auto &batch = batches[batch_idx];
		
		for(int array_idx = 0; array_idx < batch.array_count(); ++array_idx) {
			const auto &array = batch[array_idx];
			
			for(int instr_idx = 0; instr_idx < array.instr_ids.size(); ++instr_idx) {
				int instr_id = array.instr_ids[instr_idx];
				
				const auto &instr = instructions[instr_id];
				if(instr.solver != batch.solver)
					fatal_error(Mobius_Error::internal, init, "Mismatch between solver of instruction ", instr.debug_string(app), " and its batch. ");
				
				if(instr.index_sets != array.index_sets)
					fatal_error(Mobius_Error::internal, init, "Mismatch between index sets of instruction ", instr.debug_string(app), " and its batch. ");
				
				// TODO: Check that the tuple itself is valid. (wrt unions, sub-indexing etc.)
				
				if(instr.depends_on_instruction.empty() && instr.loose_depends_on_instruction.empty() && instr.instruction_is_blocking.empty())
					continue;
				
				for(int other_batch_idx = batch_idx; other_batch_idx < batches.size(); ++other_batch_idx) {
					const auto &other_batch = batches[other_batch_idx];
					
					int begin_array = 0;
					if(other_batch_idx == batch_idx) begin_array = array_idx;
					
					for(int other_array_idx = begin_array; other_array_idx < other_batch.array_count(); ++other_array_idx) {
						const auto &other_array = other_batch[other_array_idx];
						
						int begin_instr = 0;
						if(batch_idx == other_batch_idx && array_idx == other_array_idx)
							begin_instr = instr_idx + 1;
							
						for(int other_instr_idx = begin_instr; other_instr_idx < other_array.instr_ids.size(); ++other_instr_idx) {
							int other_instr_id = other_array.instr_ids[other_instr_idx];
							
							if(instr.depends_on_instruction.find(other_instr_id) != instr.depends_on_instruction.end())
								fatal_error(Mobius_Error::internal, init, "The instruction ", instr.debug_string(app), " appears before its dependency ", instructions[other_instr_id].debug_string(app), " in the batch structure.");
							
							if(batch_idx == other_batch_idx && array_idx == other_array_idx) {
								if(instr.instruction_is_blocking.find(other_instr_id) != instr.instruction_is_blocking.end())
									fatal_error(Mobius_Error::internal, init, "The instruction ", instr.debug_string(app), " appears in the same array as the instruction it is blocking \"", instructions[other_instr_id].debug_string(app), "\" in the batch structure.");
							} else {
								if(instr.loose_depends_on_instruction.find(other_instr_id) != instr.loose_depends_on_instruction.end()) {
									fatal_error(Mobius_Error::internal, init, "The instruction ", instr.debug_string(app), " appears strictly before its loose dependency ", instructions[other_instr_id].debug_string(app), " in the batch structure.");
								}
							}
						}
					}
				}
			}
		}
	}
	
}


static int llvm_module_instance = 0; //TODO: This may not be the best way to do it

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
	
	compose_and_resolve();

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
		// TODO: It would be a bit nicer if the two dependency sets were just stored as one because then we could just do the entire propagation once, however
		//   it is a bit tricky because this is only the case for the instructions that are compute_state_var.
		
		changed = false;
		
		// similarly, the initial state of a variable has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
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
	Instruction_Sort_Predicate predicate { &initial_instructions };
	topological_sort<Instruction_Sort_Predicate, int>(predicate, initial_batch.instrs, initial_instructions.size(), [&](const std::vector<int> &cycle) {
		report_instruction_cycle(this, initial_instructions, cycle, true);
	});

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
				if(instructions[var].type == Model_Instruction::Type::compute_state_var && var_ref->is_mass_balance_quantity())
					vars_ode.push_back(var);
				else
					vars.push_back(var);
			}
			build_batch_arrays(this, vars,     instructions, batch.arrays,     false);
			build_batch_arrays(this, vars_ode, instructions, batch.arrays_ode, false);
		}
	}
	
	//debug_print_batch_array(this, initial_batch.arrays, initial_instructions, global_log_stream, false);
	//debug_print_batch_structure(this, batches, instructions, global_log_stream, true);
	
	if(mobius_developer_mode) {
		validate_batch_structure(this, { initial_batch }, initial_instructions, true); // This creates a copy of the initial_batch? :(
		validate_batch_structure(this, batches, instructions);
	}
	
	set_up_result_structure(this, batches, instructions);
	set_up_assert_structure(this, initial_batch, initial_instructions);
	
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
	jit_add_global_data(llvm_data, &constants, llvm_module_instance);
	
	std::string instance_sub = std::string("_") + std::to_string(llvm_module_instance);
	
	this->initial_batch.run_code = generate_run_code(this, &initial_batch, initial_instructions, true);
	jit_add_batch(this->initial_batch.run_code, std::string("initial_values") + instance_sub, llvm_data);

	int batch_idx = 0;
	for(auto &batch : batches) {
		Run_Batch new_batch;
		new_batch.run_code = generate_run_code(this, &batch, instructions, false);
		if(is_valid(batch.solver)) {
			new_batch.solver_id    = batch.solver;

			// NOTE: same as noted above, all ODEs have to be stored contiguously.
			new_batch.first_ode_offset = result_structure.get_offset_base(instructions[batch.arrays_ode[0].instr_ids[0]].var_id);
			new_batch.n_ode = 0;
			for(auto &array : batch.arrays_ode) {
				for(int instr_id : array.instr_ids)
					new_batch.n_ode += result_structure.instance_count(instructions[instr_id].var_id);
			}
			
			for(auto var_id : vars.all_state_vars()) {
				auto var = vars[var_id];
				if(var->type != State_Var::Type::step_resolution) continue;
				auto var2 = as<State_Var::Type::step_resolution>(var);
				if(var2->solver_id != new_batch.solver_id) continue;
				new_batch.h_address = result_structure.get_offset_base(var_id);
				break;
			}
		}
		
		std::string function_name = std::string("batch_function_") + std::to_string(batch_idx) + instance_sub;
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
	
	++llvm_module_instance;
	
	jit_compile_module(llvm_data, ir_string);
	
	this->initial_batch.compiled_code = get_jitted_batch_function(std::string("initial_values") + instance_sub);
	batch_idx = 0;
	for(auto &batch : this->batches) {
		std::string function_name = std::string("batch_function_") + std::to_string(batch_idx) + instance_sub;
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
