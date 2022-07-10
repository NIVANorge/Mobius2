
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
	Multi_Array_Structure<Entity_Id> array(std::move(handles));
	
	structure.push_back(std::move(array));
	
	parameter_data.set_up(std::move(structure));
	parameter_data.allocate();
	
	// Write default parameter values (TODO: have to iterate over indexes when that is implemented)
	for(auto par : parameter_data.structure[0].handles) {
		auto offset = parameter_data.get_offset(par);
		Parameter_Value val = model->find_entity<Reg_Type::parameter>(par)->default_val;
		*(parameter_data.get_value(offset)) = val;
	}
}


void
Model_Application::set_up_result_structure() {
	if(result_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up result structure twice.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	std::vector<Var_Id> handles = batch.state_vars;
	
	Multi_Array_Structure<Var_Id> array(std::move(handles));
	structure.push_back(std::move(array));
	
	result_data.set_up(std::move(structure));
}

void
Model_Application::set_up_series_structure() {
	if(series_data.has_been_set_up)
		fatal_error(Mobius_Error::internal, "Tried to set up input structure twice.");
	
	std::vector<Multi_Array_Structure<Var_Id>> structure;
	std::vector<Var_Id> handles;
		for(auto id : model->series) handles.push_back(id);
	
	Multi_Array_Structure<Var_Id> array(std::move(handles));
	structure.push_back(std::move(array));
	
	series_data.set_up(std::move(structure));
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

bool
topological_sort_state_vars_visit(Mobius_Model *model, Var_Id var_id, std::vector<Var_Id> *push_to) {
	auto var = model->state_vars[var_id];
	if(var->visited) return true;
	if(var->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the state variables:\n");
		return false;
	}
	var->temp_visited = true;
	for(auto dep_id : var->depends.on_state_var) {
		bool success = topological_sort_state_vars_visit(model, dep_id, push_to);
		if(!success) {
			print_partial_dependency_trace(model, var_id, dep_id);
			return false;
		}
	}
	var->visited = true;
	push_to->push_back(var_id);
	return true;
}

bool
topological_sort_initial_state_vars_visit(Mobius_Model *model, Var_Id var_id, std::vector<Var_Id> *push_to) {
	auto var = model->state_vars[var_id];
	if(var->visited || !var->initial_function_tree) return true;
	if(var->temp_visited) {
		begin_error(Mobius_Error::model_building);
		error_print("There is a circular dependency between the state variables:\n");
		return false;
	}
	var->temp_visited = true;
	for(auto dep_id : var->initial_depends.on_state_var) {
		bool success = topological_sort_initial_state_vars_visit(model, dep_id, push_to);
		if(!success) {
			print_partial_dependency_trace(model, var_id, dep_id);
			return false;
		}
	}
	var->visited = true;
	push_to->push_back(var_id);
	return true;
}



void
put_var_lookup_indices(Math_Expr_FT *expr, Model_Application *model_app) {
	for(auto arg : expr->exprs)
		put_var_lookup_indices(arg, model_app);
	
	if(expr->expr_type != Math_Expr_Type::identifier_chain) return;
	
	auto ident = reinterpret_cast<Identifier_FT *>(expr);
	Math_Expr_FT *offset_code = nullptr;
	s64 back_step;
	if(ident->variable_type == Variable_Type::parameter) {
		offset_code = model_app->parameter_data.get_offset_code(ident->scope, ident->parameter);
	} else if(ident->variable_type == Variable_Type::series) {
		offset_code = model_app->series_data.get_offset_code(ident->scope, ident->series);
		back_step = model_app->series_data.total_count;
	} else if(ident->variable_type == Variable_Type::state_var) {
		offset_code = model_app->result_data.get_offset_code(ident->scope, ident->state_var);
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
add_flux(Model_Application *model_app, Math_Block_FT *scope, char oper, Var_Id var_id_flux, Var_Id var_id_sub) {
	
	auto offset_code     = model_app->result_data.get_offset_code(scope, var_id_flux);
	auto offset_code_sub = model_app->result_data.get_offset_code(scope, var_id_sub);
	
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
	
	for(auto var_id : batch->state_vars) {
		auto var = model->state_vars[var_id];
		auto fun = var->function_tree;
		if(initial)
			fun = var->initial_function_tree;
		if(fun) {
			
			//TODO: make copy of fun so that we don't make mutations on the original model spec!
			fun->scope = scope;
			
			//TODO: we should not do excessive index computations. Can keep them around as local vars and reference them (although llvm will probably optimize it).
			put_var_lookup_indices(fun, model_app);
			
			auto offset_code = model_app->result_data.get_offset_code(scope, var_id);
			auto assignment = new Math_Expr_FT(scope, Math_Expr_Type::state_var_assignment);
			assignment->exprs.push_back(offset_code);
			assignment->exprs.push_back(fun);
			
			scope->exprs.push_back(assignment);
			if(var->type == Decl_Type::flux) {
				if(var->loc1.type == Location_Type::located) {
					auto var_id_sub = model->state_vars[var->loc1];
					add_flux(model_app, scope, '-', var_id, var_id_sub); // subtract flux from source
				} if(var->loc2.type == Location_Type::located) {
					auto var_id_sub = model->state_vars[var->loc2];
					add_flux(model_app, scope, '+', var_id, var_id_sub); // add flux to target
				}
			}
		} else if(var->type != Decl_Type::quantity)
			fatal_error(Mobius_Error::internal, "Some non-quantity did not get a function tree before emulate_model_run().");
	}
	
	batch->run_code = scope;
}

void
Model_Application::compile() {
	if(is_compiled)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application twice.");
	
	if(!series_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before input series data was set up.");
	if(!parameter_data.has_been_set_up)
		fatal_error(Mobius_Error::api_usage, "Tried to compile model application before parameter data was set up.");
	
	//TODO: we should keep the sorting flags outside the model so that the model can be immutable during this stage (cleaner).
	
	warning_print("Sorting begin\n");
	for(auto var_id : model->state_vars) {
		bool success = topological_sort_state_vars_visit(model, var_id, &batch.state_vars);
		if(!success) mobius_error_exit();
	}
	
	// reset sorting flags.
	for(auto var_id : model->state_vars) {
		model->state_vars[var_id]->visited = false;
		model->state_vars[var_id]->temp_visited = false;
	}
	
	for(auto var_id : model->state_vars) {
		bool success = topological_sort_initial_state_vars_visit(model, var_id, &initial_batch.state_vars);
		if(!success) mobius_error_exit();
	}
	
	
	
	
	set_up_result_structure();
	
	warning_print("Generate inital run code\n");
	generate_run_code(this, &initial_batch, true);
	
	warning_print("Generate main run code\n");
	generate_run_code(this, &batch, false);
	
	is_compiled = true;
}
