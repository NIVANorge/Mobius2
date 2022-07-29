
#include "model_application.h"

#include <map>
#include <string>

//TODO: this should eventually get data from the par file that is stored in the model app instead!
// Note, is only meant as a helper function in set_up_parameter_structure(), don't use it elsewhere, look up the parameter structure instead.
void
get_parameter_index_sets(Mobius_Model *model, Entity_Id par_id, std::set<Entity_Id> *index_sets) {
	auto group = model->find_entity<Reg_Type::parameter>(par_id)->par_group;
	auto comp_local = model->find_entity<Reg_Type::par_group>(group)->compartment;
	auto comp_id = model->find_entity<Reg_Type::compartment>(comp_local)->global_id;
	if(!is_valid(comp_id)) comp_id = comp_local;   // In this case we were in the global scope already.
	auto compartment = model->modules[0]->compartments[comp_id];
	index_sets->insert(compartment->index_sets.begin(), compartment->index_sets.end());
}

void
Model_Application::set_up_parameter_structure() {
	if(parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	
	std::map<std::vector<Entity_Id>, std::vector<Entity_Id>> par_by_index_sets;
	
	//TODO: has to be rewritten!! Does not account for possibly multiple deps on same index set.
	for(auto module : model->modules) {
		for(auto par : module->parameters) {
			std::set<Entity_Id> index_sets;
			get_parameter_index_sets(model, par, &index_sets);
			std::vector<Entity_Id> index_sets2(index_sets.begin(), index_sets.end());
			par_by_index_sets[index_sets2].push_back(par);
		}
	}
	
	for(auto pair : par_by_index_sets) {
		std::vector<Entity_Id> index_sets = pair.first;
		std::vector<Entity_Id> handles    = pair.second;
		Multi_Array_Structure<Entity_Id> array(std::move(index_sets), std::move(handles));
		structure.push_back(std::move(array));
	}
	
	parameter_data.set_up(std::move(structure));
	parameter_data.allocate();
	
	// Write default parameter values (TODO: have to iterate over indexes when that is implemented)
	for(auto module : model->modules) {
		for(auto par : module->parameters) {
			Parameter_Value val = model->find_entity<Reg_Type::parameter>(par)->default_val;
			parameter_data.for_each(par, [&](std::vector<Index_T> *indexes, s64 offset) {
				*(parameter_data.get_value(offset)) = val;
			});
		}
	}
}

void
Model_Application::set_up_series_structure() {
	if(series_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up input structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up input structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	std::vector<Var_Id> handles;
		for(auto id : model->series) handles.push_back(id);
	
	Multi_Array_Structure<Var_Id> array({}, std::move(handles)); // TODO: index sets when we implement those.
	structure.push_back(std::move(array));
	
	series_data.set_up(std::move(structure));
}

void 
Model_Application::set_indexes(Entity_Id index_set, Array<String_View> names) {
	index_counts[index_set.id] = {index_set, (s32)names.count};
	//TODO: actually store the names.
}

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.index == 0) return false;
	return true;
}


inline Dependency_Set *
get_dep(Mobius_Model *model, Var_Id var_id, bool initial) { 
	return initial ? &model->state_vars[var_id]->initial_depends : &model->state_vars[var_id]->depends;
}

struct
Model_Instruction {
	enum class 
	Type {
		compute_state_var,
		subtract_flux_from_source,
		add_flux_to_target,
		clear_state_var,
		add_to_aggregate,
	}      type;
	
	Var_Id var_id;
	Var_Id source_or_target_id;
	
	Entity_Id           solver;
	
	std::set<Entity_Id> index_sets;
	
	std::set<int> depends_on_instruction;
	std::set<int> inherits_index_sets_from_instruction;
	
	bool visited;
	bool temp_visited;
	
	Model_Instruction() : visited(false), temp_visited(false), var_id(invalid_var), solver(invalid_entity_id) {};
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
	else if(instr->type == Model_Instruction::Type::add_flux_to_target)
		error_print("(\"", model->state_vars[instr->source_or_target_id]->name, "\" += \"", model->state_vars[instr->var_id]->name, "\")");
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
	
	if(!is_valid(instr->var_id)) return true;
	if(initial && !model->state_vars[instr->var_id]->initial_function_tree) return true;
	
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
		offset_code = model_app->parameter_data.get_offset_code(ident->parameter, &index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = model_app->series_data.get_offset_code(ident->series, &index_expr);
		back_step = model_app->series_data.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		offset_code = model_app->result_data.get_offset_code(ident->state_var, &index_expr);
		back_step = model_app->result_data.total_count;
	}
	
	//TODO: Should check that we are not at the initial step
	if(offset_code && ident->variable_type != Variable_Type::parameter && (ident->flags & ident_flags_last_result)) {
		offset_code = make_binop('-', offset_code, make_literal(back_step));
	}
	
	if(offset_code)
		expr->exprs.push_back(offset_code);
}


Math_Expr_FT *
add_or_subtract_flux_from_var(Model_Application *model_app, char oper, Var_Id var_id_flux, Var_Id var_id_sub, std::vector<Math_Expr_FT *> *indexes) {
	
	//TODO: add aggregation_weight if there is one.
	
	auto offset_code     = model_app->result_data.get_offset_code(var_id_flux, indexes);
	auto offset_code_sub = model_app->result_data.get_offset_code(var_id_sub, indexes);
	
	auto substance_ident = make_state_var_identifier(var_id_sub);
	substance_ident->exprs.push_back(offset_code_sub);
	
	auto flux_ident = make_state_var_identifier(var_id_flux);
	flux_ident->exprs.push_back(offset_code);
	
	auto sum = make_binop(oper, substance_ident, flux_ident);
	
	auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
	assignment->exprs.push_back(copy(offset_code_sub));
	assignment->exprs.push_back(sum);
	
	return assignment;
}

Math_Expr_FT *
create_nested_for_loops(Model_Application *model_app, Math_Block_FT *top_scope, Batch_Array &array, std::vector<Math_Expr_FT *> &indexes) {
	for(auto &index_set : model_app->model->modules[0]->index_sets)
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
	
	std::vector<Math_Expr_FT *> indexes(model->modules[0]->index_sets.count());
	
	for(auto &array : batch->arrays) {
		Math_Expr_FT *scope = create_nested_for_loops(model_app, top_scope, array, indexes);
			
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				auto var = model->state_vars[instr->var_id];
				
				if(var->flags & State_Variable::Flags::f_is_aggregate) continue; // NOTE: the aggregate does not compute itself. Instead it is added to by the flux it aggregates.
				
				auto fun = var->function_tree;
				if(initial)
					fun = var->initial_function_tree;
				
				if(fun) {
					fun = copy(fun);
					
					if(!is_valid(batch->solver) && var->type == Decl_Type::flux) {
						// note: create something like
						// 		flux = min(flux, source)
						// NOTE: it is a design decision by the framework to not allow negative fluxes, otherwise the flux would get a much more
						//      complicated relationship with its target. Should maybe just apply a    max(0, ...) to it as well by default?
						
						auto loc1 = model->state_vars[instr->var_id]->loc1;
						if(loc1.type == Location_Type::located) {
							Var_Id source_id = model->state_vars[loc1];
							auto source = make_state_var_identifier(source_id);
							fun = make_intrinsic_function_call(Value_Type::real, "min", fun, source);
						}
					}
					
					//TODO: we should not do excessive lookups. Can instead keep them around as local vars and reference them (although llvm will probably optimize it).
					put_var_lookup_indexes(fun, model_app, indexes);
					
					auto offset_code = model_app->result_data.get_offset_code(instr->var_id, &indexes);
					auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
					assignment->exprs.push_back(offset_code);
					assignment->exprs.push_back(fun);
					scope->exprs.push_back(assignment);
				} else if(var->type != Decl_Type::quantity)
					fatal_error(Mobius_Error::internal, "Some variable unexpectedly did not get a function tree before generate_run_code(). This should have been detected at an earlier stage.");
			} else if (instr->type == Model_Instruction::Type::subtract_flux_from_source) {
				scope->exprs.push_back(add_or_subtract_flux_from_var(model_app, '-', instr->var_id, instr->source_or_target_id, &indexes));
			} else if (instr->type == Model_Instruction::Type::add_flux_to_target) {
				scope->exprs.push_back(add_or_subtract_flux_from_var(model_app, '+', instr->var_id, instr->source_or_target_id, &indexes));
			} else if (instr->type == Model_Instruction::Type::clear_state_var) {
				auto offset = model_app->result_data.get_offset_code(instr->var_id, &indexes);
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset);
				assignment->exprs.push_back(make_literal((s64)0));
				scope->exprs.push_back(assignment);
			} else if (instr->type == Model_Instruction::Type::add_to_aggregate) {
				auto var = model->state_vars[instr->var_id];
				auto agg_var = model->state_vars[var->agg];
				auto weight = agg_var->function_tree;
				if(!weight)
					fatal_error(Mobius_Error::internal, "Somehow we got an aggregation without code for computing the weight.");
				weight = copy(weight);
				put_var_lookup_indexes(weight, model_app, indexes);			
				
				// read value of the new value we want to sum in.
				auto offset = model_app->result_data.get_offset_code(instr->var_id, &indexes);
				auto read = make_state_var_identifier(instr->var_id);
				read->exprs.push_back(offset);
				
				// read value of aggregate before the sum:
				auto offset2 = model_app->result_data.get_offset_code(instr->source_or_target_id, &indexes);
				auto read2 = make_state_var_identifier(var->agg);
				read2->exprs.push_back(offset2);
				
				auto mul = make_binop('*', read, weight);
				auto sum = make_binop('+', read2, mul);
				
				auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(copy(offset2));
				assignment->exprs.push_back(sum);
				
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
	s64 init_pos = model_app->result_data.get_offset_base(instructions[batch->arrays_ode[0].instr_ids[0]].var_id);
	for(auto &array : batch->arrays_ode) {
		Math_Expr_FT *scope = create_nested_for_loops(model_app, top_scope, array, indexes);
				
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			
			if(instr->type != Model_Instruction::Type::compute_state_var)
				fatal_error(Mobius_Error::internal, "Somehow we got an instruction that is not a state var computation inside an ODE batch.\n");
			
			// NOTE this computation is the derivative of the state variable, which is all ingoing fluxes minus all outgoing fluxes
			
			auto fun = make_literal((double)0.0);
			for(Var_Id flux_id : model->state_vars) {
				auto flux = model->state_vars[flux_id];
				if(flux->type != Decl_Type::flux) continue;
				if(flux->loc1.type == Location_Type::located && model->state_vars[flux->loc1] == instr->var_id && !(flux->flags & State_Variable::Flags::f_is_aggregate)) {
					auto flux_code = make_state_var_identifier(flux_id);
					fun = make_binop('-', fun, flux_code);
				}
				// TODO: again we need to insert aggregation if necessary.
				// TODO: if we explicitly computed an in_flux earlier, we could just refer to it here instead of re-computing it.
				if(flux->loc2.type == Location_Type::located && model->state_vars[flux->loc2] == instr->var_id && !(flux->flags & State_Variable::Flags::f_has_aggregate)) {
					auto flux_code = make_state_var_identifier(flux_id);
					fun = make_binop('+', fun, flux_code);
				}
			}
			
			put_var_lookup_indexes(fun, model_app, indexes);
			
			auto offset_var = model_app->result_data.get_offset_code(instr->var_id, &indexes);
			auto offset_deriv = make_binop('-', offset_var, make_literal(init_pos));
			auto assignment = new Math_Expr_FT(Math_Expr_Type::derivative_assignment);
			assignment->exprs.push_back(offset_deriv);
			assignment->exprs.push_back(fun);
			scope->exprs.push_back(assignment);
		}
	}
	
	return prune_tree(top_scope);
}


void
resolve_index_set_dependencies(Model_Application *model_app, std::vector<Model_Instruction> &instructions, bool initial) {
	
	Mobius_Model *model = model_app->model;
	
	for(auto var_id : model->state_vars) {
		if(model->state_vars[var_id]->flags & State_Variable::Flags::f_is_aggregate) continue;  // The aggregate should not have index sets determined by what is in the weight.
		
		for(auto par_id : get_dep(model, var_id, initial)->on_parameter) {
			auto index_sets = model_app->parameter_data.get_index_sets(par_id);
			instructions[var_id.id].index_sets.insert(index_sets.begin(), index_sets.end()); //TODO: handle matrix parameters when we make those
		}
		
		//TODO: also for input time series when we implement indexing of those. 
	}
	
	bool changed;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto &instr : instructions) {
			if(!is_valid(instr.var_id))
				continue;
			
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
build_batch_arrays(Model_Application *model_app, std::vector<int> &instrs, std::vector<Model_Instruction> &instructions, std::vector<Batch_Array> &batch_out, bool initial) {
	Mobius_Model *model = model_app->model;
	
	batch_out.clear();
	
	for(int instr_id : instrs) {
		Model_Instruction *instr = &instructions[instr_id];
		
		//auto var = model->state_vars[instr->var_id];
		//warning_print("var is ", var->name, "\n");
		
		int earliest_possible_batch = batch_out.size();
		int earliest_suitable_pos   = batch_out.size();
		
		for(int sub_batch_idx = batch_out.size()-1; sub_batch_idx >= 0 ; --sub_batch_idx) {
			auto array = &batch_out[sub_batch_idx];
			
			if(array->index_sets == instr->index_sets)
				earliest_possible_batch = sub_batch_idx;

			bool found_dependency = false;
			for(auto other_id : array->instr_ids) {
				if(instr->depends_on_instruction.find(other_id) != instr->depends_on_instruction.end()) {
					found_dependency = true;
					break;
				}
			}
			if(found_dependency) break;
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
	
#if 1
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	for(auto &pre_batch : batch_out) {
		warning_print("[");;
		for(auto index_set : pre_batch.index_sets)
			warning_print("\"", model->find_entity<Reg_Type::index_set>(index_set)->name, "\" ");
		warning_print("]\n");
		for(auto instr_id : pre_batch.instr_ids) {
			warning_print("\t");
			auto instr = &instructions[instr_id];
			if(instr->type == Model_Instruction::Type::compute_state_var)
				warning_print(model->state_vars[instr->var_id]->name, "\n");
			else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
				warning_print(model->state_vars[instr->source_or_target_id]->name, " -= ", model->state_vars[instr->var_id]->name, "\n");
			else if(instr->type == Model_Instruction::Type::add_flux_to_target)
				warning_print(model->state_vars[instr->source_or_target_id]->name, " += ", model->state_vars[instr->var_id]->name, "\n");
			else if(instr->type == Model_Instruction::Type::clear_state_var)
				warning_print(model->state_vars[instr->var_id]->name, " = 0\n");
			else if(instr->type == Model_Instruction::Type::add_to_aggregate)
				warning_print(model->state_vars[instr->source_or_target_id]->name, " += ", model->state_vars[instr->var_id]->name, " * weight\n");
		}
	}
	warning_print("\n\n");
#endif
	
	// TODO: more passes!
}

void
build_instructions(Mobius_Model *model, std::vector<Model_Instruction> &instructions, bool initial) {
	
	instructions.resize(model->state_vars.count());
	
	for(auto var_id : model->state_vars) {
		auto var = model->state_vars[var_id];
		auto fun = var->function_tree;
		if(initial) fun = var->initial_function_tree;
		if(!fun) {
			if(!initial && var->type != Decl_Type::quantity && !(var->flags & State_Variable::Flags::f_is_aggregate))
				fatal_error(Mobius_Error::internal, "Somehow we got a state variable where the function code was unexpectedly not provided. This should have been detected at an earlier stage in compilation.");
			if(initial)
				continue;     //TODO: we should reproduce the functionality from Mobius1 where the function_tree can act as the initial_function_tree (but only if it is referenced by another state var). But for now we just skip it.
		}
		
		Model_Instruction instr;
		instr.type = Model_Instruction::Type::compute_state_var;
		instr.var_id = var_id;
		
		if(!initial)              // the initial step is a purely discrete setup step, not integration.
			instr.solver = var->solver;
		
		instructions[var_id.id] = std::move(instr);
	}
	
	for(auto var_id : model->state_vars) {
		auto instr = &instructions[var_id.id];
		
		if(!is_valid(instr->var_id)) continue; // NOTE: this can happen in the initial step. In that case we don't want to compute all variables necessarily.
		
		// note we could maybe just retrieve the dependencies from the function tree here instead of doing it earlier and storing them on the State_Variable. It can be nice to keep them there for other uses though.
		for(auto dep : get_dep(model, var_id, initial)->on_state_var) {
			if(dep.type == dep_type_none)
				instr->depends_on_instruction.insert(dep.var_id.id);
			instr->inherits_index_sets_from_instruction.insert(dep.var_id.id);
		}
		
		// TODO: the following is very messy. Maybe move the cases of is_aggregate and has_aggregate out?
		
		auto var = model->state_vars[var_id];
		if(initial || var->type != Decl_Type::flux) continue;
		
		auto loc1 = var->loc1;
		auto loc2 = var->loc2;
		
		bool is_aggregate  = var->flags & State_Variable::Flags::f_is_aggregate;
		bool has_aggregate = var->flags & State_Variable::Flags::f_has_aggregate;
		
		if(has_aggregate) {
			//NOTE: Since this is an aggregate, we know loc1 and loc2 are valid.
			Var_Id source_id = model->state_vars[loc1];
			Var_Id target_id = model->state_vars[loc2];

			Entity_Id source_solver = model->state_vars[source_id]->solver;
			
			Model_Instruction *agg_instr = &instructions[var->agg.id];
			
			agg_instr->inherits_index_sets_from_instruction.clear();
			agg_instr->inherits_index_sets_from_instruction.insert(target_id.id);
			
			if(is_valid(source_solver)) {
				agg_instr->solver = source_solver;
				// NOTE: we have to clear the aggregate to 0 before we start adding to it, so we insert a clear instruction before the computation of whatever it aggregates
				Model_Instruction clear_instr;
				clear_instr.type = Model_Instruction::Type::clear_state_var;
				clear_instr.solver = source_solver;
				clear_instr.var_id = var->agg;
				clear_instr.inherits_index_sets_from_instruction.insert(target_id.id);
				int idx = (int)instructions.size();
				instructions[var_id.id].depends_on_instruction.insert(idx); // Computing the flux comes after clearing the aggregation variable.
				instructions.push_back(std::move(clear_instr));
			}
			
			auto compartment = model->find_entity<Reg_Type::compartment>(loc1.compartment);
			Model_Instruction add_aggr;
			add_aggr.type = Model_Instruction::Type::add_to_aggregate;
			add_aggr.var_id = var_id;
			add_aggr.source_or_target_id = var->agg;
			add_aggr.depends_on_instruction.insert(var_id.id);
			add_aggr.index_sets.insert(compartment->index_sets.begin(), compartment->index_sets.end());
			add_aggr.solver = source_solver;
			int idx = (int)instructions.size();
			instructions.push_back(std::move(add_aggr));
			
			//NOTE: have to look this up again since the pointer could have been invalidated.
			agg_instr = &instructions[var->agg.id];
			agg_instr->depends_on_instruction.insert(idx);
		}
		
		Entity_Id source_solver = invalid_entity_id;
		if(loc1.type == Location_Type::located && !is_aggregate) {
			Var_Id source_id = model->state_vars[loc1];
			Model_Instruction *source = &instructions[source_id.id];
			source_solver = model->state_vars[source_id]->solver;
		
			if (is_valid(source_solver)) { // NOTE: ode variables are integrated, we don't subtract or add to them in just one step.
				instr->solver = source_solver; // all fluxes are given the same solver as their source (if it has one).
				source->inherits_index_sets_from_instruction.insert(var_id.id);
			} else {
				Model_Instruction sub_source_instr;
				sub_source_instr.type = Model_Instruction::Type::subtract_flux_from_source;
				sub_source_instr.var_id = var_id;
				
				sub_source_instr.depends_on_instruction.insert(var_id.id);     // the subtraction of the flux has to be done after the flux is computed.
				sub_source_instr.inherits_index_sets_from_instruction.insert(var_id.id); // it also has to be done once per instance of the flux.
				
				sub_source_instr.source_or_target_id = source_id;
				
				//NOTE: the "compute state var" of the source "happens" after the flux has been subtracted. In the discrete case it will not generate any code, but it is useful to keep it as a stub so that other vars that depend on it happen after it (and we don't have to make them depend on all the fluxes from the var instead).
				int sub_idx = (int)instructions.size();
				source->depends_on_instruction.insert(sub_idx);
								
				instructions[var_id.id].inherits_index_sets_from_instruction.insert(source_id.id); // The flux itself has to be computed once per instance of the source.
				
				instructions.push_back(std::move(sub_source_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
		if(loc2.type == Location_Type::located && !has_aggregate) {
			Var_Id target_id = model->state_vars[loc2];
			Model_Instruction *target = &instructions[target_id.id];
			auto target_solver = model->state_vars[target_id]->solver;

			if(is_valid(target_solver)) {
				if(source_solver != target_solver) {         // If the target is run on another solver than this flux, then we need to sort the target after this flux is computed.
					target->depends_on_instruction.insert(var_id.id);
				}
			} else {
				Model_Instruction add_target_instr;
				add_target_instr.type   = Model_Instruction::Type::add_flux_to_target;
				add_target_instr.var_id = var_id;
				
				add_target_instr.depends_on_instruction.insert(var_id.id);   // the addition of the flux has to be done after the flux is computed.
				add_target_instr.inherits_index_sets_from_instruction.insert(var_id.id);  // it also has to be done (at least) once per instance of the flux
				add_target_instr.inherits_index_sets_from_instruction.insert(target_id.id); // it has to be done once per instance of the target.
				
				add_target_instr.source_or_target_id = target_id;
				
				int add_idx = (int)instructions.size();
				target->depends_on_instruction.insert(add_idx);
				
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
		if(is_valid(instr->solver)) {
			for(int dep : instr->depends_on_instruction)
				propagate_solvers(model, dep, instr->solver, instructions);
		}
	}
	
	warning_print("Remove flux dependencies\n");
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
		auto instr = &instructions[instr_id];
		// Remove dependency of quantities on fluxes if they have the same solver.
		//   this is because quantites with solvers are solved "simultaneously" as ODE variables, and so we don't care about circular dependencies with them.
		// On the other hand, if neither has a solver but are discrete, we instead generate intermediary instructions that do the adding and subtracting, so we don't want direct dependencies between source/target or flux in that case either.
		
		if(instr->type != Model_Instruction::Type::compute_state_var) continue;
		
		if(model->state_vars[instr->var_id]->type == Decl_Type::flux) {
			auto loc1 = model->state_vars[instr->var_id]->loc1;
			if(loc1.type == Location_Type::located) {
				auto source_id = model->state_vars[loc1];
				instr->depends_on_instruction.erase(source_id.id);     // NOTE: we know the source has the same solvers as the flux.
				instructions[source_id.id].depends_on_instruction.erase(instr_id);
			}
			auto loc2 = model->state_vars[instr->var_id]->loc2;
			if(loc2.type == Location_Type::located) {
				auto target_id = model->state_vars[loc2];
				if(instructions[target_id.id].solver == instr->solver) {
					instr->depends_on_instruction.erase(target_id.id);
					instructions[target_id.id].depends_on_instruction.erase(instr_id);
				}
			}
		}
	}
	
	warning_print("Sorting begin\n");
	std::vector<int> sorted_instructions;
	for(int instr_id = 0; instr_id < instructions.size(); ++instr_id) {
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
		
		//TODO: more passes to better group non-solver batches in a minimal way.
		
	}
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
	if(model_app->result_data.has_been_set_up)
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
	
	model_app->result_data.set_up(std::move(structure));
}

void
Model_Application::compile() {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application twice.");
	
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before all index sets had received indexes.");
	
	if(!series_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before input series data was set up.");
	if(!parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before parameter data was set up.");
	
	warning_print("Create instruction arrays\n");
	std::vector<Model_Instruction> initial_instructions;
	std::vector<Model_Instruction> instructions;
	build_instructions(model, initial_instructions, true);
	build_instructions(model, instructions, false);

	warning_print("Resolve index sets dependencies begin.\n");
	resolve_index_set_dependencies(this, initial_instructions, true);
	
	// NOTE: state var inherits all index set dependencies from its initial code.
	for(auto var_id : model->state_vars)
		instructions[var_id.id].index_sets = initial_instructions[var_id.id].index_sets;
	
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
	// TODO: we should allow for the code for a state var (properties at least) to act as its initial code like in Mobius1, so we should have a separate sorting system here.
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
				if(model->state_vars[instructions[var].var_id]->type == Decl_Type::quantity)
					vars_ode.push_back(var);
				else
					vars.push_back(var);
			}
			build_batch_arrays(this, vars,     instructions, batch.arrays,     false);
			build_batch_arrays(this, vars_ode, instructions, batch.arrays_ode, false);
		}
	}
	
	set_up_result_structure(this, batches, instructions);
	
	warning_print("Generate inital run code\n");
	this->initial_batch.run_code = generate_run_code(this, &initial_batch, initial_instructions, true);
	jit_add_batch(this->initial_batch.run_code, "initial_values", llvm_data);
	
	warning_print("Generate main run code\n");
	
	int batch_idx = 0;
	for(auto &batch : batches) {
		Run_Batch new_batch;
		new_batch.run_code = generate_run_code(this, &batch, instructions, false);
		if(is_valid(batch.solver)) {
			auto solver = model->find_entity<Reg_Type::solver>(batch.solver);
			new_batch.solver_fun = solver->solver_fun;
			new_batch.h          = solver->h;
			new_batch.hmin       = solver->hmin;
			// NOTE: same as noted above, all ODEs have to be stored contiguously.
			new_batch.first_ode_offset = result_data.get_offset_base(instructions[batch.arrays_ode[0].instr_ids[0]].var_id);
			new_batch.n_ode = 0;
			for(auto &array : batch.arrays_ode) {         // Hmm, this is a bit inefficient, but oh well.
				for(int instr_id : array.instr_ids)
					new_batch.n_ode += result_data.instance_count(instructions[instr_id].var_id);
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
