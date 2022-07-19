
#include "model_application.h"

#include <map>

//TODO: this should eventually get data from the par file that is stored in the model app instead!
void
get_parameter_index_sets(Mobius_Model *model, Entity_Id par_id, std::set<Entity_Id> *index_sets) {
	auto group = model->find_entity<Reg_Type::parameter>(par_id)->par_group;
	auto comp_local = model->find_entity<Reg_Type::par_group>(group)->compartment;
	auto comp_id = model->find_entity<Reg_Type::compartment>(comp_local)->global_id;
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
	
	//TODO: has to be rewritten!!
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
Model_Application::set_up_result_structure() {
	if(result_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up result structure twice.");
	if(!all_indexes_are_set())
		fatal_error(Mobius_Error::internal, "Tried to set up result structure before all index sets received indexes.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure = batch.structure;
	// NOTE: we just copy the batch structure, which should make it easier optimizing the run code for cache locality
	result_data.set_up(std::move(structure));
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
	
	Multi_Array_Structure<Var_Id> array({}, std::move(handles));
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
	}      type;
	
	Var_Id var_id;
	Var_Id source_or_target_id;
	
	std::set<Entity_Id> index_sets;
	
	std::set<int> depends_on_instruction;
	std::set<int> inherits_index_sets_from_instruction;
	
	bool visited;
	bool temp_visited;
	
	Model_Instruction() : visited(false), temp_visited(false), var_id(invalid_var) {};
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
	error_print_instruction(model, we);
	error_print(" <-- ");
	error_print_instruction(model, dep);
	error_print("\n");
}

bool
topological_sort_instructions_visit(Mobius_Model *model, int instr_idx, std::vector<int> *push_to, std::vector<Model_Instruction> *all_instrs, bool initial) {
	Model_Instruction *instr = &(*all_instrs)[instr_idx];
	
	if(!is_valid(instr->var_id)) return true;
	if(initial && !model->state_vars[instr->var_id]->initial_function_tree) return true;
	
	if(instr->visited) return true;
	if(instr->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the");
		if(initial) error_print(" initial value of the");
		error_print("state variables:\n"); //todo tell about if it is initial
		return false;
	}
	instr->temp_visited = true;
	for(int dep : instr->depends_on_instruction) {
		bool success = topological_sort_instructions_visit(model, dep, push_to, all_instrs, initial);
		if(!success) {
			print_partial_dependency_trace(model, instr, &(*all_instrs)[dep]);
			return false;
		}
	}
	instr->visited = true;
	push_to->push_back(instr_idx);
	return true;
}


void
put_var_lookup_indices(Math_Expr_FT *expr, Model_Application *model_app, std::vector<Math_Expr_FT *> *index_expr) {
	for(auto arg : expr->exprs)
		put_var_lookup_indices(arg, model_app, index_expr);
	
	if(expr->expr_type != Math_Expr_Type::identifier_chain) return;
	
	auto ident = reinterpret_cast<Identifier_FT *>(expr);
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = model_app->parameter_data.get_offset_code(ident->parameter, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = model_app->series_data.get_offset_code(ident->series, index_expr);
		back_step = model_app->series_data.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		offset_code = model_app->result_data.get_offset_code(ident->state_var, index_expr);
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

struct
Pre_Batch_Array {
	std::vector<int>         instr_ids;
	std::set<Entity_Id>      index_sets;
};

Math_Expr_FT *
generate_run_code(Model_Application *model_app, std::vector<Pre_Batch_Array> *batch, std::vector<Model_Instruction> *instructions, bool initial) {
	auto model = model_app->model;
	auto top_scope = new Math_Block_FT();
	
	std::vector<Math_Expr_FT *> indexes(model->modules[0]->index_sets.count());
	
	for(auto &pre_batch : *batch) {
		for(auto &index_set : model->modules[0]->index_sets)
			indexes[index_set.id] = nullptr;    //note: just so that it is easy to catch if we somehow use an index we shouldn't
		
		Math_Block_FT *scope = top_scope;
		for(auto &index_set : pre_batch.index_sets) {
			auto loop = make_for_loop();
			loop->exprs.push_back(make_literal((s64)model_app->index_counts[index_set.id].index));
			scope->exprs.push_back(loop);
			
			//NOTE: the scope of this item itself is replaced when it is inserted later.
			// note: this is a reference to the iterator of the loop.
			indexes[index_set.id] = make_local_var_reference(0, loop->unique_block_id, Value_Type::integer); 
			
			scope = loop;
		}
		auto body = new Math_Block_FT();
		scope->exprs.push_back(body);
		scope = body;
			
		for(int instr_id : pre_batch.instr_ids) {
			auto instr = &(*instructions)[instr_id];
			
			if(instr->type == Model_Instruction::Type::compute_state_var) {
				auto var = model->state_vars[instr->var_id];
				
				auto fun = var->function_tree;
				if(initial)
					fun = var->initial_function_tree;
				
				if(fun) {
					fun = copy(fun);
					
					if(var->type == Decl_Type::flux) {
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
					put_var_lookup_indices(fun, model_app, &indexes);
					
					auto offset_code = model_app->result_data.get_offset_code(instr->var_id, &indexes);
					auto assignment = new Math_Expr_FT(Math_Expr_Type::state_var_assignment);
					assignment->exprs.push_back(offset_code);
					assignment->exprs.push_back(fun);
					
					scope->exprs.push_back(assignment);
				} else if(var->type != Decl_Type::quantity)
					fatal_error(Mobius_Error::internal, "Some non-quantity did not get a function tree before generate_run_code(). This should have been detected at an earlier stage.");
			} else if (instr->type == Model_Instruction::Type::subtract_flux_from_source) {
				scope->exprs.push_back(add_or_subtract_flux_from_var(model_app, '-', instr->var_id, instr->source_or_target_id, &indexes));
			} else if (instr->type == Model_Instruction::Type::add_flux_to_target) {
				scope->exprs.push_back(add_or_subtract_flux_from_var(model_app, '+', instr->var_id, instr->source_or_target_id, &indexes));
			}
		}
		
		//NOTE: delete again to not leak (note that if any of these are used, they are copied, so we are free to delete the originals).
		for(auto expr : indexes) if(expr) delete expr;
	}
	
	return prune_tree(top_scope);
}


void
resolve_index_set_dependencies(Model_Application *model_app, std::vector<Model_Instruction> *instructions, bool initial) {
	
	Mobius_Model *model = model_app->model;
	
	for(auto var_id : model->state_vars) {
		for(auto par_id : get_dep(model, var_id, initial)->on_parameter) {
			get_parameter_index_sets(model, par_id, &(*instructions)[var_id.id].index_sets);
		}
		
		//TODO: also for input time series when we implement indexing of those. 
	}
	
	bool changed;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto &instr : *instructions) {
			if(!is_valid(instr.var_id))
				continue;
			
			int before = instr.index_sets.size();
			for(int dep : instr.inherits_index_sets_from_instruction) {
				auto dep_idx = &(*instructions)[dep].index_sets;
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
build_pre_batch(Model_Application *model_app, std::vector<Model_Instruction> *instructions, std::vector<Pre_Batch_Array> *batch_out, bool initial) {
	Mobius_Model *model = model_app->model;
	
	std::vector<int> sorted_instructions;
	
	warning_print("Sorting begin\n");
	
	for(int instr_id = 0; instr_id < instructions->size(); ++instr_id) {
		bool success = topological_sort_instructions_visit(model, instr_id, &sorted_instructions, instructions, initial);
		if(!success) mobius_error_exit();
	}
	
	warning_print("Build batches begin\n");
	
	batch_out->clear();
	
	for(int instr_id : sorted_instructions) {
		Model_Instruction *instr = &(*instructions)[instr_id];
		
		//auto var = model->state_vars[instr->var_id];
		//warning_print("var is ", var->name, "\n");
		
		int earliest_possible_batch = batch_out->size();
		int earliest_suitable_pos   = batch_out->size();
		
		for(int sub_batch_idx = batch_out->size()-1; sub_batch_idx >= 0 ; --sub_batch_idx) {
			auto array = &(*batch_out)[sub_batch_idx];
			
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
		if(earliest_possible_batch != batch_out->size()) {
			(*batch_out)[earliest_possible_batch].instr_ids.push_back(instr_id);
		} else {
			Pre_Batch_Array pre_batch;
			pre_batch.index_sets = instr->index_sets;
			pre_batch.instr_ids.push_back(instr_id);
			if(earliest_suitable_pos == batch_out->size())
				batch_out->push_back(std::move(pre_batch));
			else
				batch_out->insert(batch_out->begin()+earliest_suitable_pos, std::move(pre_batch));
		}
	}
	
#if 1
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	for(auto &pre_batch : *batch_out) {
		warning_print("[");;
		for(auto index_set : pre_batch.index_sets)
			warning_print("\"", model->find_entity<Reg_Type::index_set>(index_set)->name, "\" ");
		warning_print("]\n");
		for(auto instr_id : pre_batch.instr_ids) {
			warning_print("\t");
			auto instr = &(*instructions)[instr_id];
			if(instr->type == Model_Instruction::Type::compute_state_var)
				warning_print(model->state_vars[instr->var_id]->name, "\n");
			else if(instr->type == Model_Instruction::Type::subtract_flux_from_source)
				warning_print(model->state_vars[instr->source_or_target_id]->name, " -= ", model->state_vars[instr->var_id]->name, "\n");
			else if(instr->type == Model_Instruction::Type::add_flux_to_target)
				warning_print(model->state_vars[instr->source_or_target_id]->name, " += ", model->state_vars[instr->var_id]->name, "\n");
		}
	}
	warning_print("\n\n");
#endif
	
	// TODO: more passes!
}


void
build_instructions(Mobius_Model *model, std::vector<Model_Instruction> *instructions, bool initial) {
	
	instructions->resize(model->state_vars.count());
	
	for(auto var_id : model->state_vars) {
		auto fun = model->state_vars[var_id]->function_tree;
		if(initial) fun = model->state_vars[var_id]->initial_function_tree;
		if(!fun) {
			if(!initial && model->state_vars[var_id]->type != Decl_Type::quantity)
				fatal_error(Mobius_Error::internal, "Somehow we got a state variable that is not a quantity where the function code was not provided. This should have been detected at an earlier stage in compilation.");
			if(initial)
				continue;     //TODO: we should reproduce the functionality from Mobius1 where the function_tree can act as the initial_function_tree (but only if it is referenced by another state var). But for now we just skip it.
		}
		
		Model_Instruction instr;
		instr.type = Model_Instruction::Type::compute_state_var;
		instr.var_id = var_id;
		(*instructions)[var_id.id] = std::move(instr);
	}
	
	for(auto var_id : model->state_vars) {
		auto *instr = &(*instructions)[var_id.id];
		
		if(!is_valid(instr->var_id)) continue;
		
		// note we could maybe just retrieve the dependencies from the function tree here instead of doing it earlier and storing them on the State_Variable. It can be nice to keep them there for other uses though.
		for(auto dep : get_dep(model, var_id, initial)->on_state_var) {
			if(dep.type == dep_type_none)
				instr->depends_on_instruction.insert(dep.var_id.id);
			instr->inherits_index_sets_from_instruction.insert(dep.var_id.id);
		}
		
		if(!initial && model->state_vars[var_id]->type == Decl_Type::flux) {
			//TODO : if the source is an ODE, this has to be handled differently
			auto loc1 = model->state_vars[var_id]->loc1;
			if(loc1.type == Location_Type::located) {
				Var_Id source_id = model->state_vars[loc1];
				
				Model_Instruction sub_source_instr;
				sub_source_instr.type = Model_Instruction::Type::subtract_flux_from_source;
				sub_source_instr.var_id = var_id;
				
				sub_source_instr.depends_on_instruction.insert(var_id.id);     // the subtraction of the flux has to be done after the flux is computed.
				sub_source_instr.inherits_index_sets_from_instruction.insert(var_id.id); // it also has to be done once per instance of the flux.
				
				int sub_idx = (int)instructions->size();
				
				//NOTE: the "compute state var" of the source "happens" after the flux has been subtracted. Tn fact it will not generate any code, but it is useful to keep it as a stub so that other vars that depend on it happen after it (and we don't have to make them depend on all the fluxes from the var instead).
				sub_source_instr.source_or_target_id = source_id;
				Model_Instruction *source = &(*instructions)[source_id.id];
				source->depends_on_instruction.insert(sub_idx);
								
				(*instructions)[var_id.id].inherits_index_sets_from_instruction.insert(source_id.id); // The flux itself has to be computed once per instance of the source.
				
				instructions->push_back(std::move(sub_source_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
			//TODO: if the target is an ODE, this has to be handled differently
			auto loc2 = model->state_vars[var_id]->loc2;
			if(loc2.type == Location_Type::located) {
				Var_Id target_id = model->state_vars[loc2];
				
				Model_Instruction add_target_instr;
				add_target_instr.type   = Model_Instruction::Type::add_flux_to_target;
				add_target_instr.var_id = var_id;
				
				add_target_instr.depends_on_instruction.insert(var_id.id);   // the addition of the flux has to be done after the flux is computed.
				add_target_instr.inherits_index_sets_from_instruction.insert(var_id.id);  // it also has to be done (at least) once per instance of the flux
				add_target_instr.inherits_index_sets_from_instruction.insert(target_id.id); // it has to be done once per instance of the target.
				
				int add_idx = (int)instructions->size();
				
				add_target_instr.source_or_target_id = target_id;
				Model_Instruction *target = &(*instructions)[target_id.id];
				target->depends_on_instruction.insert(add_idx);
				
				//NOTE: the flux does inherit index sets from the target. However,
				//TODO: if the flux index sets are not a subset of the target index sets, we need to have been given an aggregation in the model. (there could also be an aggregation any way). Otherwise we have to throw an error (but only after the index sets are resolved later (?) )
				
				instructions->push_back(std::move(add_target_instr)); // NOTE: this must go at the bottom because it can invalidate pointers into "instructions"
			}
		}
	}
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
	
	
	
	// Resolve index set dependendencies of model instructions
	
	warning_print("Create instruction arrays\n");
	std::vector<Model_Instruction> initial_instructions;
	std::vector<Model_Instruction> instructions;
	build_instructions(model, &initial_instructions, true);
	build_instructions(model, &instructions, false);
	
	warning_print("Resolve index sets dependencies begin.\n");
	resolve_index_set_dependencies(this, &initial_instructions, true);
	
	// NOTE: state var inherits all index set dependencies from its initial code.
	for(auto var_id : model->state_vars)
		instructions[var_id.id].index_sets = initial_instructions[var_id.id].index_sets;
	
	resolve_index_set_dependencies(this, &instructions, false);
	
	// similarly, the initial state of a varialble has to be indexed like the variable. (this is just for simplicity in the code generation, so that a value is assigned to every instance of the variable, but it can cause re-computation of the same value many times. Probably not an issue since it is just for a single time step.)
	for(auto var_id : model->state_vars)
		initial_instructions[var_id.id].index_sets = instructions[var_id.id].index_sets;
	
	std::vector<Pre_Batch_Array> pre_batch;
	std::vector<Pre_Batch_Array> initial_pre_batch;
	
	warning_print("Build pre batches.\n");
	build_pre_batch(this, &initial_instructions, &initial_pre_batch, true);
	build_pre_batch(this, &instructions, &pre_batch, false);
	
	// TODO : we should determine a way of sorting the index sets (maybe like in mobius1, but may also have to optimize for neighbor affiliations, or base it on the "distribute" specifications)
	for(auto &array : initial_pre_batch) {
		Multi_Array_Structure<Var_Id> array2;
		for(auto index_set : array.index_sets) array2.index_sets.push_back(index_set);
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			if(instr->type == Model_Instruction::Type::compute_state_var)
				array2.handles.push_back(instr->var_id);
		}
		array2.finalize();
		initial_batch.structure.push_back(std::move(array2));
	}
	for(auto &array : pre_batch) {
		Multi_Array_Structure<Var_Id> array2;
		for(auto index_set : array.index_sets) array2.index_sets.push_back(index_set);
		for(int instr_id : array.instr_ids) {
			auto instr = &instructions[instr_id];
			if(instr->type == Model_Instruction::Type::compute_state_var)
				array2.handles.push_back(instr->var_id);
		}
		array2.finalize();
		batch.structure.push_back(std::move(array2));
	}
	
	set_up_result_structure();
	
	warning_print("Generate inital run code\n");
	initial_batch.run_code = generate_run_code(this, &initial_pre_batch, &initial_instructions, true);
	
	warning_print("Generate main run code\n");
	batch.run_code         = generate_run_code(this, &pre_batch, &instructions, false);
	
	is_compiled = true;
}
