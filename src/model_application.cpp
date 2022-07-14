
#include "model_application.h"


void
Model_Application::set_up_parameter_structure() {
	if(parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up parameter structure twice.");
	
	std::vector<Multi_Array_Structure<Entity_Id>> structure;
	
	std::vector<Entity_Id> handles;
	for(auto module : model->modules) {
		for(auto par : module->parameters)
			handles.push_back(par);
	}
	Multi_Array_Structure<Entity_Id> array({}, std::move(handles));
	
	structure.push_back(std::move(array));
	
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
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;

	//NOTE: copy the equation batch structure:
	for(auto &sub : batch.sub_batches)
		structure.push_back(sub.array); // NOTE: copy of sub.array
	
	result_data.set_up(std::move(structure));
}

void
Model_Application::set_up_series_structure() {
	if(series_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up input structure twice.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	std::vector<Var_Id> handles;
		for(auto id : model->series) handles.push_back(id);
	
	Multi_Array_Structure<Var_Id> array({}, std::move(handles));
	structure.push_back(std::move(array));
	
	series_data.set_up(std::move(structure));
}

void 
Model_Application::set_indexes(Entity_Id index_set, Array<String_View> *names) {
	index_counts[index_set.id] = {index_set, (s32)names->count};
	//TODO: actually store the names.
}

bool
Model_Application::all_indexes_are_set() {
	for(auto count : index_counts) if(count.index == 0) return false;
	return true;
}



void
error_print_location(Mobius_Model *model, Value_Location loc) {
	auto comp = model->find_entity(loc.compartment);
	auto prop = model->find_entity(loc.property_or_quantity);
	error_print(comp->handle_name, ".", prop->handle_name);
}

void
error_print_state_var(Mobius_Model *model, Var_Id id) {
	
	//TODO: this has to be improved
	auto var = model->state_vars[id];
	auto entity = model->find_entity(var->entity_id);
	if(entity->handle_name.data)
		error_print(entity->handle_name);
	else if(entity->name.data)
		error_print(entity->name);
	else {
		if(var->type == Decl_Type::quantity || var->type == Decl_Type::property) {
			auto has = model->modules[var->entity_id.module_id]->hases[var->entity_id];
			error_print("has(");
			error_print_location(model, has->value_location);
			error_print(")");
		} else {
			auto flux = model->modules[var->entity_id.module_id]->fluxes[var->entity_id];
			error_print("flux(");
			error_print_location(model, flux->source);
			error_print(", ");
			error_print_location(model, flux->target);
			error_print(")");
		}
	}
}

void
print_partial_dependency_trace(Mobius_Model *model, Var_Id id, Var_Id dep) {
	error_print_state_var(model, id);
	error_print(" <-- ");
	error_print_state_var(model, dep);
	error_print("\n");
}

struct
Batch_Sorting_Data {
	std::set<Entity_Id> index_sets;
	
	bool visited;
	bool temp_visited;
};

inline Dependency_Set *
get_dep(Mobius_Model *model, Var_Id var_id, bool initial) { 
	return initial ? &model->state_vars[var_id]->initial_depends : &model->state_vars[var_id]->depends;
}

bool
topological_sort_state_vars_visit(Mobius_Model *model, Var_Id var_id, std::vector<Var_Id> *push_to, std::vector<Batch_Sorting_Data> *sorting_data, bool initial) {
	//TODO: eventually allow code to act as initial_code like in original mobius
	if(initial && !model->state_vars[var_id]->initial_function_tree) return true; 
	
	bool *visited      = &(*sorting_data)[var_id.id].visited;
	bool *temp_visited = &(*sorting_data)[var_id.id].temp_visited;
	
	if(*visited) return true;
	if(*temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the state variables:\n"); //todo tell about if it is initial
		return false;
	}
	*temp_visited = true;
	for(auto &dep : get_dep(model, var_id, initial)->on_state_var) {
		if(dep.type != dep_type_none) continue; // NOTE: we don't care about circularity in dependencies on earlier time step results.
		bool success = topological_sort_state_vars_visit(model, dep.var_id, push_to, sorting_data, initial);
		if(!success) {
			print_partial_dependency_trace(model, var_id, dep.var_id);
			return false;
		}
	}
	*visited = true;
	push_to->push_back(var_id);
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
		offset_code = model_app->parameter_data.get_offset_code(ident->scope, ident->parameter, index_expr);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = model_app->series_data.get_offset_code(ident->scope, ident->series, index_expr);
		back_step = model_app->series_data.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		offset_code = model_app->result_data.get_offset_code(ident->scope, ident->state_var, index_expr);
		back_step = model_app->result_data.total_count;
	}
	
	//TODO: Should check that we are not at the initial step
	if(offset_code && ident->variable_type != Variable_Type::parameter && (ident->flags & ident_flags_last_result)) {
		offset_code = make_binop(expr->scope, '-', offset_code, make_literal(expr->scope, back_step), Value_Type::integer);
	}
	
	if(offset_code)
		expr->exprs.push_back(offset_code);
}


void
add_or_subtract_flux_from_var(Model_Application *model_app, Math_Block_FT *scope, char oper, Var_Id var_id_flux, Var_Id var_id_sub, std::vector<Math_Expr_FT *> *indexes) {
	
	auto offset_code     = model_app->result_data.get_offset_code(scope, var_id_flux, indexes);
	auto offset_code_sub = model_app->result_data.get_offset_code(scope, var_id_sub, indexes);
	
	auto substance_ident = make_state_var_identifier(scope, var_id_sub);
	substance_ident->exprs.push_back(offset_code_sub);
	
	auto flux_ident = make_state_var_identifier(scope, var_id_flux);
	flux_ident->exprs.push_back(offset_code);
	
	auto sum = make_binop(scope, oper, substance_ident, flux_ident, Value_Type::real);
	
	auto assignment = new Math_Expr_FT(scope, Math_Expr_Type::state_var_assignment);
	assignment->exprs.push_back(offset_code_sub); //Ooops, have to make a copy of it. Unless we make the lookup above into a local var.
	assignment->exprs.push_back(sum);
	
	scope->exprs.push_back(assignment);
}

void
generate_run_code(Model_Application *model_app, Run_Batch *batch, bool initial) {
	auto model = model_app->model;
	auto scope = new Math_Block_FT(nullptr);
	
	for(auto &sub_batch : batch->sub_batches) {
		
		std::vector<Math_Expr_FT *> indexes(model->modules[0]->index_sets.count());
		for(auto &index_set : model->modules[0]->index_sets) {
			indexes[index_set.id] = make_literal(scope, (s64)0);    //TODO: !!!!!!
		}
		
		for(auto var_id : sub_batch.array.handles) {
			auto var = model->state_vars[var_id];
			auto fun = var->function_tree;
			if(initial)
				fun = var->initial_function_tree;
			if(fun) {
				
				//TODO: make copy of fun so that we don't make mutations on the original model spec!
				fun->scope = scope;
				
				//TODO: we should not do excessive index computations. Can keep them around as local vars and reference them (although llvm will probably optimize it).
				put_var_lookup_indices(fun, model_app, &indexes);
				
				auto offset_code = model_app->result_data.get_offset_code(scope, var_id, &indexes);
				auto assignment = new Math_Expr_FT(scope, Math_Expr_Type::state_var_assignment);
				assignment->exprs.push_back(offset_code);
				assignment->exprs.push_back(fun);
				
				scope->exprs.push_back(assignment);
				if(var->type == Decl_Type::flux) {
					if(var->loc1.type == Location_Type::located) {
						auto var_id_sub = model->state_vars[var->loc1];
						add_or_subtract_flux_from_var(model_app, scope, '-', var_id, var_id_sub, &indexes); // subtract flux from source
					} if(var->loc2.type == Location_Type::located) {
						auto var_id_sub = model->state_vars[var->loc2];
						add_or_subtract_flux_from_var(model_app, scope, '+', var_id, var_id_sub, &indexes); // add flux to target
					}
				}
			} else if(var->type != Decl_Type::quantity)
				fatal_error(Mobius_Error::internal, "Some non-quantity did not get a function tree before emulate_model_run().");
		}
	}
	
	batch->run_code = scope;
}

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
resolve_index_set_dependencies(Model_Application *model_app, std::vector<Batch_Sorting_Data> *sorting_data, bool initial) {
	
	Mobius_Model *model = model_app->model;
	
	for(auto var_id : model->state_vars) {
		for(auto par_id : get_dep(model, var_id, initial)->on_parameter) {
			get_parameter_index_sets(model, par_id, &(*sorting_data)[var_id.id].index_sets);
		}
		
		//TODO: also for input time series when we implement indexing of those. 
	}
	
	bool changed;
	for(int it = 0; it < 100; ++it) {
		changed = false;
		
		for(auto var_id : model->state_vars) {
			auto var = model->state_vars[var_id];
			int before = (*sorting_data)[var_id.id].index_sets.size();
			for(auto &dep : get_dep(model, var_id, initial)->on_state_var) {
				auto dep_idx = &(*sorting_data)[dep.var_id.id].index_sets;
				(*sorting_data)[var_id.id].index_sets.insert(dep_idx->begin(), dep_idx->end());
			}
			int after = (*sorting_data)[var_id.id].index_sets.size();
			if(before != after) changed = true;
		}
		
		if(!changed) break;
	}
	if(changed)
		fatal_error(Mobius_Error::internal, "Failed to resolve state variable index set dependencies in the alotted amount of iterations!");
}

void
build_batches(Model_Application *model_app, std::vector<Batch_Sorting_Data> *sorting_data, bool initial) {
	Mobius_Model *model = model_app->model;
	
	std::vector<Var_Id> state_vars;
	
	warning_print("Sorting begin\n");
	
	for(auto var_id : model->state_vars) {
		bool success = topological_sort_state_vars_visit(model, var_id, &state_vars, sorting_data, initial);
		if(!success) mobius_error_exit();
	}
	
	//TODO: replace
	/*
	if(initial)
		model_app->initial_batch.state_vars = state_vars;
	else
		model_app->batch.state_vars = state_vars;
	*/
	
	Run_Batch *batch = initial ? &model_app->initial_batch : &model_app->batch;
	
	warning_print("Build batches begin\n");
	
	for(auto var_id : state_vars) {
		auto var = model->state_vars[var_id];
		//warning_print("var is ", var->name, "\n");
		
		int earliest_possible_batch = batch->sub_batches.size();
		int earliest_suitable_pos   = batch->sub_batches.size();
		
		auto dep = get_dep(model, var_id, initial);
		
		//TODO: here we have to do something clever with order. It should have to do with exactly how the compartment was distributed, and/or how neighbor fluxes go...
		std::vector<Entity_Id> index_sets;
		for(auto index_set : (*sorting_data)[var_id.id].index_sets) {
			index_sets.push_back(index_set);
			//warning_print("Index set type: ", name(index_set.reg_type), "\n");
		}
		
		for(int sub_batch_idx = batch->sub_batches.size()-1; sub_batch_idx >= 0 ; --sub_batch_idx) {
			auto array = &batch->sub_batches[sub_batch_idx].array;
			
			if(array->index_sets == index_sets)
				earliest_possible_batch = sub_batch_idx;

			bool found_dependency = false;
			for(auto other_id : array->handles) {
				if(dep->on_state_var.find(State_Var_Dependency {other_id, dep_type_none}) != dep->on_state_var.end()) {
					found_dependency = true;
					break;
				}
			}
			if(found_dependency) break;
			earliest_suitable_pos   = sub_batch_idx;
		}
		if(earliest_possible_batch != batch->sub_batches.size()) {
			batch->sub_batches[earliest_possible_batch].array.handles.push_back(var_id);
		} else {
			Sub_Batch sub_batch;
			sub_batch.array.index_sets = index_sets;
			sub_batch.array.handles.push_back(var_id);
			if(earliest_suitable_pos == batch->sub_batches.size())
				batch->sub_batches.push_back(sub_batch);
			else
				batch->sub_batches.insert(batch->sub_batches.begin()+earliest_suitable_pos, sub_batch);
		}
	}
	
	for(auto &sub_batch : batch->sub_batches) sub_batch.array.finalize();
	
#if 1
	warning_print("\n****", initial ? " initial" : "", " batch structure ****\n");
	for(auto &sub_batch : batch->sub_batches) {
		warning_print("[");;
		for(auto index_set : sub_batch.array.index_sets)
			warning_print("\"", model->find_entity<Reg_Type::index_set>(index_set)->name, "\" ");
		warning_print("]\n");
		for(auto var_id : sub_batch.array.handles)
			warning_print("\t", model->state_vars[var_id]->name, "\n");
	}
	warning_print("\n\n");
#endif
	
	// TODO: more passes!
}

void
Model_Application::compile() {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application twice.");
	
	if(!series_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before input series data was set up.");
	if(!parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before parameter data was set up.");
	
	// Resolve index set dependendencies of state variables.
	
	std::vector<Batch_Sorting_Data> sorting_data(model->state_vars.count());
	std::vector<Batch_Sorting_Data> initial_sorting_data(model->state_vars.count());
	
	warning_print("Resolve index sets dependencies begin.\n");
	resolve_index_set_dependencies(this, &initial_sorting_data, true);
	
	// NOTE: state var inherits all index set dependencies from its initial code also.
	for(auto var_id : model->state_vars)
		sorting_data[var_id.id].index_sets = initial_sorting_data[var_id.id].index_sets;
	
	resolve_index_set_dependencies(this, &sorting_data, false);
	
	// similarly, the initial state of a varialble has to be indexed like the variable.
	for(auto var_id : model->state_vars)
		initial_sorting_data[var_id.id].index_sets = sorting_data[var_id.id].index_sets; 
	
	build_batches(this, &initial_sorting_data, true);
	build_batches(this, &sorting_data, false);
	
	set_up_result_structure();
	
	warning_print("Generate inital run code\n");
	generate_run_code(this, &initial_batch, true);
	
	warning_print("Generate main run code\n");
	generate_run_code(this, &batch, false);
	
	is_compiled = true;
}
