
#include "function_tree.h"
#include "emulate.h"
#include "model_declaration.h"

#include <cmath>


Typed_Value
apply_cast(Typed_Value val, Value_Type to_type) {
	Typed_Value result;
	result.type = to_type;
	if(val.type == to_type) fatal_error(Mobius_Error::internal, "apply_cast() to same type."); // this should have been eliminated at a different stage.
	if(to_type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_cast() to bool not implemented."); // we don't cast to bool yet. May want to implement it later, but it is not really necessary.
	if(to_type == Value_Type::real) {
		if(val.type == Value_Type::integer)
			result.val_double = (double)val.val_int;
		else if(val.type == Value_Type::boolean)
			result.val_double = (double)val.val_bool;
	} else if(to_type == Value_Type::integer) {
		if(val.type == Value_Type::real)
			result.val_int = (s64)val.val_double;
		else if(val.type == Value_Type::boolean);
			result.val_int = (s64)val.val_bool;
	}
	return result;
}

Typed_Value
apply_unary(Typed_Value val, Token_Type oper) {
	Typed_Value result;
	result.type = val.type;
	if((char)oper == '-') {
		if(val.type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() minus on boolean.");
		else if(val.type == Value_Type::real) result.val_double = val.val_double;
		else if(val.type == Value_Type::integer) result.val_int = val.val_int;
	} else if((char)oper == '!') {
		if(val.type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() negation on non-boolean.");
		result.val_bool = !val.val_bool;
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

Typed_Value
apply_binary(Typed_Value lhs, Typed_Value rhs, Token_Type oper) {
	Typed_Value result;
	char op = (char)oper;
	if(op != '^' && lhs.type != rhs.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_binary()."); //this should have been eliminated at a different stage.
	result.type = lhs.type;
	//NOTE: this implementation doesn't allow for short-circuiting. Dunno if we want that.	
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	if(op == '|')      result.val_bool = lhs.val_bool || rhs.val_bool; 
	else if(op == '&') result.val_bool = lhs.val_bool && rhs.val_bool;
	else if(op == '<') {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int < rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double < rhs.val_double;
	} else if(oper == Token_Type::leq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int <= rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double <= rhs.val_double;
	} else if(op == '>') {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int > rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double > rhs.val_double;
	} else if(oper == Token_Type::geq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int >= rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double >= rhs.val_double;
	} else if(oper == Token_Type::eq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int == rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double == rhs.val_double;
	} else if(oper == Token_Type::neq) {
		if(lhs.type == Value_Type::integer)   result.val_bool = lhs.val_int != rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_bool = lhs.val_double != rhs.val_double;
	} else if(op == '+') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int + rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double + rhs.val_double;
	} else if(op == '-') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int - rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double - rhs.val_double;
	} else if(op == '*') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int * rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double * rhs.val_double;
	} else if(op == '/') {
		if(lhs.type == Value_Type::integer)   result.val_int = lhs.val_int / rhs.val_int;
		else if(lhs.type == Value_Type::real) result.val_double = lhs.val_double / rhs.val_double;
	} else if(op == '^') {
		if(rhs.type == Value_Type::integer)   result.val_double = std::pow(lhs.val_double, rhs.val_int);
		else if(rhs.type == Value_Type::real) result.val_double = std::pow(lhs.val_double, rhs.val_double);
		result.type = Value_Type::real;
	} else
		fatal_error(Mobius_Error::internal, "apply_binary() unhandled operator ", name(oper), " .");
	
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function) {
	Typed_Value result;
	bool ismin = function == "min";
	if(ismin || function == "max") {
		if(a.type != b.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_intrinsic()."); //this should have been eliminated at a different stage.
		
		result.type = a.type;
		if(a.type == Value_Type::real) {
			if(ismin) result.val_double = a.val_double < b.val_double ? a.val_double : b.val_double;
			else      result.val_double = a.val_double > b.val_double ? a.val_double : b.val_double;
		} else if(a.type == Value_Type::integer) {
			if(ismin) result.val_int = a.val_int < b.val_int ? a.val_int : b.val_int;
			else      result.val_int = a.val_int > b.val_int ? a.val_int : b.val_int;
		} else {
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		}
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, String_View function) {
	Typed_Value result;
	result.type = Value_Type::real;
	if(function == "exp") {
		if(a.type != Value_Type::real)
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic().");
		result.val_double = std::exp(a.val_double);
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic().");
	
	return result;
}

struct
Scope_Local_Vars {
	Scope_Local_Vars *scope_up;
	std::vector<Typed_Value> local_vars;
};

Typed_Value
get_local_var(Scope_Local_Vars *scope, int index, int scopes_up) {
	if(!scope)
		fatal_error(Mobius_Error::internal, "Mis-counting of scopes in emulation.");
	for(int i = 0; i < scopes_up; ++i) {
		scope = scope->scope_up;
		if(!scope)
			fatal_error(Mobius_Error::internal, "Mis-counting of scopes in emulation.");
	}
	if(index >= scope->local_vars.size())
		fatal_error(Mobius_Error::internal, "Mis-counting of local variables in emulation.");
	return scope->local_vars[index];
}

Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state, Scope_Local_Vars *locals) {
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block= reinterpret_cast<Math_Block_FT *>(expr);
			Typed_Value result;
			Scope_Local_Vars new_locals;
			new_locals.scope_up = locals;
			new_locals.local_vars.resize(block->n_locals);
			int index = 0;
			for(auto sub_expr : expr->exprs) {
				result = emulate_expression(sub_expr, state, &new_locals);
				if(sub_expr->expr_type == Math_Expr_Type::local_var) {
					new_locals.local_vars[index] = result;
					++index;
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::local_var : {
			return emulate_expression(expr->exprs[0], state, locals);
		} break;
		
		case Math_Expr_Type::identifier_chain : {
			auto ident = reinterpret_cast<Identifier_FT *>(expr);
			//TODO: instead we have to find a way to convert the id to a local index. This is just for early testing.
			Typed_Value result;
			result.type = expr->value_type;
			if(ident->variable_type == Variable_Type::parameter) {
				result = Typed_Value {state->parameters[ident->parameter.id], expr->value_type};
				//warning_print("looked up par ", ident->parameter.id, " val ", result.val_double, "\n");
			}
			else if(ident->variable_type == Variable_Type::state_var)
				result.val_double = state->state_vars[ident->state_var.id];
			else if(ident->variable_type == Variable_Type::series)
				result.val_double = state->series[ident->series.id];
			else if(ident->variable_type == Variable_Type::local)
				result = get_local_var(locals, ident->local_var.index, ident->local_var.scopes_up);
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = reinterpret_cast<Literal_FT *>(expr);
			return {literal->value, literal->value_type};
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_unary(a, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = reinterpret_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value b = emulate_expression(expr->exprs[1], state, locals);
			return apply_binary(a, b, binary->oper);
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = reinterpret_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)
				fatal_error(Mobius_Error::internal, "Unhandled function type in emulate_expression().");
			
			if(fun->exprs.size() == 1) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				return apply_intrinsic(a, fun->fun_name);
			} else if(fun->exprs.size() == 2) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				Typed_Value b = emulate_expression(fun->exprs[1], state, locals);
				return apply_intrinsic(a, b, fun->fun_name);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in emulate_expression().");
		} break;

		case Math_Expr_Type::if_chain : {
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Typed_Value cond = emulate_expression(expr->exprs[idx+1], state, locals);
				if(cond.val_bool) return emulate_expression(expr->exprs[idx], state, locals);
				return emulate_expression(expr->exprs.back(), state, locals);
			}
		} break;
		
		case Math_Expr_Type::cast : {
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_cast(a, expr->value_type);
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't emulate ", name(expr->expr_type), " expression.");
	
	return {Parameter_Value(), Value_Type::unresolved};
}



void emulate_batch(Mobius_Model *model, Run_Batch *batch, Model_Run_State *run_state, bool initial = false) {
	for(auto var_id : batch->state_vars) {
		auto var = model->state_vars[var_id];
		auto fun = var->function_tree;
		if(initial)
			fun = var->initial_function_tree;
		if(fun) {
			Typed_Value val = emulate_expression(fun, run_state, nullptr);
			run_state->state_vars[var_id.id] = val.val_double;
			if(var->type == Decl_Type::flux) {
				// TODO: lookup every time step of location here is inefficient. Even though this emulation is not supposed to be fast, we could maybe make an optimization here.
				if(var->loc1.type == Location_Type::located)
					run_state->state_vars[model->state_vars[var->loc1].id] -= val.val_double;    // Subtract the flux from the source
				if(var->loc2.type == Location_Type::located)
					run_state->state_vars[model->state_vars[var->loc2].id] += val.val_double;    // Add the flux to the target.
			}
		} else if(var->type != Decl_Type::quantity)
			fatal_error(Mobius_Error::internal, "Some non-quantity did not get a function tree before emulate_model_run().");
	}
}

void read_input_data(String_View file_name, Mobius_Model *model, double *target, s64 time_steps);
void write_result_data(String_View file_name, Mobius_Model *model, double *results, s64 time_steps);

void emulate_model_run(Mobius_Model *model) {
	if(!model->is_composed)
		fatal_error(Mobius_Error::internal, "Tried to emulate_model_run() before the model was composed.");
	
	//TODO!!!!
	auto pars = &model->modules[1]->parameters;
	
	Model_Run_State run_state;
	run_state.parameters = (Parameter_Value *)malloc(sizeof(Parameter_Value)*pars->count());
	
	for(auto par : *pars)
		run_state.parameters[par.id] = (*pars)[par]->default_val;
	
	s64 time_steps = 10;
	
	int var_count    = model->state_vars.vars.size();
	int series_count = model->series.vars.size();
	
	double *state_vars = (double *)malloc((time_steps+1)*sizeof(double)*var_count);
	double *series     = (double *)malloc(time_steps*sizeof(double)*series_count);

	
	read_input_data("testinput.dat", model, series, time_steps);
	
	run_state.state_vars      = state_vars;
	run_state.series          = series;
	
	// Initial values:
	memset(run_state.state_vars, 0, sizeof(double)*var_count); // by default 0 unless they are specified
	emulate_batch(model, &model->initial_batch, &run_state, true);
	
	for(s64 ts = 0; ts < time_steps; ++ts) {
		memcpy(run_state.state_vars+var_count, run_state.state_vars, sizeof(double)*var_count); // Copy in the last step's values as the initial state of the current step
		run_state.state_vars+=var_count;
		
		emulate_batch(model, &model->batch, &run_state);
		
		run_state.series     += series_count;
	}
	
	write_result_data("results.dat", model, state_vars, time_steps);
}







void read_input_data(String_View file_name, Mobius_Model *model, double *target, s64 time_steps) {	
	// Dummy code for initial prototype. We should reuse stuff from Mobius 1.0 later!
	
	String_View file_data = read_entire_file(file_name);
	Token_Stream stream(file_name, file_data);
	
	std::vector<std::vector<int>> order;
	
	while(true) {
		auto token = stream.peek_token();
		if(token.type != Token_Type::quoted_string) break;
		stream.read_token();
		order.resize(order.size() + 1);
		String_View name = token.string_value;
		for(auto id : *model->series[name]) order[order.size()-1].push_back(id.id);
	}
	
	size_t count = order.size();
	for(s64 ts = 0; ts < time_steps; ++ts)
		for(int idx = 0; idx < count; ++idx) {
			double val = stream.expect_real();
			for(auto id : order[idx])
				target[count*ts + id] = val;
		}
	free(file_data.data);
}

void write_result_data(String_View file_name, Mobius_Model *model, double *results, s64 time_steps) {
	FILE *file = open_file(file_name, "w");
	
	size_t result_count = model->batch.state_vars.size();
	for(int idx = 0; idx < result_count; ++idx) {
		String_View name = model->state_vars.vars[idx].name;
		fprintf(file, "\"%.*s\"\t", name.count, name.data);
	}
	fprintf(file, "\n");
	
	for(int ts = 0; ts <= time_steps; ++ts)
	{
		for(int idx = 0; idx < result_count; ++idx)
			fprintf(file, "%f\t", results[result_count*ts + idx]);
		fprintf(file, "\n");
	}
	
	fclose(file);
}

