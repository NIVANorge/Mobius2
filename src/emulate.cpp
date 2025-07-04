
#include <cmath>
#include <random>

#include "function_tree.h"
#include "emulate.h"
#include "model_application.h"

Typed_Value
apply_cast(Typed_Value val, Value_Type to_type) {
	Typed_Value result;
	result.type = to_type;
	if(val.type == to_type) fatal_error(Mobius_Error::internal, "apply_cast() to same type."); // this should have been eliminated at a different stage.
	if(to_type == Value_Type::boolean) {
		if(val.type == Value_Type::real)
			result.val_boolean = (bool)val.val_real;
		else if(val.type == Value_Type::integer)
			result.val_boolean = (bool)val.val_integer;
	} else if(to_type == Value_Type::real) {
		if(val.type == Value_Type::integer)
			result.val_real = (double)val.val_integer;
		else if(val.type == Value_Type::boolean)
			result.val_real = (double)val.val_boolean;
	} else if(to_type == Value_Type::integer) {
		if(val.type == Value_Type::real)
			result.val_integer = (s64)val.val_real;
		else if(val.type == Value_Type::boolean)
			result.val_integer = (s64)val.val_boolean;
	}
	return result;
}

Typed_Value
apply_unary(Typed_Value val, Token_Type oper) {
	Typed_Value result;
	result.type = val.type;
	if((char)oper == '-') {
		if(val.type == Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() minus on boolean.");
		else if(val.type == Value_Type::real)    result.val_real    = -val.val_real;
		else if(val.type == Value_Type::integer) result.val_integer = -val.val_integer;
	} else if((char)oper == '!') {
		if(val.type != Value_Type::boolean) fatal_error(Mobius_Error::internal, "apply_unary() negation on non-boolean.");
		result.val_boolean = !val.val_boolean;
	} else
		fatal_error(Mobius_Error::internal, "apply_unary() unhandled operator ", name(oper), " .");
	return result;
}

Typed_Value
apply_binary(Typed_Value lhs, Typed_Value rhs, Token_Type oper) {
	Typed_Value result;
	char op = (char)oper;
	if(op != '^' && lhs.type != rhs.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_binary(). lhs: ", name(lhs.type), ", rhs: ", name(rhs.type), "."); //this should have been eliminated at a different stage.
	
	//NOTE: this implementation doesn't allow for short-circuiting. Dunno if we want that.	
	
	//TODO: should probably do type checking here too, but it SHOULD be correct from the function tree resolution already.
	if(op == '|')      result.val_boolean = lhs.val_boolean || rhs.val_boolean; 
	else if(op == '&') result.val_boolean = lhs.val_boolean && rhs.val_boolean;
	else if(op == '<') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer < rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real < rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::leq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer <= rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real <= rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '>') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer > rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real > rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::geq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer >= rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real >= rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '=') {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer == rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real == rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(oper == Token_Type::neq) {
		if(lhs.type == Value_Type::integer)   result.val_boolean = lhs.val_integer != rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_boolean = lhs.val_real != rhs.val_real;
		result.type = Value_Type::boolean;
	} else if(op == '+') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer + rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real + rhs.val_real;
		result.type = lhs.type;
	} else if(op == '-') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer - rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real - rhs.val_real;
		result.type = lhs.type;
	} else if(op == '*') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer * rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real * rhs.val_real;
		result.type = lhs.type;
	} else if(op == '/') {
		if(lhs.type == Value_Type::integer)   result.val_integer = lhs.val_integer / rhs.val_integer;
		else if(lhs.type == Value_Type::real) result.val_real = lhs.val_real / rhs.val_real;
		result.type = lhs.type;
	} else if(op == '^') {
		if(rhs.type == Value_Type::integer)   result.val_real = std::pow(lhs.val_real, rhs.val_integer);
		else if(rhs.type == Value_Type::real) result.val_real = std::pow(lhs.val_real, rhs.val_real);
		result.type = Value_Type::real;
	} else if(op == '%') {
		result.val_integer = lhs.val_integer % rhs.val_integer;
		result.type = Value_Type::integer;
	} else
		fatal_error(Mobius_Error::internal, "apply_binary() unhandled operator ", name(oper), " .");
	
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, Typed_Value b, String_View function, Model_Run_State *state) {
	
	//TODO: use MAKE_INTRINSIC2 macro here!
	
	Typed_Value result;
	bool ismin = function == "min";
	if(ismin || function == "max") {
		if(a.type != b.type) fatal_error(Mobius_Error::internal, "Mismatching types in apply_intrinsic()."); //this should have been eliminated at a different stage.
		
		result.type = a.type;
		if(a.type == Value_Type::real) {
			if(ismin) result.val_real = a.val_real < b.val_real ? a.val_real : b.val_real;
			else      result.val_real = a.val_real > b.val_real ? a.val_real : b.val_real;
		} else if(a.type == Value_Type::integer) {
			if(ismin) result.val_integer = a.val_integer < b.val_integer ? a.val_integer : b.val_integer;
			else      result.val_integer = a.val_integer > b.val_integer ? a.val_integer : b.val_integer;
		} else {
			fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic(a, b).");
		}
	} else if (function == "copysign") {
		result.type = Value_Type::real;
		result.val_real = std::copysign(a.val_real, b.val_real);
	} else if (function == "uniform_real") {
		std::uniform_real_distribution<double> dist(a.val_real, b.val_real);
		result.type = Value_Type::real;
		if(state)
			result.val_real = dist(state->rand_state);
		else
			fatal_error(Mobius_Error::internal, "apply_intrinsic without random state");
	} else if (function == "normal") {
		std::normal_distribution<double> dist(a.val_real, b.val_real);
		result.type = Value_Type::real;
		if(state)
			result.val_real = dist(state->rand_state);
		else
			fatal_error(Mobius_Error::internal, "apply_intrinsic without random state");
	} else if (function == "uniform_int") {
		std::uniform_int_distribution<s64> dist(a.val_integer, b.val_integer);
		result.type = Value_Type::integer;
		if(state)
			result.val_integer = dist(state->rand_state);
		else
			fatal_error(Mobius_Error::internal, "apply_intrinsic without random state");
	} else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic(a, b).");
	return result;
}

Typed_Value
apply_intrinsic(Typed_Value a, String_View function) {
	Typed_Value result;
	if(false) {}
	#define MAKE_INTRINSIC1(name, emul, llvm, ret_type, type1) \
		else if(function == #name) { \
			result.type = Value_Type::ret_type; \
			if(a.type != Value_Type::type1) \
				fatal_error(Mobius_Error::internal, "Somehow we got wrong type of arguments to \"", function, "\" in apply_intrinsic()."); \
			result.val_##ret_type = std::emul(a.val_##type1); \
		}
	#define MAKE_INTRINSIC2(name, emul, ret_type, type1, type2)
	#include "intrinsics.incl"
	#undef MAKE_INTRINSIC1
	#undef MAKE_INTRINSIC2
	else
		fatal_error(Mobius_Error::internal, "Unhandled intrinsic \"", function, "\" in apply_intrinsic(a).");
	
	return result;
}

#define DO_DEBUG 0
#if DO_DEBUG
	#define DEBUG(a) a;
#else
	#define DEBUG(a)
#endif

Typed_Value
emulate_expression(Math_Expr_FT *expr, Model_Run_State *state, Scope_Local_Vars<Typed_Value> *locals) {
	if(!expr)
		fatal_error(Mobius_Error::internal, "Got a nullptr expression in emulate_expression().");
	
	DEBUG(warning_print("emulate expression of type ", name(expr->expr_type), "\n"))
	
	switch(expr->expr_type) {
		case Math_Expr_Type::block : {
			auto block= static_cast<Math_Block_FT *>(expr);
			Typed_Value result;
			Scope_Local_Vars<Typed_Value> new_locals;
			new_locals.scope_id = block->unique_block_id;
			new_locals.scope_up = locals;
			//new_locals.local_vars.resize(block->n_locals);
			if(!block->is_for_loop) {
				for(auto sub_expr : expr->exprs) {
					result = emulate_expression(sub_expr, state, &new_locals);
					if(sub_expr->expr_type == Math_Expr_Type::local_var) {
						auto local = static_cast<Local_Var_FT *>(sub_expr);
						new_locals.values[local->id] = result;
					}
				}
			} else {
				//DEBUG(warning_print("for loop with ", block->n_locals, " locals\n"))
				s64 n = emulate_expression(expr->exprs[0], state, locals).val_integer;
				for(s64 i = 0; i < n; ++i) {
					new_locals.values[0].val_integer = i;
					new_locals.values[0].type = Value_Type::integer;
					result = emulate_expression(expr->exprs[1], state, &new_locals);
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::local_var : {
			return emulate_expression(expr->exprs[0], state, locals);
		} break;
		
		case Math_Expr_Type::identifier : {
			auto ident = static_cast<Identifier_FT *>(expr);
			Typed_Value result;
			result.type = expr->value_type;
			s64 offset = 0;
			if(ident->variable_type == Variable_Type::parameter || ident->variable_type == Variable_Type::series
				|| ident->variable_type == Variable_Type::connection_info || ident->variable_type == Variable_Type::index_count) {
				DEBUG(warning_print("lookup var offset.\n"))
				offset = emulate_expression(expr->exprs[0], state, locals).val_integer;
			}
			//warning_print("offset for lookup was ", offset, "\n");
			
			Parameter_Value val;
			switch(ident->variable_type) {
				case Variable_Type::parameter: {
					result = Typed_Value {state->parameters[offset], expr->value_type};
				} break;
				
				case Variable_Type::series : {
					if(ident->var_id.type == Var_Id::Type::state_var)
						result.val_real = state->state_vars[offset];
					else if(ident->var_id.type == Var_Id::Type::temp_var)
						result.val_real = state->temp_vars[offset];
					else if(ident->var_id.type == Var_Id::Type::series)
						result.val_real = state->series[offset];
					else
						fatal_error(Mobius_Error::internal, "Unsupported var_id type for identifier.");
				} break;
				
				case Variable_Type::connection_info : {
					result.val_integer = state->connection_info[offset];
				} break;
				
				case Variable_Type::local : {
					result = find_local_var(locals, ident->local_var);
				} break;
				
				case Variable_Type::index_count : {
					result.val_integer = state->index_counts[offset];
				} break;
				
				#define TIME_VALUE(name, bits)\
				case Variable_Type::time_##name : { \
					val.val_integer = state->date_time.name; \
					result = Typed_Value {val, expr->value_type}; \
				} break;
				#include "time_values.incl"
				#undef TIME_VALUE
				
				case Variable_Type::time_fractional_step : {
					result.val_real = state->fractional_step;
				} break;
				
				default : {
					fatal_error(Mobius_Error::internal, "Unhandled variable type in emulate_expression().");
				}
			}
			return result;
		} break;
		
		case Math_Expr_Type::literal : {
			auto literal = static_cast<Literal_FT *>(expr);
			return {literal->value, literal->value_type};
		} break;
		
		case Math_Expr_Type::unary_operator : {
			auto unary = static_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_unary(a, unary->oper);
		} break;
		
		case Math_Expr_Type::binary_operator : {
			auto binary = static_cast<Operator_FT *>(expr);
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value b = emulate_expression(expr->exprs[1], state, locals);
			return apply_binary(a, b, binary->oper);
		} break;
		
		case Math_Expr_Type::function_call : {
			auto fun = static_cast<Function_Call_FT *>(expr);
			if(fun->fun_type != Function_Type::intrinsic)
				fatal_error(Mobius_Error::internal, "Unhandled function type in emulate_expression().");
			
			if(fun->exprs.size() == 1) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				return apply_intrinsic(a, fun->fun_name);
			} else if(fun->exprs.size() == 2) {
				Typed_Value a = emulate_expression(fun->exprs[0], state, locals);
				Typed_Value b = emulate_expression(fun->exprs[1], state, locals);
				return apply_intrinsic(a, b, fun->fun_name, state);
			} else
				fatal_error(Mobius_Error::internal, "Unhandled number of function arguments in emulate_expression().");
		} break;

		case Math_Expr_Type::if_chain : {
			for(int idx = 0; idx < expr->exprs.size()-1; idx+=2) {
				Typed_Value cond = emulate_expression(expr->exprs[idx+1], state, locals);
				if(cond.val_boolean)
					return emulate_expression(expr->exprs[idx], state, locals);
			}
			return emulate_expression(expr->exprs.back(), state, locals);
		} break;
		
		case Math_Expr_Type::local_var_assignment : {
			auto assign = static_cast<Assignment_FT *>(expr);
			Typed_Value new_val = emulate_expression(expr->exprs[0], state, locals);
			replace_local_var(locals, assign->local_var, new_val);
			return {Parameter_Value(), Value_Type::none};
		} break;
		
		case Math_Expr_Type::state_var_assignment : {
			auto assign = static_cast<Assignment_FT *>(expr);
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			if(assign->var_id.type == Var_Id::Type::state_var)
				state->state_vars[index.val_integer] = value.val_real;
			else
				state->temp_vars[index.val_integer] = value.val_real;
			return {Parameter_Value(), Value_Type::none};
		} break;
		
		case Math_Expr_Type::derivative_assignment : {
			Typed_Value index = emulate_expression(expr->exprs[0], state, locals);
			Typed_Value value = emulate_expression(expr->exprs[1], state, locals);
			state->solver_workspace[index.val_integer] = value.val_real;
			return {Parameter_Value(), Value_Type::none};
		} break;
		
		case Math_Expr_Type::cast : {
			Typed_Value a = emulate_expression(expr->exprs[0], state, locals);
			return apply_cast(a, expr->value_type);
		} break;
		
		case Math_Expr_Type::no_op : {
			return {Parameter_Value(), Value_Type::none};
		} break;
	}
	
	fatal_error(Mobius_Error::internal, "Didn't emulate ", name(expr->expr_type), " expression.");
	
	return {Parameter_Value(), Value_Type::unresolved};
}

